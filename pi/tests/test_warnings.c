#include <stddef.h>
#include <string.h>
#include <time.h>

#include <cjson/cJSON.h>
#include <glib.h>

#include "shared/http.h"
#include "shared/time.h"
#include "stderr_capture.h"
#include "unity.h"
#include "warnings.h"

#define MIDDAY "2026-10-28T12:00:00+01:00"

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

static struct http_response answer(long status, const char *body) {
    char *copy = strdup(body);
    TEST_ASSERT_NOT_NULL(copy);
    return (struct http_response){
            .ok = true, .status = status, .body = copy, .length = strlen(copy)};
}

static struct warnings read_warnings(const char *json, time_t now) {
    struct http_response response = answer(HTTP_OK, json);
    const struct warnings warnings = warnings_read(&response, now);
    http_response_free(&response);
    return warnings;
}

static gchar *entry(const char *event, const char *level, const char *published) {
    return g_strdup_printf("{\"id\":\"Sk20261028000000000\",\"nazwa_zdarzenia\":\"%s\","
                           "\"stopien\":\"%s\",\"obowiazuje_od\":\"2026-10-28 08:00:00\","
                           "\"obowiazuje_do\":\"2026-10-28 20:00:00\",\"opublikowano\":\"%s\","
                           "\"teryt\":[\"1465\"]}",
                           event, level, published);
}

static gchar *only_entry(const char *json, int index) {
    cJSON *all = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(all);
    cJSON *one = cJSON_CreateArray();
    TEST_ASSERT_TRUE(cJSON_AddItemToArray(one, cJSON_DetachItemFromArray(all, index)));
    char *printed = cJSON_PrintUnformatted(one);
    gchar *copy = g_strdup(printed);
    cJSON_free(printed);
    cJSON_Delete(one);
    cJSON_Delete(all);
    return copy;
}

static void assert_top(const char *json, const char *now, const char *label) {
    const struct warnings warnings = read_warnings(json, at(now));
    TEST_ASSERT_TRUE_MESSAGE(warnings.ok, now);
    TEST_ASSERT_TRUE_MESSAGE(warnings.any, now);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(label, warnings.label, now);
}

static void assert_none(const char *json, const char *now) {
    const struct warnings warnings = read_warnings(json, at(now));
    TEST_ASSERT_TRUE_MESSAGE(warnings.ok, now);
    TEST_ASSERT_FALSE_MESSAGE(warnings.any, now);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", warnings.label, now);
}

static void assert_error(const char *json) {
    const struct warnings warnings = read_warnings(json, at(MIDDAY));
    TEST_ASSERT_FALSE_MESSAGE(warnings.ok, json);
    TEST_ASSERT_FALSE_MESSAGE(warnings.any, json);
}

static bool in_force(const char *json, const char *now) {
    const struct warnings warnings = read_warnings(json, at(now));
    TEST_ASSERT_TRUE_MESSAGE(warnings.ok, now);
    return warnings.any;
}

static void assert_label(const char *event, const char *label) {
    gchar *one = entry(event, "1", "2026-10-28 06:00:00");
    gchar *json = g_strdup_printf("[%s]", one);
    assert_top(json, MIDDAY, label);
    g_free(json);
    g_free(one);
}

static void test_labels(void) {
    assert_label("Silny wiatr", "大风预警");
    assert_label("Intensywne opady deszczu", "暴雨预警");
    assert_label("Silny deszcz z burzami", "暴雨预警");
    assert_label("Intensywne opady śniegu", "暴雪预警");
    assert_label("Burze", "雷暴预警");
    assert_label("Burze z gradem", "雷暴预警");
    assert_label("Silne burze", "雷暴预警");
    assert_label("Silne burze z gradem", "雷暴预警");
    assert_label("Upał", "高温预警");
    assert_label("Silny mróz", "严寒预警");
    assert_label("Opady marznące", "冻雨预警");
    assert_label("Oblodzenie", "结冰预警");
    assert_label("Gęsta mgła", "大雾预警");
    assert_label("Mgła intensywnie osadzająca szadź", "大雾预警");
}

static void test_labels_ignore_case_and_composition(void) {
    assert_label("SILNY WIATR", "大风预警");
    assert_label("GĘSTA MGŁA", "大雾预警");
    assert_label("Ge\\u0328sta mg\\u0142a", "大雾预警");
}

static void test_imgw_county_and_time(void) {
    gchar *json = fixture("imgw_warnings.json");
    assert_top(json, "2026-10-28T19:00:00+01:00", "暴雨预警");
    assert_top(json, "2026-10-28T07:30:00+01:00", "结冰预警");
    assert_top(json, "2026-10-28T08:00:00+01:00", "大风预警");
    assert_top(json, "2026-10-28T08:30:00+01:00", "大风预警");
    assert_none(json, "2026-10-30T12:00:00+01:00");
    g_free(json);
}

static void test_top_warning_is_highest_level_then_latest_published(void) {
    gchar *json = fixture("imgw_warnings.json");
    assert_top(json, "2026-10-29T07:00:00+01:00", "大雾预警");
    g_free(json);

    gchar *wind = entry("Silny wiatr", "1", "2026-10-28 09:00:00");
    gchar *fog = entry("Gęsta mgła", "1", "2026-10-28 06:00:00");
    gchar *heat = entry("Upał", "2", "2026-10-28 05:00:00");
    gchar *later_first = g_strdup_printf("[%s,%s]", wind, fog);
    gchar *later_last = g_strdup_printf("[%s,%s]", fog, wind);
    gchar *higher_last = g_strdup_printf("[%s,%s,%s]", wind, fog, heat);
    assert_top(later_first, MIDDAY, "大风预警");
    assert_top(later_last, MIDDAY, "大风预警");
    assert_top(higher_last, MIDDAY, "高温预警");
    g_free(higher_last);
    g_free(later_last);
    g_free(later_first);
    g_free(heat);
    g_free(fog);
    g_free(wind);
}

static void test_imgw_times_across_dst(void) {
    gchar *json = fixture("imgw_warnings_dst.json");
    gchar *wind = only_entry(json, 0);
    gchar *fog = only_entry(json, 1);
    gchar *frost = only_entry(json, 2);
    gchar *ice = only_entry(json, 3);
    TEST_ASSERT_FALSE(in_force(wind, "2026-10-24T17:59:59Z"));
    TEST_ASSERT_TRUE(in_force(wind, "2026-10-24T18:00:00Z"));
    TEST_ASSERT_TRUE(in_force(wind, "2026-10-25T07:59:59Z"));
    TEST_ASSERT_FALSE(in_force(wind, "2026-10-25T08:00:00Z"));
    TEST_ASSERT_FALSE(in_force(fog, "2026-10-25T00:29:59Z"));
    TEST_ASSERT_TRUE(in_force(fog, "2026-10-25T00:30:00Z"));
    TEST_ASSERT_TRUE(in_force(fog, "2026-10-25T08:59:59Z"));
    TEST_ASSERT_FALSE(in_force(fog, "2026-10-25T09:00:00Z"));
    TEST_ASSERT_FALSE(in_force(frost, "2027-03-27T20:59:59Z"));
    TEST_ASSERT_TRUE(in_force(frost, "2027-03-27T21:00:00Z"));
    TEST_ASSERT_TRUE(in_force(frost, "2027-03-28T04:59:59Z"));
    TEST_ASSERT_FALSE(in_force(frost, "2027-03-28T05:00:00Z"));
    TEST_ASSERT_FALSE(in_force(ice, "2027-03-28T01:29:59Z"));
    TEST_ASSERT_TRUE(in_force(ice, "2027-03-28T01:30:00Z"));
    TEST_ASSERT_TRUE(in_force(ice, "2027-03-28T09:59:59Z"));
    TEST_ASSERT_FALSE(in_force(ice, "2027-03-28T10:00:00Z"));

    assert_top(json, "2026-10-25T07:59:59Z", "大雾预警");
    assert_top(json, "2027-03-28T04:59:59Z", "严寒预警");
    assert_top(json, "2027-03-28T05:00:00Z", "结冰预警");
    g_free(ice);
    g_free(frost);
    g_free(fog);
    g_free(wind);
    g_free(json);
}

static void test_unmapped_warning_ignored(void) {
    gchar *thaw = entry("Roztopy", "3", "2026-10-28 06:00:00");
    gchar *unnamed = entry("", "3", "2026-10-28 06:00:00");
    gchar *wind = entry("Silny wiatr", "1", "2026-10-28 05:00:00");
    gchar *unmapped = g_strdup_printf("[%s,%s]", thaw, unnamed);
    gchar *with_wind = g_strdup_printf("[%s,%s,%s]", thaw, wind, unnamed);
    assert_none(unmapped, MIDDAY);
    assert_top(with_wind, MIDDAY, "大风预警");
    g_free(with_wind);
    g_free(unmapped);
    g_free(wind);
    g_free(unnamed);
    g_free(thaw);

    gchar *json = fixture("imgw_warnings.json");
    assert_none(json, "2026-10-29T11:30:00+01:00");
    g_free(json);
}

static void test_imgw_skips_malformed_entries(void) {
    const char *json =
            "[{\"nazwa_zdarzenia\":\"Burze\",\"stopien\":\"2\",\"obowiazuje_od\":\"2026-10-28 "
            "08:00\",\"obowiazuje_do\":\"2026-10-28 20:00:00\",\"opublikowano\":\"2026-10-28 "
            "06:00:00\",\"teryt\":[\"1465\"]},"
            "{\"nazwa_zdarzenia\":\"Upał\",\"stopien\":\"high\",\"obowiazuje_od\":\"2026-10-28 "
            "08:00:00\",\"obowiazuje_do\":\"2026-10-28 20:00:00\",\"opublikowano\":\"2026-10-28 "
            "06:00:00\",\"teryt\":[\"1465\"]},"
            "{\"nazwa_zdarzenia\":\"Silny wiatr\",\"stopien\":\"1\",\"obowiazuje_od\":\"2026-10-28 "
            "08:00:00\",\"obowiazuje_do\":\"2026-10-28 20:00:00\",\"opublikowano\":\"2026-10-28 "
            "06:00:00\",\"teryt\":[\"1465\"]}]";
    assert_top(json, MIDDAY, "大风预警");
    assert_error("<html>");
    assert_error("{}");
}

static void test_imgw_no_warnings_message_is_not_an_error(void) {
    gchar *json = fixture("imgw_no_warnings.json");
    const time_t now = at("2026-11-05T11:05:00+01:00");
    char captured[4096];

    start_capture();
    const struct warnings none = read_warnings(json, now);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_TRUE(none.ok);
    TEST_ASSERT_FALSE(none.any);
    TEST_ASSERT_EQUAL_STRING("", captured);

    start_capture();
    const struct warnings reworded =
            read_warnings("{\"message\":\"Brak aktualnych ostrze\\u017ce\\u0144\"}", now);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_TRUE(reworded.ok);
    TEST_ASSERT_FALSE(reworded.any);
    TEST_ASSERT_EQUAL_STRING("", captured);

    start_capture();
    const struct warnings empty_list = read_warnings("[]", now);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_TRUE(empty_list.ok);
    TEST_ASSERT_FALSE(empty_list.any);
    TEST_ASSERT_EQUAL_STRING("", captured);

    start_capture();
    const struct warnings not_a_list = read_warnings("{}", now);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_FALSE(not_a_list.ok);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(captured, "IMGW"), captured);
    g_free(json);
}

static void test_non_200_warnings_answer_is_not_ok(void) {
    struct http_response response = answer(404, "");
    char captured[256];
    start_capture();
    const struct warnings warnings = warnings_read(&response, at(MIDDAY));
    finish_capture(captured, sizeof captured);
    http_response_free(&response);
    TEST_ASSERT_FALSE(warnings.ok);
    TEST_ASSERT_FALSE(warnings.any);
    TEST_ASSERT_EQUAL_STRING("<3>" WARNINGS_URL " answered 404\n", captured);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_labels);
    RUN_TEST(test_labels_ignore_case_and_composition);
    RUN_TEST(test_imgw_county_and_time);
    RUN_TEST(test_top_warning_is_highest_level_then_latest_published);
    RUN_TEST(test_imgw_times_across_dst);
    RUN_TEST(test_unmapped_warning_ignored);
    RUN_TEST(test_imgw_skips_malformed_entries);
    RUN_TEST(test_imgw_no_warnings_message_is_not_an_error);
    RUN_TEST(test_non_200_warnings_answer_is_not_ok);
    return UNITY_END();
}
