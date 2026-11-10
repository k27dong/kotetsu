#pragma once

#include <stddef.h>

// errno says why it failed, EFBIG for a file of more than size bytes
[[nodiscard]] bool file_read(const char *path, void *out, size_t size, size_t *length);
// errno says why it failed
[[nodiscard]] bool file_write_atomic(const char *path, const void *data, size_t length);
