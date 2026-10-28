#pragma once

#include <stdint.h>

#include <cjson/cJSON.h>

const cJSON *json_member(const cJSON *object, const char *name);
// a number with no fraction within the ±2^53 that doubles hold exactly
[[nodiscard]] bool json_whole_number(const cJSON *item, int64_t *out);
