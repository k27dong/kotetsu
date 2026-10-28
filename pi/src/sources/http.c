#include "sources/http.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <curl/curl.h>
#include <glib.h>

#include "config.h"
#include "log.h"

#define USER_AGENT "kotetsu/2.0 github.com/k27dong/kotetsu"

static constexpr curl_off_t MAX_BODY_BYTES = 1024 * 1024;

static size_t append_body(char *chunk, size_t size, size_t count, void *context) {
    struct http_response *response = context;
    const size_t chunk_length = size * count;
    response->body = g_realloc(response->body, response->length + chunk_length + 1);
    memcpy(response->body + response->length, chunk, chunk_length);
    response->length += chunk_length;
    response->body[response->length] = '\0';
    return chunk_length;
}

static bool append_header(struct curl_slist **headers, const char *name, const char *value) {
    if (value == nullptr) {
        return true;
    }
    gchar *line = g_strconcat(name, ": ", value, nullptr);
    struct curl_slist *appended = curl_slist_append(*headers, line);
    g_free(line);
    if (appended == nullptr) {
        return false;
    }
    *headers = appended;
    return true;
}

static bool add_parts(curl_mime *form, const struct http_part *parts, size_t count) {
    for (size_t i = 0; i < count; i++) {
        curl_mimepart *field = curl_mime_addpart(form);
        if (field == nullptr || curl_mime_name(field, parts[i].name) != CURLE_OK ||
            curl_mime_type(field, parts[i].content_type) != CURLE_OK ||
            curl_mime_data(field, (const char *)parts[i].data, parts[i].length) != CURLE_OK) {
            return false;
        }
        // form parsers read a part without a filename as text, which garbles binary data
        const bool binary = !g_str_has_prefix(parts[i].content_type, "text/") &&
                            strcmp(parts[i].content_type, "application/json") != 0;
        if (binary && curl_mime_filename(field, parts[i].name) != CURLE_OK) {
            return false;
        }
    }
    return true;
}

static bool request(const char *url, const char *bearer, const char *now_header,
                    const struct http_part *parts, size_t count, struct http_response *out) {
    *out = (struct http_response){.body = g_strdup("")};
    char error[CURL_ERROR_SIZE] = "";
    gchar *authorization = bearer == nullptr ? nullptr : g_strconcat("Bearer ", bearer, nullptr);
    struct curl_slist *headers = nullptr;
    curl_mime *form = nullptr;
    CURL *curl = curl_easy_init();
    bool ready =
            curl != nullptr && append_header(&headers, "Authorization", authorization) &&
            append_header(&headers, "X-Kotetsu-Now", now_header) &&
            curl_easy_setopt(curl, CURLOPT_URL, url) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)HTTP_CONNECT_TIMEOUT_SECONDS) ==
                    CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)HTTP_TOTAL_TIMEOUT_SECONDS) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, MAX_BODY_BYTES) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_body) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, out) == CURLE_OK;
    if (ready && parts != nullptr) {
        form = curl_mime_init(curl);
        ready = form != nullptr && add_parts(form, parts, count) &&
                curl_easy_setopt(curl, CURLOPT_MIMEPOST, form) == CURLE_OK &&
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT") == CURLE_OK;
    }
    const CURLcode result = ready ? curl_easy_perform(curl) : CURLE_FAILED_INIT;
    bool answered = false;
    if (!ready) {
        log_error("can't set up the request to %s", url);
    } else if (result != CURLE_OK) {
        log_error("%s: %s", url, error[0] != '\0' ? error : curl_easy_strerror(result));
    } else if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out->status) != CURLE_OK) {
        log_error("%s: no status code", url);
    } else {
        answered = true;
    }
    curl_easy_cleanup(curl);
    curl_mime_free(form);
    curl_slist_free_all(headers);
    g_free(authorization);
    if (!answered) {
        http_response_free(out);
    }
    return answered;
}

bool http_get(const char *url, const char *bearer, const char *now_header,
              struct http_response *out) {
    return request(url, bearer, now_header, nullptr, 0, out);
}

bool http_put_multipart(const char *url, const char *bearer, const char *now_header,
                        const struct http_part *parts, size_t count, struct http_response *out) {
    return request(url, bearer, now_header, parts, count, out);
}

void http_response_free(struct http_response *response) {
    g_free(response->body);
    *response = (struct http_response){};
}
