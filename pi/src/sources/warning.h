#pragma once

#include <stddef.h>
#include <stdint.h>

static constexpr size_t WARNING_LABEL_BYTES = 32;

const char *warning_label(const char *polish_event);
// Warsaw's warnings in force at now_s, highest level first, then the most recently published
size_t warning_parse_imgw(const char *json, int64_t now_s, char labels[][WARNING_LABEL_BYTES],
                          int *levels, size_t max);
