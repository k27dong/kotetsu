#pragma once

#include <time.h>

#include "warnings.h"

enum weather_icon {
    WEATHER_ICON_SUN,
    WEATHER_ICON_PARTLY,
    WEATHER_ICON_CLOUD,
    WEATHER_ICON_FOG,
    WEATHER_ICON_DRIZZLE,
    WEATHER_ICON_RAIN,
    WEATHER_ICON_HEAVY_RAIN,
    WEATHER_ICON_STORM,
    WEATHER_ICON_SLEET,
    WEATHER_ICON_SNOW,
    WEATHER_ICON_HEAVY_SNOW,
    WEATHER_ICON_OFFLINE,
};

enum weather_line_kind {
    WEATHER_LINE_WORD,
    WEATHER_LINE_BADGE,
    WEATHER_LINE_UPDATED,
    WEATHER_LINE_UNAVAILABLE,
};

struct weather_view {
    enum weather_icon icon;
    char temperature[16];
    char range[32];
    char line[64];
    enum weather_line_kind line_kind;
};

struct weather_report {
    time_t fetched_at;
    int now_c;
    int low_c;
    int high_c;
    int code;
};

struct weather_state {
    bool have_report;
    struct weather_report report;
    time_t next_fetch; // 0 before the first fetch
    int open_meteo_failures;
    int eaqi; // -1 when unknown
    bool have_warning;
    char warning[WARNING_LABEL_BYTES];
};

struct forecast {
    bool ok;
    bool from_met;
    struct weather_report report;
};

struct air {
    bool ok;
    int eaqi;
};

struct weather_fetched { // one thread fills each field
    struct forecast forecast;
    struct air air;
    struct warnings warnings;
};

bool weather_due(const struct weather_state *state, time_t now);
struct forecast weather_fetch_forecast(const struct weather_state *state, time_t now);
struct air weather_fetch_air(void);
void weather_absorb(struct weather_state *state, const struct weather_fetched *fetched, time_t now);
struct weather_view weather_view(const struct weather_state *state, time_t now);
[[nodiscard]] bool weather_parse_open_meteo(const char *json, time_t now,
                                            struct weather_report *out);
[[nodiscard]] bool weather_parse_met_norway(const char *json, time_t now,
                                            struct weather_report *out);
[[nodiscard]] bool weather_parse_eaqi(const char *json, int *eaqi);
