#pragma once

#include <stddef.h>
#include <stdint.h>

static constexpr size_t FRAME_BYTES = 48'000;

struct frame {
    uint8_t bits[FRAME_BYTES]; // rows of 800 pixels, the leftmost in the high bit; 1 is white
};

struct png {
    bool ok;
    uint8_t *data; // freed with free()
    size_t length;
};

void frame_fill_white(struct frame *frame);
void frame_from_a8(const uint8_t *coverage, int stride, struct frame *out);
bool frame_equal(const struct frame *a, const struct frame *b);
bool frame_is_black(const struct frame *frame, int x, int y);
struct png frame_encode_png(const struct frame *frame);
