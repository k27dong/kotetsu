#pragma once

#include "screen/epd.h"

struct board;

struct busy_reading {
    bool ok;
    bool busy;
};

// PWR held on; nullptr when GPIO or SPI can't be opened
struct board *board_open(void);
// false on I/O error or BUSY over 10 s
[[nodiscard]] bool board_run(struct board *board, const struct epd_steps *steps);
[[nodiscard]] struct busy_reading board_read_busy(struct board *board);
void board_close(struct board *board);
