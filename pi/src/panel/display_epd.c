#include <glib.h>

#include "log.h"
#include "panel/board.h"
#include "panel/display.h"
#include "panel/epd.h"
#include "panel/refresh.h"
#include "render/frame.h"

static constexpr bool POWER_OFF_BETWEEN_UPDATES = false; // until a bench check clears PWR low

struct display {
    struct board_ops board;
};

struct display *display_open([[maybe_unused]] const char *png_dir) {
    struct display *display = g_new0(struct display, 1);
    if (!board_linux_open(&display->board)) {
        g_free(display);
        return nullptr;
    }
    return display;
}

bool display_show(struct display *display, const struct frame *on_glass, const struct frame *next,
                  enum refresh_kind kind) {
    if (kind == REFRESH_NONE) {
        log_error("asked to show a frame with no refresh");
        return false;
    }
    return epd_update(&display->board, on_glass, next, kind, POWER_OFF_BETWEEN_UPDATES) == EPD_OK;
}

bool display_clear(struct display *display) {
    return epd_clear(&display->board) == EPD_OK;
}

void display_close(struct display *display) {
    if (display == nullptr) {
        return;
    }
    board_linux_close(&display->board);
    g_free(display);
}
