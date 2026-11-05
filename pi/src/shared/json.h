#pragma once

#include <stdint.h>

#include <cjson/cJSON.h>

const cJSON *json_member(const cJSON *object, const char *name); // nullptr for a non-object
// a whole number within ±(2^53 − 1), so reading it can't have rounded it
[[nodiscard]] bool json_whole_number(const cJSON *item, int64_t *out);
