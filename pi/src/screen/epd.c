#include "screen/epd.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "draw/frame.h"
#include "screen/screen.h"
#include "shared/log.h"

static constexpr uint8_t DRIVER_OUTPUT_CONTROL = 0x01;
static constexpr uint8_t BOOSTER_SOFT_START = 0x0C;
static constexpr uint8_t DEEP_SLEEP = 0x10;
static constexpr uint8_t DATA_ENTRY_MODE = 0x11;
static constexpr uint8_t SOFTWARE_RESET = 0x12;
static constexpr uint8_t TEMPERATURE_SENSOR = 0x18;
static constexpr uint8_t MASTER_ACTIVATION = 0x20;
static constexpr uint8_t DISPLAY_UPDATE_CONTROL_1 = 0x21;
static constexpr uint8_t DISPLAY_UPDATE_CONTROL_2 = 0x22;
static constexpr uint8_t WRITE_NEXT_FRAME = 0x24;
static constexpr uint8_t WRITE_GLASS_FRAME = 0x26;
static constexpr uint8_t BORDER_WAVEFORM = 0x3C;
static constexpr uint8_t RAM_X_RANGE = 0x44;
static constexpr uint8_t RAM_Y_RANGE = 0x45;
static constexpr uint8_t RAM_X_COUNTER = 0x4E;
static constexpr uint8_t RAM_Y_COUNTER = 0x4F;

static const uint8_t INTERNAL_SENSOR[] = {0x80};
static const uint8_t SOFT_START_PHASES[] = {0xAE, 0xC7, 0xC3, 0xC0, 0x80};
static const uint8_t GATES_480[] = {0xDF, 0x01, 0x02};
static const uint8_t BORDER_FULL[] = {0x01};
static const uint8_t BORDER_PARTIAL[] = {0x80};
static const uint8_t X_UP_Y_DOWN[] = {0x01};
static const uint8_t X_RANGE_800[] = {0x00, 0x00, 0x1F, 0x03};
static const uint8_t Y_RANGE_480_DOWN[] = {0xDF, 0x01, 0x00, 0x00};
static const uint8_t X_COUNTER_START[] = {0x00, 0x00};
static const uint8_t Y_COUNTER_START[] = {0xDF, 0x01};
static const uint8_t UPDATE_CONTROL_FULL[] = {0x40, 0x00};
static const uint8_t UPDATE_CONTROL_PARTIAL[] = {0x00, 0x00};
static const uint8_t UPDATE_SEQUENCE_FULL[] = {0xF7};
static const uint8_t UPDATE_SEQUENCE_PARTIAL[] = {0xFF};
static const uint8_t SLEEP_MODE_1[] = {0x03};

static constexpr int SETTLE_MS = 10;

static void add(struct epd_steps *steps, struct epd_step step) {
    if (steps->count == EPD_STEPS_MAX) {
        log_error("the panel sequence needs more than %zu steps", EPD_STEPS_MAX);
        abort();
    }
    steps->steps[steps->count++] = step;
}

static void add_command(struct epd_steps *steps, uint8_t code, const uint8_t *data, size_t length) {
    add(steps, (struct epd_step){.kind = EPD_COMMAND, .byte = code});
    if (length > 0) {
        add(steps, (struct epd_step){.kind = EPD_DATA, .data = data, .length = length});
    }
}

static void add_reset(struct epd_steps *steps) {
    add(steps, (struct epd_step){.kind = EPD_PAUSE, .milliseconds = SETTLE_MS});
    add(steps, (struct epd_step){.kind = EPD_RESET});
    add(steps, (struct epd_step){.kind = EPD_WAIT_IDLE});
}

struct epd_steps epd_refresh_steps(const struct frame *on_glass, const struct frame *next,
                                   enum refresh_kind kind) {
    if (kind != REFRESH_PARTIAL && kind != REFRESH_FULL) {
        log_error("the panel can't run refresh kind %d", (int)kind);
        abort();
    }
    const bool full = kind == REFRESH_FULL;
    struct epd_steps steps = {};

    /* wake the controller with a hardware reset, then a software reset */
    add_reset(&steps);
    add_command(&steps, SOFTWARE_RESET, nullptr, 0);
    add(&steps, (struct epd_step){.kind = EPD_WAIT_IDLE});

    /* set it up for an 800 × 480 frame written top-down */
    add_command(&steps, TEMPERATURE_SENSOR, INTERNAL_SENSOR, sizeof INTERNAL_SENSOR);
    add_command(&steps, BOOSTER_SOFT_START, SOFT_START_PHASES, sizeof SOFT_START_PHASES);
    add_command(&steps, DRIVER_OUTPUT_CONTROL, GATES_480, sizeof GATES_480);
    add_command(&steps, BORDER_WAVEFORM, full ? BORDER_FULL : BORDER_PARTIAL, sizeof BORDER_FULL);
    add_command(&steps, DATA_ENTRY_MODE, X_UP_Y_DOWN, sizeof X_UP_Y_DOWN);
    add_command(&steps, RAM_X_RANGE, X_RANGE_800, sizeof X_RANGE_800);
    add_command(&steps, RAM_Y_RANGE, Y_RANGE_480_DOWN, sizeof Y_RANGE_480_DOWN);
    add_command(&steps, RAM_X_COUNTER, X_COUNTER_START, sizeof X_COUNTER_START);
    add_command(&steps, RAM_Y_COUNTER, Y_COUNTER_START, sizeof Y_COUNTER_START);

    /* both RAMs: what is on the glass, then what comes next */
    add_command(&steps, WRITE_GLASS_FRAME, on_glass->bits, FRAME_BYTES);
    add_command(&steps, WRITE_NEXT_FRAME, next->bits, FRAME_BYTES);

    /* run the update, then put the controller to sleep */
    add_command(&steps, DISPLAY_UPDATE_CONTROL_1,
                full ? UPDATE_CONTROL_FULL : UPDATE_CONTROL_PARTIAL, sizeof UPDATE_CONTROL_FULL);
    add_command(&steps, DISPLAY_UPDATE_CONTROL_2,
                full ? UPDATE_SEQUENCE_FULL : UPDATE_SEQUENCE_PARTIAL, sizeof UPDATE_SEQUENCE_FULL);
    add_command(&steps, MASTER_ACTIVATION, nullptr, 0);
    add(&steps, (struct epd_step){.kind = EPD_WAIT_IDLE});
    add_command(&steps, DEEP_SLEEP, SLEEP_MODE_1, sizeof SLEEP_MODE_1);
    return steps;
}

struct epd_steps epd_clear_steps(const struct frame *white) {
    return epd_refresh_steps(white, white, REFRESH_FULL);
}

struct epd_steps epd_sleep_steps(void) {
    struct epd_steps steps = {};
    add_reset(&steps);
    add_command(&steps, DEEP_SLEEP, SLEEP_MODE_1, sizeof SLEEP_MODE_1);
    add(&steps, (struct epd_step){.kind = EPD_PAUSE, .milliseconds = SETTLE_MS}); // BUSY can lag
    return steps;
}
