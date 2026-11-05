#pragma once

#include <stddef.h>
#include <stdint.h>

static constexpr long HTTP_OK = 200;

struct http_response {
    bool ok; // an answer came back, whatever its status
    long status;
    char *body; // NUL-terminated, "" when empty; nullptr unless ok
    size_t length;
};

struct http_part {
    const char *name;
    const char *content_type;
    const uint8_t *data;
    size_t length;
};

// one curl handle per call, so threads can make requests at once; main runs curl_global_init
struct http_response http_get(const char *url, const char *bearer);
struct http_response http_get_ok(const char *url); // only a 200 is ok; any other status is logged
struct http_response http_put_multipart(const char *url, const char *bearer,
                                        const struct http_part *parts, size_t count);
void http_response_free(struct http_response *response);
