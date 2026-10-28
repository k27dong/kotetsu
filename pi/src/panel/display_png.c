#include <errno.h>

#include <glib.h>

#include "log.h"
#include "panel/display.h"
#include "panel/refresh.h"
#include "render/frame.h"

struct display {
    char *png_dir;
    int refreshes;
};

struct display *display_open(const char *png_dir) {
    if (g_mkdir_with_parents(png_dir, 0755) != 0) {
        log_error("can't create %s: %s", png_dir, g_strerror(errno));
        return nullptr;
    }
    struct display *display = g_new0(struct display, 1);
    display->png_dir = g_strdup(png_dir);
    return display;
}

bool display_show(struct display *display, [[maybe_unused]] const struct frame *on_glass,
                  const struct frame *next, enum refresh_kind kind) {
    if (kind == REFRESH_NONE) {
        log_error("asked to show a frame with no refresh");
        return false;
    }
    display->refreshes++;
    gchar *path = g_strdup_printf("%s/%04d-%s.png", display->png_dir, display->refreshes,
                                  kind == REFRESH_FULL ? "full" : "partial");
    const bool written = frame_write_png(next, path);
    g_free(path);
    return written;
}

bool display_clear(struct display *display) {
    struct frame white;
    frame_fill_white(&white);
    return display_show(display, &white, &white, REFRESH_FULL);
}

void display_close(struct display *display) {
    if (display == nullptr) {
        return;
    }
    g_free(display->png_dir);
    g_free(display);
}
