#include "render/icons.h"

#include <stddef.h>
#include <string.h>

#include <cairo.h>
#include <glib.h>

#include "model.h"

static constexpr double SMALL_ICON_SIZE = 26;
static constexpr int SUN_RAYS = 8;

static const char *const SMALL_ICON_NAMES[] = {
        [SMALL_ICON_HEART] = "heart", [SMALL_ICON_RINGS] = "rings",   [SMALL_ICON_CAKE] = "cake",
        [SMALL_ICON_CAT] = "cat",     [SMALL_ICON_HEARTS] = "hearts", [SMALL_ICON_STAR] = "star",
};

static void circle(cairo_t *cr, double x, double y, double radius) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x, y, radius, 0, 2 * G_PI);
}

static void line(cairo_t *cr, double x0, double y0, double x1, double y1, double width) {
    cairo_set_line_width(cr, width);
    cairo_move_to(cr, x0, y0);
    cairo_line_to(cr, x1, y1);
    cairo_stroke(cr);
}

static void cloud_path(cairo_t *cr, double left, double top, double width) {
    cairo_save(cr);
    cairo_translate(cr, left, top);
    cairo_scale(cr, width, width);
    circle(cr, 0.24, 0.40, 0.22);
    circle(cr, 0.52, 0.30, 0.30);
    circle(cr, 0.78, 0.42, 0.20);
    circle(cr, 0.13, 0.51, 0.11);
    circle(cr, 0.87, 0.51, 0.11);
    cairo_rectangle(cr, 0.13, 0.40, 0.74, 0.22);
    cairo_restore(cr);
}

static void cloud(cairo_t *cr, double left, double top, double width) {
    cloud_path(cr, left, top, width);
    cairo_fill(cr);
}

static void sun(cairo_t *cr, double x, double y, double radius) {
    circle(cr, x, y, radius);
    cairo_fill(cr);
    cairo_set_line_width(cr, radius * 0.40);
    for (int ray = 0; ray < SUN_RAYS; ray++) {
        cairo_save(cr);
        cairo_translate(cr, x, y);
        cairo_rotate(cr, ray * 2 * G_PI / SUN_RAYS);
        cairo_move_to(cr, radius * 1.45, 0);
        cairo_line_to(cr, radius * 2.10, 0);
        cairo_stroke(cr);
        cairo_restore(cr);
    }
}

static void upper_cloud(cairo_t *cr) {
    cloud(cr, 0.04, 0.02, 0.92);
}

static void rain(cairo_t *cr, const double *columns, size_t count, double top, double bottom,
                 double width) {
    const double slant = (bottom - top) * 0.32;
    for (size_t i = 0; i < count; i++) {
        line(cr, columns[i], top, columns[i] - slant, bottom, width);
    }
}

static void flakes(cairo_t *cr, const double *columns, size_t count, double y) {
    for (size_t i = 0; i < count; i++) {
        circle(cr, columns[i], y, 0.065);
    }
    cairo_fill(cr);
}

static void bolt(cairo_t *cr) {
    cairo_move_to(cr, 0.50, 0.64);
    cairo_line_to(cr, 0.34, 0.86);
    cairo_line_to(cr, 0.47, 0.86);
    cairo_line_to(cr, 0.40, 1.00);
    cairo_line_to(cr, 0.68, 0.78);
    cairo_line_to(cr, 0.54, 0.78);
    cairo_line_to(cr, 0.62, 0.64);
    cairo_close_path(cr);
    cairo_fill(cr);
}

static void erase_stroke(cairo_t *cr, double width) {
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OUT);
    cairo_set_line_width(cr, width);
    cairo_stroke(cr);
    cairo_restore(cr);
}

void icons_draw_weather(cairo_t *cr, enum weather_icon icon, double x, double y, double size) {
    static const double three[] = {0.30, 0.50, 0.70};
    static const double four[] = {0.22, 0.40, 0.58, 0.76};
    static const double sides[] = {0.30, 0.70};
    static const double middle[] = {0.50};
    static const double upper[] = {0.26, 0.50, 0.74};
    static const double lower[] = {0.38, 0.62};
    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, size, size);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    switch (icon) {
    case WEATHER_ICON_SUN:
        sun(cr, 0.50, 0.50, 0.21);
        break;
    case WEATHER_ICON_PARTLY:
        sun(cr, 0.34, 0.34, 0.15);
        cloud_path(cr, 0.14, 0.40, 0.84);
        erase_stroke(cr, 0.09);
        cloud(cr, 0.14, 0.40, 0.84);
        break;
    case WEATHER_ICON_CLOUD:
        cloud(cr, 0.02, 0.19, 0.96);
        break;
    case WEATHER_ICON_FOG:
        upper_cloud(cr);
        line(cr, 0.12, 0.72, 0.88, 0.72, 0.08);
        line(cr, 0.24, 0.88, 0.76, 0.88, 0.08);
        break;
    case WEATHER_ICON_DRIZZLE:
        upper_cloud(cr);
        rain(cr, three, G_N_ELEMENTS(three), 0.72, 0.84, 0.075);
        break;
    case WEATHER_ICON_RAIN:
        upper_cloud(cr);
        rain(cr, three, G_N_ELEMENTS(three), 0.70, 0.96, 0.08);
        break;
    case WEATHER_ICON_HEAVY_RAIN:
        upper_cloud(cr);
        rain(cr, four, G_N_ELEMENTS(four), 0.68, 0.98, 0.085);
        break;
    case WEATHER_ICON_STORM:
        upper_cloud(cr);
        bolt(cr);
        break;
    case WEATHER_ICON_SLEET:
        upper_cloud(cr);
        rain(cr, sides, G_N_ELEMENTS(sides), 0.70, 0.96, 0.08);
        flakes(cr, middle, G_N_ELEMENTS(middle), 0.85);
        break;
    case WEATHER_ICON_SNOW:
        upper_cloud(cr);
        flakes(cr, three, G_N_ELEMENTS(three), 0.84);
        break;
    case WEATHER_ICON_HEAVY_SNOW:
        upper_cloud(cr);
        flakes(cr, upper, G_N_ELEMENTS(upper), 0.75);
        flakes(cr, lower, G_N_ELEMENTS(lower), 0.93);
        break;
    case WEATHER_ICON_OFFLINE:
        cloud(cr, 0.02, 0.19, 0.96);
        cairo_move_to(cr, 0.16, 0.14);
        cairo_line_to(cr, 0.84, 0.90);
        erase_stroke(cr, 0.20);
        line(cr, 0.16, 0.14, 0.84, 0.90, 0.08);
        break;
    }
    cairo_restore(cr);
}

static void heart_path(cairo_t *cr) {
    cairo_move_to(cr, 0.50, 0.92);
    cairo_curve_to(cr, 0.20, 0.70, 0.02, 0.50, 0.05, 0.30);
    cairo_curve_to(cr, 0.09, 0.08, 0.38, 0.06, 0.50, 0.26);
    cairo_curve_to(cr, 0.62, 0.06, 0.91, 0.08, 0.95, 0.30);
    cairo_curve_to(cr, 0.98, 0.50, 0.80, 0.70, 0.50, 0.92);
    cairo_close_path(cr);
}

static void placed_heart(cairo_t *cr, double left, double top, double size) {
    cairo_save(cr);
    cairo_translate(cr, left, top);
    cairo_scale(cr, size, size);
    heart_path(cr);
    cairo_restore(cr);
}

static void star_path(cairo_t *cr, double x, double y, double outer, double inner) {
    cairo_save(cr);
    cairo_translate(cr, x, y);
    for (int point = 0; point < 10; point++) {
        cairo_save(cr);
        cairo_rotate(cr, point * G_PI / 5);
        const double radius = point % 2 == 0 ? outer : inner;
        if (point == 0) {
            cairo_move_to(cr, 0, -radius);
        } else {
            cairo_line_to(cr, 0, -radius);
        }
        cairo_restore(cr);
    }
    cairo_close_path(cr);
    cairo_restore(cr);
}

void icons_draw_small(cairo_t *cr, enum small_icon icon, double x, double y) {
    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, SMALL_ICON_SIZE, SMALL_ICON_SIZE);
    switch (icon) {
    case SMALL_ICON_HEART:
        heart_path(cr);
        break;
    case SMALL_ICON_RINGS:
        cairo_set_line_width(cr, 0.11);
        circle(cr, 0.34, 0.56, 0.26);
        circle(cr, 0.66, 0.56, 0.26);
        cairo_stroke(cr);
        break;
    case SMALL_ICON_CAKE:
        cairo_rectangle(cr, 0.04, 0.64, 0.92, 0.32);
        cairo_rectangle(cr, 0.20, 0.42, 0.60, 0.22);
        cairo_rectangle(cr, 0.45, 0.20, 0.10, 0.22);
        cairo_save(cr);
        cairo_translate(cr, 0.50, 0.07);
        cairo_scale(cr, 0.07, 0.08);
        circle(cr, 0, 0, 1);
        cairo_restore(cr);
        break;
    case SMALL_ICON_CAT:
        circle(cr, 0.50, 0.60, 0.36);
        cairo_move_to(cr, 0.17, 0.44);
        cairo_line_to(cr, 0.20, 0.06);
        cairo_line_to(cr, 0.50, 0.28);
        cairo_close_path(cr);
        cairo_move_to(cr, 0.83, 0.44);
        cairo_line_to(cr, 0.50, 0.28);
        cairo_line_to(cr, 0.80, 0.06);
        cairo_close_path(cr);
        break;
    case SMALL_ICON_HEARTS:
        placed_heart(cr, 0.00, 0.02, 0.52);
        placed_heart(cr, 0.44, 0.40, 0.56);
        break;
    case SMALL_ICON_STAR:
        star_path(cr, 0.50, 0.54, 0.50, 0.21);
        break;
    }
    cairo_fill(cr);
    cairo_restore(cr);
}

bool icons_small_from_name(const char *name, enum small_icon *out) {
    for (size_t i = 0; i < G_N_ELEMENTS(SMALL_ICON_NAMES); i++) {
        if (strcmp(name, SMALL_ICON_NAMES[i]) == 0) {
            *out = (enum small_icon)i;
            return true;
        }
    }
    return false;
}
