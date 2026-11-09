#pragma once

#include <time.h>

#include "shared/http.h"
#include "warnings.h"

#define WARSAW_LATITUDE  "52.23"
#define WARSAW_LONGITUDE "21.01"

#define WEATHER_OPEN_METEO_URL                                                                \
    "https://api.open-meteo.com/v1/forecast?latitude=" WARSAW_LATITUDE                        \
    "&longitude=" WARSAW_LONGITUDE "&current=temperature_2m,weather_code&hourly=weather_code" \
    "&daily=temperature_2m_max,temperature_2m_min"                                            \
    "&timezone=Europe%2FWarsaw&timeformat=unixtime&forecast_days=1"
#define WEATHER_MET_NORWAY_URL                                                        \
    "https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=" WARSAW_LATITUDE \
    "&lon=" WARSAW_LONGITUDE
#define WEATHER_AIR_QUALITY_URL                                                       \
    "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" WARSAW_LATITUDE \
    "&longitude=" WARSAW_LONGITUDE "&current=european_aqi"

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

[[nodiscard]] struct forecast weather_read_open_meteo(const struct http_response *response,
                                                      time_t now);
[[nodiscard]] struct forecast weather_read_met_norway(const struct http_response *response,
                                                      time_t now);
bool weather_wants_met_norway(const struct weather_state *state, const struct forecast *open_meteo);
// a failed forecast keeps the last report, air and warning
[[nodiscard]] struct weather_state weather_update(struct weather_state previous,
                                                  const struct forecast *forecast,
                                                  const struct http_response *air,
                                                  const struct http_response *warnings, time_t now);
struct weather_view weather_view(const struct weather_state *state, time_t now);
