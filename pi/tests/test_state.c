#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "config.h"
#include "draw/frame.h"
#include "screen/screen.h"
#include "shared/time.h"
#include "state.h"
#include "stderr_capture.h"
#include "unity.h"
#include "weather.h"

#define PHRASE            "慢慢来，\n比较快"
#define MASTER_PHRASE     "新的一天，新的开始"
#define NOON              "2026-10-26T12:00:00+01:00"
#define HALF_PAST_ELEVEN  "2026-10-26T11:30:00+01:00"
#define OLD_PROGRAM_STATE "fixtures/old_program_state"
#define MASTER_STATE      "fixtures/state_master"

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
static struct frame master_frame;
static char captured[CAPTURE_BYTES];

void setUp(void) {
    dir = g_dir_make_tmp("kotetsu-state-XXXXXX", nullptr);
    TEST_ASSERT_NOT_NULL(dir);
    path = g_build_filename(dir, "state", nullptr);
    for (size_t i = 0; i < FRAME_BYTES; i++) {
        glass_frame.bits[i] = (uint8_t)(i * 37 + i / 100 * 11);
        master_frame.bits[i] = (uint8_t)(i * 37 + i / 100);
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
    if (g_rmdir(dir) != 0) { // not an assert, which would cut tearDown short
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

static void master_wall(struct wall *wall) {
    *wall = screen_new_wall();
    wall->known = true;
    wall->frame = master_frame;
    wall->phrase_id = 5;
    memcpy(wall->phrase_text, MASTER_PHRASE, sizeof MASTER_PHRASE);
    wall->full_refresh_done = 7;
    wall->cleanup_minute = 600;
    wall->partials_since_full = 2;
    wall->last_refresh = 1'793'000'000;
}

static struct weather_state master_weather(void) {
    return (struct weather_state){
            .have_report = true,
            .report = {.fetched_at = 1'792'999'000,
                       .now_c = -3,
                       .low_c = -7,
                       .high_c = 2,
                       .code = 61},
            .eaqi = -1,
    };
}

static void save_sample(void) {
    struct wall wall;
    saved_wall(&wall);
    const struct weather_state weather = saved_weather();
    TEST_ASSERT_TRUE(state_save(path, &wall, &weather));
}

static void assert_no_weather(const struct saved_state *saved) {
    TEST_ASSERT_FALSE(saved->have_weather);
    TEST_ASSERT_EQUAL_INT64(0, saved->weather.fetched_at);
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
    start_capture();
    const struct saved_state saved = state_restore(state_path);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_FALSE_MESSAGE(saved.ok, captured);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, count_lines(captured), captured);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("<4>", captured, 3, captured);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(captured, state_path), captured);
    assert_wall_as_started(&saved.wall);
    assert_no_weather(&saved);
    assert_first_refresh_full(&saved.wall);
}

static void assert_ignored_because(const char *state_path, const char *reason) {
    assert_ignored_with_one_warning(state_path);
    gchar *expected = g_strdup_printf("<4>ignoring the saved state %s: %s\n", state_path, reason);
    TEST_ASSERT_EQUAL_STRING(expected, captured);
    g_free(expected);
}

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
    memcpy(checksum, &sum, sizeof sum); // signed right, as only a bug could write it
    TEST_ASSERT_TRUE(g_file_set_contents(path, contents, (gssize)size, nullptr));
    g_free(contents);
}

static void test_save_then_restore_round_trips_with_glass_unknown(void) {
    save_sample();
    start_capture();
    const struct saved_state saved = state_restore(path);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_TRUE_MESSAGE(saved.ok, captured);
    TEST_ASSERT_EQUAL_STRING("", captured);

    const struct wall *wall = &saved.wall;
    TEST_ASSERT_FALSE(wall->known);
    TEST_ASSERT_EQUAL_MEMORY(glass_frame.bits, wall->frame.bits, FRAME_BYTES);
    TEST_ASSERT_EQUAL_INT64(183, wall->phrase_id);
    TEST_ASSERT_EQUAL_STRING(PHRASE, wall->phrase_text);
    TEST_ASSERT_EQUAL_INT64(41, wall->full_refresh_done);
    TEST_ASSERT_EQUAL_INT(660, wall->cleanup_minute);
    TEST_ASSERT_EQUAL_INT(3, wall->partials_since_full);
    TEST_ASSERT_EQUAL_INT64(at(NOON), wall->last_refresh);
    TEST_ASSERT_EQUAL_INT(0, wall->date.year); // unsaved fields start as in screen_new_wall
    TEST_ASSERT_EQUAL_INT(REFRESH_NONE, wall->last_kind);
    TEST_ASSERT_EQUAL_INT64(0, wall->last_tick);
    TEST_ASSERT_FALSE(wall->cleanup_due);
    TEST_ASSERT_EQUAL_INT64(0, wall->greeting_wait_since);

    TEST_ASSERT_TRUE(saved.have_weather);
    TEST_ASSERT_EQUAL_INT64(at(HALF_PAST_ELEVEN), saved.weather.fetched_at);
    TEST_ASSERT_EQUAL_INT(-3, saved.weather.now_c);
    TEST_ASSERT_EQUAL_INT(-7, saved.weather.low_c);
    TEST_ASSERT_EQUAL_INT(2, saved.weather.high_c);
    TEST_ASSERT_EQUAL_INT(71, saved.weather.code);
    assert_first_refresh_full(wall);
}

static void test_save_then_restore_without_phrase_request_or_weather(void) {
    struct wall wall = screen_new_wall();
    wall.cleanup_minute = 0; // midnight is a real minute, not "none"
    const struct weather_state nothing = starting_weather();
    TEST_ASSERT_TRUE(state_save(path, &wall, &nothing));

    const struct saved_state saved = state_restore(path);
    TEST_ASSERT_TRUE(saved.ok);
    TEST_ASSERT_FALSE(saved.wall.known);
    TEST_ASSERT_EQUAL_INT64(0, saved.wall.phrase_id);
    TEST_ASSERT_EQUAL_STRING("", saved.wall.phrase_text);
    TEST_ASSERT_EQUAL_INT64(0, saved.wall.full_refresh_done);
    TEST_ASSERT_EQUAL_INT(0, saved.wall.cleanup_minute);
    assert_no_weather(&saved);
}

static void test_longest_phrase_round_trips(void) {
    struct wall wall;
    saved_wall(&wall);
    memset(wall.phrase_text, '\x01', PHRASE_TEXT_MAX_BYTES - 1); // each byte is 6 in JSON
    wall.phrase_text[PHRASE_TEXT_MAX_BYTES - 1] = '\0';
    const struct weather_state weather = saved_weather();
    TEST_ASSERT_TRUE(state_save(path, &wall, &weather));

    const struct saved_state saved = state_restore(path);
    TEST_ASSERT_TRUE(saved.ok);
    TEST_ASSERT_EQUAL_size_t(PHRASE_TEXT_MAX_BYTES - 1, strlen(saved.wall.phrase_text));
    TEST_ASSERT_EQUAL_STRING(wall.phrase_text, saved.wall.phrase_text);
    TEST_ASSERT_TRUE(saved.have_weather);
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
        TEST_ASSERT_TRUE(state_restore(path).ok);
    }
}

static void test_restore_ignores_unusable_values(void) {
    struct wall wall;
    const struct weather_state weather = saved_weather();

    saved_wall(&wall);
    wall.cleanup_minute = MINUTES_PER_DAY;
    TEST_ASSERT_TRUE(state_save(path, &wall, &weather));
    assert_ignored_because(path, "its clean-up minute is unusable");

    saved_wall(&wall);
    wall.cleanup_minute = -1;
    TEST_ASSERT_TRUE(state_save(path, &wall, &weather));
    assert_ignored_because(path, "its clean-up minute is unusable");

    saved_wall(&wall);
    wall.phrase_id = -183;
    TEST_ASSERT_TRUE(state_save(path, &wall, &weather));
    assert_ignored_because(path, "its phrase is unusable");

    saved_wall(&wall);
    wall.phrase_text[0] = '\0';
    TEST_ASSERT_TRUE(state_save(path, &wall, &weather));
    assert_ignored_because(path, "its phrase is unusable");

    saved_wall(&wall);
    memcpy(wall.phrase_text, "abc\xc3", sizeof "abc\xc3");
    TEST_ASSERT_TRUE(state_save(path, &wall, &weather));
    assert_ignored_because(path, "its phrase is unusable");
}

static void test_restore_ignores_json_that_isnt_an_object(void) {
    static const char good[] = "{\"cleanupMinute\":700}"; // the area is replaced and signed right
    write_json_area(good, strlen(good));
    const struct saved_state saved = state_restore(path);
    TEST_ASSERT_TRUE(saved.ok);
    TEST_ASSERT_EQUAL_INT(700, saved.wall.cleanup_minute);

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
    char *area = g_malloc(JSON_AREA_BYTES); // with no NUL or end, parsing reaches the edge
    memset(area, 'a', JSON_AREA_BYTES);
    static const char start[] = "{\"cleanupMinute\":660,\"pad\":\"";
    memcpy(area, start, strlen(start));
    write_json_area(area, JSON_AREA_BYTES);
    assert_ignored_because(path, "its JSON isn't an object");
    g_free(area);
}

static void test_restore_missing_file_is_quiet(void) {
    gchar *missing = g_build_filename(dir, "missing", nullptr);
    start_capture();
    const struct saved_state saved = state_restore(missing);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_FALSE(saved.ok);
    gchar *expected = g_strdup_printf("<6>no saved state at %s\n", missing);
    TEST_ASSERT_EQUAL_STRING(expected, captured);
    assert_wall_as_started(&saved.wall);
    assert_no_weather(&saved);
    assert_first_refresh_full(&saved.wall);
    g_free(expected);
    g_free(missing);
}

static void test_save_leaves_no_temp_file(void) {
    save_sample();
    const long full = file_size(path);
    const struct wall wall = screen_new_wall();
    const struct weather_state nothing = starting_weather();
    TEST_ASSERT_TRUE(state_save(path, &wall, &nothing));
    TEST_ASSERT_EQUAL_UINT(1, entries_in_dir());
    TEST_ASSERT_EQUAL_INT64(full, file_size(path)); // the size never varies with the contents
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
    const bool saved = state_save(nowhere, &wall, &weather);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_FALSE(saved);
    gchar *expected =
            g_strdup_printf("<3>can't save the state to %s: %s\n", nowhere, strerror(ENOENT));
    TEST_ASSERT_EQUAL_STRING(expected, captured);
    TEST_ASSERT_EQUAL_UINT(0, entries_in_dir());
    g_free(expected);
    g_free(nowhere);
}

static void test_master_state_file_restores(void) {
    start_capture();
    const struct saved_state saved = state_restore(MASTER_STATE);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_TRUE_MESSAGE(saved.ok, captured);
    TEST_ASSERT_EQUAL_STRING("", captured);

    TEST_ASSERT_FALSE(saved.wall.known);
    TEST_ASSERT_EQUAL_MEMORY(master_frame.bits, saved.wall.frame.bits, FRAME_BYTES);
    TEST_ASSERT_EQUAL_INT64(5, saved.wall.phrase_id);
    TEST_ASSERT_EQUAL_STRING(MASTER_PHRASE, saved.wall.phrase_text);
    TEST_ASSERT_EQUAL_INT64(7, saved.wall.full_refresh_done);
    TEST_ASSERT_EQUAL_INT(600, saved.wall.cleanup_minute);
    TEST_ASSERT_EQUAL_INT(2, saved.wall.partials_since_full);
    TEST_ASSERT_EQUAL_INT64(1'793'000'000, saved.wall.last_refresh);

    TEST_ASSERT_TRUE(saved.have_weather);
    TEST_ASSERT_EQUAL_INT64(1'792'999'000, saved.weather.fetched_at);
    TEST_ASSERT_EQUAL_INT(-3, saved.weather.now_c);
    TEST_ASSERT_EQUAL_INT(-7, saved.weather.low_c);
    TEST_ASSERT_EQUAL_INT(2, saved.weather.high_c);
    TEST_ASSERT_EQUAL_INT(61, saved.weather.code);
}

static void test_save_writes_masters_bytes(void) {
    struct wall wall;
    master_wall(&wall);
    const struct weather_state weather = master_weather();
    TEST_ASSERT_TRUE(state_save(path, &wall, &weather));

    gchar *master = nullptr;
    gsize master_length = 0;
    TEST_ASSERT_TRUE(g_file_get_contents(MASTER_STATE, &master, &master_length, nullptr));
    gchar *saved = nullptr;
    gsize saved_length = 0;
    TEST_ASSERT_TRUE(g_file_get_contents(path, &saved, &saved_length, nullptr));
    TEST_ASSERT_EQUAL_size_t(master_length, saved_length);
    TEST_ASSERT_EQUAL_MEMORY(master, saved, master_length);
    g_free(saved);
    g_free(master);
}

static void test_restore_returns_the_last_saved_wall_with_glass_unknown(void) {
    struct wall wall;
    saved_wall(&wall);
    const struct weather_state weather = saved_weather();
    TEST_ASSERT_TRUE(state_save(path, &wall, &weather));

    const struct saved_state saved = state_restore(path);
    TEST_ASSERT_TRUE(saved.ok);
    TEST_ASSERT_FALSE(saved.wall.known);
    TEST_ASSERT_EQUAL_MEMORY(glass_frame.bits, saved.wall.frame.bits, FRAME_BYTES);
    TEST_ASSERT_EQUAL_INT64(183, saved.wall.phrase_id);
    TEST_ASSERT_EQUAL_STRING(PHRASE, saved.wall.phrase_text);
    TEST_ASSERT_EQUAL_INT64(at(NOON), saved.wall.last_refresh);
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
    RUN_TEST(test_master_state_file_restores);
    RUN_TEST(test_save_writes_masters_bytes);
    RUN_TEST(test_restore_returns_the_last_saved_wall_with_glass_unknown);
    return UNITY_END();
}
