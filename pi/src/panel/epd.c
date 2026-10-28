#include "panel/epd.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "config.h"
#include "log.h"
#include "panel/board.h"
#include "panel/refresh.h"
#include "render/frame.h"

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

static constexpr int POWER_ON_SETTLE_MS = 10;
static constexpr int RESET_PULSE_MS = 2;
static constexpr int BUSY_POLL_MS = 10;
static constexpr int POWER_OFF_SETTLE_MS = 2000;

struct command {
    uint8_t code;
    uint8_t length;
    uint8_t data[5];
};

static bool send_command(const struct board_ops *board, uint8_t code, const uint8_t *data,
                         size_t length) {
    board->set_line(board->context, BOARD_LINE_DC, 0);
    if (!board->spi_write(board->context, &code, 1)) {
        return false;
    }
    board->set_line(board->context, BOARD_LINE_DC, 1);
    for (size_t sent = 0; sent < length;) {
        size_t chunk = length - sent;
        if (chunk > BOARD_SPI_WRITE_MAX_BYTES) {
            chunk = BOARD_SPI_WRITE_MAX_BYTES;
        }
        if (!board->spi_write(board->context, data + sent, chunk)) {
            return false;
        }
        sent += chunk;
    }
    return true;
}

static bool send_all(const struct board_ops *board, const struct command *commands, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (!send_command(board, commands[i].code, commands[i].data, commands[i].length)) {
            return false;
        }
    }
    return true;
}

static void pulse_reset(const struct board_ops *board) {
    board->set_line(board->context, BOARD_LINE_RST, 0);
    board->sleep_ms(board->context, RESET_PULSE_MS);
    board->set_line(board->context, BOARD_LINE_RST, 1);
}

static enum epd_status wait_while_busy(const struct board_ops *board, const char *after) {
    for (int waited_ms = 0; waited_ms < BUSY_TIMEOUT_MS; waited_ms += BUSY_POLL_MS) {
        board->sleep_ms(board->context, BUSY_POLL_MS); // BUSY can lag the command it reports on
        const int busy = board->read_busy(board->context);
        if (busy < 0) {
            return EPD_IO_ERROR;
        }
        if (busy == 0) {
            return EPD_OK;
        }
    }
    log_error("the panel was still busy %d ms after the %s", BUSY_TIMEOUT_MS, after);
    return EPD_BUSY_TIMEOUT;
}

static enum epd_status run_sequence(const struct board_ops *board, const struct frame *on_glass,
                                    const struct frame *next, bool full) {
    const struct command setup[] = {
            {TEMPERATURE_SENSOR, 1, {0x80}},
            {BOOSTER_SOFT_START, 5, {0xAE, 0xC7, 0xC3, 0xC0, 0x80}},
            {DRIVER_OUTPUT_CONTROL, 3, {0xDF, 0x01, 0x02}},
            {BORDER_WAVEFORM, 1, {full ? 0x01 : 0x80}},
            {DATA_ENTRY_MODE, 1, {0x01}},
            {RAM_X_RANGE, 4, {0x00, 0x00, 0x1F, 0x03}},
            {RAM_Y_RANGE, 4, {0xDF, 0x01, 0x00, 0x00}},
            {RAM_X_COUNTER, 2, {0x00, 0x00}},
            {RAM_Y_COUNTER, 2, {0xDF, 0x01}},
    };
    const struct command start_refresh[] = {
            {DISPLAY_UPDATE_CONTROL_1, 2, {full ? 0x40 : 0x00, 0x00}},
            {DISPLAY_UPDATE_CONTROL_2, 1, {full ? 0xF7 : 0xFF}},
            {MASTER_ACTIVATION, 0, {}},
    };

    board->set_line(board->context, BOARD_LINE_PWR, 1);
    board->sleep_ms(board->context, POWER_ON_SETTLE_MS);
    pulse_reset(board);
    enum epd_status status = wait_while_busy(board, "reset");
    if (status != EPD_OK) {
        return status;
    }
    if (!send_command(board, SOFTWARE_RESET, nullptr, 0)) {
        return EPD_IO_ERROR;
    }
    status = wait_while_busy(board, "software reset");
    if (status != EPD_OK) {
        return status;
    }
    if (!send_all(board, setup, sizeof setup / sizeof setup[0]) ||
        !send_command(board, WRITE_GLASS_FRAME, on_glass->bits, FRAME_BYTES) ||
        !send_command(board, WRITE_NEXT_FRAME, next->bits, FRAME_BYTES) ||
        !send_all(board, start_refresh, sizeof start_refresh / sizeof start_refresh[0])) {
        return EPD_IO_ERROR;
    }
    status = wait_while_busy(board, "refresh");
    if (status != EPD_OK) {
        return status;
    }
    if (!send_command(board, DEEP_SLEEP, (const uint8_t[]){0x03}, 1)) {
        return EPD_IO_ERROR;
    }
    return EPD_OK;
}

enum epd_status epd_update(const struct board_ops *board, const struct frame *on_glass,
                           const struct frame *next, enum refresh_kind kind, bool power_off_after) {
    if (kind != REFRESH_PARTIAL && kind != REFRESH_FULL) {
        log_error("the panel can't run refresh kind %d", (int)kind);
        abort();
    }
    const enum epd_status status = run_sequence(board, on_glass, next, kind == REFRESH_FULL);
    if (status == EPD_BUSY_TIMEOUT) {
        pulse_reset(board);
    }
    if (power_off_after) {
        board->sleep_ms(board->context, POWER_OFF_SETTLE_MS);
        board->set_line(board->context, BOARD_LINE_RST, 0); // no 3.3 V into an unpowered chip
        board->set_line(board->context, BOARD_LINE_DC, 0);
        board->set_line(board->context, BOARD_LINE_PWR, 0);
    }
    return status;
}

enum epd_status epd_clear(const struct board_ops *board) {
    struct frame white;
    frame_fill_white(&white);
    return epd_update(board, &white, &white, REFRESH_FULL, false);
}
