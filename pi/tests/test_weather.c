#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#include "shared/time.h"
#include "stderr_capture.h"
#include "unity.h"
#include "weather.h"

#define NOON "2026-10-28T12:00:00+01:00"

static constexpr time_t FETCH_PERIOD_SECONDS = 30 * SECONDS_PER_MINUTE;
static constexpr time_t STALE_AFTER_SECONDS = 2 * SECONDS_PER_HOUR;
static constexpr time_t OFFLINE_AFTER_SECONDS = 12 * SECONDS_PER_HOUR;

struct hour {
    const char *at;
    int code;
};

struct word_case {
    const char *name;
    const char *fetched;
    int now_code;
    struct hour hour;
    int expected;
};

struct met_entry {
    const char *at;
    double degrees;
    const char *symbol;
};

struct met_case {
    const char *name;
    const char *fetched;
    struct met_entry entries[3];
    size_t count;
    int expected;
};

void setUp(void) {
}

void tearDown(void) {
}

static time_t at(const char *text) {
    time_t moment = 0;
    TEST_ASSERT_TRUE_MESSAGE(time_parse_iso8601(text, &moment), text);
    return moment;
}

static gchar *fixture(const char *name) {
    gchar *path = g_build_filename("fixtures", name, nullptr);
    gchar *contents = nullptr;
    TEST_ASSERT_TRUE_MESSAGE(g_file_get_contents(path, &contents, nullptr, nullptr), path);
    g_free(path);
    return contents;
}

static gchar *open_meteo_json(int now_code, const struct hour *hours, size_t count) {
    GString *times = g_string_new(nullptr);
    GString *codes = g_string_new(nullptr);
    for (size_t i = 0; i < count; i++) {
        g_string_append_printf(times, "%s%lld", i > 0 ? "," : "", (long long)at(hours[i].at));
        g_string_append_printf(codes, "%s%d", i > 0 ? "," : "", hours[i].code);
    }
    gchar *json = g_strdup_printf("{\"current\":{\"temperature_2m\":7.0,\"weather_code\":%d},"
                                  "\"hourly\":{\"time\":[%s],\"weather_code\":[%s]},"
                                  "\"daily\":{\"temperature_2m_max\":[9.0],"
                                  "\"temperature_2m_min\":[2.0]}}",
                                  now_code, times->str, codes->str);
    g_string_free(times, TRUE);
    g_string_free(codes, TRUE);
    return json;
}

static void assert_open_meteo_codes(const struct word_case *cases, size_t count) {
    for (size_t i = 0; i < count; i++) {
        gchar *json = open_meteo_json(cases[i].now_code, &cases[i].hour, 1);
        struct weather_report report = {};
        TEST_ASSERT_TRUE_MESSAGE(weather_parse_open_meteo(json, at(cases[i].fetched), &report),
                                 cases[i].name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(cases[i].expected, report.code, cases[i].name);
        g_free(json);
    }
}

static gchar *met_norway_json(const struct met_entry *entries, size_t count) {
    GString *json = g_string_new("{\"properties\":{\"timeseries\":[");
    for (size_t i = 0; i < count; i++) {
        g_string_append_printf(json,
                               "%s{\"time\":\"%s\",\"data\":{\"instant\":{\"details\":{"
                               "\"air_temperature\":%.1f}},\"next_1_hours\":{\"summary\":{"
                               "\"symbol_code\":\"%s\"}}}}",
                               i > 0 ? "," : "", entries[i].at, entries[i].degrees,
                               entries[i].symbol);
    }
    g_string_append(json, "]}}");
    return g_string_free_and_steal(json);
}

static void assert_report(struct weather_report expected, struct weather_report actual) {
    TEST_ASSERT_EQUAL_INT64(expected.fetched_at, actual.fetched_at);
    TEST_ASSERT_EQUAL_INT(expected.now_c, actual.now_c);
    TEST_ASSERT_EQUAL_INT(expected.low_c, actual.low_c);
    TEST_ASSERT_EQUAL_INT(expected.high_c, actual.high_c);
    TEST_ASSERT_EQUAL_INT(expected.code, actual.code);
}

static void assert_view(struct weather_view view, enum weather_icon icon, const char *temperature,
                        const char *range, const char *line, enum weather_line_kind kind) {
    TEST_ASSERT_EQUAL_INT(icon, view.icon);
    TEST_ASSERT_EQUAL_STRING(temperature, view.temperature);
    TEST_ASSERT_EQUAL_STRING(range, view.range);
    TEST_ASSERT_EQUAL_STRING(line, view.line);
    TEST_ASSERT_EQUAL_INT(kind, view.line_kind);
}

static void assert_unavailable(struct weather_view view) {
    assert_view(view, WEATHER_ICON_OFFLINE, "—", "", "天气暂不可用", WEATHER_LINE_UNAVAILABLE);
}

static struct weather_state fresh_state(int code, int now_c, int low_c, int high_c) {
    return (struct weather_state){
            .have_report = true,
            .report = {.fetched_at = at(NOON) - 10 * SECONDS_PER_MINUTE,
                       .now_c = now_c,
                       .low_c = low_c,
                       .high_c = high_c,
                       .code = code},
            .eaqi = 20,
    };
}

static struct weather_fetched everything_fetched(time_t now) {
    return (struct weather_fetched){
            .forecast = {.ok = true, .report = {now, 11, 3, 12, 3}},
            .air = {.ok = true, .eaqi = 75},
            .warnings = {.ok = true, .any = true, .label = "大风预警"},
    };
}

static void test_word_and_icon_for_each_code(void) {
    const struct {
        int code;
        const char *word;
        enum weather_icon icon;
    } rows[] = {
            {0, "晴", WEATHER_ICON_SUN},           {1, "晴", WEATHER_ICON_SUN},
            {2, "多云", WEATHER_ICON_PARTLY},      {3, "阴", WEATHER_ICON_CLOUD},
            {45, "雾", WEATHER_ICON_FOG},          {48, "雾", WEATHER_ICON_FOG},
            {51, "毛毛雨", WEATHER_ICON_DRIZZLE},  {53, "毛毛雨", WEATHER_ICON_DRIZZLE},
            {55, "毛毛雨", WEATHER_ICON_DRIZZLE},  {56, "冻雨", WEATHER_ICON_SLEET},
            {57, "冻雨", WEATHER_ICON_SLEET},      {66, "冻雨", WEATHER_ICON_SLEET},
            {67, "冻雨", WEATHER_ICON_SLEET},      {61, "小雨", WEATHER_ICON_RAIN},
            {63, "中雨", WEATHER_ICON_RAIN},       {65, "大雨", WEATHER_ICON_HEAVY_RAIN},
            {80, "阵雨", WEATHER_ICON_RAIN},       {81, "阵雨", WEATHER_ICON_RAIN},
            {82, "大雨", WEATHER_ICON_HEAVY_RAIN}, {71, "小雪", WEATHER_ICON_SNOW},
            {77, "小雪", WEATHER_ICON_SNOW},       {73, "中雪", WEATHER_ICON_SNOW},
            {75, "大雪", WEATHER_ICON_HEAVY_SNOW}, {85, "阵雪", WEATHER_ICON_SNOW},
            {86, "阵雪", WEATHER_ICON_SNOW},       {95, "雷阵雨", WEATHER_ICON_STORM},
            {96, "雷阵雨", WEATHER_ICON_STORM},    {99, "雷阵雨", WEATHER_ICON_STORM},
    };

    for (size_t i = 0; i < G_N_ELEMENTS(rows); i++) {
        const struct weather_state state = fresh_state(rows[i].code, 5, 2, 6);
        const struct weather_view view = weather_view(&state, at(NOON));
        char message[24];
        snprintf(message, sizeof message, "code %d", rows[i].code);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(rows[i].word, view.line, message);
        TEST_ASSERT_EQUAL_INT_MESSAGE(rows[i].icon, view.icon, message);
        TEST_ASSERT_EQUAL_INT_MESSAGE(WEATHER_LINE_WORD, view.line_kind, message);
    }
}

static void test_code_without_word_is_unavailable(void) {
    const int unknown[] = {-1, 4, 50, 60, 100};
    const time_t noon = at(NOON);
    for (size_t i = 0; i < G_N_ELEMENTS(unknown); i++) {
        const struct weather_state state = fresh_state(unknown[i], 5, 2, 6);
        char captured[256];
        start_capture();
        const struct weather_view view = weather_view(&state, noon);
        finish_capture(captured, sizeof captured);
        assert_unavailable(view);
        char expected[64];
        snprintf(expected, sizeof expected, "<3>weather code %d has no word\n", unknown[i]);
        TEST_ASSERT_EQUAL_STRING(expected, captured);
    }
}

static void test_parse_open_meteo(void) {
    gchar *json = fixture("open_meteo_forecast.json");
    const time_t fetched = at("2026-10-28T15:14:18Z");
    struct weather_report report = {};
    TEST_ASSERT_TRUE(weather_parse_open_meteo(json, fetched, &report));
    assert_report((struct weather_report){fetched, 11, 3, 12, 3}, report);
    g_free(json);
}

static void test_parse_open_meteo_rejects_missing_values(void) {
    const char *broken[] = {
            "not json",
            "{}",
            "{\"current\":{\"temperature_2m\":11.1,\"weather_code\":3},"
            "\"hourly\":{\"time\":[1793199600],\"weather_code\":[3]},"
            "\"daily\":{\"temperature_2m_max\":[12.2],\"temperature_2m_min\":[null]}}",
            "{\"current\":{\"temperature_2m\":11.1,\"weather_code\":4},"
            "\"hourly\":{\"time\":[1793199600],\"weather_code\":[3]},"
            "\"daily\":{\"temperature_2m_max\":[12.2],\"temperature_2m_min\":[3.0]}}",
            "{\"current\":{\"temperature_2m\":1e300,\"weather_code\":3},"
            "\"hourly\":{\"time\":[1793199600],\"weather_code\":[3]},"
            "\"daily\":{\"temperature_2m_max\":[12.2],\"temperature_2m_min\":[3.0]}}",
            "{\"current\":{\"temperature_2m\":11.1,\"weather_code\":3},"
            "\"hourly\":{\"time\":[1793199600],\"weather_code\":[4]},"
            "\"daily\":{\"temperature_2m_max\":[12.2],\"temperature_2m_min\":[3.0]}}",
            "{\"current\":{\"temperature_2m\":11.1,\"weather_code\":3},"
            "\"hourly\":{\"time\":[1793199600,1793203200],\"weather_code\":[3]},"
            "\"daily\":{\"temperature_2m_max\":[12.2],\"temperature_2m_min\":[3.0]}}",
            "{\"current\":{\"temperature_2m\":11.1,"
            "\"weather_code\":3},"
            "\"daily\":{\"temperature_2m_max\":[12.2],\"temperature_2m_min\":[3.0]}}",
    };
    for (size_t i = 0; i < G_N_ELEMENTS(broken); i++) {
        struct weather_report report = {.code = 77};
        TEST_ASSERT_FALSE_MESSAGE(weather_parse_open_meteo(broken[i], 0, &report), broken[i]);
        TEST_ASSERT_EQUAL_INT(77, report.code);
    }
}

static void assert_current_temperature_shows(const char *current, const char *expected) {
    gchar *json = g_strdup_printf("{\"current\":{\"temperature_2m\":%s,\"weather_code\":3},"
                                  "\"hourly\":{\"time\":[1793199600],\"weather_code\":[3]},"
                                  "\"daily\":{\"temperature_2m_max\":[1.0],"
                                  "\"temperature_2m_min\":[-3.0]}}",
                                  current);
    const time_t noon = at(NOON);
    struct weather_state state = {.have_report = true, .eaqi = -1};
    TEST_ASSERT_TRUE_MESSAGE(weather_parse_open_meteo(json, noon, &state.report), current);
    const struct weather_view view = weather_view(&state, noon);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, view.temperature, current);
    g_free(json);
}

static void test_negative_temperatures_round_away_from_zero(void) {
    assert_current_temperature_shows("-0.4", "0°");
    assert_current_temperature_shows("-0.5", "−1°");
    assert_current_temperature_shows("-1.5", "−2°");
}

static void test_parse_met_norway(void) {
    gchar *json = fixture("met_norway_compact.json");
    struct weather_report report = {};
    const time_t afternoon = at("2026-10-28T15:14:18Z");
    TEST_ASSERT_TRUE(weather_parse_met_norway(json, afternoon, &report));
    assert_report((struct weather_report){afternoon, 11, 9, 12, 3}, report);
    const time_t foggy_morning = at("2026-10-29T07:30:00+01:00");
    TEST_ASSERT_TRUE(weather_parse_met_norway(json, foggy_morning, &report));
    assert_report((struct weather_report){foggy_morning, 7, 7, 12, 45}, report);
    const time_t midnight = at("2026-10-30T00:10:00+01:00");
    TEST_ASSERT_TRUE(weather_parse_met_norway(json, midnight, &report));
    assert_report((struct weather_report){midnight, 9, 9, 9, 3}, report);
    report.code = 77;
    TEST_ASSERT_FALSE(weather_parse_met_norway(json, at("2026-11-05T12:00:00+01:00"), &report));
    TEST_ASSERT_EQUAL_INT(77, report.code);
    g_free(json);
}

static void test_open_meteo_word_is_the_weather_now_not_tonights_fog(void) {
    gchar *json = fixture("open_meteo_forecast_fog_tonight.json");
    const time_t fetched = at("2026-11-05T11:15:00+01:00");
    struct weather_report report = {};
    TEST_ASSERT_TRUE(weather_parse_open_meteo(json, fetched, &report));
    assert_report((struct weather_report){fetched, 7, 2, 9, 0}, report);
    g_free(json);
}

static void test_word_is_weather_now_when_nothing_due(void) {
    gchar *json = fixture("open_meteo_forecast_fog_tonight.json");
    const time_t morning = at("2026-11-05T10:30:00+01:00");
    struct weather_state state = {.have_report = true, .eaqi = -1};
    TEST_ASSERT_TRUE(weather_parse_open_meteo(json, morning, &state.report));
    TEST_ASSERT_EQUAL_INT(0, state.report.code);
    assert_view(weather_view(&state, morning), WEATHER_ICON_SUN, "7°", "2~9°", "晴",
                WEATHER_LINE_WORD);
    g_free(json);
}

static void test_precipitation_due_before_20_shows_ahead(void) {
    const struct word_case cases[] = {
            {"rain this afternoon",
             "2026-11-05T08:00:00+01:00",
             0,
             {"2026-11-05T15:00:00+01:00", 61},
             61},
            {"rain in the hour before 20:00",
             "2026-11-05T08:00:00+01:00",
             0,
             {"2026-11-05T19:00:00+01:00", 61},
             61},
            {"rain in the hour under way",
             "2026-11-05T10:30:00+01:00",
             0,
             {"2026-11-05T10:00:00+01:00", 61},
             61},
            {"drizzle, the lightest that counts",
             "2026-11-05T08:00:00+01:00",
             3,
             {"2026-11-05T12:00:00+01:00", 51},
             51},
            {"snow at noon", "2026-11-05T08:00:00+01:00", 3, {"2026-11-05T12:00:00+01:00", 73}, 73},
    };
    assert_open_meteo_codes(cases, G_N_ELEMENTS(cases));

    const struct hour afternoon_rain = {"2026-11-05T15:00:00+01:00", 61};
    gchar *json = open_meteo_json(0, &afternoon_rain, 1);
    const time_t morning = at("2026-11-05T08:00:00+01:00");
    struct weather_state state = {.have_report = true, .eaqi = -1};
    TEST_ASSERT_TRUE(weather_parse_open_meteo(json, morning, &state.report));
    assert_view(weather_view(&state, morning), WEATHER_ICON_RAIN, "7°", "2~9°", "小雨",
                WEATHER_LINE_WORD);
    g_free(json);
}

static void test_precipitation_after_20_ignored(void) {
    const struct word_case cases[] = {
            {"rain from 20:00",
             "2026-11-05T08:00:00+01:00",
             0,
             {"2026-11-05T20:00:00+01:00", 61},
             0},
            {"a storm at 22:00",
             "2026-11-05T08:00:00+01:00",
             2,
             {"2026-11-05T22:00:00+01:00", 95},
             2},
    };
    assert_open_meteo_codes(cases, G_N_ELEMENTS(cases));
}

static void test_precipitation_passed_or_tomorrow_ignored(void) {
    const struct word_case cases[] = {
            {"rain that has passed",
             "2026-11-05T10:30:00+01:00",
             0,
             {"2026-11-05T09:00:00+01:00", 61},
             0},
            {"rain in the hour that just ended",
             "2026-11-05T10:00:00+01:00",
             0,
             {"2026-11-05T09:00:00+01:00", 61},
             0},
            {"tomorrow's rain",
             "2026-11-05T22:00:00+01:00",
             0,
             {"2026-11-06T07:00:00+01:00", 61},
             0},
    };
    assert_open_meteo_codes(cases, G_N_ELEMENTS(cases));
}

static void test_highest_due_code_wins(void) {
    const struct word_case cases[] = {
            {"a storm now beats later rain",
             "2026-11-05T08:00:00+01:00",
             95,
             {"2026-11-05T15:00:00+01:00", 61},
             95},
            {"later rain beats fog now",
             "2026-11-05T08:00:00+01:00",
             45,
             {"2026-11-05T15:00:00+01:00", 61},
             61},
            {"later fog isn't due weather",
             "2026-11-05T08:00:00+01:00",
             3,
             {"2026-11-05T15:00:00+01:00", 45},
             3},
    };
    assert_open_meteo_codes(cases, G_N_ELEMENTS(cases));

    const struct hour hours[] = {
            {"2026-11-05T12:00:00+01:00", 61},
            {"2026-11-05T15:00:00+01:00", 65},
            {"2026-11-05T19:00:00+01:00", 63},
            {"2026-11-05T21:00:00+01:00", 95},
    };
    gchar *json = open_meteo_json(0, hours, G_N_ELEMENTS(hours));
    struct weather_report report = {};
    TEST_ASSERT_TRUE(weather_parse_open_meteo(json, at("2026-11-05T08:00:00+01:00"), &report));
    TEST_ASSERT_EQUAL_INT(65, report.code);
    g_free(json);
}

static void test_met_norway_word_is_the_weather_now(void) {
    gchar *json = fixture("met_norway_compact.json");
    const time_t after_the_fog = at("2026-10-29T10:30:00+01:00");
    struct weather_report report = {};
    TEST_ASSERT_TRUE(weather_parse_met_norway(json, after_the_fog, &report));
    assert_report((struct weather_report){after_the_fog, 10, 7, 12, 3}, report);
    g_free(json);
}

static void test_met_norway_same_rule(void) {
    const struct met_case cases[] = {
            {"rain due before 20:00, heavier rain after",
             "2026-11-05T08:30:00+01:00",
             {{"2026-11-05T07:00:00Z", 7.0, "clearsky_day"},
              {"2026-11-05T14:00:00Z", 9.0, "lightrain"},
              {"2026-11-05T20:00:00Z", 5.0, "heavyrain"}},
             3,
             61},
            {"the highest due code wins over fog now",
             "2026-11-05T08:30:00+01:00",
             {{"2026-11-05T08:00:00+01:00", 6.0, "fog"},
              {"2026-11-05T12:00:00+01:00", 8.0, "lightrain"},
              {"2026-11-05T16:00:00+01:00", 9.0, "rainandthunder"}},
             3,
             95},
            {"rain that has passed",
             "2026-11-05T10:30:00+01:00",
             {{"2026-11-05T08:00:00+01:00", 6.0, "lightrain"},
              {"2026-11-05T10:00:00+01:00", 8.0, "partlycloudy_day"}},
             2,
             2},
            {"tomorrow's rain",
             "2026-11-05T22:15:00+01:00",
             {{"2026-11-05T22:00:00+01:00", 5.0, "cloudy"},
              {"2026-11-06T07:00:00+01:00", 4.0, "lightrain"}},
             2,
             3},
    };
    for (size_t i = 0; i < G_N_ELEMENTS(cases); i++) {
        gchar *json = met_norway_json(cases[i].entries, cases[i].count);
        struct weather_report report = {};
        TEST_ASSERT_TRUE_MESSAGE(weather_parse_met_norway(json, at(cases[i].fetched), &report),
                                 cases[i].name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(cases[i].expected, report.code, cases[i].name);
        g_free(json);
    }

    gchar *json = met_norway_json(cases[0].entries, cases[0].count);
    const time_t morning = at(cases[0].fetched);
    struct weather_report report = {};
    TEST_ASSERT_TRUE(weather_parse_met_norway(json, morning, &report));
    assert_report((struct weather_report){morning, 7, 5, 9, 61}, report);
    g_free(json);
}

static void test_parse_eaqi(void) {
    gchar *json = fixture("open_meteo_air.json");
    int eaqi = -7;
    TEST_ASSERT_TRUE(weather_parse_eaqi(json, &eaqi));
    TEST_ASSERT_EQUAL_INT(50, eaqi);
    eaqi = -7;
    TEST_ASSERT_FALSE(weather_parse_eaqi("{\"current\":{\"european_aqi\":null}}", &eaqi));
    TEST_ASSERT_FALSE(weather_parse_eaqi("{\"current\":{\"european_aqi\":-3}}", &eaqi));
    TEST_ASSERT_FALSE(weather_parse_eaqi("[]", &eaqi));
    TEST_ASSERT_EQUAL_INT(-7, eaqi);
    g_free(json);
}

static void test_strings(void) {
    const time_t noon = at(NOON);
    struct weather_state state = fresh_state(71, -3, -6, -1);
    assert_view(weather_view(&state, noon), WEATHER_ICON_SNOW, "−3°", "−6~−1°", "小雪",
                WEATHER_LINE_WORD);
    state = fresh_state(61, 8, 7, 10);
    assert_view(weather_view(&state, noon), WEATHER_ICON_RAIN, "8°", "7~10°", "小雨",
                WEATHER_LINE_WORD);
    state = fresh_state(0, -17, -21, -12);
    assert_view(weather_view(&state, noon), WEATHER_ICON_SUN, "−17°", "−21~−12°", "晴",
                WEATHER_LINE_WORD);
    state = fresh_state(66, 0, -2, 1);
    assert_view(weather_view(&state, noon), WEATHER_ICON_SLEET, "0°", "−2~1°", "冻雨",
                WEATHER_LINE_WORD);
}

static void test_eaqi_60_shows_air_badge(void) {
    const time_t noon = at(NOON);
    struct weather_state state = fresh_state(3, -4, -7, -2);
    state.eaqi = 60;
    assert_view(weather_view(&state, noon), WEATHER_ICON_CLOUD, "−4°", "−7~−2°", "空气差",
                WEATHER_LINE_BADGE);
    state.eaqi = 59;
    assert_view(weather_view(&state, noon), WEATHER_ICON_CLOUD, "−4°", "−7~−2°", "阴",
                WEATHER_LINE_WORD);
    state.eaqi = -1;
    assert_view(weather_view(&state, noon), WEATHER_ICON_CLOUD, "−4°", "−7~−2°", "阴",
                WEATHER_LINE_WORD);
}

static void test_warning_beats_air_badge(void) {
    struct weather_state state = fresh_state(3, 5, 2, 6);
    state.have_warning = true;
    snprintf(state.warning, sizeof state.warning, "%s", "大风预警");
    state.eaqi = 75;
    assert_view(weather_view(&state, at(NOON)), WEATHER_ICON_CLOUD, "5°", "2~6°", "大风预警",
                WEATHER_LINE_BADGE);
}

static void test_badge_replaces_word(void) {
    const time_t noon = at(NOON);
    struct weather_state state = fresh_state(61, 8, 7, 10);
    state.have_warning = true;
    snprintf(state.warning, sizeof state.warning, "%s", "暴雨预警");
    assert_view(weather_view(&state, noon), WEATHER_ICON_RAIN, "8°", "7~10°", "暴雨预警",
                WEATHER_LINE_BADGE);
    state.have_warning = false;
    state.eaqi = 80;
    assert_view(weather_view(&state, noon), WEATHER_ICON_RAIN, "8°", "7~10°", "空气差",
                WEATHER_LINE_BADGE);
}

static void test_stale_after_2h_shows_update_time_without_range(void) {
    struct weather_state state = fresh_state(61, 8, 7, 10);
    state.report.fetched_at = at("2026-10-28T14:00:00+01:00");
    state.have_warning = true;
    snprintf(state.warning, sizeof state.warning, "%s", "暴雨预警");
    state.eaqi = 80;
    assert_view(weather_view(&state, state.report.fetched_at + STALE_AFTER_SECONDS - 1),
                WEATHER_ICON_RAIN, "8°", "7~10°", "暴雨预警", WEATHER_LINE_BADGE);
    assert_view(weather_view(&state, state.report.fetched_at + STALE_AFTER_SECONDS),
                WEATHER_ICON_RAIN, "8°", "", "14:00更新", WEATHER_LINE_UPDATED);
    state.report.fetched_at = at("2026-07-01T07:05:00+02:00");
    assert_view(weather_view(&state, at("2026-07-01T11:00:00+02:00")), WEATHER_ICON_RAIN, "8°", "",
                "07:05更新", WEATHER_LINE_UPDATED);
}

static void test_offline_after_12h(void) {
    struct weather_state state = fresh_state(61, 8, 7, 10);
    state.report.fetched_at = at("2026-10-28T04:00:00+01:00");
    assert_view(weather_view(&state, state.report.fetched_at + OFFLINE_AFTER_SECONDS - 1),
                WEATHER_ICON_RAIN, "8°", "", "04:00更新", WEATHER_LINE_UPDATED);
    assert_unavailable(weather_view(&state, state.report.fetched_at + OFFLINE_AFTER_SECONDS));
    const struct weather_state never_fetched = {.eaqi = -1};
    assert_unavailable(weather_view(&never_fetched, at("2026-10-28T16:00:00+01:00")));
}

static void test_absorb_takes_forecast_air_and_warning(void) {
    struct weather_state state = {.eaqi = -1};
    const time_t now = at("2026-10-28T19:00:00+01:00");
    const struct weather_fetched fetched = everything_fetched(now);
    weather_absorb(&state, &fetched, now);
    TEST_ASSERT_TRUE(state.have_report);
    assert_report(fetched.forecast.report, state.report);
    TEST_ASSERT_EQUAL_INT(75, state.eaqi);
    TEST_ASSERT_TRUE(state.have_warning);
    TEST_ASSERT_EQUAL_STRING("大风预警", state.warning);
    assert_view(weather_view(&state, now), WEATHER_ICON_CLOUD, "11°", "3~12°", "大风预警",
                WEATHER_LINE_BADGE);
}

static void test_absorb_keeps_previous_report_without_forecast(void) {
    struct weather_state state = {.eaqi = -1};
    const time_t first = at("2026-10-28T19:00:00+01:00");
    const struct weather_fetched good = everything_fetched(first);
    weather_absorb(&state, &good, first);

    const time_t later = first + FETCH_PERIOD_SECONDS;
    const struct weather_fetched no_forecast = {
            .air = {.ok = true, .eaqi = 10},
            .warnings = {.ok = true},
    };
    weather_absorb(&state, &no_forecast, later);
    TEST_ASSERT_TRUE(state.have_report);
    assert_report(good.forecast.report, state.report);
    TEST_ASSERT_EQUAL_INT(75, state.eaqi);
    TEST_ASSERT_TRUE(state.have_warning);
    TEST_ASSERT_EQUAL_STRING("大风预警", state.warning);
    assert_view(weather_view(&state, later), WEATHER_ICON_CLOUD, "11°", "3~12°", "大风预警",
                WEATHER_LINE_BADGE);

    struct weather_state never_fetched = {.eaqi = -1};
    weather_absorb(&never_fetched, &no_forecast, later);
    TEST_ASSERT_FALSE(never_fetched.have_report);
    assert_unavailable(weather_view(&never_fetched, later));
}

static void test_absorb_forecast_without_air_or_warnings_clears_badges(void) {
    struct weather_state state = {.have_warning = true, .warning = "暴雨预警", .eaqi = 99};
    const time_t now = at("2026-10-28T19:00:00+01:00");
    const struct weather_fetched only_forecast = {
            .forecast = {.ok = true, .report = {now, 11, 3, 12, 3}},
    };
    weather_absorb(&state, &only_forecast, now);
    TEST_ASSERT_TRUE(state.have_report);
    TEST_ASSERT_FALSE(state.have_warning);
    TEST_ASSERT_EQUAL_INT(-1, state.eaqi);
    assert_view(weather_view(&state, now), WEATHER_ICON_CLOUD, "11°", "3~12°", "阴",
                WEATHER_LINE_WORD);
}

static void test_due_first_tick_then_30_min_after_each_attempt(void) {
    struct weather_state state = {.eaqi = -1};
    const time_t first = at("2026-10-28T19:00:00+01:00");
    TEST_ASSERT_TRUE(weather_due(&state, first));

    const struct weather_fetched failed = {};
    weather_absorb(&state, &failed, first);
    TEST_ASSERT_FALSE(weather_due(&state, first));
    TEST_ASSERT_FALSE(weather_due(&state, first + FETCH_PERIOD_SECONDS - 1));
    TEST_ASSERT_TRUE(weather_due(&state, first + FETCH_PERIOD_SECONDS));

    const time_t second = first + FETCH_PERIOD_SECONDS + 15;
    const struct weather_fetched good = everything_fetched(second);
    weather_absorb(&state, &good, second);
    TEST_ASSERT_FALSE(weather_due(&state, second + FETCH_PERIOD_SECONDS - 1));
    TEST_ASSERT_TRUE(weather_due(&state, second + FETCH_PERIOD_SECONDS));
}

static void test_open_meteo_failures_count_and_reset(void) {
    struct weather_state state = {.eaqi = -1};
    const time_t start = at("2026-10-28T19:00:00+01:00");
    const struct weather_fetched nothing = {};
    weather_absorb(&state, &nothing, start);
    TEST_ASSERT_EQUAL_INT(1, state.open_meteo_failures);

    struct weather_fetched from_met = everything_fetched(start + FETCH_PERIOD_SECONDS);
    from_met.forecast.from_met = true;
    weather_absorb(&state, &from_met, start + FETCH_PERIOD_SECONDS);
    TEST_ASSERT_EQUAL_INT(2, state.open_meteo_failures);
    TEST_ASSERT_TRUE(state.have_report);
    weather_absorb(&state, &from_met, start + 2 * FETCH_PERIOD_SECONDS);
    TEST_ASSERT_EQUAL_INT(3, state.open_meteo_failures);

    const struct weather_fetched from_open_meteo =
            everything_fetched(start + 3 * FETCH_PERIOD_SECONDS);
    weather_absorb(&state, &from_open_meteo, start + 3 * FETCH_PERIOD_SECONDS);
    TEST_ASSERT_EQUAL_INT(0, state.open_meteo_failures);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_word_and_icon_for_each_code);
    RUN_TEST(test_code_without_word_is_unavailable);
    RUN_TEST(test_parse_open_meteo);
    RUN_TEST(test_parse_open_meteo_rejects_missing_values);
    RUN_TEST(test_negative_temperatures_round_away_from_zero);
    RUN_TEST(test_parse_met_norway);
    RUN_TEST(test_open_meteo_word_is_the_weather_now_not_tonights_fog);
    RUN_TEST(test_word_is_weather_now_when_nothing_due);
    RUN_TEST(test_precipitation_due_before_20_shows_ahead);
    RUN_TEST(test_precipitation_after_20_ignored);
    RUN_TEST(test_precipitation_passed_or_tomorrow_ignored);
    RUN_TEST(test_highest_due_code_wins);
    RUN_TEST(test_met_norway_word_is_the_weather_now);
    RUN_TEST(test_met_norway_same_rule);
    RUN_TEST(test_parse_eaqi);
    RUN_TEST(test_strings);
    RUN_TEST(test_eaqi_60_shows_air_badge);
    RUN_TEST(test_warning_beats_air_badge);
    RUN_TEST(test_badge_replaces_word);
    RUN_TEST(test_stale_after_2h_shows_update_time_without_range);
    RUN_TEST(test_offline_after_12h);
    RUN_TEST(test_absorb_takes_forecast_air_and_warning);
    RUN_TEST(test_absorb_keeps_previous_report_without_forecast);
    RUN_TEST(test_absorb_forecast_without_air_or_warnings_clears_badges);
    RUN_TEST(test_due_first_tick_then_30_min_after_each_attempt);
    RUN_TEST(test_open_meteo_failures_count_and_reset);
    return UNITY_END();
}
