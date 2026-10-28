#pragma once

#include <stddef.h>
#include <stdint.h>

struct http_response {
    long status;
    char *body;
    size_t length;
};

struct http_part {
    const char *name;
    const char *content_type;
    const uint8_t *data;
    size_t length;
};

// true on any response, whatever its status; on true the body is NUL-terminated, "" when empty
[[nodiscard]] bool http_get(const char *url, const char *bearer, const char *now_header,
                            struct http_response *out);
[[nodiscard]] bool http_put_multipart(const char *url, const char *bearer, const char *now_header,
                                      const struct http_part *parts, size_t count,
                                      struct http_response *out);
void http_response_free(struct http_response *response);
