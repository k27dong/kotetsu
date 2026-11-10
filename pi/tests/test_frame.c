#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cairo.h>
#include <glib.h>

#include "config.h"
#include "draw/frame.h"
#include "unity.h"

static constexpr int COVERAGE_STRIDE = SCREEN_WIDTH + 32;

void setUp(void) {
}

void tearDown(void) {
}

static uint8_t *blank_coverage(void) {
    uint8_t *coverage = calloc((size_t)(COVERAGE_STRIDE * SCREEN_HEIGHT), 1);
    TEST_ASSERT_NOT_NULL(coverage);
    return coverage;
}

static void fill_pattern(struct frame *frame) {
    for (size_t i = 0; i < FRAME_BYTES; i++) {
        frame->bits[i] = (uint8_t)(i * 37 + i / 100 * 11); // as in fixtures/frame_pattern.png
    }
}

static void assert_image_matches(cairo_surface_t *image, const struct frame *frame) {
    TEST_ASSERT_EQUAL_INT(CAIRO_STATUS_SUCCESS, cairo_surface_status(image));
    TEST_ASSERT_EQUAL_INT(CAIRO_FORMAT_RGB24, cairo_image_surface_get_format(image));
    TEST_ASSERT_EQUAL_INT(SCREEN_WIDTH, cairo_image_surface_get_width(image));
    TEST_ASSERT_EQUAL_INT(SCREEN_HEIGHT, cairo_image_surface_get_height(image));
    const uint8_t *data = cairo_image_surface_get_data(image);
    const int stride = cairo_image_surface_get_stride(image);
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        const uint32_t *row = (const uint32_t *)(data + y * stride);
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            const uint32_t expected = frame_is_black(frame, x, y) ? 0x000000 : 0xFFFFFF;
            if ((row[x] & 0xFFFFFF) != expected) {
                char message[64];
                snprintf(message, sizeof message, "pixel (%d, %d) is %06x", x, y,
                         row[x] & 0xFFFFFF);
                TEST_FAIL_MESSAGE(message);
            }
        }
    }
}

struct png_reader {
    const uint8_t *bytes;
    size_t length;
    size_t offset;
};

static cairo_status_t read_png(void *closure, unsigned char *data, unsigned int length) {
    struct png_reader *reader = closure;
    if (length > reader->length - reader->offset) {
        return CAIRO_STATUS_READ_ERROR;
    }
    memcpy(data, reader->bytes + reader->offset, length);
    reader->offset += length;
    return CAIRO_STATUS_SUCCESS;
}

static void test_cut_at_half_coverage(void) {
    uint8_t *coverage = blank_coverage();
    coverage[5 * COVERAGE_STRIDE + 10] = 127;
    coverage[5 * COVERAGE_STRIDE + 11] = 128;
    coverage[5 * COVERAGE_STRIDE + 12] = 255;
    coverage[5 * COVERAGE_STRIDE + SCREEN_WIDTH] = 255;
    struct frame frame;
    frame_from_a8(coverage, COVERAGE_STRIDE, &frame);
    TEST_ASSERT_FALSE(frame_is_black(&frame, 10, 5));
    TEST_ASSERT_TRUE(frame_is_black(&frame, 11, 5));
    TEST_ASSERT_TRUE(frame_is_black(&frame, 12, 5));
    TEST_ASSERT_FALSE(frame_is_black(&frame, 13, 5));
    TEST_ASSERT_FALSE(frame_is_black(&frame, 0, 6));
    free(coverage);
}

static void test_bit_order(void) {
    uint8_t *coverage = blank_coverage();
    coverage[0] = 255;
    struct frame frame;
    frame_from_a8(coverage, COVERAGE_STRIDE, &frame);
    TEST_ASSERT_EQUAL_HEX8(0x7F, frame.bits[0]);
    for (size_t i = 1; i < FRAME_BYTES; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, frame.bits[i]);
    }
    coverage[0] = 0;
    coverage[COVERAGE_STRIDE + 15] = 255;
    coverage[(SCREEN_HEIGHT - 1) * COVERAGE_STRIDE + SCREEN_WIDTH - 1] = 255;
    frame_from_a8(coverage, COVERAGE_STRIDE, &frame);
    TEST_ASSERT_EQUAL_HEX8(0xFF, frame.bits[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFE, frame.bits[101]);
    TEST_ASSERT_EQUAL_HEX8(0xFE, frame.bits[FRAME_BYTES - 1]);
    TEST_ASSERT_TRUE(frame_is_black(&frame, 15, 1));
    TEST_ASSERT_TRUE(frame_is_black(&frame, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1));
    free(coverage);
}

static void test_white_and_equal(void) {
    struct frame white;
    frame_fill_white(&white);
    for (size_t i = 0; i < FRAME_BYTES; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, white.bits[i]);
    }
    struct frame other = white;
    TEST_ASSERT_TRUE(frame_equal(&white, &other));
    other.bits[FRAME_BYTES - 1] = 0xFE;
    TEST_ASSERT_FALSE(frame_equal(&white, &other));
}

static void test_png_round_trip(void) {
    static struct frame frame;
    fill_pattern(&frame);
    struct png png = frame_encode_png(&frame);
    TEST_ASSERT_TRUE(png.ok);
    TEST_ASSERT_NOT_NULL(png.data);
    struct png_reader reader = {.bytes = png.data, .length = png.length};
    cairo_surface_t *image = cairo_image_surface_create_from_png_stream(read_png, &reader);
    assert_image_matches(image, &frame);
    cairo_surface_destroy(image);
    free(png.data);
}

static void test_png_bytes_match_old_encoder(void) {
    static struct frame frame;
    fill_pattern(&frame);
    gchar *expected = nullptr;
    gsize expected_length = 0;
    TEST_ASSERT_TRUE(g_file_get_contents("fixtures/frame_pattern.png", &expected, &expected_length,
                                         nullptr));
    struct png png = frame_encode_png(&frame);
    TEST_ASSERT_TRUE(png.ok);
    TEST_ASSERT_EQUAL_size_t(expected_length, png.length);
    TEST_ASSERT_EQUAL_MEMORY(expected, png.data, expected_length);
    free(png.data);
    g_free(expected);
}

static void exit_with_signal(int signal_number) {
    _exit(128 + signal_number);
}

static int exit_code_of_pixel_query(int x, int y) {
    fflush(stdout);
    const pid_t child = fork();
    TEST_ASSERT_NOT_EQUAL_INT(-1, child);
    if (child == 0) {
        signal(SIGABRT, exit_with_signal); // a caught abort skips macOS's crash reporter
        static struct frame frame;
        frame_fill_white(&frame);
        _exit(frame_is_black(&frame, x, y) ? 1 : 0);
    }
    int status = 0;
    TEST_ASSERT_EQUAL_INT(child, waitpid(child, &status, 0));
    TEST_ASSERT_TRUE(WIFEXITED(status));
    return WEXITSTATUS(status);
}

static void test_pixel_outside_frame_aborts(void) {
    TEST_ASSERT_EQUAL_INT(0, exit_code_of_pixel_query(SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1));
    TEST_ASSERT_EQUAL_INT(128 + SIGABRT, exit_code_of_pixel_query(SCREEN_WIDTH, 0));
    TEST_ASSERT_EQUAL_INT(128 + SIGABRT, exit_code_of_pixel_query(-1, 0));
    TEST_ASSERT_EQUAL_INT(128 + SIGABRT, exit_code_of_pixel_query(0, SCREEN_HEIGHT));
    TEST_ASSERT_EQUAL_INT(128 + SIGABRT, exit_code_of_pixel_query(0, -1));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_cut_at_half_coverage);
    RUN_TEST(test_bit_order);
    RUN_TEST(test_white_and_equal);
    RUN_TEST(test_png_round_trip);
    RUN_TEST(test_png_bytes_match_old_encoder);
    RUN_TEST(test_pixel_outside_frame_aborts);
    return UNITY_END();
}
