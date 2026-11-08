#include "weather.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cjson/cJSON.h>

#include "shared/http.h"
#include "shared/json.h"
#include "shared/log.h"
#include "shared/time.h"
#include "warnings.h"

#define WARSAW_LATITUDE  "52.23"
#define WARSAW_LONGITUDE "21.01"

#define OPEN_METEO_URL                                                                        \
    "https://api.open-meteo.com/v1/forecast?latitude=" WARSAW_LATITUDE                        \
    "&longitude=" WARSAW_LONGITUDE "&current=temperature_2m,weather_code&hourly=weather_code" \
    "&daily=temperature_2m_max,temperature_2m_min"                                            \
    "&timezone=Europe%2FWarsaw&timeformat=unixtime&forecast_days=1"
#define MET_NORWAY_URL                                                                \
    "https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=" WARSAW_LATITUDE \
    "&lon=" WARSAW_LONGITUDE
#define AIR_QUALITY_URL                                                               \
    "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" WARSAW_LATITUDE \
    "&longitude=" WARSAW_LONGITUDE "&current=european_aqi"

#define MINUS_SIGN  "−"
#define DEGREE_SIGN "°"

static constexpr int FETCH_PERIOD_SECONDS = 30 * SECONDS_PER_MINUTE;
static constexpr int STALE_AFTER_SECONDS = 2 * SECONDS_PER_HOUR;
static constexpr int OFFLINE_AFTER_SECONDS = 12 * SECONDS_PER_HOUR;
static constexpr int OPEN_METEO_FAILURES_BEFORE_MET_NORWAY = 2;
static constexpr int POOR_AIR_EAQI = 60;
static constexpr int EAQI_UNKNOWN = -1;
static constexpr int NO_WEATHER_CODE = -1;
static constexpr int FIRST_PRECIPITATION_CODE = 51;
static constexpr int THUNDERSTORM_CODE = 95;
static constexpr double IMPLAUSIBLE_DEGREES = 100.0;
// rain due from 20:00 isn't shown ahead; it shows once it starts
static constexpr int PRECIPITATION_AHEAD_UNTIL_MINUTE = 20 * MINUTES_PER_HOUR;

struct weather_word {
    const char *word;
    enum weather_icon icon;
};

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

static struct weather_word word_for(int code) {
    switch (code) {
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
    if (!cJSON_IsNumber(item) || word_for(item->valueint).word == nullptr) {
        return false;
    }
    *out = item->valueint;
    return true;
}

static bool shows_ahead(int code, time_t hour, time_t now) {
    return code >= FIRST_PRECIPITATION_CODE && hour + SECONDS_PER_HOUR > now &&
           time_same_date(time_warsaw_date(hour), time_warsaw_date(now)) &&
           time_warsaw_minute_of_day(hour) < PRECIPITATION_AHEAD_UNTIL_MINUTE;
}

bool weather_parse_open_meteo(const char *json, time_t now, struct weather_report *out) {
    /* read the weather now and today's range */
    cJSON *root = cJSON_Parse(json);
    const cJSON *current = json_member(root, "current");
    const cJSON *daily = json_member(root, "daily");
    const cJSON *hour_times = json_member(json_member(root, "hourly"), "time");
    const cJSON *hour_codes = json_member(json_member(root, "hourly"), "weather_code");
    struct weather_report report = {.fetched_at = now};
    bool parsed = read_degrees(json_member(current, "temperature_2m"), &report.now_c) &&
                  read_code(json_member(current, "weather_code"), &report.code) &&
                  read_degrees(cJSON_GetArrayItem(json_member(daily, "temperature_2m_min"), 0),
                               &report.low_c) &&
                  read_degrees(cJSON_GetArrayItem(json_member(daily, "temperature_2m_max"), 0),
                               &report.high_c) &&
                  cJSON_IsArray(hour_times) && cJSON_IsArray(hour_codes) &&
                  cJSON_GetArraySize(hour_times) == cJSON_GetArraySize(hour_codes);

    /* rain, snow or a storm due today before 20:00 shows instead, if its code is higher */
    for (int i = 0; parsed && i < cJSON_GetArraySize(hour_times); i++) {
        int64_t hour = 0;
        int code = 0;
        parsed = json_whole_number(cJSON_GetArrayItem(hour_times, i), &hour) &&
                 read_code(cJSON_GetArrayItem(hour_codes, i), &code);
        if (parsed && code > report.code && shows_ahead(code, hour, now)) {
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

static int met_code(const cJSON *data) {
    /* take the symbol for the coming hour; entries far ahead have none */
    const char *symbol = cJSON_GetStringValue(
            json_member(json_member(json_member(data, "next_1_hours"), "summary"), "symbol_code"));
    if (symbol == nullptr) {
        return NO_WEATHER_CODE;
    }

    /* match it without the _day, _night or _polartwilight MET adds to some */
    if (strstr(symbol, "thunder") != nullptr) {
        return THUNDERSTORM_CODE;
    }
    const size_t length = strcspn(symbol, "_");
    for (size_t i = 0; i < sizeof MET_SYMBOLS / sizeof MET_SYMBOLS[0]; i++) {
        if (strlen(MET_SYMBOLS[i].name) == length &&
            strncmp(symbol, MET_SYMBOLS[i].name, length) == 0) {
            return MET_SYMBOLS[i].code;
        }
    }
    log_warning("MET Norway's symbol %s has no weather code", symbol);
    return NO_WEATHER_CODE;
}

bool weather_parse_met_norway(const char *json, time_t now, struct weather_report *out) {
    /* go through the time series, which needs a time and a temperature in every entry */
    cJSON *root = cJSON_Parse(json);
    const cJSON *series = json_member(json_member(root, "properties"), "timeseries");
    const struct civil_date today = time_warsaw_date(now);
    struct weather_report report = {.fetched_at = now, .code = NO_WEATHER_CODE};
    bool parsed = cJSON_IsArray(series);
    bool have_now = false;
    bool have_today = false;
    int ahead_code = NO_WEATHER_CODE;
    const cJSON *entry;
    cJSON_ArrayForEach(entry, series) {
        const cJSON *data = json_member(entry, "data");
        const char *time_text = cJSON_GetStringValue(json_member(entry, "time"));
        time_t entry_at = 0;
        int degrees = 0;
        if (time_text == nullptr || !time_parse_iso8601(time_text, &entry_at) ||
            !read_degrees(json_member(json_member(json_member(data, "instant"), "details"),
                                      "air_temperature"),
                          &degrees)) {
            parsed = false;
            break;
        }

        /* the latest entry that has begun is the weather now; today's give the range */
        const bool is_now = !have_now || entry_at <= now;
        const bool is_today = time_same_date(time_warsaw_date(entry_at), today);
        if (!is_now && !is_today) {
            continue;
        }
        const int code = met_code(data);
        if (is_now) {
            report.now_c = degrees;
            report.code = code;
            have_now = true;
        }
        if (is_today) {
            report.low_c = !have_today || degrees < report.low_c ? degrees : report.low_c;
            report.high_c = !have_today || degrees > report.high_c ? degrees : report.high_c;
            have_today = true;
            if (code > ahead_code && shows_ahead(code, entry_at, now)) {
                ahead_code = code;
            }
        }
    }
    cJSON_Delete(root);

    /* rain, snow or a storm due today before 20:00 shows instead, if its code is higher */
    report.code = ahead_code > report.code ? ahead_code : report.code;
    if (!parsed) {
        log_error("MET Norway's forecast doesn't parse");
        return false;
    }
    if (!have_today || report.code == NO_WEATHER_CODE) {
        log_error("MET Norway has no weather for today");
        return false;
    }
    *out = report;
    return true;
}

bool weather_parse_eaqi(const char *json, int *eaqi) {
    cJSON *root = cJSON_Parse(json);
    const cJSON *value = json_member(json_member(root, "current"), "european_aqi");
    const bool parsed = cJSON_IsNumber(value) && value->valueint >= 0;
    if (parsed) {
        *eaqi = value->valueint;
    } else {
        log_error("Open-Meteo's air quality has no current european_aqi");
    }
    cJSON_Delete(root);
    return parsed;
}

struct forecast weather_fetch_forecast(const struct weather_state *state, time_t now) {
    /* ask Open-Meteo */
    struct forecast forecast = {};
    struct http_response answer = http_get_ok(OPEN_METEO_URL);
    forecast.ok = answer.ok && weather_parse_open_meteo(answer.body, now, &forecast.report);
    http_response_free(&answer);

    /* MET Norway stands in once Open-Meteo has failed twice in a row, counting this time */
    if (forecast.ok || state->open_meteo_failures + 1 < OPEN_METEO_FAILURES_BEFORE_MET_NORWAY) {
        return forecast;
    }
    answer = http_get_ok(MET_NORWAY_URL);
    forecast.ok = answer.ok && weather_parse_met_norway(answer.body, now, &forecast.report);
    forecast.from_met = forecast.ok;
    http_response_free(&answer);
    return forecast;
}

struct air weather_fetch_air(void) {
    struct air air = {.eaqi = EAQI_UNKNOWN};
    struct http_response answer = http_get_ok(AIR_QUALITY_URL);
    air.ok = answer.ok && weather_parse_eaqi(answer.body, &air.eaqi);
    http_response_free(&answer);
    return air;
}

bool weather_due(const struct weather_state *state, time_t now) {
    return now >= state->next_fetch;
}

void weather_absorb(struct weather_state *state, const struct weather_fetched *fetched,
                    time_t now) {
    const struct forecast *forecast = &fetched->forecast;
    state->next_fetch = now + FETCH_PERIOD_SECONDS;
    state->open_meteo_failures =
            forecast->ok && !forecast->from_met ? 0 : state->open_meteo_failures + 1;
    if (!forecast->ok) {
        return; // all of the last good fetch stays, its air and warning too
    }
    state->have_report = true;
    state->report = forecast->report;
    state->eaqi = fetched->air.ok ? fetched->air.eaqi : EAQI_UNKNOWN;
    state->have_warning = fetched->warnings.ok && fetched->warnings.any;
    memcpy(state->warning, fetched->warnings.label, sizeof state->warning);
}

static const char *sign_of(int degrees) {
    return degrees < 0 ? MINUS_SIGN : "";
}

struct weather_view weather_view(const struct weather_state *state, time_t now) {
    /* with no report, or one 12 hours old, the weather is unavailable */
    const struct weather_view unavailable = {
            .icon = WEATHER_ICON_OFFLINE,
            .temperature = "—",
            .line = "天气暂不可用",
            .line_kind = WEATHER_LINE_UNAVAILABLE,
    };
    const struct weather_report *report = &state->report;
    const time_t age = now - report->fetched_at;
    if (!state->have_report || age >= OFFLINE_AFTER_SECONDS) {
        return unavailable;
    }
    const struct weather_word word = word_for(report->code);
    if (word.word == nullptr) {
        log_error("weather code %d has no word", report->code);
        return unavailable;
    }

    /* show the icon and the temperature now */
    struct weather_view view = {.icon = word.icon};
    snprintf(view.temperature, sizeof view.temperature, "%s%d" DEGREE_SIGN, sign_of(report->now_c),
             abs(report->now_c));

    /* once the report is 2 hours old, say when it came instead of the range and the word */
    if (age >= STALE_AFTER_SECONDS) {
        const int minute = time_warsaw_minute_of_day(report->fetched_at);
        snprintf(view.line, sizeof view.line, "%02d:%02d更新", minute / MINUTES_PER_HOUR,
                 minute % MINUTES_PER_HOUR);
        view.line_kind = WEATHER_LINE_UPDATED;
        return view;
    }

    /* show today's range, and a warning, poor air or the word */
    snprintf(view.range, sizeof view.range, "%s%d~%s%d" DEGREE_SIGN, sign_of(report->low_c),
             abs(report->low_c), sign_of(report->high_c), abs(report->high_c));
    if (state->have_warning) {
        snprintf(view.line, sizeof view.line, "%s", state->warning);
        view.line_kind = WEATHER_LINE_BADGE;
    } else if (state->eaqi >= POOR_AIR_EAQI) {
        snprintf(view.line, sizeof view.line, "%s", "空气差");
        view.line_kind = WEATHER_LINE_BADGE;
    } else {
        snprintf(view.line, sizeof view.line, "%s", word.word);
        view.line_kind = WEATHER_LINE_WORD;
    }
    return view;
}
