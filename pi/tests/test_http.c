#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "fake_server.h"
#include "shared/http.h"
#include "stderr_capture.h"
#include "unity.h"

#define HANGING_HOST                    "dns-hangs.kotetsu.test"
#define LOOK_UP_HANGING_HOST            "--look-up-hanging-host"
#define LOOK_UP_HANGING_HOST_IN_A_BATCH "--look-up-hanging-host-in-a-batch"
#define USER_AGENT_LINE                 "\r\nUser-Agent: kotetsu/2.0 github.com/k27dong/kotetsu\r\n"

#ifdef __APPLE__
#define PRELOAD_VARIABLE "DYLD_INSERT_LIBRARIES"
#else
#define PRELOAD_VARIABLE "LD_PRELOAD"
#endif

static constexpr size_t ONE_MEBIBYTE = 1024 * 1024;
static constexpr int64_t CONNECT_TIMEOUT_MS = 5'000;
static constexpr int64_t TOTAL_TIMEOUT_MS = 10'000;
static constexpr int64_t GIVE_UP_WITHIN_MS = 11'000;

static const struct fake_route ROUTES[] = {
        {"/api/pi/now", 200, "fixtures/pi_now.json", 0},
        {"/api/pi/screen", 204, nullptr, 0},
        {"/broken", 500, nullptr, 0},
        {"/one-mebibyte", 200, nullptr, ONE_MEBIBYTE},
        {"/over-one-mebibyte", 200, nullptr, ONE_MEBIBYTE + 1},
        {"/a", 200, "fixtures/pi_now.json", 0},
        {"/b", 404, nullptr, 0},
};

static struct fake_server server;
static const char *test_program;

void setUp(void) {
    fake_server_start(&server, ROUTES, G_N_ELEMENTS(ROUTES));
}

void tearDown(void) {
    fake_server_stop(&server);
}

static int64_t monotonic_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000 + now.tv_nsec / 1'000'000;
}

static gchar *fixture(const char *path) {
    gchar *contents = nullptr;
    TEST_ASSERT_TRUE(g_file_get_contents(path, &contents, nullptr, nullptr));
    return contents;
}

static void assert_contains(const GString *request, const char *expected) {
    TEST_ASSERT_NOT_NULL_MESSAGE(memmem(request->str, request->len, expected, strlen(expected)),
                                 expected);
}

static void assert_lacks(const GString *request, const char *unexpected) {
    TEST_ASSERT_NULL_MESSAGE(memmem(request->str, request->len, unexpected, strlen(unexpected)),
                             unexpected);
}

static const GString *request_starting(const char *start) {
    const GString *found = nullptr;
    for (size_t i = 0; i < fake_server_request_count(&server); i++) {
        const GString *request = fake_server_request(&server, i);
        if (g_str_has_prefix(request->str, start)) {
            found = request;
        }
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(found, start);
    return found;
}

static void assert_not_ok(struct http_response *response) {
    TEST_ASSERT_FALSE(response->ok);
    TEST_ASSERT_NULL(response->body);
    TEST_ASSERT_EQUAL_size_t(0, response->length);
    http_response_free(response);
}

static void test_get_returns_status_and_body(void) {
    gchar *url = g_strconcat(server.base, "/api/pi/now", nullptr);
    struct http_response response = http_get(url, nullptr);
    TEST_ASSERT_TRUE(response.ok);
    TEST_ASSERT_EQUAL_INT64(200, response.status);
    gchar *expected = fixture("fixtures/pi_now.json");
    TEST_ASSERT_EQUAL_size_t(strlen(expected), response.length);
    TEST_ASSERT_EQUAL_STRING(expected, response.body);
    fake_assert_request_line(&server, 0, "GET /api/pi/now HTTP/1.1");
    assert_contains(fake_server_request(&server, 0), USER_AGENT_LINE);
    http_response_free(&response);
    TEST_ASSERT_NULL(response.body);
    g_free(expected);
    g_free(url);
}

static void test_get_sends_bearer(void) {
    gchar *url = g_strconcat(server.base, "/api/pi/now", nullptr);
    struct http_response with_bearer = http_get(url, "pi-secret");
    TEST_ASSERT_TRUE(with_bearer.ok);
    assert_contains(fake_server_request(&server, 0), "\r\nAuthorization: Bearer pi-secret\r\n");
    struct http_response without_bearer = http_get(url, nullptr);
    TEST_ASSERT_TRUE(without_bearer.ok);
    assert_lacks(fake_server_request(&server, 1), "Authorization:");
    http_response_free(&with_bearer);
    http_response_free(&without_bearer);
    g_free(url);
}

static void test_error_status_is_a_response(void) {
    gchar *url = g_strconcat(server.base, "/broken", nullptr);
    struct http_response response = http_get(url, nullptr);
    TEST_ASSERT_TRUE(response.ok);
    TEST_ASSERT_EQUAL_INT64(500, response.status);
    TEST_ASSERT_EQUAL_size_t(0, response.length);
    TEST_ASSERT_EQUAL_STRING("", response.body);
    http_response_free(&response);
    g_free(url);
}

static void test_get_ok_passes_200_and_logs_other_statuses_once(void) {
    gchar *now_url = g_strconcat(server.base, "/api/pi/now", nullptr);
    gchar *broken_url = g_strconcat(server.base, "/broken", nullptr);
    char captured[512];
    start_capture();
    struct http_response passed = http_get_ok(now_url);
    struct http_response refused = http_get_ok(broken_url);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_TRUE(passed.ok);
    TEST_ASSERT_EQUAL_INT64(200, passed.status);
    gchar *expected_body = fixture("fixtures/pi_now.json");
    TEST_ASSERT_EQUAL_STRING(expected_body, passed.body);
    assert_not_ok(&refused);
    gchar *expected_log = g_strdup_printf("<3>%s answered 500\n", broken_url);
    TEST_ASSERT_EQUAL_STRING(expected_log, captured);
    fake_assert_request_line(&server, 1, "GET /broken HTTP/1.1");
    http_response_free(&passed);
    g_free(expected_log);
    g_free(expected_body);
    g_free(broken_url);
    g_free(now_url);
}

static void test_put_multipart_sends_parts(void) {
    const uint8_t png[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0x00, 0xff, 0xfe};
    const char *report = "{\"refresh\":\"full\",\"tooLong\":[]}";
    const struct http_part parts[] = {
            {.name = "screen", .content_type = "image/png", .data = png, .length = sizeof png},
            {.name = "report",
             .content_type = "application/json",
             .data = (const uint8_t *)report,
             .length = strlen(report)},
    };
    gchar *url = g_strconcat(server.base, "/api/pi/screen", nullptr);
    struct http_response response =
            http_put_multipart(url, "pi-secret", parts, G_N_ELEMENTS(parts));
    TEST_ASSERT_TRUE(response.ok);
    TEST_ASSERT_EQUAL_INT64(204, response.status);
    TEST_ASSERT_EQUAL_STRING("", response.body);
    const GString *request = fake_server_request(&server, 0);
    fake_assert_request_line(&server, 0, "PUT /api/pi/screen HTTP/1.1");
    assert_contains(request, "\r\nContent-Type: multipart/form-data; boundary=");
    assert_contains(request, "\r\nAuthorization: Bearer pi-secret\r\n");
    assert_contains(request, USER_AGENT_LINE);
    GString *screen_part = g_string_new("Content-Disposition: form-data; name=\"screen\"; "
                                        "filename=\"screen\"\r\nContent-Type: image/png\r\n\r\n");
    g_string_append_len(screen_part, (const gchar *)png, sizeof png);
    g_string_append(screen_part, "\r\n");
    TEST_ASSERT_NOT_NULL(memmem(request->str, request->len, screen_part->str, screen_part->len));
    assert_contains(request, "Content-Disposition: form-data; name=\"report\"\r\n"
                             "Content-Type: application/json\r\n\r\n"
                             "{\"refresh\":\"full\",\"tooLong\":[]}\r\n");
    g_string_free(screen_part, TRUE);
    http_response_free(&response);
    g_free(url);
}

static void test_requests_carry_no_time_header(void) {
    const uint8_t byte = 0;
    const struct http_part part = {
            .name = "screen", .content_type = "image/png", .data = &byte, .length = 1};
    gchar *now_url = g_strconcat(server.base, "/api/pi/now", nullptr);
    gchar *screen_url = g_strconcat(server.base, "/api/pi/screen", nullptr);
    struct http_response get = http_get(now_url, "pi-secret");
    struct http_response put = http_put_multipart(screen_url, "pi-secret", &part, 1);
    TEST_ASSERT_TRUE(get.ok);
    TEST_ASSERT_TRUE(put.ok);
    for (size_t i = 0; i < 2; i++) {
        const GString *request = fake_server_request(&server, i);
        gchar *lowered = g_ascii_strdown(request->str, (gssize)request->len);
        TEST_ASSERT_NULL_MESSAGE(strstr(lowered, "x-kotetsu-now"), request->str);
        g_free(lowered);
    }
    http_response_free(&get);
    http_response_free(&put);
    g_free(screen_url);
    g_free(now_url);
}

static void test_get_refuses_a_bad_url(void) {
    struct http_response response = http_get("http://", nullptr);
    assert_not_ok(&response);
}

static void test_get_without_server_is_not_ok(void) {
    int port = 0;
    const int listener = fake_listen(&port);
    TEST_ASSERT_EQUAL_INT(0, close(listener)); // the port is free again, so connecting is refused
    gchar *url = g_strdup_printf("http://127.0.0.1:%d/api/pi/now", port);
    struct http_response response = http_get(url, "pi-secret");
    assert_not_ok(&response);
    g_free(url);
}

static void assert_body_capped_at_one_mebibyte(void) {
    gchar *url = g_strconcat(server.base, "/one-mebibyte", nullptr);
    struct http_response response = http_get(url, nullptr);
    TEST_ASSERT_TRUE(response.ok);
    TEST_ASSERT_EQUAL_size_t(ONE_MEBIBYTE, response.length);
    TEST_ASSERT_EQUAL_size_t(ONE_MEBIBYTE, strlen(response.body));
    http_response_free(&response);
    g_free(url);
    url = g_strconcat(server.base, "/over-one-mebibyte", nullptr);
    response = http_get(url, nullptr);
    assert_not_ok(&response);
    g_free(url);
}

static void test_get_caps_the_body_at_one_mebibyte(void) {
    assert_body_capped_at_one_mebibyte();
}

static void test_get_caps_a_chunked_body_at_one_mebibyte(void) {
    fake_server_stop(&server); // a chunked body has no length for libcurl to refuse up front
    fake_server_start_chunked(&server, ROUTES, G_N_ELEMENTS(ROUTES));
    assert_body_capped_at_one_mebibyte();
}

static void test_get_gives_up_on_silent_server(void) {
    int port = 0;
    const int listener = fake_listen(&port); // never accepts, so nothing ever answers
    gchar *url = g_strdup_printf("http://127.0.0.1:%d/api/pi/now", port);
    const int64_t start_ms = monotonic_ms();
    struct http_response response = http_get(url, "pi-secret");
    const int64_t elapsed_ms = monotonic_ms() - start_ms;
    assert_not_ok(&response);
    TEST_ASSERT_GREATER_OR_EQUAL_INT64(TOTAL_TIMEOUT_MS - 500, elapsed_ms);
    TEST_ASSERT_LESS_THAN_INT64(GIVE_UP_WITHIN_MS, elapsed_ms);
    g_free(url);
    TEST_ASSERT_EQUAL_INT(0, close(listener));
}

static int look_up_hanging_host(void) {
    const int64_t start_ms = monotonic_ms();
    struct http_response response = http_get("http://" HANGING_HOST "/", nullptr);
    printf("%d %lld\n", response.ok, (long long)(monotonic_ms() - start_ms));
    http_response_free(&response);
    return EXIT_SUCCESS;
}

static int look_up_hanging_host_in_a_batch(void) {
    const struct http_request request = {.url = "http://" HANGING_HOST "/"};
    const int64_t start_ms = monotonic_ms();
    struct http_batch batch = http_get_batch(&request, 1);
    printf("%d %lld\n", batch.responses[0].ok, (long long)(monotonic_ms() - start_ms));
    http_batch_free(&batch);
    return EXIT_SUCCESS;
}

static void assert_gives_up_on_hanging_dns(const char *child_flag) {
    gchar *dir = g_dir_make_tmp("kotetsu-dns-XXXXXX", nullptr);
    TEST_ASSERT_NOT_NULL(dir);
    gchar *library = g_build_filename(dir, "hanging_dns.so", nullptr);
    gchar *compiler = g_strdup(g_getenv("CC") != nullptr ? g_getenv("CC") : "cc");
#ifdef __APPLE__
    gchar *build[] = {compiler, "-std=c23", "-dynamiclib", "-o", library, "hanging_dns.c", nullptr};
#else
    gchar *build[] = {compiler, "-std=c23", "-D_GNU_SOURCE", "-shared", "-fPIC",
                      "-o",     library,    "hanging_dns.c", nullptr};
#endif
    gint status = 0;
    TEST_ASSERT_TRUE(g_spawn_sync(nullptr, build, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr,
                                  nullptr, nullptr, &status, nullptr));
    TEST_ASSERT_TRUE(g_spawn_check_wait_status(status, nullptr));
    gchar **environment = g_environ_setenv(g_get_environ(), PRELOAD_VARIABLE, library, TRUE);
    // AddressSanitizer refuses to start when another library is preloaded ahead of it
    environment = g_environ_setenv(environment, "ASAN_OPTIONS", "verify_asan_link_order=0", TRUE);
    gchar *child[] = {(gchar *)test_program, (gchar *)child_flag, nullptr};
    gchar *output = nullptr;
    TEST_ASSERT_TRUE(g_spawn_sync(nullptr, child, environment, G_SPAWN_DEFAULT, nullptr, nullptr,
                                  &output, nullptr, &status, nullptr));
    // removed before any assertion can end the test
    const bool removed = g_remove(library) == 0 && g_rmdir(dir) == 0;
    TEST_ASSERT_TRUE(g_spawn_check_wait_status(status, nullptr));
    gchar *rest = nullptr;
    const gint64 ok = g_ascii_strtoll(output, &rest, 10);
    const gint64 elapsed_ms = g_ascii_strtoll(rest, nullptr, 10);
    TEST_ASSERT_EQUAL_INT64(0, ok);
    // the lookup counts toward the connect timeout
    TEST_ASSERT_GREATER_OR_EQUAL_INT64(CONNECT_TIMEOUT_MS - 500, elapsed_ms);
    TEST_ASSERT_LESS_THAN_INT64(CONNECT_TIMEOUT_MS + 2'000, elapsed_ms);
    TEST_ASSERT_TRUE(removed);
    g_free(output);
    g_strfreev(environment);
    g_free(compiler);
    g_free(library);
    g_free(dir);
}

static void test_get_gives_up_on_hanging_dns(void) {
    assert_gives_up_on_hanging_dns(LOOK_UP_HANGING_HOST);
}

struct fetch {
    const char *url;
    struct http_response response;
};

static void *fetch_in_thread(void *argument) {
    struct fetch *fetch = argument;
    fetch->response = http_get(fetch->url, "pi-secret");
    return nullptr;
}

static void test_two_threads_fetch_at_once(void) {
    gchar *url = g_strconcat(server.base, "/api/pi/now", nullptr);
    gchar *expected = fixture("fixtures/pi_now.json");
    struct fetch fetches[2] = {{.url = url}, {.url = url}};
    pthread_t threads[2];
    for (size_t i = 0; i < 2; i++) {
        TEST_ASSERT_EQUAL_INT(0,
                              pthread_create(&threads[i], nullptr, fetch_in_thread, &fetches[i]));
    }
    for (size_t i = 0; i < 2; i++) {
        TEST_ASSERT_EQUAL_INT(0, pthread_join(threads[i], nullptr));
    }
    for (size_t i = 0; i < 2; i++) {
        TEST_ASSERT_TRUE(fetches[i].response.ok);
        TEST_ASSERT_EQUAL_INT64(200, fetches[i].response.status);
        TEST_ASSERT_EQUAL_STRING(expected, fetches[i].response.body);
        http_response_free(&fetches[i].response);
    }
    TEST_ASSERT_EQUAL_size_t(2, fake_server_request_count(&server));
    g_free(expected);
    g_free(url);
}

static void test_batch_returns_each_answer_in_order(void) {
    gchar *a_url = g_strconcat(server.base, "/a", nullptr);
    gchar *b_url = g_strconcat(server.base, "/b", nullptr);
    const struct http_request requests[] = {{.url = a_url}, {.url = b_url}};
    struct http_batch batch = http_get_batch(requests, G_N_ELEMENTS(requests));
    TEST_ASSERT_EQUAL_size_t(2, batch.count);
    TEST_ASSERT_TRUE(batch.responses[0].ok);
    TEST_ASSERT_EQUAL_INT64(200, batch.responses[0].status);
    gchar *expected = fixture("fixtures/pi_now.json");
    TEST_ASSERT_EQUAL_size_t(strlen(expected), batch.responses[0].length);
    TEST_ASSERT_EQUAL_STRING(expected, batch.responses[0].body);
    TEST_ASSERT_TRUE(batch.responses[1].ok);
    TEST_ASSERT_EQUAL_INT64(404, batch.responses[1].status);
    http_batch_free(&batch);
    TEST_ASSERT_NULL(batch.responses[0].body);
    g_free(expected);
    g_free(b_url);
    g_free(a_url);
}

static void test_batch_sends_bearer_only_where_given(void) {
    gchar *a_url = g_strconcat(server.base, "/a", nullptr);
    gchar *b_url = g_strconcat(server.base, "/b", nullptr);
    const struct http_request requests[] = {{.url = a_url, .bearer = "pi-secret"}, {.url = b_url}};
    struct http_batch batch = http_get_batch(requests, G_N_ELEMENTS(requests));
    TEST_ASSERT_TRUE(batch.responses[0].ok);
    TEST_ASSERT_TRUE(batch.responses[1].ok);
    assert_contains(request_starting("GET /a HTTP/1.1\r\n"),
                    "\r\nAuthorization: Bearer pi-secret\r\n");
    assert_lacks(request_starting("GET /b HTTP/1.1\r\n"), "Authorization:");
    http_batch_free(&batch);
    g_free(b_url);
    g_free(a_url);
}

static void test_batch_failure_stays_with_its_request(void) {
    int port = 0;
    const int listener = fake_listen(&port);
    TEST_ASSERT_EQUAL_INT(0, close(listener)); // the port is free again, so connecting is refused
    gchar *a_url = g_strconcat(server.base, "/a", nullptr);
    gchar *closed_url = g_strdup_printf("http://127.0.0.1:%d/x", port);
    const struct http_request requests[] = {{.url = a_url}, {.url = closed_url}};
    char captured[512];
    start_capture();
    struct http_batch batch = http_get_batch(requests, G_N_ELEMENTS(requests));
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_TRUE(batch.responses[0].ok);
    TEST_ASSERT_EQUAL_INT64(200, batch.responses[0].status);
    TEST_ASSERT_FALSE(batch.responses[1].ok);
    TEST_ASSERT_NULL(batch.responses[1].body);
    gchar *expected_start = g_strdup_printf("<3>%s: ", closed_url);
    TEST_ASSERT_TRUE_MESSAGE(g_str_has_prefix(captured, expected_start), captured);
    TEST_ASSERT_EQUAL_PTR(captured + strlen(captured) - 1, strchr(captured, '\n')); // logged once
    http_batch_free(&batch);
    g_free(expected_start);
    g_free(closed_url);
    g_free(a_url);
}

static void test_batch_waits_for_silent_servers_once(void) {
    int first_port = 0;
    int second_port = 0;
    const int first_listener = fake_listen(&first_port); // never accepts, so nothing ever answers
    const int second_listener = fake_listen(&second_port);
    gchar *first_url = g_strdup_printf("http://127.0.0.1:%d/x", first_port);
    gchar *a_url = g_strconcat(server.base, "/a", nullptr);
    gchar *second_url = g_strdup_printf("http://127.0.0.1:%d/x", second_port);
    const struct http_request requests[] = {
            {.url = first_url}, {.url = a_url}, {.url = second_url}};
    const int64_t start_ms = monotonic_ms();
    struct http_batch batch = http_get_batch(requests, G_N_ELEMENTS(requests));
    const int64_t elapsed_ms = monotonic_ms() - start_ms;
    TEST_ASSERT_GREATER_OR_EQUAL_INT64(TOTAL_TIMEOUT_MS - 500, elapsed_ms);
    TEST_ASSERT_LESS_THAN_INT64(TOTAL_TIMEOUT_MS + 2'000, elapsed_ms);
    TEST_ASSERT_FALSE(batch.responses[0].ok);
    TEST_ASSERT_TRUE(batch.responses[1].ok);
    TEST_ASSERT_EQUAL_INT64(200, batch.responses[1].status);
    TEST_ASSERT_FALSE(batch.responses[2].ok);
    http_batch_free(&batch);
    g_free(second_url);
    g_free(a_url);
    g_free(first_url);
    TEST_ASSERT_EQUAL_INT(0, close(second_listener));
    TEST_ASSERT_EQUAL_INT(0, close(first_listener));
}

static void test_batch_gives_up_on_hanging_dns(void) {
    assert_gives_up_on_hanging_dns(LOOK_UP_HANGING_HOST_IN_A_BATCH);
}

static void test_batch_of_one_matches_get(void) {
    gchar *url = g_strconcat(server.base, "/api/pi/now", nullptr);
    struct http_response single = http_get(url, "pi-secret");
    const struct http_request request = {.url = url, .bearer = "pi-secret"};
    struct http_batch batch = http_get_batch(&request, 1);
    TEST_ASSERT_TRUE(single.ok);
    TEST_ASSERT_EQUAL_size_t(1, batch.count);
    TEST_ASSERT_TRUE(batch.responses[0].ok);
    TEST_ASSERT_EQUAL_INT64(single.status, batch.responses[0].status);
    TEST_ASSERT_EQUAL_size_t(single.length, batch.responses[0].length);
    TEST_ASSERT_EQUAL_STRING(single.body, batch.responses[0].body);
    TEST_ASSERT_EQUAL_STRING(fake_server_request(&server, 0)->str,
                             fake_server_request(&server, 1)->str);
    http_batch_free(&batch);
    http_response_free(&single);
    g_free(url);
}

static void test_http_ok_passes_200_and_logs_other_statuses(void) {
    const struct http_response answered_200 = {.ok = true, .status = 200};
    const struct http_response answered_503 = {.ok = true, .status = 503};
    const struct http_response unanswered = {};
    char captured[512];
    start_capture();
    const bool passed = http_ok("https://example.test/a", &answered_200);
    const bool refused = http_ok("https://example.test/b", &answered_503);
    const bool missing = http_ok("https://example.test/c", &unanswered);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_TRUE(passed);
    TEST_ASSERT_FALSE(refused);
    TEST_ASSERT_FALSE(missing);
    TEST_ASSERT_EQUAL_STRING("<3>https://example.test/b answered 503\n", captured);
}

int main(int argc, char *argv[]) {
    if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) { // the program's main does this too
        return EXIT_FAILURE;
    }
    if (argc == 2 && strcmp(argv[1], LOOK_UP_HANGING_HOST) == 0) {
        return look_up_hanging_host();
    }
    if (argc == 2 && strcmp(argv[1], LOOK_UP_HANGING_HOST_IN_A_BATCH) == 0) {
        return look_up_hanging_host_in_a_batch();
    }
    test_program = argv[0];
    UNITY_BEGIN();
    RUN_TEST(test_get_returns_status_and_body);
    RUN_TEST(test_get_sends_bearer);
    RUN_TEST(test_error_status_is_a_response);
    RUN_TEST(test_get_ok_passes_200_and_logs_other_statuses_once);
    RUN_TEST(test_put_multipart_sends_parts);
    RUN_TEST(test_requests_carry_no_time_header);
    RUN_TEST(test_get_refuses_a_bad_url);
    RUN_TEST(test_get_without_server_is_not_ok);
    RUN_TEST(test_get_caps_the_body_at_one_mebibyte);
    RUN_TEST(test_get_caps_a_chunked_body_at_one_mebibyte);
    RUN_TEST(test_get_gives_up_on_silent_server);
    RUN_TEST(test_get_gives_up_on_hanging_dns);
    RUN_TEST(test_two_threads_fetch_at_once);
    RUN_TEST(test_batch_returns_each_answer_in_order);
    RUN_TEST(test_batch_sends_bearer_only_where_given);
    RUN_TEST(test_batch_failure_stays_with_its_request);
    RUN_TEST(test_batch_waits_for_silent_servers_once);
    RUN_TEST(test_batch_gives_up_on_hanging_dns);
    RUN_TEST(test_batch_of_one_matches_get);
    RUN_TEST(test_http_ok_passes_200_and_logs_other_statuses);
    const int failures = UNITY_END();
    curl_global_cleanup();
    return failures;
}
