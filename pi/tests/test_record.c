#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <glib.h>

#include "config.h"
#include "draw/frame.h"
#include "fake_server.h"
#include "record.h"
#include "screen/screen.h"
#include "shared/time.h"
#include "stderr_capture.h"
#include "unity.h"

#define NOON             "2026-10-26T12:00:00+01:00"
#define HALF_PAST_ELEVEN "2026-10-26T11:30:00+01:00"
#define BEARER_LINE      "\r\nAuthorization: Bearer pi-secret\r\n"
#define REPORT_PART \
    "Content-Disposition: form-data; name=\"report\"\r\nContent-Type: application/json\r\n\r\n"
#define KERNEL_LINK_QUALITY "/proc/net/wireless"
#define TEMPERATURE_PATH    "/sys/class/thermal/thermal_zone0/temp"

static constexpr size_t CAPTURE_BYTES = 16 * 1024;

static struct frame glass_frame;
static char captured[CAPTURE_BYTES];

void setUp(void) {
    for (size_t i = 0; i < FRAME_BYTES; i++) {
        glass_frame.bits[i] = (uint8_t)(i * 37 + i / 100 * 11);
    }
}

void tearDown(void) {
}

static time_t at(const char *text) {
    time_t moment = 0;
    TEST_ASSERT_TRUE_MESSAGE(time_parse_iso8601(text, &moment), text);
    return moment;
}

static unsigned count_lines(const char *text) {
    unsigned count = 0;
    for (const char *end = strchr(text, '\n'); end != nullptr; end = strchr(end + 1, '\n')) {
        count++;
    }
    return count;
}

static struct config config_for(const char *dashboard_url) {
    struct config config = {.ok = true, .pi_key = "pi-secret"};
    TEST_ASSERT_LESS_THAN_size_t(
            sizeof config.dashboard_url,
            g_strlcpy(config.dashboard_url, dashboard_url, sizeof config.dashboard_url));
    return config;
}

static void assert_contains(const GString *request, const char *expected) {
    TEST_ASSERT_NOT_NULL_MESSAGE(memmem(request->str, request->len, expected, strlen(expected)),
                                 expected);
}

static struct report sample_report(void) {
    return (struct report){
            .refresh = REFRESH_FULL,
            .at = at(NOON),
            .phrase_id = 5,
            .weather_at = at(HALF_PAST_ELEVEN),
            .too_long_id = 7,
    };
}

static bool upload_answered(int status, struct record_state *state, const struct report *report) {
    const struct fake_route routes[] = {{"/api/pi/screen", status, nullptr, 0}};
    struct fake_server server;
    fake_server_start(&server, routes, G_N_ELEMENTS(routes));
    const struct config config = config_for(server.base);
    start_capture();
    const bool uploaded = record_upload(state, &config, &glass_frame, report);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_EQUAL_size_t(1, fake_server_request_count(&server));
    fake_server_stop(&server);
    return uploaded;
}

static void assert_upload_keeps(int status, const char *expected_error) {
    struct record_state state = {.reported_too_long_id = 3};
    const struct report report = sample_report();
    TEST_ASSERT_FALSE_MESSAGE(upload_answered(status, &state, &report), expected_error);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected_error, state.last_error, captured);
    TEST_ASSERT_EQUAL_INT64(3, state.reported_too_long_id);
    gchar *logged = g_strdup_printf("answered %d", status); // once, where it happened
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(captured, logged), captured);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, count_lines(captured), captured);
    g_free(logged);
}

static void test_error_is_kept_without_logging_it_again(void) {
    struct record_state state = {};
    start_capture();
    record_error(&state, "panel refresh failed");
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_EQUAL_STRING("", captured); // the caller logged it where it happened
    TEST_ASSERT_EQUAL_STRING("panel refresh failed", state.last_error);

    gchar *long_error = g_strnfill(300, 'e');
    start_capture();
    record_error(&state, long_error);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_EQUAL_size_t(sizeof state.last_error - 1, strlen(state.last_error));
    TEST_ASSERT_EQUAL_MEMORY(long_error, state.last_error, sizeof state.last_error - 1);
    g_free(long_error);
}

static void test_kept_error_keeps_whole_characters(void) {
    /* 错 is 3 bytes: after 124 bytes it fits the 127 kept, after 125 or 126 it would be cut */
    const size_t lead_bytes[] = {124, 125, 126};
    const size_t kept_bytes[] = {127, 125, 126};
    for (size_t i = 0; i < G_N_ELEMENTS(lead_bytes); i++) {
        GString *text = g_string_new(nullptr);
        for (size_t j = 0; j < lead_bytes[i]; j++) {
            g_string_append_c(text, 'e');
        }
        for (int j = 0; j < 60; j++) {
            g_string_append(text, "错");
        }
        struct record_state state = {};
        start_capture();
        record_error(&state, text->str);
        finish_capture(captured, sizeof captured);
        TEST_ASSERT_EQUAL_size_t(kept_bytes[i], strlen(state.last_error));
        TEST_ASSERT_EQUAL_MEMORY(text->str, state.last_error, kept_bytes[i]);
        TEST_ASSERT_TRUE(g_utf8_validate(state.last_error, -1, nullptr));

        /* the report carries the kept text exactly */
        const struct report report = {
                .refresh = REFRESH_FULL, .at = at(NOON), .last_error = state.last_error};
        const struct readings unmeasured = {};
        char json[1024];
        TEST_ASSERT_TRUE(record_report_json(&report, &unmeasured, json, sizeof json));
        TEST_ASSERT_TRUE(g_utf8_validate(json, -1, nullptr));
        gchar *ending = g_strdup_printf("\"lastError\":\"%s\"}", state.last_error);
        TEST_ASSERT_TRUE_MESSAGE(g_str_has_suffix(json, ending), json);
        g_free(ending);
        g_string_free(text, TRUE);
    }

    /* a report's own error that ends in a broken character loses that character */
    const struct report broken = {.refresh = REFRESH_FULL, .at = at(NOON), .last_error = "ab\xe9"};
    const struct readings unmeasured = {};
    char json[1024];
    TEST_ASSERT_TRUE(record_report_json(&broken, &unmeasured, json, sizeof json));
    TEST_ASSERT_TRUE_MESSAGE(g_str_has_suffix(json, "\"lastError\":\"ab\"}"), json);
}

static void test_report_json_matches_old_shape(void) {
    const struct report full = {
            .refresh = REFRESH_FULL,
            .at = at(NOON),
            .phrase_id = 5,
            .weather_at = at(HALF_PAST_ELEVEN),
            .too_long_id = 7,
            .last_error = "weather fetch \"failed\"",
    };
    const struct readings measured = {
            .has_wifi_signal = true,
            .wifi_signal = 70,
            .has_pi_temperature = true,
            .pi_temperature_c = 45.277,
    };
    char json[512];
    TEST_ASSERT_TRUE(record_report_json(&full, &measured, json, sizeof json));
    TEST_ASSERT_EQUAL_STRING("{\"refresh\":\"full\",\"at\":\"2026-10-26T11:00:00Z\",\"phraseId\":5,"
                             "\"weatherAt\":\"2026-10-26T10:30:00Z\",\"tooLong\":[7],"
                             "\"missingGlyphs\":[],\"wifiSignal\":70,\"piTemperatureC\":45.277,"
                             "\"lastError\":\"weather fetch \\\"failed\\\"\"}",
                             json);

    const struct report sparse = {
            .refresh = REFRESH_PARTIAL,
            .at = at("2026-10-27T00:01:05+01:00"),
            .missing_glyphs_id = 3,
    };
    const struct readings unmeasured = {};
    TEST_ASSERT_TRUE(record_report_json(&sparse, &unmeasured, json, sizeof json));
    TEST_ASSERT_EQUAL_STRING("{\"refresh\":\"partial\",\"at\":\"2026-10-26T23:01:05Z\","
                             "\"phraseId\":null,\"weatherAt\":null,\"tooLong\":[],"
                             "\"missingGlyphs\":[3],\"wifiSignal\":null,\"piTemperatureC\":null,"
                             "\"lastError\":null}",
                             json);

    char small[64];
    start_capture();
    const bool fits = record_report_json(&sparse, &unmeasured, small, sizeof small);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_FALSE(fits);
    TEST_ASSERT_EQUAL_STRING("<3>the screen report doesn't fit in 64 bytes\n", captured);

    const struct report none = {.refresh = REFRESH_NONE, .at = sparse.at};
    start_capture();
    const bool reported = record_report_json(&none, &unmeasured, json, sizeof json);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_FALSE(reported);
    TEST_ASSERT_EQUAL_STRING("<3>a screen report needs the refresh that was done\n", captured);
}

static void assert_last_error_length(const char *last_error, size_t expected_bytes) {
    const struct report report = {
            .refresh = REFRESH_FULL, .at = at(NOON), .last_error = last_error};
    const struct readings unmeasured = {};
    char json[8192];
    TEST_ASSERT_TRUE(record_report_json(&report, &unmeasured, json, sizeof json));
    const char *value = strstr(json, "\"lastError\":\"");
    TEST_ASSERT_NOT_NULL(value);
    value += strlen("\"lastError\":\"");
    const char *end = strchr(value, '"');
    TEST_ASSERT_NOT_NULL(end);
    TEST_ASSERT_EQUAL_size_t(expected_bytes, (size_t)(end - value));
    TEST_ASSERT_EQUAL_STRING("\"}", end);
    TEST_ASSERT_EQUAL_MEMORY(last_error, value, expected_bytes);
}

static void test_report_json_caps_last_error_at_1000_characters(void) {
    gchar *exact = g_strnfill(1000, 'e');
    gchar *over = g_strnfill(1500, 'e');
    assert_last_error_length(exact, 1000);
    assert_last_error_length(over, 1000);
    GString *han = g_string_new(nullptr);
    for (int i = 0; i < 1001; i++) {
        g_string_append(han, "错");
    }
    assert_last_error_length(han->str, 3000); // 1000 characters of 3 bytes each, none split
    g_string_free(han, TRUE);
    g_free(over);
    g_free(exact);
}

static void test_upload_sends_screen_and_report_parts(void) {
    const struct fake_route routes[] = {{"/api/pi/screen", 204, nullptr, 0}};
    struct fake_server server;
    fake_server_start(&server, routes, G_N_ELEMENTS(routes));
    const struct config config = config_for(server.base);
    struct record_state state = {};
    const struct report report = sample_report();
    TEST_ASSERT_TRUE(record_upload(&state, &config, &glass_frame, &report));

    TEST_ASSERT_EQUAL_size_t(1, fake_server_request_count(&server));
    fake_assert_request_line(&server, 0, "PUT /api/pi/screen HTTP/1.1");
    const GString *request = fake_server_request(&server, 0);
    assert_contains(request, BEARER_LINE);
    assert_contains(request, "\r\nContent-Type: multipart/form-data; boundary=");
    struct png png = frame_encode_png(&glass_frame);
    TEST_ASSERT_TRUE(png.ok);
    GString *screen_part = g_string_new("Content-Disposition: form-data; name=\"screen\"; "
                                        "filename=\"screen\"\r\nContent-Type: image/png\r\n\r\n");
    g_string_append_len(screen_part, (const gchar *)png.data, (gssize)png.length);
    g_string_append(screen_part, "\r\n");
    TEST_ASSERT_NOT_NULL(memmem(request->str, request->len, screen_part->str, screen_part->len));
    assert_contains(request, REPORT_PART
                    "{\"refresh\":\"full\",\"at\":\"2026-10-26T11:00:00Z\",\"phraseId\":5,"
                    "\"weatherAt\":\"2026-10-26T10:30:00Z\",\"tooLong\":[7],"
                    "\"missingGlyphs\":[],\"wifiSignal\":");
    assert_contains(request, ",\"lastError\":null}\r\n");
    gchar *lowered = g_ascii_strdown(request->str, (gssize)request->len);
    TEST_ASSERT_NULL(strstr(lowered, "x-kotetsu-now"));
    g_free(lowered);
    g_string_free(screen_part, TRUE);
    free(png.data);
    fake_server_stop(&server);
}

static void test_failed_upload_keeps_error(void) {
    assert_upload_keeps(400, "upload failed: dashboard bad response");
    assert_upload_keeps(401, "upload failed: dashboard unauthorized");
    assert_upload_keeps(403, "upload failed: dashboard unauthorized");
    assert_upload_keeps(413, "upload failed: dashboard bad response");
    assert_upload_keeps(500, "upload failed: dashboard unreachable");
    assert_upload_keeps(503, "upload failed: dashboard unreachable");
    assert_upload_keeps(200, "upload failed: dashboard bad response"); // only 204 means stored

    int port = 0;
    const int listener = fake_listen(&port);
    TEST_ASSERT_EQUAL_INT(0, close(listener)); // nothing listens there now
    gchar *base = g_strdup_printf("http://127.0.0.1:%d", port);
    const struct config config = config_for(base);
    struct record_state state = {.reported_too_long_id = 3};
    const struct report report = sample_report();
    start_capture();
    const bool uploaded = record_upload(&state, &config, &glass_frame, &report);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_FALSE(uploaded);
    TEST_ASSERT_EQUAL_STRING("upload failed: dashboard unreachable", state.last_error);
    TEST_ASSERT_EQUAL_INT64(3, state.reported_too_long_id);
    g_free(base);
}

static void test_successful_upload_clears_error(void) {
    struct record_state state = {.last_error = "dashboard unreachable", .reported_too_long_id = 3};
    struct report report = sample_report();
    report.too_long_id = 9;
    report.last_error = state.last_error;
    const struct fake_route routes[] = {{"/api/pi/screen", 204, nullptr, 0}};
    struct fake_server server;
    fake_server_start(&server, routes, G_N_ELEMENTS(routes));
    const struct config config = config_for(server.base);
    TEST_ASSERT_TRUE(record_upload(&state, &config, &glass_frame, &report));
    assert_contains(fake_server_request(&server, 0),
                    ",\"lastError\":\"dashboard unreachable\"}\r\n");
    TEST_ASSERT_EQUAL_STRING("", state.last_error);
    TEST_ASSERT_EQUAL_INT64(9, state.reported_too_long_id);
    fake_server_stop(&server);
}

static void test_upload_after_refresh(void) {
    const struct record_state state = {};
    TEST_ASSERT_TRUE(record_should_upload(&state, true, 0, true));
    TEST_ASSERT_TRUE(record_should_upload(&state, true, 7, true));
    TEST_ASSERT_FALSE(record_should_upload(&state, false, 0, true));
}

static void test_upload_new_too_long_when_glass_known(void) {
    const struct record_state state = {};
    TEST_ASSERT_TRUE(record_should_upload(&state, false, 9, true));
    TEST_ASSERT_FALSE(record_should_upload(&state, false, 9, false)); // no known picture to send
}

static void test_no_upload_for_reported_too_long(void) {
    struct record_state state = {.reported_too_long_id = 9};
    TEST_ASSERT_FALSE(record_should_upload(&state, false, 9, true));
    TEST_ASSERT_TRUE(record_should_upload(&state, false, 10, true));

    struct report report = sample_report();
    report.too_long_id = 10;
    TEST_ASSERT_TRUE(upload_answered(204, &state, &report));
    TEST_ASSERT_FALSE(record_should_upload(&state, false, 10, true));

    report.too_long_id = 11;
    TEST_ASSERT_FALSE(upload_answered(500, &state, &report));
    TEST_ASSERT_TRUE(record_should_upload(&state, false, 11, true)); // tried again next tick
}

#ifdef __linux__
// the kernel's own figure for the first connected interface, which reading it warns is deprecated
static bool kernel_link_quality(double *out) {
    gchar *contents = nullptr;
    if (!g_file_get_contents(KERNEL_LINK_QUALITY, &contents, nullptr, nullptr)) {
        return false;
    }
    bool found = false;
    gchar **lines = g_strsplit(contents, "\n", -1);
    for (gchar **line = lines; *line != nullptr && !found; line++) {
        const char *after_name = strchr(*line, ':');
        if (after_name == nullptr) {
            continue;
        }
        const char *quality = after_name + 1 + strspn(after_name + 1, " ");
        quality += strcspn(quality, " ");
        char *end = nullptr;
        *out = g_ascii_strtod(quality, &end);
        found = end != quality;
    }
    g_strfreev(lines);
    g_free(contents);
    return found;
}
#endif

static void test_wifi_signal_on_0_to_70_scale(void) {
    const struct readings readings = record_read_device();
#ifdef __linux__
    double expected = 0;
    const bool linked = kernel_link_quality(&expected);
    TEST_ASSERT_EQUAL_INT(linked, readings.has_wifi_signal);
    if (linked) {
        TEST_ASSERT_FLOAT_WITHIN(5.0f, (float)expected, (float)readings.wifi_signal);
        TEST_ASSERT_TRUE(readings.wifi_signal >= 0 && readings.wifi_signal <= 70);
    }
#else
    TEST_ASSERT_FALSE(readings.has_wifi_signal);
#endif
}

static void test_temperature_from_millidegrees(void) {
#ifdef __linux__
    const struct readings readings = record_read_device();
    TEST_ASSERT_TRUE(readings.has_pi_temperature);
    TEST_ASSERT_FLOAT_WITHIN(50.0f, 60.0f, (float)readings.pi_temperature_c);
#else
    const struct readings readings = record_read_device();
    TEST_ASSERT_FALSE(readings.has_pi_temperature);
#endif
}

static void test_log_line_has_every_part(void) {
    const struct tick_summary refreshed = {
            .dashboard = "dashboard ok",
            .weather = "weather fetched",
            .drawn = true,
            .phrase_id = 183,
            .too_long = true,
            .missing_glyphs = 2,
            .decision = {REFRESH_FULL, REASON_NEW_PHRASE},
            .refreshed = true,
            .panel = true,
            .upload = "upload ok",
    };
    start_capture();
    record_log_tick(&refreshed);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_EQUAL_STRING("<6>dashboard ok; weather fetched; phrase 183 instead of a too-long "
                             "one, 2 missing glyphs; full refresh (new phrase) done; upload ok\n",
                             captured);

    const struct tick_summary failed = {
            .dashboard = "dashboard unreachable",
            .weather = "weather not due",
            .drawn = true,
            .phrase_id = 7,
            .missing_glyphs = 1,
            .decision = {REFRESH_PARTIAL, REASON_CHANGE},
            .panel = true,
            .upload = "upload failed",
    };
    start_capture();
    record_log_tick(&failed);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_EQUAL_STRING(
            "<6>dashboard unreachable; weather not due; phrase 7, 1 missing glyph; "
            "partial refresh (change) failed; upload failed\n",
            captured);

    const struct tick_summary quiet = {
            .dashboard = "no phrase available",
            .weather = "weather failed",
            .drawn = true,
            .decision = {REFRESH_NONE, REASON_SAME_FRAME},
            .panel = true,
            .upload = "no upload",
    };
    start_capture();
    record_log_tick(&quiet);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_EQUAL_STRING("<6>no phrase available; weather failed; no phrase, 0 missing glyphs; "
                             "no refresh (same frame); no upload\n",
                             captured);
}

static void test_log_line_says_no_panel_and_nothing_drawn(void) {
    const struct tick_summary without_panel = {
            .dashboard = "dashboard ok",
            .weather = "weather not due",
            .drawn = true,
            .phrase_id = 183,
            .decision = {REFRESH_FULL, REASON_UNKNOWN_GLASS},
            .refreshed = true,
            .upload = "upload ok",
    };
    start_capture();
    record_log_tick(&without_panel);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_EQUAL_STRING("<6>dashboard ok; weather not due; phrase 183, 0 missing glyphs; "
                             "full refresh (unknown glass) done (no panel); upload ok\n",
                             captured);

    const struct tick_summary undrawn = {
            .dashboard = "dashboard ok",
            .weather = "weather fetched",
            .upload = "no upload",
    };
    start_capture();
    record_log_tick(&undrawn);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_EQUAL_STRING(
            "<6>dashboard ok; weather fetched; nothing drawn; no refresh; no upload\n", captured);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_error_is_kept_without_logging_it_again);
    RUN_TEST(test_kept_error_keeps_whole_characters);
    RUN_TEST(test_report_json_matches_old_shape);
    RUN_TEST(test_report_json_caps_last_error_at_1000_characters);
    RUN_TEST(test_upload_sends_screen_and_report_parts);
    RUN_TEST(test_failed_upload_keeps_error);
    RUN_TEST(test_successful_upload_clears_error);
    RUN_TEST(test_upload_after_refresh);
    RUN_TEST(test_upload_new_too_long_when_glass_known);
    RUN_TEST(test_no_upload_for_reported_too_long);
    RUN_TEST(test_wifi_signal_on_0_to_70_scale);
    RUN_TEST(test_temperature_from_millidegrees);
    RUN_TEST(test_log_line_has_every_part);
    RUN_TEST(test_log_line_says_no_panel_and_nothing_drawn);
    return UNITY_END();
}
