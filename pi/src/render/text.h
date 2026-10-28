#pragma once

#include <stddef.h>
#include <stdint.h>

struct byte_range {
    size_t start;
    size_t end;
};

[[nodiscard]] bool text_is_valid_utf8(const char *text, size_t length);
bool text_is_digit(uint32_t code_point);
bool text_is_han(uint32_t code_point);
size_t text_digit_runs(const char *text, struct byte_range *out, size_t max);
size_t text_gap_offsets(const char *text, size_t *out, size_t max);
bool text_equal_folded(const char *a, const char *b);
