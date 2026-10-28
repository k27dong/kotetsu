#pragma once

#include <stddef.h>
#include <stdint.h>

static constexpr size_t BOARD_SPI_WRITE_MAX_BYTES = 4096;

enum board_line {
    BOARD_LINE_RST,
    BOARD_LINE_DC,
    BOARD_LINE_PWR,
};

struct board_ops {
    void *context;
    bool (*spi_write)(void *context, const uint8_t *data, size_t length);
    void (*set_line)(void *context, enum board_line line, int value);
    int (*read_busy)(void *context); // 1 while busy, 0 when idle, -1 if BUSY can't be read
    void (*sleep_ms)(void *context, int milliseconds);
};

[[nodiscard]] bool board_linux_open(struct board_ops *out);
void board_linux_close(struct board_ops *board);
