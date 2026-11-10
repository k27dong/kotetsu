#pragma once

#include <stdint.h>

#include <cjson/cJSON.h>

// nullptr for a non-object
const cJSON *json_member(const cJSON *object, const char *name);
// true for a whole number within ±(2^53 − 1), where doubles are exact
[[nodiscard]] bool json_whole_number(const cJSON *item, int64_t *out);
