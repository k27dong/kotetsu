#include <stdlib.h>

#include "screen/board.h"
#include "screen/epd.h"
#include "shared/log.h"

struct board *board_open(void) {
    return nullptr;
}

bool board_run([[maybe_unused]] struct board *board,
               [[maybe_unused]] const struct epd_steps *steps) {
    log_error("this build has no panel driver");
    abort();
}

int board_read_busy([[maybe_unused]] struct board *board) {
    log_error("this build has no panel driver");
    abort();
}

void board_close([[maybe_unused]] struct board *board) {
    log_error("this build has no panel driver");
    abort();
}
