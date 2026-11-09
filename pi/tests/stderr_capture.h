#pragma once

#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>

#include "unity.h"

static int saved_stderr = -1;
static int capture_read_end = -1;

[[maybe_unused]] static void start_capture(void) {
    int ends[2];
    TEST_ASSERT_EQUAL_INT(0, pipe(ends));
    saved_stderr = dup(STDERR_FILENO);
    TEST_ASSERT_NOT_EQUAL_INT(-1, saved_stderr);
    TEST_ASSERT_NOT_EQUAL_INT(-1, dup2(ends[1], STDERR_FILENO));
    TEST_ASSERT_EQUAL_INT(0, close(ends[1]));
    capture_read_end = ends[0];
}

[[maybe_unused]] static void finish_capture(char *out, size_t size) {
    TEST_ASSERT_NOT_EQUAL_INT(-1, dup2(saved_stderr, STDERR_FILENO));
    TEST_ASSERT_EQUAL_INT(0, close(saved_stderr));
    size_t length = 0;
    ssize_t count;
    while ((count = read(capture_read_end, out + length, size - 1 - length)) > 0) {
        length += (size_t)count;
    }
    TEST_ASSERT_EQUAL_INT(0, count);
    out[length] = '\0';
    TEST_ASSERT_EQUAL_INT(0, close(capture_read_end));
}
