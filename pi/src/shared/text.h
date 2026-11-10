#pragma once

#include <stddef.h>

[[nodiscard]] bool text_is_valid_utf8(const char *text, size_t length);
// compares the composed, case-folded forms; false for text that isn't UTF-8
bool text_equal_folded(const char *a, const char *b);
