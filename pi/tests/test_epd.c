#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "draw/frame.h"
#include "screen/epd.h"
#include "screen/screen.h"
#include "unity.h"

static constexpr size_t MAX_BYTES = 2 * FRAME_BYTES + 512; // one whole update
static constexpr int POWER_ON_SETTLE_MS = 10;
static constexpr int BUSY_LAG_MS = 10;

static constexpr uint8_t SOFTWARE_RESET = 0x12;
static constexpr uint8_t MASTER_ACTIVATION = 0x20;
static constexpr uint8_t WRITE_NEXT_FRAME = 0x24;
static constexpr uint8_t WRITE_GLASS_FRAME = 0x26;
static constexpr uint8_t DEEP_SLEEP = 0x10;

static struct frame glass_frame;
static struct frame next_frame;

static uint8_t sent_bytes[MAX_BYTES];
static uint8_t sent_dc[MAX_BYTES];
static size_t sent_count;

static uint8_t expected_bytes[MAX_BYTES];
static uint8_t expected_dc[MAX_BYTES];
static size_t expected_count;

void setUp(void) {
    for (size_t i = 0; i < FRAME_BYTES; i++) {
        glass_frame.bits[i] = (uint8_t)(i * 37 + i / 100);
        next_frame.bits[i] = (uint8_t)(i * 11 + 0x5A);
    }
    sent_count = 0;
    expected_count = 0;
}

void tearDown(void) {
}

static void flatten(const struct epd_steps *steps) {
    TEST_ASSERT_LESS_OR_EQUAL_size_t(EPD_STEPS_MAX, steps->count);
    for (size_t i = 0; i < steps->count; i++) {
        const struct epd_step *step = &steps->steps[i];
        if (step->kind == EPD_COMMAND) {
            TEST_ASSERT_LESS_THAN_size_t(MAX_BYTES, sent_count);
            sent_bytes[sent_count] = step->byte;
            sent_dc[sent_count] = 0;
            sent_count++;
        } else if (step->kind == EPD_DATA) {
            TEST_ASSERT_NOT_NULL(step->data);
            TEST_ASSERT_GREATER_THAN_size_t(0, step->length);
            TEST_ASSERT_LESS_OR_EQUAL_size_t(MAX_BYTES - sent_count, step->length);
            memcpy(sent_bytes + sent_count, step->data, step->length);
            memset(sent_dc + sent_count, 1, step->length);
            sent_count += step->length;
        }
    }
}

static void expect_bytes(const uint8_t *command_then_data, size_t length) {
    TEST_ASSERT_LESS_OR_EQUAL_size_t(MAX_BYTES - expected_count, length);
    for (size_t i = 0; i < length; i++) {
        expected_bytes[expected_count] = command_then_data[i];
        expected_dc[expected_count] = i == 0 ? 0 : 1;
        expected_count++;
    }
}

#define EXPECT(...) \
    expect_bytes((const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}))

static void expect_plane(uint8_t command, const struct frame *frame) {
    EXPECT(command);
    memcpy(expected_bytes + expected_count, frame->bits, FRAME_BYTES);
    memset(expected_dc + expected_count, 1, FRAME_BYTES);
    expected_count += FRAME_BYTES;
}

static void expect_update(const struct frame *on_glass, const struct frame *next, bool full) {
    expected_count = 0;
    EXPECT(0x12);
    EXPECT(0x18, 0x80);
    EXPECT(0x0C, 0xAE, 0xC7, 0xC3, 0xC0, 0x80);
    EXPECT(0x01, 0xDF, 0x01, 0x02);
    EXPECT(0x3C, full ? 0x01 : 0x80);
    EXPECT(0x11, 0x01);
    EXPECT(0x44, 0x00, 0x00, 0x1F, 0x03);
    EXPECT(0x45, 0xDF, 0x01, 0x00, 0x00);
    EXPECT(0x4E, 0x00, 0x00);
    EXPECT(0x4F, 0xDF, 0x01);
    expect_plane(0x26, on_glass);
    expect_plane(0x24, next);
    EXPECT(0x21, full ? 0x40 : 0x00, 0x00);
    EXPECT(0x22, full ? 0xF7 : 0xFF);
    EXPECT(0x20);
    EXPECT(0x10, 0x03);
}

static void assert_sent_expected(void) {
    const size_t common = sent_count < expected_count ? sent_count : expected_count;
    for (size_t i = 0; i < common; i++) {
        if (sent_bytes[i] != expected_bytes[i] || sent_dc[i] != expected_dc[i]) {
            char message[128];
            snprintf(message, sizeof message,
                     "byte %zu: sent 0x%02X with DC %u, expected 0x%02X with DC %u", i,
                     (unsigned)sent_bytes[i], (unsigned)sent_dc[i], (unsigned)expected_bytes[i],
                     (unsigned)expected_dc[i]);
            TEST_FAIL_MESSAGE(message);
        }
    }
    TEST_ASSERT_EQUAL_size_t(expected_count, sent_count);
}

static size_t find_command(const struct epd_steps *steps, uint8_t command) {
    for (size_t i = 0; i < steps->count; i++) {
        if (steps->steps[i].kind == EPD_COMMAND && steps->steps[i].byte == command) {
            return i;
        }
    }
    TEST_FAIL_MESSAGE("the command isn't in the steps");
    return 0;
}

static int count_kind(const struct epd_steps *steps, enum epd_step_kind kind) {
    int count = 0;
    for (size_t i = 0; i < steps->count; i++) {
        count += steps->steps[i].kind == kind;
    }
    return count;
}

static void assert_wakes_then_resets(const struct epd_steps *steps) {
    TEST_ASSERT_EQUAL_INT(EPD_PAUSE, steps->steps[0].kind);
    TEST_ASSERT_EQUAL_INT(POWER_ON_SETTLE_MS, steps->steps[0].milliseconds);
    TEST_ASSERT_EQUAL_INT(EPD_RESET, steps->steps[1].kind);
    TEST_ASSERT_EQUAL_INT(EPD_WAIT_IDLE, steps->steps[2].kind);
    TEST_ASSERT_EQUAL_INT(1, count_kind(steps, EPD_RESET));
}

static void assert_refresh_shape(const struct epd_steps *steps, const struct frame *on_glass,
                                 const struct frame *next) {
    assert_wakes_then_resets(steps);
    TEST_ASSERT_EQUAL_size_t(3, find_command(steps, SOFTWARE_RESET));
    TEST_ASSERT_EQUAL_INT(EPD_WAIT_IDLE, steps->steps[4].kind);
    const struct epd_step *glass = &steps->steps[find_command(steps, WRITE_GLASS_FRAME) + 1];
    TEST_ASSERT_EQUAL_INT(EPD_DATA, glass->kind);
    TEST_ASSERT_EQUAL_PTR(on_glass->bits, glass->data);
    TEST_ASSERT_EQUAL_size_t(FRAME_BYTES, glass->length);
    const struct epd_step *coming = &steps->steps[find_command(steps, WRITE_NEXT_FRAME) + 1];
    TEST_ASSERT_EQUAL_INT(EPD_DATA, coming->kind);
    TEST_ASSERT_EQUAL_PTR(next->bits, coming->data);
    TEST_ASSERT_EQUAL_size_t(FRAME_BYTES, coming->length);
    TEST_ASSERT_EQUAL_INT(EPD_WAIT_IDLE,
                          steps->steps[find_command(steps, MASTER_ACTIVATION) + 1].kind);
    TEST_ASSERT_EQUAL_INT(3, count_kind(steps, EPD_WAIT_IDLE));
}

static void assert_ends_in_deep_sleep(const struct epd_steps *steps) {
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(2, steps->count);
    const struct epd_step *command = &steps->steps[steps->count - 2];
    const struct epd_step *mode = &steps->steps[steps->count - 1];
    TEST_ASSERT_EQUAL_INT(EPD_COMMAND, command->kind);
    TEST_ASSERT_EQUAL_HEX8(DEEP_SLEEP, command->byte);
    TEST_ASSERT_EQUAL_INT(EPD_DATA, mode->kind);
    TEST_ASSERT_EQUAL_size_t(1, mode->length);
    TEST_ASSERT_EQUAL_HEX8(0x03, mode->data[0]);
}

static void test_full_refresh_steps(void) {
    const struct epd_steps steps = epd_refresh_steps(&glass_frame, &next_frame, REFRESH_FULL);
    assert_refresh_shape(&steps, &glass_frame, &next_frame);
    flatten(&steps);
    expect_update(&glass_frame, &next_frame, true);
    assert_sent_expected();
}

static void test_partial_refresh_uses_0xFF(void) {
    const struct epd_steps steps = epd_refresh_steps(&glass_frame, &next_frame, REFRESH_PARTIAL);
    assert_refresh_shape(&steps, &glass_frame, &next_frame);
    flatten(&steps);
    expect_update(&glass_frame, &next_frame, false);
    assert_sent_expected();
}

static void test_every_refresh_ends_in_deep_sleep(void) {
    const struct epd_steps partial = epd_refresh_steps(&glass_frame, &next_frame, REFRESH_PARTIAL);
    assert_ends_in_deep_sleep(&partial);
    TEST_ASSERT_EQUAL_INT(EPD_WAIT_IDLE, partial.steps[partial.count - 3].kind);
    const struct epd_steps full = epd_refresh_steps(&glass_frame, &next_frame, REFRESH_FULL);
    assert_ends_in_deep_sleep(&full);
    TEST_ASSERT_EQUAL_INT(EPD_WAIT_IDLE, full.steps[full.count - 3].kind);
}

static void test_sleep_steps_for_detection(void) {
    const struct epd_steps steps = epd_sleep_steps();
    TEST_ASSERT_EQUAL_size_t(6, steps.count);
    assert_wakes_then_resets(&steps);
    TEST_ASSERT_EQUAL_INT(EPD_COMMAND, steps.steps[3].kind);
    TEST_ASSERT_EQUAL_HEX8(DEEP_SLEEP, steps.steps[3].byte);
    TEST_ASSERT_EQUAL_INT(EPD_DATA, steps.steps[4].kind);
    TEST_ASSERT_EQUAL_INT(EPD_PAUSE, steps.steps[5].kind); // BUSY can lag the command
    TEST_ASSERT_EQUAL_INT(BUSY_LAG_MS, steps.steps[5].milliseconds);
    flatten(&steps);
    EXPECT(0x10, 0x03);
    assert_sent_expected();
}

static void exit_with_signal(int signal_number) {
    _exit(128 + signal_number);
}

static void test_no_refresh_aborts(void) {
    fflush(stdout);
    const pid_t child = fork();
    TEST_ASSERT_NOT_EQUAL_INT(-1, child);
    if (child == 0) {
        signal(SIGABRT, exit_with_signal); // a caught abort skips macOS's crash reporter
        signal(SIGTRAP, exit_with_signal);
        (void)epd_refresh_steps(&glass_frame, &next_frame, REFRESH_NONE);
        _exit(0);
    }
    int status = 0;
    TEST_ASSERT_EQUAL_INT(child, waitpid(child, &status, 0));
    TEST_ASSERT_TRUE(WIFEXITED(status));
    TEST_ASSERT_EQUAL_INT(128 + SIGABRT, WEXITSTATUS(status));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_full_refresh_steps);
    RUN_TEST(test_partial_refresh_uses_0xFF);
    RUN_TEST(test_every_refresh_ends_in_deep_sleep);
    RUN_TEST(test_sleep_steps_for_detection);
    RUN_TEST(test_no_refresh_aborts);
    return UNITY_END();
}
