#include "sources/air.h"

#include <cjson/cJSON.h>

#include "log.h"

bool air_parse_eaqi(const char *json, int *eaqi) {
    cJSON *root = cJSON_Parse(json);
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(
            cJSON_GetObjectItemCaseSensitive(root, "current"), "european_aqi");
    const bool parsed = cJSON_IsNumber(value) && value->valueint >= 0;
    if (parsed) {
        *eaqi = value->valueint;
    } else {
        log_error("Open-Meteo's air quality has no current european_aqi");
    }
    cJSON_Delete(root);
    return parsed;
}
