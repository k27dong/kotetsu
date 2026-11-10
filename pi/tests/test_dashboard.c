#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>

#include "shared/dashboard.h"
#include "shared/http.h"
#include "stderr_capture.h"
#include "unity.h"

#define URL "http://127.0.0.1:8787/api/pi/now"

static char captured[256];

void setUp(void) {
}

void tearDown(void) {
}

static enum dashboard_status log_answer(long status, const char *body) {
    char copy[128];
    snprintf(copy, sizeof copy, "%s", body);
    const struct http_response response = {
            .ok = true, .status = status, .body = copy, .length = strlen(copy)};
    start_capture();
    const enum dashboard_status problem = dashboard_log_answer(URL, &response);
    finish_capture(captured, sizeof captured);
    return problem;
}

static void test_answer_is_logged_once_with_its_error_field(void) {
    TEST_ASSERT_EQUAL_INT(DASHBOARD_UNAUTHORIZED, log_answer(401, "{\"error\":\"bad key\"}"));
    TEST_ASSERT_EQUAL_STRING("<3>" URL " answered 401: bad key\n", captured);

    // no JSON, no error field and an error that isn't text each log the status alone
    TEST_ASSERT_EQUAL_INT(DASHBOARD_UNREACHABLE, log_answer(502, "<html>Bad Gateway</html>"));
    TEST_ASSERT_EQUAL_STRING("<3>" URL " answered 502\n", captured);
    TEST_ASSERT_EQUAL_INT(DASHBOARD_BAD_RESPONSE, log_answer(404, "{}"));
    TEST_ASSERT_EQUAL_STRING("<3>" URL " answered 404\n", captured);
    TEST_ASSERT_EQUAL_INT(DASHBOARD_BAD_RESPONSE, log_answer(400, "{\"error\":7}"));
    TEST_ASSERT_EQUAL_STRING("<3>" URL " answered 400\n", captured);
    TEST_ASSERT_EQUAL_INT(DASHBOARD_BAD_RESPONSE, log_answer(200, ""));
    TEST_ASSERT_EQUAL_STRING("<3>" URL " answered 200\n", captured);
}

static void test_status_names_the_problem(void) {
    static const struct {
        long status;
        enum dashboard_status expected;
    } answers[] = {
            {401, DASHBOARD_UNAUTHORIZED}, {403, DASHBOARD_UNAUTHORIZED},
            {500, DASHBOARD_UNREACHABLE},  {503, DASHBOARD_UNREACHABLE},
            {599, DASHBOARD_UNREACHABLE},  {200, DASHBOARD_BAD_RESPONSE},
            {400, DASHBOARD_BAD_RESPONSE}, {404, DASHBOARD_BAD_RESPONSE},
            {413, DASHBOARD_BAD_RESPONSE}, {499, DASHBOARD_BAD_RESPONSE},
    };

    for (size_t i = 0; i < G_N_ELEMENTS(answers); i++) {
        char message[32];
        snprintf(message, sizeof message, "%ld", answers[i].status);
        TEST_ASSERT_EQUAL_INT_MESSAGE(answers[i].expected,
                                      log_answer(answers[i].status, "{\"error\":\"x\"}"), message);
    }
}

static void test_problem_texts(void) {
    TEST_ASSERT_NULL(dashboard_problem(DASHBOARD_OK));
    TEST_ASSERT_EQUAL_STRING("dashboard unreachable", dashboard_problem(DASHBOARD_UNREACHABLE));
    TEST_ASSERT_EQUAL_STRING("dashboard unauthorized", dashboard_problem(DASHBOARD_UNAUTHORIZED));
    TEST_ASSERT_EQUAL_STRING("dashboard bad response", dashboard_problem(DASHBOARD_BAD_RESPONSE));
    TEST_ASSERT_EQUAL_STRING("no phrase available", dashboard_problem(DASHBOARD_NO_PHRASE));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_answer_is_logged_once_with_its_error_field);
    RUN_TEST(test_status_names_the_problem);
    RUN_TEST(test_problem_texts);
    return UNITY_END();
}
