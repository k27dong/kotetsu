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
static constexpr int POLL_TIMEOUT_MS = 1000;

struct transfer {
    CURL *curl;
    struct curl_slist *headers;
    char error[CURL_ERROR_SIZE];
    struct http_response response;
};

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

static bool set_up_transfer(struct transfer *transfer, const char *url, const char *bearer) {
    /* set every request's options */
    CURL *curl = curl_easy_init();
    transfer->curl = curl;
    transfer->response.body = calloc(1, 1);
    bool ready =
            transfer->response.body != nullptr && curl != nullptr &&
            curl_easy_setopt(curl, CURLOPT_URL, url) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_SECONDS) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, TOTAL_TIMEOUT_SECONDS) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_QUICK_EXIT, 1L) == CURLE_OK && // don't wait on hung DNS
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) == CURLE_OK &&   // leave SIGPIPE to main
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, MAX_BODY_BYTES) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, transfer->error) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_body) == CURLE_OK &&
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer->response) == CURLE_OK;

    /* add the bearer token */
    char authorization[sizeof AUTHORIZATION + PI_KEY_MAX_BYTES];
    if (ready && bearer != nullptr) {
        const int printed =
                snprintf(authorization, sizeof authorization, AUTHORIZATION "%s", bearer);
        if (printed > 0 && (size_t)printed < sizeof authorization) {
            transfer->headers = curl_slist_append(nullptr, authorization);
        }
        ready = transfer->headers != nullptr &&
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, transfer->headers) == CURLE_OK;
    }
    return ready;
}

static void keep_status(struct transfer *transfer, const char *url, CURLcode result) {
    if (result != CURLE_OK) {
        log_error("%s: %s", url,
                  transfer->error[0] != '\0' ? transfer->error : curl_easy_strerror(result));
    } else if (curl_easy_getinfo(transfer->curl, CURLINFO_RESPONSE_CODE,
                                 &transfer->response.status) != CURLE_OK) {
        log_error("%s: no status code", url);
    } else {
        transfer->response.ok = true;
    }
}

static struct http_response take_answer(struct transfer *transfer) {
    curl_easy_cleanup(transfer->curl);
    curl_slist_free_all(transfer->headers);
    if (!transfer->response.ok) {
        http_response_free(&transfer->response);
    }
    return transfer->response;
}

static struct http_response request(const char *url, const char *bearer,
                                    const struct http_part *parts, size_t count) {
    /* set up the transfer */
    struct transfer transfer = {};
    bool ready = set_up_transfer(&transfer, url, bearer);

    /* attach an upload's parts */
    curl_mime *form = nullptr;
    if (ready && parts != nullptr) {
        form = curl_mime_init(transfer.curl);
        ready = form != nullptr && add_parts(form, parts, count) &&
                curl_easy_setopt(transfer.curl, CURLOPT_MIMEPOST, form) == CURLE_OK &&
                curl_easy_setopt(transfer.curl, CURLOPT_CUSTOMREQUEST, "PUT") == CURLE_OK;
    }

    /* send the request */
    if (ready) {
        keep_status(&transfer, url, curl_easy_perform(transfer.curl));
    } else {
        log_error("can't set up the request to %s", url);
    }

    /* free all but the answer */
    const struct http_response response = take_answer(&transfer);
    curl_mime_free(form);
    return response;
}

struct http_response http_get(const char *url, const char *bearer) {
    return request(url, bearer, nullptr, 0);
}

struct http_response http_get_ok(const char *url) {
    struct http_response response = http_get(url, nullptr);
    if (!http_ok(url, &response)) {
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

struct http_batch http_get_batch(const struct http_request *requests, size_t count) {
    if (count == 0 || count > HTTP_BATCH_MAX) {
        log_error("a batch holds 1 to %zu requests, not %zu", HTTP_BATCH_MAX, count);
        abort();
    }

    /* add one transfer per request */
    struct http_batch batch = {.count = count};
    CURLM *multi = curl_multi_init();
    if (multi == nullptr) {
        log_error("can't set up the requests");
        return batch;
    }
    struct transfer transfers[HTTP_BATCH_MAX] = {};
    for (size_t i = 0; i < count; i++) {
        if (!set_up_transfer(&transfers[i], requests[i].url, requests[i].bearer) ||
            curl_multi_add_handle(multi, transfers[i].curl) != CURLM_OK) {
            log_error("can't set up the request to %s", requests[i].url);
        }
    }

    /* run them all at once */
    int running = 0;
    CURLMcode code = CURLM_OK;
    do {
        code = curl_multi_perform(multi, &running);
        if (code == CURLM_OK && running > 0) {
            code = curl_multi_poll(multi, nullptr, 0, POLL_TIMEOUT_MS, nullptr);
        }
    } while (code == CURLM_OK && running > 0);
    if (code != CURLM_OK) {
        log_error("can't run the requests: %s", curl_multi_strerror(code));
    }

    /* keep each answer's status */
    CURLMsg *message = nullptr;
    int queued = 0;
    while ((message = curl_multi_info_read(multi, &queued)) != nullptr) {
        for (size_t i = 0; i < count; i++) {
            if (message->msg == CURLMSG_DONE && message->easy_handle == transfers[i].curl) {
                keep_status(&transfers[i], requests[i].url, message->data.result);
            }
        }
    }

    /* free all but the answers */
    for (size_t i = 0; i < count; i++) {
        curl_multi_remove_handle(multi, transfers[i].curl);
        batch.responses[i] = take_answer(&transfers[i]);
    }
    curl_multi_cleanup(multi);
    return batch;
}

void http_batch_free(struct http_batch *batch) {
    for (size_t i = 0; i < batch->count; i++) {
        http_response_free(&batch->responses[i]);
    }
    *batch = (struct http_batch){};
}

bool http_ok(const char *url, const struct http_response *response) {
    if (!response->ok) {
        return false; // the failed transfer was already logged
    }
    if (response->status != HTTP_OK) {
        log_error("%s answered %ld", url, response->status);
        return false;
    }
    return true;
}
