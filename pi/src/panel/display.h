#pragma once

#include "panel/refresh.h"
#include "render/frame.h"

struct display;

[[nodiscard]] struct display *display_open(const char *png_dir);
// Refuses REFRESH_NONE and returns false.
[[nodiscard]] bool display_show(struct display *display, const struct frame *on_glass,
                                const struct frame *next, enum refresh_kind kind);
[[nodiscard]] bool display_clear(struct display *display);
void display_close(struct display *display);
