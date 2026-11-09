#include "draw/frame.h"

#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cairo.h>

#include "config.h"
#include "shared/log.h"

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
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
        log_error("pixel (%d, %d) is outside the frame", x, y);
        abort();
    }
    return (frame->bits[y * ROW_BYTES + x / 8] & (0x80 >> (x % 8))) == 0;
}

static cairo_status_t append_png(void *closure, const unsigned char *data, unsigned int length) {
    struct png *png = closure;
    size_t grown_length;
    if (ckd_add(&grown_length, png->length, length)) {
        return CAIRO_STATUS_NO_MEMORY;
    }
    uint8_t *grown = realloc(png->data, grown_length);
    if (grown == nullptr) {
        return CAIRO_STATUS_NO_MEMORY;
    }
    memcpy(grown + png->length, data, length);
    png->data = grown;
    png->length = grown_length;
    return CAIRO_STATUS_SUCCESS;
}

struct png frame_encode_png(const struct frame *frame) {
    /* make a grey surface */
    cairo_surface_t *surface =
            cairo_image_surface_create(CAIRO_FORMAT_A8, SCREEN_WIDTH, SCREEN_HEIGHT);
    cairo_status_t status = cairo_surface_status(surface);
    if (status != CAIRO_STATUS_SUCCESS) {
        log_error("can't create a surface for the frame: %s", cairo_status_to_string(status));
        cairo_surface_destroy(surface);
        return (struct png){};
    }

    /* paint each pixel */
    cairo_surface_flush(surface);
    uint8_t *pixels = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            pixels[y * stride + x] = frame_is_black(frame, x, y) ? 0 : 255;
        }
    }
    cairo_surface_mark_dirty(surface);

    /* encode it as PNG */
    struct png png = {};
    status = cairo_surface_write_to_png_stream(surface, append_png, &png);
    cairo_surface_destroy(surface);
    if (status != CAIRO_STATUS_SUCCESS) {
        log_error("can't encode the frame as PNG: %s", cairo_status_to_string(status));
        free(png.data);
        return (struct png){};
    }
    png.ok = true;
    return png;
}
