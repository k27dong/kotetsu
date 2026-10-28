#include "render/frame.h"

#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cairo.h>

#include "config.h"
#include "log.h"

static constexpr int ROW_BYTES = SCREEN_WIDTH / 8;
static constexpr uint8_t HALF_COVERAGE = 128;

static_assert(FRAME_BYTES == (size_t)(ROW_BYTES * SCREEN_HEIGHT));

void frame_fill_white(struct frame *frame) {
    memset(frame->bits, 0xFF, sizeof frame->bits);
}

void frame_from_a8(const uint8_t *coverage, int stride, struct frame *out) {
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        const uint8_t *row = coverage + (ptrdiff_t)y * stride;
        for (int column = 0; column < ROW_BYTES; column++) {
            uint8_t bits = 0;
            for (int x = column * 8; x < column * 8 + 8; x++) {
                bits = (uint8_t)((bits << 1) | (row[x] < HALF_COVERAGE));
            }
            out->bits[y * ROW_BYTES + column] = bits;
        }
    }
}

bool frame_equal(const struct frame *a, const struct frame *b) {
    return memcmp(a->bits, b->bits, sizeof a->bits) == 0;
}

bool frame_is_black(const struct frame *frame, int x, int y) {
    return (frame->bits[y * ROW_BYTES + x / 8] & (0x80 >> (x % 8))) == 0;
}

static cairo_surface_t *surface_for(const struct frame *frame) {
    cairo_surface_t *surface =
            cairo_image_surface_create(CAIRO_FORMAT_A8, SCREEN_WIDTH, SCREEN_HEIGHT);
    const cairo_status_t status = cairo_surface_status(surface);
    if (status != CAIRO_STATUS_SUCCESS) {
        log_error("can't create a surface for the frame: %s", cairo_status_to_string(status));
        cairo_surface_destroy(surface);
        return nullptr;
    }
    cairo_surface_flush(surface);
    uint8_t *pixels = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            const bool black = frame_is_black(frame, x, y);
            pixels[y * stride + x] = black ? 0 : 255; // Cairo saves A8 as grey: 255 is white
        }
    }
    cairo_surface_mark_dirty(surface);
    return surface;
}

struct png_buffer {
    uint8_t *bytes;
    size_t length;
};

static cairo_status_t append_png(void *closure, const unsigned char *data, unsigned int length) {
    struct png_buffer *buffer = closure;
    size_t grown_length;
    if (ckd_add(&grown_length, buffer->length, length)) {
        return CAIRO_STATUS_NO_MEMORY;
    }
    uint8_t *grown = realloc(buffer->bytes, grown_length);
    if (grown == nullptr) {
        return CAIRO_STATUS_NO_MEMORY;
    }
    memcpy(grown + buffer->length, data, length);
    buffer->bytes = grown;
    buffer->length = grown_length;
    return CAIRO_STATUS_SUCCESS;
}

bool frame_encode_png(const struct frame *frame, uint8_t **png, size_t *length) {
    cairo_surface_t *surface = surface_for(frame);
    if (surface == nullptr) {
        return false;
    }
    struct png_buffer buffer = {};
    const cairo_status_t status = cairo_surface_write_to_png_stream(surface, append_png, &buffer);
    cairo_surface_destroy(surface);
    if (status != CAIRO_STATUS_SUCCESS) {
        log_error("can't encode the frame as PNG: %s", cairo_status_to_string(status));
        free(buffer.bytes);
        return false;
    }
    *png = buffer.bytes;
    *length = buffer.length;
    return true;
}

bool frame_write_png(const struct frame *frame, const char *path) {
    cairo_surface_t *surface = surface_for(frame);
    if (surface == nullptr) {
        return false;
    }
    const cairo_status_t status = cairo_surface_write_to_png(surface, path);
    cairo_surface_destroy(surface);
    if (status != CAIRO_STATUS_SUCCESS) {
        log_error("can't write %s: %s", path, cairo_status_to_string(status));
        return false;
    }
    return true;
}
