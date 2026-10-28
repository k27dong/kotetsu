#include "json.h"

#include <stdint.h>

#include <cjson/cJSON.h>

static constexpr double LARGEST_EXACT_INTEGER = 9'007'199'254'740'991.0;

const cJSON *json_member(const cJSON *object, const char *name) {
    return cJSON_GetObjectItemCaseSensitive(object, name);
}

bool json_whole_number(const cJSON *item, int64_t *out) {
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    const double value = item->valuedouble;
    if (value < -LARGEST_EXACT_INTEGER || value > LARGEST_EXACT_INTEGER ||
        (double)(int64_t)value != value) {
        return false;
    }
    *out = (int64_t)value;
    return true;
}
