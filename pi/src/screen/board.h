#pragma once

#include "screen/epd.h"

struct board;

struct board *board_open(void); // PWR held on; nullptr when GPIO or SPI can't be opened
[[nodiscard]] bool board_run(struct board *board,
                             const struct epd_steps *steps); // false on I/O error or BUSY over 10 s
int board_read_busy(struct board *board);                    // 1 busy, 0 idle, -1 unreadable
void board_close(struct board *board);
