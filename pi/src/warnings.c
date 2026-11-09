#include "warnings.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <cjson/cJSON.h>

#include "shared/http.h"
#include "shared/json.h"
#include "shared/log.h"
#include "shared/text.h"
#include "shared/time.h"

#define WARSAW_COUNTY_CODE "1465"

static constexpr size_t IMGW_DATE_LENGTH = sizeof "YYYY-MM-DD" - 1;
static constexpr size_t IMGW_TIME_LENGTH = sizeof "YYYY-MM-DD HH:MM:SS" - 1;

struct warning_name {
    const char *polish;
    const char *label;
};

struct imgw_warning {
    const char *label;
    int level;
    time_t published;
};

static const struct warning_name WARNING_NAMES[] = {
        {"Silny wiatr", "大风预警"},
        {"Intensywne opady deszczu", "暴雨预警"},
        {"Silny deszcz z burzami", "暴雨预警"},
        {"Intensywne opady śniegu", "暴雪预警"},
        {"Burze", "雷暴预警"},
        {"Burze z gradem", "雷暴预警"},
        {"Silne burze", "雷暴预警"},
        {"Silne burze z gradem", "雷暴预警"},
        {"Upał", "高温预警"},
        {"Silny mróz", "严寒预警"},
        {"Opady marznące", "冻雨预警"},
        {"Oblodzenie", "结冰预警"},
        {"Gęsta mgła", "大雾预警"},
        {"Mgła intensywnie osadzająca szadź", "大雾预警"},
};

static bool read_warsaw_time(const cJSON *item, time_t *out) {
    /* read IMGW's "YYYY-MM-DD HH:MM:SS" as if it were UTC */
    const char *text = cJSON_GetStringValue(item);
    if (text == nullptr || strlen(text) != IMGW_TIME_LENGTH || text[IMGW_DATE_LENGTH] != ' ') {
        return false;
    }
    char iso[IMGW_TIME_LENGTH + 2];
    memcpy(iso, text, IMGW_TIME_LENGTH);
    iso[IMGW_DATE_LENGTH] = 'T';
    iso[IMGW_TIME_LENGTH] = 'Z';
    iso[IMGW_TIME_LENGTH + 1] = '\0';
    time_t wall = 0;
    if (!time_parse_iso8601(iso, &wall)) {
        return false;
    }

    /* take off Warsaw's offset: 2 hours in summer, 1 in winter */
    const time_t summer = wall - 2 * SECONDS_PER_HOUR;
    // a repeated autumn hour reads as its first pass, and a skipped spring hour as winter time
    const bool is_summer =
            time_warsaw_minute_of_day(summer) == wall % SECONDS_PER_DAY / SECONDS_PER_MINUTE;
    *out = is_summer ? summer : wall - SECONDS_PER_HOUR;
    return true;
}

static bool read_warning_in_force(const cJSON *entry, time_t now, struct imgw_warning *out) {
    /* read the entry, skipping one that doesn't parse */
    const char *event = cJSON_GetStringValue(json_member(entry, "nazwa_zdarzenia"));
    const cJSON *counties = json_member(entry, "teryt");
    const char *level = cJSON_GetStringValue(json_member(entry, "stopien"));
    time_t start = 0;
    time_t end = 0;
    time_t published = 0;
    if (event == nullptr || !cJSON_IsArray(counties) || level == nullptr || strlen(level) != 1 ||
        level[0] < '1' || level[0] > '9' ||
        !read_warsaw_time(json_member(entry, "obowiazuje_od"), &start) ||
        !read_warsaw_time(json_member(entry, "obowiazuje_do"), &end) ||
        !read_warsaw_time(json_member(entry, "opublikowano"), &published)) {
        const char *id = cJSON_GetStringValue(json_member(entry, "id"));
        log_warning("skipping IMGW warning %s, which doesn't parse",
                    id != nullptr ? id : "with no id");
        return false;
    }

    /* keep it only if it covers Warsaw now */
    bool covers_warsaw = false;
    const cJSON *county;
    cJSON_ArrayForEach(county, counties) {
        const char *code = cJSON_GetStringValue(county);
        if (code != nullptr && strcmp(code, WARSAW_COUNTY_CODE) == 0) {
            covers_warsaw = true;
        }
    }
    if (!covers_warsaw || now < start || now >= end) {
        return false;
    }

    /* name it in Chinese; a kind of warning with no label isn't shown */
    for (size_t i = 0; i < sizeof WARNING_NAMES / sizeof WARNING_NAMES[0]; i++) {
        if (text_equal_folded(event, WARNING_NAMES[i].polish)) {
            *out = (struct imgw_warning){WARNING_NAMES[i].label, level[0] - '0', published};
            return true;
        }
    }
    log_warning("IMGW warning \"%s\" has no Chinese label", event);
    return false;
}

static struct warnings warnings_parse(const char *json, time_t now) {
    /* IMGW answers with a message while nothing is in force, and with a list otherwise */
    cJSON *root = cJSON_Parse(json);
    struct warnings warnings = {.ok = true};
    if (!cJSON_IsArray(root)) {
        warnings.ok = cJSON_IsString(json_member(root, "message"));
        if (!warnings.ok) {
            log_error("IMGW's warnings aren't a JSON array");
        }
        cJSON_Delete(root);
        return warnings;
    }

    /* keep the top warning in force: the highest level, then the latest published */
    struct imgw_warning top = {};
    const cJSON *entry;
    cJSON_ArrayForEach(entry, root) {
        struct imgw_warning warning = {};
        if (read_warning_in_force(entry, now, &warning) &&
            (top.label == nullptr || warning.level > top.level ||
             (warning.level == top.level && warning.published > top.published))) {
            top = warning;
        }
    }
    cJSON_Delete(root);
    warnings.any = top.label != nullptr;
    if (warnings.any) {
        snprintf(warnings.label, sizeof warnings.label, "%s", top.label);
    }
    return warnings;
}

struct warnings warnings_read(const struct http_response *response, time_t now) {
    if (!http_ok(WARNINGS_URL, response)) {
        return (struct warnings){};
    }
    return warnings_parse(response->body, now);
}
