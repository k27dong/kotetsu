#pragma once

#include <stddef.h>

[[nodiscard]] bool text_is_valid_utf8(const char *text, size_t length);
bool text_equal_folded(const char *a, const char *b); // composed, case-folded; false if not UTF-8
