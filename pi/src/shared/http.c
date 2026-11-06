#include "shared/http.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include "config.h"
#include "shared/log.h"

#define USER_AGENT    "kotetsu/2.0 github.com/k27dong/kotetsu"
#define AUTHORIZATION "Authorization: Bearer "

static constexpr long CONNECT_TIMEOUT_SECONDS = 5;
static constexpr long TOTAL_TIMEOUT_SECONDS = 10;
static constexpr curl_off_t MAX_BODY_BYTES = 1024 * 1024;

static size_t append_body(char *chunk, size_t size, size_t count, void *context) {
    struct http_response *response = context;
    const size_t chunk_length = size * count;
    char *grown = realloc(response->body, response->length + chunk_length + 1);
    if (grown == nullptr) {
        return 0; // curl then fails the transfer
    }
    memcpy(grown + response->length, chunk, chunk_length);
    response->body = grown;
    response->length += chunk_length;
    response->body[response->length] = '\0';
    return chunk_length;
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
        const bool binary = strncmp(parts[i].content_type, "text/", strlen("text/")) != 0 &&
                            strcmp(parts[i].content_type, "application/json") != 0;
        if (binary && curl_mime_filename(field, parts[i].name) != CURLE_OK) {
            return false;
        }
    }
    return true;
}

static struct http_response request(const char *url, const char *bearer,
                                    const struct http_part *parts, size_t count) {
    /* set up a handle with the timeouts, the TLS checks and the body limit */
    struct http_response response = {.body = calloc(1, 1)};
    char error[CURL_ERROR_SIZE] = "";
    CURL *curl = curl_easy_init();
    bool ready =
            response.body != nullptr && curl != nullptr &&
            curl_easy_setopt(curl, CURLOPT_URL, url) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_SECONDS) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, TOTAL_TIMEOUT_SECONDS) == CURLE_OK &&
            // without it, a timed-out request still waits for a hung DNS lookup to finish
            curl_easy_setopt(curl, CURLOPT_QUICK_EXIT, 1L) == CURLE_OK &&
            // transfers run on several threads at once, so curl must leave SIGPIPE to main
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, MAX_BODY_BYTES) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_body) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response) == CURLE_OK;

    /* add the key as a bearer token */
    char authorization[sizeof AUTHORIZATION + PI_KEY_MAX_BYTES];
    struct curl_slist *headers = nullptr;
    if (ready && bearer != nullptr) {
        const int printed =
                snprintf(authorization, sizeof authorization, AUTHORIZATION "%s", bearer);
        if (printed > 0 && (size_t)printed < sizeof authorization) {
            headers = curl_slist_append(nullptr, authorization);
        }
        ready = headers != nullptr &&
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers) == CURLE_OK;
    }

    /* attach the parts of an upload */
    curl_mime *form = nullptr;
    if (ready && parts != nullptr) {
        form = curl_mime_init(curl);
        ready = form != nullptr && add_parts(form, parts, count) &&
                curl_easy_setopt(curl, CURLOPT_MIMEPOST, form) == CURLE_OK &&
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT") == CURLE_OK;
    }

    /* send it, and keep the status of any answer */
    const CURLcode result = ready ? curl_easy_perform(curl) : CURLE_FAILED_INIT;
    if (!ready) {
        log_error("can't set up the request to %s", url);
    } else if (result != CURLE_OK) {
        log_error("%s: %s", url, error[0] != '\0' ? error : curl_easy_strerror(result));
    } else if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status) != CURLE_OK) {
        log_error("%s: no status code", url);
    } else {
        response.ok = true;
    }

    /* free all but the answer */
    curl_easy_cleanup(curl);
    curl_mime_free(form);
    curl_slist_free_all(headers);
    if (!response.ok) {
        http_response_free(&response);
    }
    return response;
}

struct http_response http_get(const char *url, const char *bearer) {
    return request(url, bearer, nullptr, 0);
}

struct http_response http_get_ok(const char *url) {
    struct http_response response = http_get(url, nullptr);
    if (response.ok && response.status != HTTP_OK) {
        log_error("%s answered %ld", url, response.status);
        http_response_free(&response);
    }
    return response;
}

struct http_response http_put_multipart(const char *url, const char *bearer,
                                        const struct http_part *parts, size_t count) {
    return request(url, bearer, parts, count);
}

void http_response_free(struct http_response *response) {
    free(response->body);
    *response = (struct http_response){};
}
