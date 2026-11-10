#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "shared/log.h"
#include "stderr_capture.h"
#include "unity.h"

static constexpr int THREADS = 8;
static constexpr int LINES_PER_THREAD = 200;

#define FILLER "the quick brown fox jumps over the lazy dog, then does it again"

void setUp(void) {
}

void tearDown(void) {
}

static void test_error_line_has_priority_3(void) {
    char captured[64];
    start_capture();
    log_error("dashboard %s", "unreachable");
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_EQUAL_STRING("<3>dashboard unreachable\n", captured);
}

static void test_warning_line_has_priority_4(void) {
    char captured[64];
    start_capture();
    log_warning("x %d", 1);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_EQUAL_STRING("<4>x 1\n", captured);
}

static void test_info_line_has_priority_6(void) {
    char captured[64];
    start_capture();
    log_info("x %d", 1);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_EQUAL_STRING("<6>x 1\n", captured);
}

static void test_long_line_is_cut_but_ends_the_line(void) {
    char *message = g_strnfill(10'000, 'x');
    char captured[16'384];
    start_capture();
    log_info("%s", message);
    log_info("next");
    finish_capture(captured, sizeof captured);
    gchar **lines = g_strsplit(captured, "\n", -1);
    TEST_ASSERT_EQUAL_UINT(3, g_strv_length(lines));
    TEST_ASSERT_TRUE(g_str_has_prefix(lines[0], "<6>xxxx"));
    TEST_ASSERT_LESS_THAN_size_t(10'000, strlen(lines[0]));
    TEST_ASSERT_EQUAL_STRING("<6>next", lines[1]);
    TEST_ASSERT_EQUAL_STRING("", lines[2]);
    g_strfreev(lines);
    g_free(message);
}

static void *write_lines(void *argument) {
    const int thread = *(const int *)argument;
    for (int line = 0; line < LINES_PER_THREAD; line++) {
        log_info("thread %d line %d " FILLER, thread, line);
    }
    return nullptr;
}

static void test_lines_from_threads_never_mix(void) {
    gchar *path = nullptr;
    const int file = g_file_open_tmp("kotetsu-log-XXXXXX", &path, nullptr); // a pipe would fill up
    TEST_ASSERT_NOT_EQUAL_INT(-1, file);
    TEST_ASSERT_EQUAL_INT(0, fcntl(file, F_SETFL, O_APPEND));
    const int original_stderr = dup(STDERR_FILENO);
    TEST_ASSERT_NOT_EQUAL_INT(-1, dup2(file, STDERR_FILENO));
    pthread_t threads[THREADS];
    int numbers[THREADS];
    for (int i = 0; i < THREADS; i++) {
        numbers[i] = i;
        TEST_ASSERT_EQUAL_INT(0, pthread_create(&threads[i], nullptr, write_lines, &numbers[i]));
    }
    for (int i = 0; i < THREADS; i++) {
        TEST_ASSERT_EQUAL_INT(0, pthread_join(threads[i], nullptr));
    }
    TEST_ASSERT_NOT_EQUAL_INT(-1, dup2(original_stderr, STDERR_FILENO));
    TEST_ASSERT_EQUAL_INT(0, close(original_stderr));
    TEST_ASSERT_EQUAL_INT(0, close(file));

    gchar *contents = nullptr;
    TEST_ASSERT_TRUE(g_file_get_contents(path, &contents, nullptr, nullptr));
    TEST_ASSERT_EQUAL_INT(0, g_remove(path));
    gchar **lines = g_strsplit(contents, "\n", -1);
    TEST_ASSERT_EQUAL_UINT(THREADS * LINES_PER_THREAD + 1, g_strv_length(lines));
    int next_line[THREADS] = {};
    for (int i = 0; i < THREADS * LINES_PER_THREAD; i++) {
        int thread = -1;
        int line = -1;
        int consumed = 0;
        const bool parsed =
                sscanf(lines[i], "<6>thread %d line %d %n", &thread, &line, &consumed) == 2 &&
                thread >= 0 && thread < THREADS;
        TEST_ASSERT_TRUE_MESSAGE(parsed, lines[i]);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(FILLER, lines[i] + consumed, lines[i]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(next_line[thread], line, lines[i]);
        next_line[thread]++;
    }
    TEST_ASSERT_EQUAL_STRING("", lines[THREADS * LINES_PER_THREAD]);
    g_strfreev(lines);
    g_free(contents);
    g_free(path);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_error_line_has_priority_3);
    RUN_TEST(test_warning_line_has_priority_4);
    RUN_TEST(test_info_line_has_priority_6);
    RUN_TEST(test_long_line_is_cut_but_ends_the_line);
    RUN_TEST(test_lines_from_threads_never_mix);
    return UNITY_END();
}
