#pragma once

#include <stddef.h>

// both leave errno set on failure; reading a file of more than size bytes fails with EFBIG
[[nodiscard]] bool file_read(const char *path, void *out, size_t size, size_t *length);
[[nodiscard]] bool file_write_atomic(const char *path, const void *data, size_t length);
