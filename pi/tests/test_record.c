#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "config.h"
#include "draw/frame.h"
#include "fake_server.h"
#include "record.h"
#include "screen/screen.h"
#include "shared/time.h"
#include "stderr_capture.h"
#include "unity.h"
#include "weather.h"

#define PHRASE            "慢慢来，\n比较快"
#define NOON              "2026-10-26T12:00:00+01:00"
#define HALF_PAST_ELEVEN  "2026-10-26T11:30:00+01:00"
#define OLD_PROGRAM_STATE "fixtures/old_program_state"
#define BEARER_LINE       "\r\nAuthorization: Bearer pi-secret\r\n"
#define REPORT_PART \
    "Content-Disposition: form-data; name=\"report\"\r\nContent-Type: application/json\r\n\r\n"
#define KERNEL_LINK_QUALITY "/proc/net/wireless"
#define TEMPERATURE_PATH    "/sys/class/thermal/thermal_zone0/temp"

static constexpr size_t CAPTURE_BYTES = 16 * 1024;
static constexpr long OLD_PROGRAM_STATE_BYTES = 56'242;
static constexpr long CHECKSUM_BYTES = 8;
static constexpr long HEADER_BYTES = 5;
static constexpr long JSON_AREA_BYTES = 16'384;
static constexpr uint64_t FNV_OFFSET_BASIS = 0xcbf2'9ce4'8422'2325;
static constexpr uint64_t FNV_PRIME = 0x100'0000'01b3;

static gchar *dir;
static gchar *path;
static struct frame glass_frame;
static char captured[CAPTURE_BYTES];

void setUp(void) {
    dir = g_dir_make_tmp("kotetsu-record-XXXXXX", nullptr);
    TEST_ASSERT_NOT_NULL(dir);
    path = g_build_filename(dir, "state", nullptr);
    for (size_t i = 0; i < FRAME_BYTES; i++) {
        glass_frame.bits[i] = (uint8_t)(i * 37 + i / 100 * 11);
    }
}

void tearDown(void) {
    GDir *entries = g_dir_open(dir, 0, nullptr);
    if (entries != nullptr) {
        const gchar *name;
        while ((name = g_dir_read_name(entries)) != nullptr) {
            gchar *entry = g_build_filename(dir, name, nullptr);
            if (g_remove(entry) != 0) {
                fprintf(stderr, "can't remove %s\n", entry);
            }
            g_free(entry);
        }
        g_dir_close(entries);
    }
    if (g_rmdir(dir) != 0) { // no asserts: after a failed test, Unity stops at the first one
        fprintf(stderr, "can't remove %s\n", dir);
    }
    g_free(path);
    g_free(dir);
}

static time_t at(const char *text) {
    time_t moment = 0;
    TEST_ASSERT_TRUE_MESSAGE(time_parse_iso8601(text, &moment), text);
    return moment;
}

static unsigned entries_in_dir(void) {
    GDir *entries = g_dir_open(dir, 0, nullptr);
    TEST_ASSERT_NOT_NULL(entries);
    unsigned count = 0;
    while (g_dir_read_name(entries) != nullptr) {
        count++;
    }
    g_dir_close(entries);
    return count;
}

static long file_size(const char *file) {
    struct stat status;
    TEST_ASSERT_EQUAL_INT(0, stat(file, &status));
    return (long)status.st_size;
}

static int read_byte(long offset) {
    FILE *file = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_INT(0, fseek(file, offset, SEEK_SET));
    const int byte = fgetc(file);
    TEST_ASSERT_EQUAL_INT(0, fclose(file));
    return byte;
}

static void write_byte(long offset, int byte) {
    FILE *file = fopen(path, "r+b");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_INT(0, fseek(file, offset, SEEK_SET));
    TEST_ASSERT_EQUAL_INT(byte, fputc(byte, file));
    TEST_ASSERT_EQUAL_INT(0, fclose(file));
}

static unsigned count_lines(const char *text) {
    unsigned count = 0;
    for (const char *end = strchr(text, '\n'); end != nullptr; end = strchr(end + 1, '\n')) {
        count++;
    }
    return count;
}

static void saved_wall(struct wall *wall) {
    *wall = screen_new_wall();
    wall->known = true;
    wall->frame = glass_frame;
    wall->phrase_id = 183;
    memcpy(wall->phrase_text, PHRASE, sizeof PHRASE);
    wall->date = (struct civil_date){2026, 10, 26};
    wall->last_kind = REFRESH_PARTIAL;
    wall->partials_since_full = 3;
    wall->last_refresh = at(NOON);
    wall->last_tick = at(NOON) + 15;
    wall->cleanup_minute = 660;
    wall->cleanup_due = true;
    wall->full_refresh_done = 41;
    wall->greeting_wait_since = at(NOON) - 30;
}

static struct weather_state saved_weather(void) {
    return (struct weather_state){
            .have_report = true,
            .report = {.fetched_at = at(HALF_PAST_ELEVEN),
                       .now_c = -3,
                       .low_c = -7,
                       .high_c = 2,
                       .code = 71},
            .next_fetch = at(NOON) + 1800,
            .open_meteo_failures = 1,
            .eaqi = 64,
            .have_warning = true,
            .warning = "Silny wiatr",
    };
}

static struct weather_state starting_weather(void) {
    return (struct weather_state){.eaqi = -1};
}

static void save_sample(void) {
    struct wall wall;
    saved_wall(&wall);
    const struct weather_state weather = saved_weather();
    TEST_ASSERT_TRUE(record_save(path, &wall, &weather));
}

static void assert_weather_untouched(const struct weather_state *weather) {
    TEST_ASSERT_FALSE(weather->have_report);
    TEST_ASSERT_EQUAL_INT64(0, weather->report.fetched_at);
    TEST_ASSERT_EQUAL_INT(-1, weather->eaqi);
    TEST_ASSERT_EQUAL_INT64(0, weather->next_fetch);
    TEST_ASSERT_EQUAL_INT(0, weather->open_meteo_failures);
    TEST_ASSERT_FALSE(weather->have_warning);
}

static void assert_wall_as_started(const struct wall *wall) {
    static const struct frame blank = {};
    TEST_ASSERT_FALSE(wall->known);
    TEST_ASSERT_EQUAL_MEMORY(blank.bits, wall->frame.bits, FRAME_BYTES);
    TEST_ASSERT_EQUAL_INT64(0, wall->phrase_id);
    TEST_ASSERT_EQUAL_STRING("", wall->phrase_text);
    TEST_ASSERT_EQUAL_INT(0, wall->date.year);
    TEST_ASSERT_EQUAL_INT(REFRESH_NONE, wall->last_kind);
    TEST_ASSERT_EQUAL_INT(0, wall->partials_since_full);
    TEST_ASSERT_EQUAL_INT64(0, wall->last_refresh);
    TEST_ASSERT_EQUAL_INT64(0, wall->last_tick);
    TEST_ASSERT_EQUAL_INT(600, wall->cleanup_minute);
    TEST_ASSERT_FALSE(wall->cleanup_due);
    TEST_ASSERT_EQUAL_INT64(0, wall->full_refresh_done);
    TEST_ASSERT_EQUAL_INT64(0, wall->greeting_wait_since);
}

static void assert_first_refresh_full(const struct wall *wall) {
    const struct wanted wanted = {
            .frame = &glass_frame,
            .phrase_id = wall->phrase_id,
            .phrase_text = wall->phrase_text,
            .date = {2026, 10, 26},
            .cleanup_minute = wall->cleanup_minute,
            .answered_today = true,
    };
    const struct decision decision = screen_decide(wall, &wanted, at(NOON) + 600);
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, decision.kind);
    TEST_ASSERT_EQUAL_INT(REASON_UNKNOWN_GLASS, decision.reason);
}

static void assert_ignored_with_one_warning(const char *state_path) {
    struct wall wall;
    memset(&wall, 0xA5, sizeof wall);
    struct weather_state weather = starting_weather();
    start_capture();
    const bool restored = record_restore(state_path, &wall, &weather);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_FALSE_MESSAGE(restored, captured);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, count_lines(captured), captured);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("<4>", captured, 3, captured);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(captured, state_path), captured);
    assert_wall_as_started(&wall);
    assert_weather_untouched(&weather);
    assert_first_refresh_full(&wall);
}

static void assert_ignored_because(const char *state_path, const char *reason) {
    assert_ignored_with_one_warning(state_path);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(captured, reason), captured);
}

// a good file with its JSON area replaced and its checksum made again, as only a bug could write
static void write_json_area(const char *area, size_t length) {
    save_sample();
    gchar *contents = nullptr;
    gsize size = 0;
    TEST_ASSERT_TRUE(g_file_get_contents(path, &contents, &size, nullptr));
    char *checksum = contents + size - CHECKSUM_BYTES;
    char *json = checksum - JSON_AREA_BYTES;
    TEST_ASSERT_EQUAL_MEMORY("{\"phraseId\"", json, strlen("{\"phraseId\""));
    TEST_ASSERT_LESS_OR_EQUAL_size_t(JSON_AREA_BYTES, length);
    memset(json, 0, JSON_AREA_BYTES);
    memcpy(json, area, length);
    uint64_t sum = FNV_OFFSET_BASIS;
    for (const char *byte = contents; byte < checksum; byte++) {
        sum = (sum ^ (uint8_t)*byte) * FNV_PRIME;
    }
    memcpy(checksum, &sum, sizeof sum);
    TEST_ASSERT_TRUE(g_file_set_contents(path, contents, (gssize)size, nullptr));
    g_free(contents);
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

static void test_save_then_restore_round_trips_with_glass_unknown(void) {
    save_sample();
    struct wall wall;
    memset(&wall, 0xA5, sizeof wall);
    struct weather_state weather = starting_weather();
    start_capture();
    const bool restored = record_restore(path, &wall, &weather);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_TRUE_MESSAGE(restored, captured);
    TEST_ASSERT_EQUAL_STRING("", captured);

    TEST_ASSERT_FALSE(wall.known);
    TEST_ASSERT_EQUAL_MEMORY(glass_frame.bits, wall.frame.bits, FRAME_BYTES);
    TEST_ASSERT_EQUAL_INT64(183, wall.phrase_id);
    TEST_ASSERT_EQUAL_STRING(PHRASE, wall.phrase_text);
    TEST_ASSERT_EQUAL_INT64(41, wall.full_refresh_done);
    TEST_ASSERT_EQUAL_INT(660, wall.cleanup_minute);
    TEST_ASSERT_EQUAL_INT(3, wall.partials_since_full);
    TEST_ASSERT_EQUAL_INT64(at(NOON), wall.last_refresh);
    TEST_ASSERT_EQUAL_INT(0, wall.date.year); // not saved: these start as screen_new_wall has them
    TEST_ASSERT_EQUAL_INT(REFRESH_NONE, wall.last_kind);
    TEST_ASSERT_EQUAL_INT64(0, wall.last_tick);
    TEST_ASSERT_FALSE(wall.cleanup_due);
    TEST_ASSERT_EQUAL_INT64(0, wall.greeting_wait_since);

    TEST_ASSERT_TRUE(weather.have_report);
    TEST_ASSERT_EQUAL_INT64(at(HALF_PAST_ELEVEN), weather.report.fetched_at);
    TEST_ASSERT_EQUAL_INT(-3, weather.report.now_c);
    TEST_ASSERT_EQUAL_INT(-7, weather.report.low_c);
    TEST_ASSERT_EQUAL_INT(2, weather.report.high_c);
    TEST_ASSERT_EQUAL_INT(71, weather.report.code);
    TEST_ASSERT_EQUAL_INT(-1, weather.eaqi); // the rest of the weather state is the caller's
    TEST_ASSERT_EQUAL_INT64(0, weather.next_fetch);
    TEST_ASSERT_EQUAL_INT(0, weather.open_meteo_failures);
    TEST_ASSERT_FALSE(weather.have_warning);
    assert_first_refresh_full(&wall);
}

static void test_save_then_restore_without_phrase_request_or_weather(void) {
    struct wall wall = screen_new_wall();
    wall.cleanup_minute = 0; // midnight is a real minute, not "none"
    const struct weather_state nothing = starting_weather();
    TEST_ASSERT_TRUE(record_save(path, &wall, &nothing));

    struct wall restored;
    memset(&restored, 0xA5, sizeof restored);
    struct weather_state weather = starting_weather();
    TEST_ASSERT_TRUE(record_restore(path, &restored, &weather));
    TEST_ASSERT_FALSE(restored.known);
    TEST_ASSERT_EQUAL_INT64(0, restored.phrase_id);
    TEST_ASSERT_EQUAL_STRING("", restored.phrase_text);
    TEST_ASSERT_EQUAL_INT64(0, restored.full_refresh_done);
    TEST_ASSERT_EQUAL_INT(0, restored.cleanup_minute);
    assert_weather_untouched(&weather);
}

static void test_longest_phrase_round_trips(void) {
    struct wall wall;
    saved_wall(&wall);
    memset(wall.phrase_text, '\x01', PHRASE_TEXT_MAX_BYTES - 1); // each byte is 6 in JSON
    wall.phrase_text[PHRASE_TEXT_MAX_BYTES - 1] = '\0';
    const struct weather_state weather = saved_weather();
    TEST_ASSERT_TRUE(record_save(path, &wall, &weather));

    struct wall restored;
    struct weather_state restored_weather = starting_weather();
    TEST_ASSERT_TRUE(record_restore(path, &restored, &restored_weather));
    TEST_ASSERT_EQUAL_size_t(PHRASE_TEXT_MAX_BYTES - 1, strlen(restored.phrase_text));
    TEST_ASSERT_EQUAL_STRING(wall.phrase_text, restored.phrase_text);
    TEST_ASSERT_TRUE(restored_weather.have_report);
}

static void test_restore_ignores_old_program_file(void) {
    TEST_ASSERT_EQUAL_INT64(OLD_PROGRAM_STATE_BYTES, file_size(OLD_PROGRAM_STATE));
    gchar *contents = nullptr;
    TEST_ASSERT_TRUE(g_file_get_contents(OLD_PROGRAM_STATE, &contents, nullptr, nullptr));
    TEST_ASSERT_EQUAL_MEMORY("KTS2\x01", contents, HEADER_BYTES); // the old magic, version 1
    g_free(contents);
    assert_ignored_with_one_warning(OLD_PROGRAM_STATE);
}

static void test_restore_ignores_truncated_file(void) {
    save_sample();
    const long full = file_size(path);
    const long lengths[] = {full - 1, full - CHECKSUM_BYTES, full / 2, HEADER_BYTES, 3, 0};
    for (size_t i = 0; i < G_N_ELEMENTS(lengths); i++) {
        TEST_ASSERT_EQUAL_INT(0, truncate(path, (off_t)lengths[i]));
        assert_ignored_with_one_warning(path);
    }
}

static void test_restore_ignores_longer_file(void) {
    save_sample();
    FILE *file = fopen(path, "ab");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_INT('x', fputc('x', file));
    TEST_ASSERT_EQUAL_INT(0, fclose(file));
    assert_ignored_with_one_warning(path);
}

static void test_restore_ignores_corrupt_file(void) {
    save_sample();
    const long full = file_size(path);
    gchar *contents = nullptr;
    gsize length = 0;
    TEST_ASSERT_TRUE(g_file_get_contents(path, &contents, &length, nullptr));
    const char *json = memmem(contents, length, "{\"phraseId\"", strlen("{\"phraseId\""));
    TEST_ASSERT_NOT_NULL(json);
    const long json_start = (long)(json - contents);
    g_free(contents);

    const long offsets[] = {0,
                            3,
                            4,
                            HEADER_BYTES,
                            full / 2,
                            json_start,
                            json_start + 40,
                            full - CHECKSUM_BYTES - 1,
                            full - CHECKSUM_BYTES,
                            full - 1};
    for (size_t i = 0; i < G_N_ELEMENTS(offsets); i++) {
        const int byte = read_byte(offsets[i]);
        write_byte(offsets[i], byte ^ 0x01);
        assert_ignored_with_one_warning(path);
        write_byte(offsets[i], byte);
        struct wall wall;
        struct weather_state weather = starting_weather();
        TEST_ASSERT_TRUE(record_restore(path, &wall, &weather));
    }
}

static void test_restore_ignores_unusable_values(void) {
    struct wall wall;
    const struct weather_state weather = saved_weather();

    saved_wall(&wall);
    wall.cleanup_minute = MINUTES_PER_DAY;
    TEST_ASSERT_TRUE(record_save(path, &wall, &weather));
    assert_ignored_because(path, "its clean-up minute is unusable");

    saved_wall(&wall);
    wall.cleanup_minute = -1;
    TEST_ASSERT_TRUE(record_save(path, &wall, &weather));
    assert_ignored_because(path, "its clean-up minute is unusable");

    saved_wall(&wall);
    wall.phrase_id = -183;
    TEST_ASSERT_TRUE(record_save(path, &wall, &weather));
    assert_ignored_because(path, "its phrase is unusable");

    saved_wall(&wall);
    wall.phrase_text[0] = '\0';
    TEST_ASSERT_TRUE(record_save(path, &wall, &weather));
    assert_ignored_because(path, "its phrase is unusable");

    saved_wall(&wall);
    memcpy(wall.phrase_text, "abc\xc3", sizeof "abc\xc3");
    TEST_ASSERT_TRUE(record_save(path, &wall, &weather));
    assert_ignored_because(path, "its phrase is unusable");
}

static void test_restore_ignores_json_that_isnt_an_object(void) {
    static const char good[] = "{\"cleanupMinute\":700}"; // the area is replaced and signed right
    write_json_area(good, strlen(good));
    struct wall wall;
    struct weather_state weather = starting_weather();
    TEST_ASSERT_TRUE(record_restore(path, &wall, &weather));
    TEST_ASSERT_EQUAL_INT(700, wall.cleanup_minute);

    static const char *const areas[] = {"[]", "\"x\"", "7", "null", "nonsense", ""};
    for (size_t i = 0; i < G_N_ELEMENTS(areas); i++) {
        write_json_area(areas[i], strlen(areas[i]));
        assert_ignored_because(path, "its JSON isn't an object");
    }
}

static void test_restore_ignores_unusable_full_refresh(void) {
    static const char *const areas[] = {
            "{\"fullRefresh\":\"41\",\"cleanupMinute\":660}",
            "{\"fullRefresh\":41.5,\"cleanupMinute\":660}",
            "{\"fullRefresh\":null,\"cleanupMinute\":660}",
    };
    for (size_t i = 0; i < G_N_ELEMENTS(areas); i++) {
        write_json_area(areas[i], strlen(areas[i]));
        assert_ignored_because(path, "its full refresh is unusable");
    }
}

static void test_restore_ignores_unusable_weather(void) {
    static const char *const areas[] = {
            "{\"cleanupMinute\":660,\"weather\":{\"fetchedAt\":1790000000,\"nowC\":7,\"lowC\":3,"
            "\"highC\":11}}",
            "{\"cleanupMinute\":660,\"weather\":{\"fetchedAt\":1790000000,\"nowC\":7,\"lowC\":3,"
            "\"highC\":11,\"code\":\"61\"}}",
            "{\"cleanupMinute\":660,\"weather\":{\"fetchedAt\":1790000000,\"nowC\":7.5,\"lowC\":3,"
            "\"highC\":11,\"code\":61}}",
            "{\"cleanupMinute\":660,\"weather\":7}",
    };
    for (size_t i = 0; i < G_N_ELEMENTS(areas); i++) {
        write_json_area(areas[i], strlen(areas[i]));
        assert_ignored_because(path, "its weather report is unusable");
    }
}

static void test_restore_ignores_unterminated_json(void) {
    char *area =
            g_malloc(JSON_AREA_BYTES); // no NUL and no end, so the parse runs to the area's edge
    memset(area, 'a', JSON_AREA_BYTES);
    static const char start[] = "{\"cleanupMinute\":660,\"pad\":\"";
    memcpy(area, start, strlen(start));
    write_json_area(area, JSON_AREA_BYTES);
    assert_ignored_because(path, "its JSON isn't an object");
    g_free(area);
}

static void test_restore_missing_file_is_quiet(void) {
    gchar *missing = g_build_filename(dir, "missing", nullptr);
    struct wall wall;
    memset(&wall, 0xA5, sizeof wall);
    struct weather_state weather = starting_weather();
    start_capture();
    const bool restored = record_restore(missing, &wall, &weather);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_FALSE(restored);
    gchar *expected = g_strdup_printf("<6>no saved state at %s\n", missing);
    TEST_ASSERT_EQUAL_STRING(expected, captured);
    assert_wall_as_started(&wall);
    assert_weather_untouched(&weather);
    assert_first_refresh_full(&wall);
    g_free(expected);
    g_free(missing);
}

static void test_save_leaves_no_temp_file(void) {
    save_sample();
    const long full = file_size(path);
    const struct wall wall = screen_new_wall();
    const struct weather_state nothing = starting_weather();
    TEST_ASSERT_TRUE(record_save(path, &wall, &nothing));
    TEST_ASSERT_EQUAL_UINT(1, entries_in_dir());
    TEST_ASSERT_EQUAL_INT64(full, file_size(path)); // the file is the same size, whatever it holds
    struct stat status;
    TEST_ASSERT_EQUAL_INT(0, stat(path, &status));
    TEST_ASSERT_EQUAL_HEX(0600, status.st_mode & 0777);
}

static void test_save_to_missing_directory_fails(void) {
    gchar *nowhere = g_build_filename(dir, "missing", "state", nullptr);
    struct wall wall;
    saved_wall(&wall);
    const struct weather_state weather = saved_weather();
    start_capture();
    const bool saved = record_save(nowhere, &wall, &weather);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_FALSE(saved);
    gchar *expected =
            g_strdup_printf("<3>can't save the state to %s: %s\n", nowhere, strerror(ENOENT));
    TEST_ASSERT_EQUAL_STRING(expected, captured);
    TEST_ASSERT_EQUAL_UINT(0, entries_in_dir());
    g_free(expected);
    g_free(nowhere);
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
    RUN_TEST(test_save_then_restore_round_trips_with_glass_unknown);
    RUN_TEST(test_save_then_restore_without_phrase_request_or_weather);
    RUN_TEST(test_longest_phrase_round_trips);
    RUN_TEST(test_restore_ignores_old_program_file);
    RUN_TEST(test_restore_ignores_truncated_file);
    RUN_TEST(test_restore_ignores_longer_file);
    RUN_TEST(test_restore_ignores_corrupt_file);
    RUN_TEST(test_restore_ignores_unusable_values);
    RUN_TEST(test_restore_ignores_json_that_isnt_an_object);
    RUN_TEST(test_restore_ignores_unusable_full_refresh);
    RUN_TEST(test_restore_ignores_unusable_weather);
    RUN_TEST(test_restore_ignores_unterminated_json);
    RUN_TEST(test_restore_missing_file_is_quiet);
    RUN_TEST(test_save_leaves_no_temp_file);
    RUN_TEST(test_save_to_missing_directory_fails);
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
