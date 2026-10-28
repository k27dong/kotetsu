#pragma once

#include <stdint.h>

#include "model.h"
#include "sources/warning.h"

struct weather_report {
    int64_t fetched_s;
    int now_c;
    int low_c;
    int high_c;
    int code;
};

struct weather_word {
    const char *word;
    enum weather_icon icon;
};

struct weather_inputs {
    bool have_report;
    struct weather_report report;
    int eaqi;
    bool have_warning;
    char warning[WARNING_LABEL_BYTES];
    int64_t now_s;
};

struct weather_source {
    int open_meteo_failures;
};

struct weather_word weather_word_for(int wmo_code);
[[nodiscard]] bool weather_parse_open_meteo(const char *json, int64_t fetched_s,
                                            struct weather_report *out);
[[nodiscard]] bool weather_parse_met_norway(const char *json, struct civil_date today,
                                            int64_t fetched_s, struct weather_report *out);
struct weather_view weather_view_for(const struct weather_inputs *inputs);
// leaves *out untouched when neither forecast comes back
[[nodiscard]] bool weather_fetch(struct weather_source *source, int64_t now_s,
                                 struct weather_inputs *out);
