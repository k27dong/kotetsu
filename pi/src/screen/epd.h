#pragma once

#include <stddef.h>
#include <stdint.h>

#include "draw/frame.h"
#include "screen/screen.h"

enum epd_step_kind {
    EPD_RESET,
    EPD_COMMAND,
    EPD_DATA,
    EPD_WAIT_IDLE,
    EPD_PAUSE,
};

struct epd_step {
    enum epd_step_kind kind;
    uint8_t byte;        // EPD_COMMAND
    const uint8_t *data; // EPD_DATA
    size_t length;
    int milliseconds; // EPD_PAUSE
};

static constexpr size_t EPD_STEPS_MAX = 64;

// a panel sequence, for the board to send
struct epd_steps {
    size_t count;
    struct epd_step steps[EPD_STEPS_MAX];
};

struct epd_steps epd_refresh_steps(const struct frame *on_glass, const struct frame *next,
                                   enum refresh_kind kind);
struct epd_steps epd_sleep_steps(void);
