#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "config.h"
#include "fake_server.h"
#include "phrase.h"
#include "shared/time.h"
#include "unity.h"

#define PHRASE       "慢慢来，\n比较快"
#define NOON         "2026-10-26T12:00:00+01:00"
#define A_TICK_LATER "2026-10-26T12:00:15+01:00"
#define PI_NOW       "fixtures/pi_now.json"
#define BEARER_LINE  "\r\nAuthorization: Bearer pi-secret\r\n"
#define PLAIN_PHRASE \
    "\"phrase\":{\"id\":7,\"text\":\"新的一天\",\"until\":\"2026-10-27T03:00:00+01:00\"}"
#define CLEANUP_MINUTE ",\"cleanupMinute\":600"

static const char *const FULL_ANSWER =
        "{\"phrase\":{\"id\":183,\"text\":\"慢慢来，\\n比较快\",\"until\":\"2026-10-27T15:00:00."
        "000Z\"},"
        "\"greeting\":{\"icon\":\"cake\",\"text\":\"生日快乐\"},\"full_refresh\":41,"
        "\"cleanupMinute\":660}";
static const char *const PLAIN_ANSWER =
        "{" PLAIN_PHRASE ",\"greeting\":null,\"full_refresh\":null" CLEANUP_MINUTE "}";

static gchar *dir;

void setUp(void) {
    dir = g_dir_make_tmp("kotetsu-phrase-XXXXXX", nullptr);
    TEST_ASSERT_NOT_NULL(dir);
}

void tearDown(void) {
    gchar *body = g_build_filename(dir, "answer.json", nullptr);
    if (g_remove(body) != 0 && g_file_test(body, G_FILE_TEST_EXISTS)) {
        fprintf(stderr, "can't remove %s\n", body);
    }
    if (g_rmdir(dir) != 0) { // no asserts: after a failed test, Unity stops at the first one
        fprintf(stderr, "can't remove %s\n", dir);
    }
    g_free(body);
    g_free(dir);
}

static time_t at(const char *text) {
    time_t moment = 0;
    TEST_ASSERT_TRUE_MESSAGE(time_parse_iso8601(text, &moment), text);
    return moment;
}

static gchar *text_of_bytes(size_t bytes) {
    GString *text = g_string_new(nullptr);
    while (text->len + strlen("字") <= bytes) {
        g_string_append(text, "字");
    }
    while (text->len < bytes) {
        g_string_append_c(text, 'a');
    }
    return g_string_free(text, FALSE);
}

static gchar *answer_with_text(const char *text) {
    return g_strdup_printf("{\"phrase\":{\"id\":7,\"text\":\"%s\",\"until\":"
                           "\"2026-10-27T03:00:00+01:00\"},\"greeting\":null,"
                           "\"full_refresh\":null" CLEANUP_MINUTE "}",
                           text);
}

static gchar *answer_with_greeting(const char *greeting) {
    return g_strdup_printf(
            "{" PLAIN_PHRASE ",\"greeting\":%s,\"full_refresh\":null" CLEANUP_MINUTE "}", greeting);
}

static struct config config_for(const char *dashboard_url) {
    struct config config = {.ok = true, .pi_key = "pi-secret"};
    TEST_ASSERT_LESS_THAN_size_t(
            sizeof config.dashboard_url,
            g_strlcpy(config.dashboard_url, dashboard_url, sizeof config.dashboard_url));
    return config;
}

static struct phrase fetch_from(int status, const char *fixture, struct phrase_state *state,
                                time_t now) {
    const struct fake_route routes[] = {{"/api/pi/now", status, fixture, 0}};
    struct fake_server server;
    fake_server_start(&server, routes, G_N_ELEMENTS(routes));
    const struct config config = config_for(server.base);
    const struct phrase phrase = phrase_fetch(state, &config, now);
    fake_server_stop(&server);
    return phrase;
}

static struct phrase fetch_body(const char *body, struct phrase_state *state, time_t now) {
    gchar *path = g_build_filename(dir, "answer.json", nullptr);
    TEST_ASSERT_TRUE(g_file_set_contents(path, body, -1, nullptr));
    const struct phrase phrase = fetch_from(200, path, state, now);
    TEST_ASSERT_EQUAL_INT(0, g_remove(path));
    g_free(path);
    return phrase;
}

static void assert_pi_now_answer(const struct phrase_answer *answer) {
    TEST_ASSERT_EQUAL_INT64(183, answer->id);
    TEST_ASSERT_EQUAL_STRING(PHRASE, answer->text);
    TEST_ASSERT_EQUAL_INT64(at("2026-10-27T15:00:00Z"), answer->until);
    TEST_ASSERT_TRUE(answer->has_greeting);
    TEST_ASSERT_EQUAL_INT(GREETING_CAKE, answer->greeting_icon);
    TEST_ASSERT_EQUAL_STRING("生日快乐", answer->greeting);
    TEST_ASSERT_EQUAL_INT64(41, answer->full_refresh);
    TEST_ASSERT_EQUAL_INT(600, answer->cleanup_minute);
}

static struct phrase_state answered_at(const char *moment) {
    struct phrase_state state = {};
    const struct phrase phrase = fetch_from(200, PI_NOW, &state, at(moment));
    TEST_ASSERT_EQUAL_INT(DASHBOARD_OK, phrase.status);
    return state;
}

static void assert_previous_kept(const struct phrase *phrase, const struct phrase_state *state) {
    TEST_ASSERT_TRUE(phrase->have_answer);
    assert_pi_now_answer(&phrase->answer);
    TEST_ASSERT_TRUE(phrase->answered_today);
    TEST_ASSERT_TRUE(state->have_answer);
    assert_pi_now_answer(&state->answer);
}

static void assert_rejected(const char *json) {
    struct phrase_answer answer = {.id = 99};
    TEST_ASSERT_EQUAL_INT_MESSAGE(DASHBOARD_BAD_RESPONSE, phrase_parse(json, &answer), json);
    TEST_ASSERT_EQUAL_INT64_MESSAGE(99, answer.id, json);
}

static void assert_rejected_and_previous_kept(const char *json) {
    assert_rejected(json);
    struct phrase_state state = answered_at(NOON);
    const struct phrase phrase = fetch_body(json, &state, at(A_TICK_LATER));
    TEST_ASSERT_EQUAL_INT_MESSAGE(DASHBOARD_BAD_RESPONSE, phrase.status, json);
    assert_previous_kept(&phrase, &state);
}

static void test_parse_reads_every_field(void) {
    struct phrase_answer answer;
    memset(&answer, 0xA5, sizeof answer);
    TEST_ASSERT_EQUAL_INT(DASHBOARD_OK, phrase_parse(FULL_ANSWER, &answer));
    TEST_ASSERT_EQUAL_INT64(183, answer.id);
    TEST_ASSERT_EQUAL_STRING(PHRASE, answer.text);
    TEST_ASSERT_EQUAL_INT64(at("2026-10-27T15:00:00Z"), answer.until);
    TEST_ASSERT_TRUE(answer.has_greeting);
    TEST_ASSERT_EQUAL_INT(GREETING_CAKE, answer.greeting_icon);
    TEST_ASSERT_EQUAL_STRING("生日快乐", answer.greeting);
    TEST_ASSERT_EQUAL_INT64(41, answer.full_refresh);
    TEST_ASSERT_EQUAL_INT(660, answer.cleanup_minute);
}

static void test_parse_accepts_nulls(void) {
    struct phrase_answer answer;
    memset(&answer, 0xA5, sizeof answer);
    TEST_ASSERT_EQUAL_INT(DASHBOARD_OK, phrase_parse(PLAIN_ANSWER, &answer));
    TEST_ASSERT_EQUAL_INT64(7, answer.id);
    TEST_ASSERT_EQUAL_STRING("新的一天", answer.text);
    TEST_ASSERT_EQUAL_INT64(at("2026-10-27T03:00:00+01:00"), answer.until);
    TEST_ASSERT_FALSE(answer.has_greeting);
    TEST_ASSERT_EQUAL_STRING("", answer.greeting);
    TEST_ASSERT_EQUAL_INT64(0, answer.full_refresh);
    TEST_ASSERT_EQUAL_INT(600, answer.cleanup_minute);
}

static void test_parse_needs_a_cleanup_minute_of_the_day(void) {
    struct phrase_answer answer;
    TEST_ASSERT_EQUAL_INT(DASHBOARD_OK,
                          phrase_parse("{" PLAIN_PHRASE ",\"greeting\":null,\"full_refresh\":null,"
                                       "\"cleanupMinute\":0}",
                                       &answer));
    TEST_ASSERT_EQUAL_INT(0, answer.cleanup_minute);
    TEST_ASSERT_EQUAL_INT(DASHBOARD_OK,
                          phrase_parse("{" PLAIN_PHRASE ",\"greeting\":null,\"full_refresh\":null,"
                                       "\"cleanupMinute\":1439}",
                                       &answer));
    TEST_ASSERT_EQUAL_INT(1439, answer.cleanup_minute);
    assert_rejected("{" PLAIN_PHRASE ",\"greeting\":null,\"full_refresh\":null}");
    assert_rejected("{" PLAIN_PHRASE
                    ",\"greeting\":null,\"full_refresh\":null,\"cleanupMinute\":null}");
    assert_rejected("{" PLAIN_PHRASE
                    ",\"greeting\":null,\"full_refresh\":null,\"cleanupMinute\":-1}");
    assert_rejected("{" PLAIN_PHRASE
                    ",\"greeting\":null,\"full_refresh\":null,\"cleanupMinute\":1440}");
    assert_rejected("{" PLAIN_PHRASE
                    ",\"greeting\":null,\"full_refresh\":null,\"cleanupMinute\":600.5}");
    assert_rejected("{" PLAIN_PHRASE
                    ",\"greeting\":null,\"full_refresh\":null,\"cleanupMinute\":\"600\"}");
}

static void test_parse_rejects_bad_shapes(void) {
    static const char *const bad[] = {
            "",
            "nonsense",
            "[]",
            "{}",
            "{\"phrase\":null,\"greeting\":null,\"full_refresh\":null" CLEANUP_MINUTE "}",
            "{\"phrase\":{\"id\":0,\"text\":\"x\",\"until\":\"2026-10-27T03:00:00+01:00\"},"
            "\"greeting\":null,\"full_refresh\":null" CLEANUP_MINUTE "}",
            "{\"phrase\":{\"id\":-7,\"text\":\"x\",\"until\":\"2026-10-27T03:00:00+01:00\"},"
            "\"greeting\":null,\"full_refresh\":null" CLEANUP_MINUTE "}",
            "{\"phrase\":{\"id\":7.5,\"text\":\"x\",\"until\":\"2026-10-27T03:00:00+01:00\"},"
            "\"greeting\":null,\"full_refresh\":null" CLEANUP_MINUTE "}",
            "{\"phrase\":{\"id\":\"7\",\"text\":\"x\",\"until\":\"2026-10-27T03:00:00+01:00\"},"
            "\"greeting\":null,\"full_refresh\":null" CLEANUP_MINUTE "}",
            "{\"phrase\":{\"id\":7,\"text\":\"\",\"until\":\"2026-10-27T03:00:00+01:00\"},"
            "\"greeting\":null,\"full_refresh\":null" CLEANUP_MINUTE "}",
            "{\"phrase\":{\"id\":7,\"until\":\"2026-10-27T03:00:00+01:00\"},"
            "\"greeting\":null,\"full_refresh\":null" CLEANUP_MINUTE "}",
            "{\"phrase\":{\"id\":7,\"text\":5,\"until\":\"2026-10-27T03:00:00+01:00\"},"
            "\"greeting\":null,\"full_refresh\":null" CLEANUP_MINUTE "}",
            "{\"phrase\":{\"id\":7,\"text\":\"x\"},\"greeting\":null,\"full_refresh\":"
            "null" CLEANUP_MINUTE "}",
            "{\"phrase\":{\"id\":7,\"text\":\"x\",\"until\":\"soon\"},"
            "\"greeting\":null,\"full_refresh\":null" CLEANUP_MINUTE "}",
            "{\"phrase\":{\"id\":7,\"text\":\"x\",\"until\":\"2026-10-27T03:00:00\"},"
            "\"greeting\":null,\"full_refresh\":null" CLEANUP_MINUTE "}",
            "{" PLAIN_PHRASE ",\"full_refresh\":null" CLEANUP_MINUTE "}",
            "{" PLAIN_PHRASE ",\"greeting\":\"cake\",\"full_refresh\":null" CLEANUP_MINUTE "}",
            "{" PLAIN_PHRASE
            ",\"greeting\":{\"icon\":\"cake\",\"text\":\"\"},\"full_refresh\":null" CLEANUP_MINUTE
            "}",
            "{" PLAIN_PHRASE
            ",\"greeting\":{\"icon\":\"cake\"},\"full_refresh\":null" CLEANUP_MINUTE "}",
            "{" PLAIN_PHRASE ",\"greeting\":null" CLEANUP_MINUTE "}",
            "{" PLAIN_PHRASE ",\"greeting\":null,\"full_refresh\":\"41\"" CLEANUP_MINUTE "}",
            "{" PLAIN_PHRASE ",\"greeting\":null,\"full_refresh\":0" CLEANUP_MINUTE "}",
            "{" PLAIN_PHRASE ",\"greeting\":null,\"full_refresh\":-3" CLEANUP_MINUTE "}",
            "{" PLAIN_PHRASE ",\"greeting\":null,\"full_refresh\":41.5" CLEANUP_MINUTE "}",
    };
    for (size_t i = 0; i < G_N_ELEMENTS(bad); i++) {
        assert_rejected(bad[i]);
    }
}

static void test_text_of_2047_bytes_accepted(void) {
    gchar *text = text_of_bytes(2047);
    gchar *json = answer_with_text(text);
    struct phrase_answer answer;
    TEST_ASSERT_EQUAL_INT(DASHBOARD_OK, phrase_parse(json, &answer));
    TEST_ASSERT_EQUAL_STRING(text, answer.text);

    struct phrase_state state = answered_at(NOON);
    const struct phrase phrase = fetch_body(json, &state, at(A_TICK_LATER));
    TEST_ASSERT_EQUAL_INT(DASHBOARD_OK, phrase.status);
    TEST_ASSERT_EQUAL_size_t(2047, strlen(phrase.answer.text));
    TEST_ASSERT_EQUAL_STRING(text, phrase.answer.text);
    TEST_ASSERT_EQUAL_STRING(text, state.answer.text);
    g_free(json);
    g_free(text);
}

static void test_text_of_2048_bytes_rejected(void) {
    gchar *text = text_of_bytes(2048);
    TEST_ASSERT_EQUAL_size_t(2048, strlen(text));
    gchar *json = answer_with_text(text);
    assert_rejected_and_previous_kept(json);
    g_free(json);
    g_free(text);
}

static void test_invalid_utf8_rejected(void) {
    gchar *answers[] = {
            answer_with_text("\xff\xfe"),
            answer_with_text("abc\xc3"),
            answer_with_text("\xc0\xaf"),
            answer_with_greeting("{\"icon\":\"cake\",\"text\":\"\xed\xa0\x80\"}"),
    };
    for (size_t i = 0; i < G_N_ELEMENTS(answers); i++) {
        assert_rejected_and_previous_kept(answers[i]);
        g_free(answers[i]);
    }
}

static void test_unknown_greeting_icon_rejected(void) {
    static const char *const greetings[] = {
            "{\"icon\":\"moon\",\"text\":\"x\"}",
            "{\"icon\":\"Cake\",\"text\":\"x\"}",
            "{\"icon\":\"\",\"text\":\"x\"}",
            "{\"icon\":3,\"text\":\"x\"}",
            "{\"text\":\"x\"}",
    };
    for (size_t i = 0; i < G_N_ELEMENTS(greetings); i++) {
        gchar *json = answer_with_greeting(greetings[i]);
        assert_rejected_and_previous_kept(json);
        g_free(json);
    }
}

static void test_six_greeting_icons(void) {
    static const struct {
        const char *name;
        enum greeting_icon icon;
    } icons[] = {
            {"heart", GREETING_HEART}, {"rings", GREETING_RINGS},   {"cake", GREETING_CAKE},
            {"cat", GREETING_CAT},     {"hearts", GREETING_HEARTS}, {"star", GREETING_STAR},
    };

    for (size_t i = 0; i < G_N_ELEMENTS(icons); i++) {
        gchar *greeting = g_strdup_printf("{\"icon\":\"%s\",\"text\":\"x\"}", icons[i].name);
        gchar *json = answer_with_greeting(greeting);
        struct phrase_answer answer = {};
        TEST_ASSERT_EQUAL_INT_MESSAGE(DASHBOARD_OK, phrase_parse(json, &answer), icons[i].name);
        TEST_ASSERT_TRUE(answer.has_greeting);
        TEST_ASSERT_EQUAL_INT_MESSAGE(icons[i].icon, answer.greeting_icon, icons[i].name);
        TEST_ASSERT_EQUAL_STRING("x", answer.greeting);
        g_free(json);
        g_free(greeting);
    }
}

static void test_greeting_of_63_bytes_accepted_64_rejected(void) {
    gchar *longest = text_of_bytes(63);
    gchar *too_long = text_of_bytes(64);
    gchar *fits_greeting = g_strdup_printf("{\"icon\":\"star\",\"text\":\"%s\"}", longest);
    gchar *overflows_greeting = g_strdup_printf("{\"icon\":\"star\",\"text\":\"%s\"}", too_long);
    gchar *fits = answer_with_greeting(fits_greeting);
    gchar *overflows = answer_with_greeting(overflows_greeting);
    struct phrase_answer answer;
    TEST_ASSERT_EQUAL_INT(DASHBOARD_OK, phrase_parse(fits, &answer));
    TEST_ASSERT_EQUAL_STRING(longest, answer.greeting);
    TEST_ASSERT_EQUAL_INT(GREETING_STAR, answer.greeting_icon);
    assert_rejected(overflows);
    g_free(overflows);
    g_free(fits);
    g_free(overflows_greeting);
    g_free(fits_greeting);
    g_free(too_long);
    g_free(longest);
}

static void test_fetch_asks_for_now_with_the_key(void) {
    const struct fake_route routes[] = {{"/api/pi/now", 200, PI_NOW, 0}};
    struct fake_server server;
    fake_server_start(&server, routes, G_N_ELEMENTS(routes));
    const struct config config = config_for(server.base);
    struct phrase_state state = {};
    const struct phrase phrase = phrase_fetch(&state, &config, at(NOON));
    TEST_ASSERT_EQUAL_INT(DASHBOARD_OK, phrase.status);
    TEST_ASSERT_EQUAL_size_t(1, fake_server_request_count(&server));
    fake_assert_request_line(&server, 0, "GET /api/pi/now HTTP/1.1");
    const GString *request = fake_server_request(&server, 0);
    TEST_ASSERT_NOT_NULL(memmem(request->str, request->len, BEARER_LINE, strlen(BEARER_LINE)));
    gchar *lowered = g_ascii_strdown(request->str, (gssize)request->len);
    TEST_ASSERT_NULL(strstr(lowered, "x-kotetsu-now"));
    g_free(lowered);
    fake_server_stop(&server);
}

static void test_200_sets_answer_and_answered_today(void) {
    struct phrase_state state = {};
    const struct phrase phrase = fetch_from(200, PI_NOW, &state, at(NOON));
    TEST_ASSERT_EQUAL_INT(DASHBOARD_OK, phrase.status);
    TEST_ASSERT_TRUE(phrase.have_answer);
    assert_pi_now_answer(&phrase.answer);
    TEST_ASSERT_TRUE(phrase.answered_today);
    TEST_ASSERT_TRUE(state.have_answer);
    assert_pi_now_answer(&state.answer);
    TEST_ASSERT_EQUAL_INT(2026, state.answered_on.year);
    TEST_ASSERT_EQUAL_INT(10, state.answered_on.month);
    TEST_ASSERT_EQUAL_INT(26, state.answered_on.day);
}

static void test_new_answer_replaces_the_last_one(void) {
    struct phrase_state state = answered_at(NOON);
    const struct phrase phrase = fetch_body(PLAIN_ANSWER, &state, at(A_TICK_LATER));
    TEST_ASSERT_EQUAL_INT(DASHBOARD_OK, phrase.status);
    TEST_ASSERT_EQUAL_INT64(7, phrase.answer.id);
    TEST_ASSERT_EQUAL_STRING("新的一天", phrase.answer.text);
    TEST_ASSERT_FALSE(phrase.answer.has_greeting);
    TEST_ASSERT_EQUAL_INT64(0, phrase.answer.full_refresh);
    TEST_ASSERT_TRUE(phrase.answered_today);
    TEST_ASSERT_EQUAL_INT64(7, state.answer.id);
    TEST_ASSERT_FALSE(state.answer.has_greeting);
}

static void test_401_keeps_previous_answer(void) {
    struct phrase_state state = answered_at(NOON);
    const struct phrase phrase = fetch_from(401, nullptr, &state, at(A_TICK_LATER));
    TEST_ASSERT_EQUAL_INT(DASHBOARD_UNAUTHORIZED, phrase.status);
    assert_previous_kept(&phrase, &state);
}

static void test_503_is_none_available(void) {
    struct phrase_state never_answered = {};
    const struct phrase first = fetch_from(503, nullptr, &never_answered, at(NOON));
    TEST_ASSERT_EQUAL_INT(DASHBOARD_NO_PHRASE, first.status);
    TEST_ASSERT_FALSE(first.have_answer);
    TEST_ASSERT_FALSE(first.answered_today);
    TEST_ASSERT_FALSE(never_answered.have_answer);

    struct phrase_state state = answered_at(NOON);
    const struct phrase phrase = fetch_from(503, nullptr, &state, at(A_TICK_LATER));
    TEST_ASSERT_EQUAL_INT(DASHBOARD_NO_PHRASE, phrase.status);
    assert_previous_kept(&phrase, &state);
}

static void test_403_is_unauthorized_500_unreachable_404_bad_response(void) {
    static const struct {
        int status;
        enum dashboard_status expected;
    } answers[] = {
            {403, DASHBOARD_UNAUTHORIZED},
            {500, DASHBOARD_UNREACHABLE},
            {404, DASHBOARD_BAD_RESPONSE},
    };

    for (size_t i = 0; i < G_N_ELEMENTS(answers); i++) {
        struct phrase_state state = answered_at(NOON);
        const struct phrase phrase =
                fetch_from(answers[i].status, nullptr, &state, at(A_TICK_LATER));
        TEST_ASSERT_EQUAL_INT(answers[i].expected, phrase.status);
        assert_previous_kept(&phrase, &state);
    }
}

static void test_garbage_is_bad_response_and_keeps_previous(void) {
    struct phrase_state state = answered_at(NOON);
    const struct phrase html = fetch_body("<html>502 Bad Gateway</html>", &state, at(A_TICK_LATER));
    TEST_ASSERT_EQUAL_INT(DASHBOARD_BAD_RESPONSE, html.status);
    assert_previous_kept(&html, &state);

    const struct phrase other_json =
            fetch_from(200, "fixtures/open_meteo_air.json", &state, at(A_TICK_LATER));
    TEST_ASSERT_EQUAL_INT(DASHBOARD_BAD_RESPONSE, other_json.status);
    assert_previous_kept(&other_json, &state);
}

static void test_no_server_is_unreachable(void) {
    struct phrase_state state = answered_at(NOON);
    int port = 0;
    const int listener = fake_listen(&port);
    TEST_ASSERT_EQUAL_INT(0, close(listener)); // nothing listens there now
    gchar *url = g_strdup_printf("http://127.0.0.1:%d", port);
    const struct config config = config_for(url);
    const struct phrase phrase = phrase_fetch(&state, &config, at(A_TICK_LATER));
    TEST_ASSERT_EQUAL_INT(DASHBOARD_UNREACHABLE, phrase.status);
    assert_previous_kept(&phrase, &state);
    g_free(url);
}

static void test_answered_today_false_next_warsaw_day(void) {
    // the first two moments are both on 26 October in UTC; only the Warsaw date moves on
    struct phrase_state state = {};
    const struct phrase evening = fetch_from(200, PI_NOW, &state, at("2026-10-26T23:59:50+01:00"));
    TEST_ASSERT_TRUE(evening.answered_today);

    const struct phrase midnight =
            fetch_from(503, nullptr, &state, at("2026-10-27T00:00:05+01:00"));
    TEST_ASSERT_EQUAL_INT(DASHBOARD_NO_PHRASE, midnight.status);
    TEST_ASSERT_TRUE(midnight.have_answer);
    assert_pi_now_answer(&midnight.answer);
    TEST_ASSERT_FALSE(midnight.answered_today);

    const struct phrase next = fetch_from(200, PI_NOW, &state, at("2026-10-27T00:00:20+01:00"));
    TEST_ASSERT_TRUE(next.answered_today);
    TEST_ASSERT_EQUAL_INT(27, state.answered_on.day);
}

int main(void) {
    if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) { // the program's main does this too
        return EXIT_FAILURE;
    }
    UNITY_BEGIN();
    RUN_TEST(test_parse_reads_every_field);
    RUN_TEST(test_parse_accepts_nulls);
    RUN_TEST(test_parse_needs_a_cleanup_minute_of_the_day);
    RUN_TEST(test_parse_rejects_bad_shapes);
    RUN_TEST(test_text_of_2047_bytes_accepted);
    RUN_TEST(test_text_of_2048_bytes_rejected);
    RUN_TEST(test_invalid_utf8_rejected);
    RUN_TEST(test_unknown_greeting_icon_rejected);
    RUN_TEST(test_six_greeting_icons);
    RUN_TEST(test_greeting_of_63_bytes_accepted_64_rejected);
    RUN_TEST(test_fetch_asks_for_now_with_the_key);
    RUN_TEST(test_200_sets_answer_and_answered_today);
    RUN_TEST(test_new_answer_replaces_the_last_one);
    RUN_TEST(test_401_keeps_previous_answer);
    RUN_TEST(test_503_is_none_available);
    RUN_TEST(test_403_is_unauthorized_500_unreachable_404_bad_response);
    RUN_TEST(test_garbage_is_bad_response_and_keeps_previous);
    RUN_TEST(test_no_server_is_unreachable);
    RUN_TEST(test_answered_today_false_next_warsaw_day);
    const int failures = UNITY_END();
    curl_global_cleanup();
    return failures;
}
