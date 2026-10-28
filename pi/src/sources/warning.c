#include "sources/warning.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>
#include <glib.h>

#include "calendar/clock.h"
#include "config.h"
#include "log.h"
#include "render/text.h"

static constexpr int64_t SECONDS_PER_MINUTE = 60;
static constexpr int64_t SECONDS_PER_HOUR = 3600;
static constexpr int64_t SECONDS_PER_DAY = 86'400;
static constexpr size_t IMGW_DATE_LENGTH = sizeof "YYYY-MM-DD" - 1;
static constexpr size_t IMGW_TIME_LENGTH = sizeof "YYYY-MM-DD HH:MM:SS" - 1;

struct warning_name {
    const char *polish;
    const char *label;
};

struct imgw_warning {
    const char *label;
    int level;
    int64_t published_s;
};

static const struct warning_name WARNING_NAMES[] = {
        {"Silny wiatr", "大风预警"},
        {"Intensywne opady deszczu", "暴雨预警"},
        {"Intensywne opady śniegu", "暴雪预警"},
        {"Burze", "雷暴预警"},
        {"Upał", "高温预警"},
        {"Silny mróz", "严寒预警"},
        {"Marznące opady", "冻雨预警"},
        {"Oblodzenie", "结冰预警"},
        {"Gęsta mgła", "大雾预警"},
};

const char *warning_label(const char *polish_event) {
    for (size_t i = 0; i < G_N_ELEMENTS(WARNING_NAMES); i++) {
        if (text_equal_folded(polish_event, WARNING_NAMES[i].polish)) {
            return WARNING_NAMES[i].label;
        }
    }
    return nullptr;
}

static bool read_warsaw_time(const cJSON *item, int64_t *out_s) {
    const char *text = cJSON_GetStringValue(item);
    if (text == nullptr || strlen(text) != IMGW_TIME_LENGTH || text[IMGW_DATE_LENGTH] != ' ') {
        return false;
    }
    char iso[IMGW_TIME_LENGTH + 2];
    memcpy(iso, text, IMGW_TIME_LENGTH);
    iso[IMGW_DATE_LENGTH] = 'T';
    iso[IMGW_TIME_LENGTH] = 'Z';
    iso[IMGW_TIME_LENGTH + 1] = '\0';
    int64_t wall_s = 0;
    if (!clock_parse_iso8601(iso, &wall_s)) {
        return false;
    }
    const int64_t summer_s = wall_s - 2 * SECONDS_PER_HOUR;
    // a repeated autumn hour reads as its first pass, and a skipped spring hour as winter time
    const bool summer =
            clock_warsaw_minute_of_day(summer_s) == wall_s % SECONDS_PER_DAY / SECONDS_PER_MINUTE;
    *out_s = summer ? summer_s : wall_s - SECONDS_PER_HOUR;
    return true;
}

static bool read_level(const cJSON *item, int *out) {
    const char *text = cJSON_GetStringValue(item);
    if (text == nullptr || strlen(text) != 1 || text[0] < '1' || text[0] > '9') {
        return false;
    }
    *out = text[0] - '0';
    return true;
}

static bool covers_warsaw(const cJSON *teryt) {
    const cJSON *code;
    cJSON_ArrayForEach(code, teryt) {
        if (g_strcmp0(cJSON_GetStringValue(code), WARSAW_COUNTY_CODE) == 0) {
            return true;
        }
    }
    return false;
}

static int compare_warnings(const void *a, const void *b) {
    const struct imgw_warning *first = a;
    const struct imgw_warning *second = b;
    if (first->level != second->level) {
        return second->level - first->level;
    }
    return (second->published_s > first->published_s) - (second->published_s < first->published_s);
}

size_t warning_parse_imgw(const char *json, int64_t now_s, char labels[][WARNING_LABEL_BYTES],
                          int *levels, size_t max) {
    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsArray(root)) {
        log_error("IMGW's warnings aren't a JSON array");
        cJSON_Delete(root);
        return 0;
    }
    struct imgw_warning *found = g_new(struct imgw_warning, (gsize)cJSON_GetArraySize(root));
    size_t found_count = 0;
    const cJSON *entry;
    cJSON_ArrayForEach(entry, root) {
        const char *event =
                cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(entry, "nazwa_zdarzenia"));
        const cJSON *teryt = cJSON_GetObjectItemCaseSensitive(entry, "teryt");
        struct imgw_warning warning = {};
        int64_t start_s = 0;
        int64_t end_s = 0;
        if (event == nullptr || !cJSON_IsArray(teryt) ||
            !read_level(cJSON_GetObjectItemCaseSensitive(entry, "stopien"), &warning.level) ||
            !read_warsaw_time(cJSON_GetObjectItemCaseSensitive(entry, "obowiazuje_od"), &start_s) ||
            !read_warsaw_time(cJSON_GetObjectItemCaseSensitive(entry, "obowiazuje_do"), &end_s) ||
            !read_warsaw_time(cJSON_GetObjectItemCaseSensitive(entry, "opublikowano"),
                              &warning.published_s)) {
            const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(entry, "id"));
            log_warning("skipping IMGW warning %s, which doesn't parse",
                        id != nullptr ? id : "with no id");
            continue;
        }
        if (!covers_warsaw(teryt) || now_s < start_s || now_s >= end_s) {
            continue;
        }
        warning.label = warning_label(event);
        if (warning.label == nullptr) {
            log_warning("IMGW warning \"%s\" has no Chinese label", event);
            continue;
        }
        found[found_count++] = warning;
    }
    if (found_count > 1) {
        qsort(found, found_count, sizeof *found, compare_warnings);
    }
    const size_t count = found_count < max ? found_count : max;
    for (size_t i = 0; i < count; i++) {
        g_strlcpy(labels[i], found[i].label, WARNING_LABEL_BYTES);
        levels[i] = found[i].level;
    }
    g_free(found);
    cJSON_Delete(root);
    return count;
}
