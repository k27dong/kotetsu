#include "sources/weather.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>
#include <glib.h>

#include "calendar/calendar.h"
#include "calendar/clock.h"
#include "config.h"
#include "json.h"
#include "log.h"
#include "model.h"
#include "sources/air.h"
#include "sources/http.h"
#include "sources/warning.h"

#define WEATHER_BASE_VARIABLE "KOTETSU_WEATHER_BASE"

#define OPEN_METEO_ORIGIN "https://api.open-meteo.com"
#define FORECAST_PATH                                                       \
    "/v1/forecast?latitude=" WARSAW_LATITUDE "&longitude=" WARSAW_LONGITUDE \
    "&current=temperature_2m,weather_code&hourly=weather_code"              \
    "&daily=temperature_2m_max,temperature_2m_min"                          \
    "&timezone=Europe%2FWarsaw&timeformat=unixtime&forecast_days=1"
#define AIR_QUALITY_ORIGIN "https://air-quality-api.open-meteo.com"
#define AIR_QUALITY_PATH                                                       \
    "/v1/air-quality?latitude=" WARSAW_LATITUDE "&longitude=" WARSAW_LONGITUDE \
    "&current=european_aqi"
#define MET_NORWAY_ORIGIN "https://api.met.no"
#define MET_NORWAY_PATH \
    "/weatherapi/locationforecast/2.0/compact?lat=" WARSAW_LATITUDE "&lon=" WARSAW_LONGITUDE
#define IMGW_ORIGIN        "https://danepubliczne.imgw.pl"
#define IMGW_WARNINGS_PATH "/api/data/warningsmeteo"

#define MINUS_SIGN  "−"
#define DEGREE_SIGN "°"

static constexpr int OPEN_METEO_FAILURES_BEFORE_MET_NORWAY = 2;
static constexpr int POOR_AIR_EAQI = 60;
static constexpr int EAQI_UNKNOWN = -1;
static constexpr double IMPLAUSIBLE_DEGREES = 100.0;
static constexpr int THUNDERSTORM_CODE = 95;
static constexpr int FIRST_PRECIPITATION_CODE = 51;
static constexpr int PRECIPITATION_AHEAD_UNTIL_MINUTE = 20 * 60; // later rain shows once it starts
static constexpr int64_t SECONDS_PER_HOUR = 3600;

struct met_symbol {
    const char *name;
    int code;
};

static const struct met_symbol MET_SYMBOLS[] = {
        {"clearsky", 0},
        {"fair", 1},
        {"partlycloudy", 2},
        {"cloudy", 3},
        {"fog", 45},
        {"lightrain", 61},
        {"rain", 63},
        {"heavyrain", 65},
        {"lightsleet", 66}, // MET's sleet is rain and snow; 冻雨 is the word with the sleet icon
        {"sleet", 66},
        {"heavysleet", 67},
        {"lightsnow", 71},
        {"snow", 73},
        {"heavysnow", 75},
        {"lightrainshowers", 80},
        {"rainshowers", 81},
        {"heavyrainshowers", 82},
        {"lightsleetshowers", 66},
        {"sleetshowers", 66},
        {"heavysleetshowers", 67},
        {"lightsnowshowers", 85},
        {"snowshowers", 85},
        {"heavysnowshowers", 86},
};

struct weather_word weather_word_for(int wmo_code) {
    switch (wmo_code) {
    case 0:
    case 1:
        return (struct weather_word){"晴", WEATHER_ICON_SUN};
    case 2:
        return (struct weather_word){"多云", WEATHER_ICON_PARTLY};
    case 3:
        return (struct weather_word){"阴", WEATHER_ICON_CLOUD};
    case 45:
    case 48:
        return (struct weather_word){"雾", WEATHER_ICON_FOG};
    case 51:
    case 53:
    case 55:
        return (struct weather_word){"毛毛雨", WEATHER_ICON_DRIZZLE};
    case 56:
    case 57:
    case 66:
    case 67:
        return (struct weather_word){"冻雨", WEATHER_ICON_SLEET};
    case 61:
        return (struct weather_word){"小雨", WEATHER_ICON_RAIN};
    case 63:
        return (struct weather_word){"中雨", WEATHER_ICON_RAIN};
    case 65:
    case 82:
        return (struct weather_word){"大雨", WEATHER_ICON_HEAVY_RAIN};
    case 80:
    case 81:
        return (struct weather_word){"阵雨", WEATHER_ICON_RAIN};
    case 71:
    case 77:
        return (struct weather_word){"小雪", WEATHER_ICON_SNOW};
    case 73:
        return (struct weather_word){"中雪", WEATHER_ICON_SNOW};
    case 75:
        return (struct weather_word){"大雪", WEATHER_ICON_HEAVY_SNOW};
    case 85:
    case 86:
        return (struct weather_word){"阵雪", WEATHER_ICON_SNOW};
    case 95:
    case 96:
    case 99:
        return (struct weather_word){"雷阵雨", WEATHER_ICON_STORM};
    default:
        return (struct weather_word){nullptr, WEATHER_ICON_OFFLINE};
    }
}

static bool read_degrees(const cJSON *item, int *out) {
    if (!cJSON_IsNumber(item) || item->valuedouble <= -IMPLAUSIBLE_DEGREES ||
        item->valuedouble >= IMPLAUSIBLE_DEGREES) {
        return false;
    }
    const double degrees = item->valuedouble;
    *out = (int)(degrees < 0 ? degrees - 0.5 : degrees + 0.5); // lround would need libm
    return true;
}

static bool read_code(const cJSON *item, int *out) {
    if (!cJSON_IsNumber(item) || weather_word_for(item->valueint).word == nullptr) {
        return false;
    }
    *out = item->valueint;
    return true;
}

static bool shows_ahead(int code, int64_t hour_s, int64_t now_s) {
    return code >= FIRST_PRECIPITATION_CODE && hour_s + SECONDS_PER_HOUR > now_s &&
           calendar_days_between(clock_warsaw_date(now_s), clock_warsaw_date(hour_s)) == 0 &&
           clock_warsaw_minute_of_day(hour_s) < PRECIPITATION_AHEAD_UNTIL_MINUTE;
}

bool weather_parse_open_meteo(const char *json, int64_t fetched_s, struct weather_report *out) {
    cJSON *root = cJSON_Parse(json);
    const cJSON *current = json_member(root, "current");
    const cJSON *hour_times = json_member(json_member(root, "hourly"), "time");
    const cJSON *hour_codes = json_member(json_member(root, "hourly"), "weather_code");
    const cJSON *daily = json_member(root, "daily");
    struct weather_report report = {.fetched_s = fetched_s};
    bool parsed = read_degrees(json_member(current, "temperature_2m"), &report.now_c) &&
                  read_code(json_member(current, "weather_code"), &report.code) &&
                  read_degrees(cJSON_GetArrayItem(json_member(daily, "temperature_2m_min"), 0),
                               &report.low_c) &&
                  read_degrees(cJSON_GetArrayItem(json_member(daily, "temperature_2m_max"), 0),
                               &report.high_c) &&
                  cJSON_IsArray(hour_times) && cJSON_IsArray(hour_codes) &&
                  cJSON_GetArraySize(hour_times) == cJSON_GetArraySize(hour_codes);
    for (int i = 0; parsed && i < cJSON_GetArraySize(hour_times); i++) {
        int64_t hour_s = 0;
        int code = 0;
        parsed = json_whole_number(cJSON_GetArrayItem(hour_times, i), &hour_s) &&
                 read_code(cJSON_GetArrayItem(hour_codes, i), &code);
        if (parsed && code > report.code && shows_ahead(code, hour_s, fetched_s)) {
            report.code = code;
        }
    }
    cJSON_Delete(root);
    if (!parsed) {
        log_error("Open-Meteo's forecast lacks today's temperatures or weather codes");
        return false;
    }
    *out = report;
    return true;
}

static bool met_code(const char *symbol, int *out) {
    if (strstr(symbol, "thunder") != nullptr) {
        *out = THUNDERSTORM_CODE;
        return true;
    }
    const size_t length = strcspn(symbol, "_"); // MET adds _day, _night or _polartwilight to some
    for (size_t i = 0; i < G_N_ELEMENTS(MET_SYMBOLS); i++) {
        if (strlen(MET_SYMBOLS[i].name) == length &&
            strncmp(symbol, MET_SYMBOLS[i].name, length) == 0) {
            *out = MET_SYMBOLS[i].code;
            return true;
        }
    }
    return false;
}

static int met_entry_code(const cJSON *data) {
    const char *symbol = cJSON_GetStringValue(
            json_member(json_member(json_member(data, "next_1_hours"), "summary"), "symbol_code"));
    int code = -1;
    if (symbol != nullptr && !met_code(symbol, &code)) {
        log_warning("MET Norway's symbol %s has no weather code", symbol);
    }
    return code;
}

bool weather_parse_met_norway(const char *json, struct civil_date today, int64_t fetched_s,
                              struct weather_report *out) {
    cJSON *root = cJSON_Parse(json);
    const cJSON *series = json_member(json_member(root, "properties"), "timeseries");
    struct weather_report report = {.fetched_s = fetched_s, .code = -1};
    bool parsed = cJSON_IsArray(series);
    bool have_now = false;
    bool have_today = false;
    int ahead_code = -1;
    const cJSON *entry;
    cJSON_ArrayForEach(entry, series) {
        const cJSON *data = json_member(entry, "data");
        int64_t entry_s = 0;
        int degrees = 0;
        const char *time = cJSON_GetStringValue(json_member(entry, "time"));
        if (time == nullptr || !clock_parse_iso8601(time, &entry_s) ||
            !read_degrees(json_member(json_member(json_member(data, "instant"), "details"),
                                      "air_temperature"),
                          &degrees)) {
            parsed = false;
            break;
        }
        const bool is_now = !have_now || entry_s <= fetched_s;
        const bool is_today = calendar_days_between(today, clock_warsaw_date(entry_s)) == 0;
        if (!is_now && !is_today) {
            continue;
        }
        const int code = met_entry_code(data);
        if (is_now) {
            report.now_c = degrees;
            report.code = code;
            have_now = true;
        }
        if (is_today) {
            report.low_c = !have_today || degrees < report.low_c ? degrees : report.low_c;
            report.high_c = !have_today || degrees > report.high_c ? degrees : report.high_c;
            have_today = true;
            if (code > ahead_code && shows_ahead(code, entry_s, fetched_s)) {
                ahead_code = code;
            }
        }
    }
    report.code = ahead_code > report.code ? ahead_code : report.code; // higher codes are worse
    cJSON_Delete(root);
    if (!parsed) {
        log_error("MET Norway's forecast doesn't parse");
        return false;
    }
    if (!have_today || report.code < 0) {
        log_error("MET Norway has no weather for today");
        return false;
    }
    *out = report;
    return true;
}

static const char *sign_of(int degrees) {
    return degrees < 0 ? MINUS_SIGN : "";
}

struct weather_view weather_view_for(const struct weather_inputs *inputs) {
    const struct weather_view unavailable = {
            .icon = WEATHER_ICON_OFFLINE,
            .temperature = "—",
            .line = "天气暂不可用",
            .line_kind = WEATHER_LINE_UNAVAILABLE,
    };
    const struct weather_report *report = &inputs->report;
    const int64_t age_s = inputs->now_s - report->fetched_s;
    if (!inputs->have_report || age_s >= WEATHER_OFFLINE_SECONDS) {
        return unavailable;
    }
    const struct weather_word word = weather_word_for(report->code);
    if (word.word == nullptr) {
        log_error("weather code %d has no word", report->code);
        return unavailable;
    }
    struct weather_view view = {.icon = word.icon};
    snprintf(view.temperature, sizeof view.temperature, "%s%d" DEGREE_SIGN, sign_of(report->now_c),
             abs(report->now_c));
    if (age_s >= WEATHER_STALE_SECONDS) {
        const int minute = clock_warsaw_minute_of_day(report->fetched_s);
        snprintf(view.line, sizeof view.line, "%02d:%02d更新", minute / 60, minute % 60);
        view.line_kind = WEATHER_LINE_UPDATED;
        return view;
    }
    snprintf(view.range, sizeof view.range, "%s%d~%s%d" DEGREE_SIGN, sign_of(report->low_c),
             abs(report->low_c), sign_of(report->high_c), abs(report->high_c));
    if (inputs->have_warning) {
        snprintf(view.line, sizeof view.line, "%s", inputs->warning);
        view.line_kind = WEATHER_LINE_BADGE;
    } else if (inputs->eaqi >= POOR_AIR_EAQI) {
        snprintf(view.line, sizeof view.line, "%s", "空气差");
        view.line_kind = WEATHER_LINE_BADGE;
    } else {
        snprintf(view.line, sizeof view.line, "%s", word.word);
        view.line_kind = WEATHER_LINE_WORD;
    }
    return view;
}

static bool fetch(const char *origin, const char *path, struct http_response *out) {
    const char *base = getenv(WEATHER_BASE_VARIABLE);
    gchar *url = g_strconcat(base != nullptr ? base : origin, path, nullptr);
    bool fetched = http_get(url, nullptr, nullptr, out);
    if (fetched && out->status != 200) {
        log_error("%s answered %ld", url, out->status);
        http_response_free(out);
        fetched = false;
    }
    g_free(url);
    return fetched;
}

bool weather_fetch(struct weather_source *source, int64_t now_s, struct weather_inputs *out) {
    struct weather_report report = {};
    struct http_response response;
    bool have_report = fetch(OPEN_METEO_ORIGIN, FORECAST_PATH, &response) &&
                       weather_parse_open_meteo(response.body, now_s, &report);
    http_response_free(&response);
    if (have_report) {
        source->open_meteo_failures = 0;
    } else if (++source->open_meteo_failures >= OPEN_METEO_FAILURES_BEFORE_MET_NORWAY) {
        have_report =
                fetch(MET_NORWAY_ORIGIN, MET_NORWAY_PATH, &response) &&
                weather_parse_met_norway(response.body, clock_warsaw_date(now_s), now_s, &report);
        http_response_free(&response);
    }
    if (!have_report) {
        return false;
    }
    int eaqi = EAQI_UNKNOWN;
    const bool have_eaqi = fetch(AIR_QUALITY_ORIGIN, AIR_QUALITY_PATH, &response) &&
                           air_parse_eaqi(response.body, &eaqi);
    http_response_free(&response);
    char labels[1][WARNING_LABEL_BYTES];
    int levels[1];
    const bool have_warning = fetch(IMGW_ORIGIN, IMGW_WARNINGS_PATH, &response) &&
                              warning_parse_imgw(response.body, now_s, labels, levels, 1) == 1;
    http_response_free(&response);
    *out = (struct weather_inputs){
            .have_report = true,
            .report = report,
            .eaqi = have_eaqi ? eaqi : EAQI_UNKNOWN,
            .have_warning = have_warning,
            .now_s = now_s,
    };
    if (have_warning) {
        g_strlcpy(out->warning, labels[0], sizeof out->warning);
    }
    return true;
}
