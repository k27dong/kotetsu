#pragma once

#include "panel/board.h"
#include "panel/refresh.h"
#include "render/frame.h"

enum epd_status {
    EPD_OK,
    EPD_BUSY_TIMEOUT,
    EPD_IO_ERROR,
};

[[nodiscard]] enum epd_status epd_update(const struct board_ops *board,
                                         const struct frame *on_glass, const struct frame *next,
                                         enum refresh_kind kind, bool power_off_after);
[[nodiscard]] enum epd_status epd_clear(const struct board_ops *board);
