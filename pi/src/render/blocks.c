#include "render/blocks.h"

#include <stddef.h>
#include <stdio.h>

#include <cairo.h>
#include <glib.h>
#include <pango/pango.h>

#include "log.h"
#include "model.h"
#include "render/fonts.h"
#include "render/icons.h"
#include "render/layout.h"
#include "render/wifi_qr.h"

static constexpr double NUMBER_BASELINE = 414;
static constexpr double LINE_BASELINE = 450;
static constexpr double NUMBER_SIZE = 64;
static constexpr double LABEL_SIZE = 26;
static constexpr double LINE_SIZE = 24;
static constexpr double LINE_SIZE_MIN = 20;
static constexpr double PILL_TEXT_SIZE = 22;
static constexpr double PILL_TOP = 428;
static constexpr double PILL_HEIGHT = 30;
static constexpr double PILL_PADDING = 10;
// WenKai's ideographic box runs 0.88 em above the baseline and 0.12 em below it.
static constexpr double HAN_CENTRE_ABOVE_BASELINE = 0.38;

static constexpr double DATE_LEFT = 32;
static constexpr double DATE_RIGHT = 268;
static constexpr double DATE_COLUMN_GAP = 14;
static constexpr double DATE_MONTH_BASELINE = 384;
static constexpr double DATE_PILL_GAP = 8;
static constexpr double REST_BADGE_LEFT = 222;
static constexpr double REST_BADGE_TOP = 360;
static constexpr double REST_BADGE_SIZE = 38;
static constexpr double REST_BADGE_RADIUS = 6;

static constexpr double WEATHER_LEFT = 286;
static constexpr double WEATHER_RIGHT = 508;
static constexpr double WEATHER_ICON_LEFT = 294;
static constexpr double WEATHER_ICON_TOP = 362;
static constexpr double WEATHER_ICON_SIZE = 58;
static constexpr double WEATHER_ICON_CENTRE = 323;
static constexpr double TEMPERATURE_MAX_WIDTH = 150;
static constexpr double RANGE_CLEARANCE = 8;

static constexpr double COUNTER_CENTRE_X = 652;
static constexpr double COUNTER_CENTRE_Y = 406;
static constexpr double COUNTER_RIGHT_LIMIT = 762;
static constexpr double COUNTER_UNIT_GAP = 10;
static constexpr double COUNTER_TEXT_LEFT = 548;
static constexpr double TILE_LEFT = 536;
static constexpr double TILE_TOP = 352;
static constexpr double TILE_WIDTH = 232;
static constexpr double TILE_HEIGHT = 108;
static constexpr double TILE_RADIUS = 8;
static constexpr double TILE_RIGHT_LIMIT = 756;
static constexpr double TILE_ICON_TOP = 428;
static constexpr double GREETING_LEFT = 582;

static constexpr double QR_LEFT = 48;
static constexpr double QR_TOP = 66;
static constexpr double QR_MODULE = 12;
static constexpr double WIFI_TEXT_LEFT = 456;
static constexpr double WIFI_TITLE_SIZE = 52;
static constexpr double WIFI_TITLE_BASELINE = 140;
static constexpr double WIFI_STEP_SIZE = 30;
static constexpr double WIFI_STEP_TEXT_LEFT = 508;
static constexpr double WIFI_STEP_CIRCLE_X = 474;
static constexpr double WIFI_STEP_CIRCLE_RADIUS = 18;
static constexpr double WIFI_STEP_NUMBER_SIZE = 22;
static constexpr double WIFI_FOOTER_SIZE = 22;
static constexpr double WIFI_FOOTER_BASELINE = 420;

struct wifi_step {
    double baseline;
    const char *text;
};

static const struct wifi_step WIFI_STEPS[] = {
        {220, "扫描左边的二维码"},
        {286, "选择家里的 Wi-Fi"},
        {352, "输入密码"},
};

#define COUNTER_UNIT "天"
#define REST_DAY     "休"
#define WIFI_TITLE   "连接 Wi-Fi"
#define WIFI_FOOTER  "热点：kotetsu-setup"

enum ink {
    INK_BLACK,
    INK_WHITE,
};

struct text_metrics {
    double advance;
    double ink_left;
    double ink_right;
    double ink_top;
    double ink_bottom;
};

static struct text_metrics measure(PangoLayout *layout) {
    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_extents(layout, &ink, &logical);
    const int baseline = pango_layout_get_baseline(layout);
    return (struct text_metrics){
            .advance = (double)logical.width / PANGO_SCALE,
            .ink_left = (double)ink.x / PANGO_SCALE,
            .ink_right = (double)(ink.x + ink.width) / PANGO_SCALE,
            .ink_top = (double)(ink.y - baseline) / PANGO_SCALE,
            .ink_bottom = (double)(ink.y + ink.height - baseline) / PANGO_SCALE,
    };
}

static void set_ink(cairo_t *cr, enum ink ink) {
    if (ink == INK_WHITE) {
        cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OUT); // A8 has no white, so remove coverage
    } else {
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    }
}

static void fill_rounded_rect(cairo_t *cr, double left, double top, double width, double height,
                              double radius) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, left + width - radius, top + radius, radius, -G_PI / 2, 0);
    cairo_arc(cr, left + width - radius, top + height - radius, radius, 0, G_PI / 2);
    cairo_arc(cr, left + radius, top + height - radius, radius, G_PI / 2, G_PI);
    cairo_arc(cr, left + radius, top + radius, radius, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
    cairo_fill(cr);
}

static PangoLayout *shrunk_to_fit(PangoContext *context, const char *field, const char *family,
                                  double size, double size_min, enum digit_figures figures,
                                  const char *text, double max_advance) {
    for (;; size -= 1) {
        PangoLayout *layout = layout_text(context, family, size, figures, text);
        if (measure(layout).advance <= max_advance) {
            return layout;
        }
        if (size <= size_min) {
            log_warning("the %s doesn't fit at %.0f px", field, size);
            return layout;
        }
        g_object_unref(layout);
    }
}

static double show_text(cairo_t *cr, PangoContext *context, double size, const char *text, double x,
                        double baseline) {
    PangoLayout *layout = layout_text(context, FONT_FAMILY_CJK, size, DIGITS_PROPORTIONAL, text);
    layout_show(cr, layout, x, baseline);
    const double advance = measure(layout).advance;
    g_object_unref(layout);
    return advance;
}

static void show_right_aligned(cairo_t *cr, PangoLayout *layout, double right, double baseline) {
    layout_show(cr, layout, right - measure(layout).ink_right, baseline);
}

static void show_centred(cairo_t *cr, PangoLayout *layout, double centre_x, double centre_y) {
    const struct text_metrics metrics = measure(layout);
    layout_show(cr, layout, centre_x - (metrics.ink_left + metrics.ink_right) / 2,
                centre_y - (metrics.ink_top + metrics.ink_bottom) / 2);
}

static double pill_width(PangoLayout *label) {
    return measure(label).advance + 2 * PILL_PADDING;
}

static void draw_pill(cairo_t *cr, PangoLayout *label, double left) {
    set_ink(cr, INK_BLACK);
    fill_rounded_rect(cr, left, PILL_TOP, pill_width(label), PILL_HEIGHT, PILL_HEIGHT / 2);
    set_ink(cr, INK_WHITE);
    layout_show(cr, label, left + PILL_PADDING, LINE_BASELINE);
    set_ink(cr, INK_BLACK);
}

static void draw_date_line(cairo_t *cr, PangoContext *context, const struct date_line *line) {
    double x = DATE_LEFT;
    if (line->pill != nullptr) {
        PangoLayout *label = layout_text(context, FONT_FAMILY_CJK, PILL_TEXT_SIZE,
                                         DIGITS_PROPORTIONAL, line->pill);
        draw_pill(cr, label, x);
        x += pill_width(label) + DATE_PILL_GAP;
        g_object_unref(label);
    }
    char text[64];
    snprintf(text, sizeof text, "%s%s%s", line->prefix == nullptr ? "" : line->prefix,
             line->prefix == nullptr ? "" : " ", line->lunar);
    PangoLayout *layout = shrunk_to_fit(context, "date line", FONT_FAMILY_CJK, LINE_SIZE,
                                        LINE_SIZE_MIN, DIGITS_PROPORTIONAL, text, DATE_RIGHT - x);
    layout_show(cr, layout, x, LINE_BASELINE);
    g_object_unref(layout);
}

static void draw_rest_badge(cairo_t *cr, PangoContext *context) {
    set_ink(cr, INK_BLACK);
    fill_rounded_rect(cr, REST_BADGE_LEFT, REST_BADGE_TOP, REST_BADGE_SIZE, REST_BADGE_SIZE,
                      REST_BADGE_RADIUS);
    PangoLayout *label =
            layout_text(context, FONT_FAMILY_CJK, LABEL_SIZE, DIGITS_PROPORTIONAL, REST_DAY);
    set_ink(cr, INK_WHITE);
    layout_show(cr, label, REST_BADGE_LEFT + (REST_BADGE_SIZE - measure(label).advance) / 2,
                REST_BADGE_TOP + REST_BADGE_SIZE / 2 + HAN_CENTRE_ABOVE_BASELINE * LABEL_SIZE);
    set_ink(cr, INK_BLACK);
    g_object_unref(label);
}

void blocks_draw_date(cairo_t *cr, PangoContext *context, const struct screen_model *model) {
    char day[16];
    snprintf(day, sizeof day, "%d", model->day_of_month);
    PangoLayout *number =
            layout_text(context, FONT_FAMILY_DIGITS, NUMBER_SIZE, DIGITS_PROPORTIONAL, day);
    layout_show(cr, number, DATE_LEFT, NUMBER_BASELINE);
    const double column_left = DATE_LEFT + measure(number).advance + DATE_COLUMN_GAP;
    g_object_unref(number);
    show_text(cr, context, LABEL_SIZE, model->month_name, column_left, DATE_MONTH_BASELINE);
    show_text(cr, context, LABEL_SIZE, model->weekday_name, column_left, NUMBER_BASELINE);
    draw_date_line(cr, context, &model->date_line);
    if (model->date_line.rest_day) {
        draw_rest_badge(cr, context);
    }
}

void blocks_draw_weather(cairo_t *cr, PangoContext *context, const struct weather_view *weather) {
    icons_draw_weather(cr, weather->icon, WEATHER_ICON_LEFT, WEATHER_ICON_TOP, WEATHER_ICON_SIZE);
    PangoLayout *temperature =
            shrunk_to_fit(context, "temperature", FONT_FAMILY_DIGITS, NUMBER_SIZE, LINE_SIZE,
                          DIGITS_TABULAR, weather->temperature, TEMPERATURE_MAX_WIDTH);
    show_right_aligned(cr, temperature, WEATHER_RIGHT, NUMBER_BASELINE);
    g_object_unref(temperature);
    double line_right;
    if (weather->line_kind == WEATHER_LINE_BADGE) {
        PangoLayout *label = layout_text(context, FONT_FAMILY_CJK, PILL_TEXT_SIZE,
                                         DIGITS_PROPORTIONAL, weather->line);
        const double left =
                MAX(layout_snap(WEATHER_ICON_CENTRE - pill_width(label) / 2), WEATHER_LEFT);
        draw_pill(cr, label, left);
        line_right = left + pill_width(label);
        g_object_unref(label);
    } else {
        PangoLayout *line =
                layout_text(context, FONT_FAMILY_CJK, LINE_SIZE, DIGITS_TABULAR, weather->line);
        const double left = MAX(WEATHER_ICON_CENTRE - measure(line).advance / 2, WEATHER_LEFT);
        layout_show(cr, line, left, LINE_BASELINE);
        line_right = left + measure(line).advance;
        g_object_unref(line);
    }
    if (weather->range[0] != '\0') {
        PangoLayout *range = shrunk_to_fit(context, "weather range", FONT_FAMILY_DIGITS, LINE_SIZE,
                                           LINE_SIZE_MIN, DIGITS_TABULAR, weather->range,
                                           WEATHER_RIGHT - line_right - RANGE_CLEARANCE);
        show_right_aligned(cr, range, WEATHER_RIGHT, LINE_BASELINE);
        g_object_unref(range);
    }
}

static void draw_counter_number(cairo_t *cr, PangoContext *context, const char *days,
                                PangoLayout *unit) {
    const double unit_advance = measure(unit).advance;
    PangoLayout *number = shrunk_to_fit(
            context, "counter", FONT_FAMILY_DIGITS, NUMBER_SIZE, LINE_SIZE, DIGITS_PROPORTIONAL,
            days, COUNTER_RIGHT_LIMIT - COUNTER_TEXT_LEFT - COUNTER_UNIT_GAP - unit_advance);
    const struct text_metrics metrics = measure(number);
    const double centred_left = COUNTER_CENTRE_X - (metrics.ink_left + metrics.ink_right) / 2;
    const double pair_width = metrics.advance + COUNTER_UNIT_GAP + unit_advance;
    const double left =
            MIN(centred_left, COUNTER_RIGHT_LIMIT - pair_width); // centring yields first
    const double baseline = COUNTER_CENTRE_Y - (metrics.ink_top + metrics.ink_bottom) / 2;
    layout_show(cr, number, left, baseline);
    layout_show(cr, unit, left + metrics.advance + COUNTER_UNIT_GAP, baseline);
    g_object_unref(number);
}

static void draw_counter_tile(cairo_t *cr, PangoContext *context, const char *days,
                              PangoLayout *unit, const struct screen_model *model) {
    set_ink(cr, INK_BLACK);
    fill_rounded_rect(cr, TILE_LEFT, TILE_TOP, TILE_WIDTH, TILE_HEIGHT, TILE_RADIUS);
    set_ink(cr, INK_WHITE);
    const double unit_advance = measure(unit).advance;
    PangoLayout *number =
            shrunk_to_fit(context, "tile counter", FONT_FAMILY_DIGITS, NUMBER_SIZE, LINE_SIZE,
                          DIGITS_PROPORTIONAL, days,
                          TILE_RIGHT_LIMIT - COUNTER_TEXT_LEFT - COUNTER_UNIT_GAP - unit_advance);
    layout_show(cr, number, COUNTER_TEXT_LEFT, NUMBER_BASELINE);
    layout_show(cr, unit, COUNTER_TEXT_LEFT + measure(number).advance + COUNTER_UNIT_GAP,
                NUMBER_BASELINE);
    g_object_unref(number);
    icons_draw_small(cr, model->greeting_icon, COUNTER_TEXT_LEFT, TILE_ICON_TOP);
    PangoLayout *greeting =
            shrunk_to_fit(context, "greeting", FONT_FAMILY_CJK, LABEL_SIZE, LINE_SIZE_MIN,
                          DIGITS_PROPORTIONAL, model->greeting, TILE_RIGHT_LIMIT - GREETING_LEFT);
    layout_show(cr, greeting, GREETING_LEFT, LINE_BASELINE);
    g_object_unref(greeting);
    set_ink(cr, INK_BLACK);
}

void blocks_draw_counter(cairo_t *cr, PangoContext *context, const struct screen_model *model) {
    char days[16];
    snprintf(days, sizeof days, "%d", model->counter_days);
    PangoLayout *unit =
            layout_text(context, FONT_FAMILY_CJK, LABEL_SIZE, DIGITS_PROPORTIONAL, COUNTER_UNIT);
    if (model->has_greeting) {
        draw_counter_tile(cr, context, days, unit, model);
    } else {
        draw_counter_number(cr, context, days, unit);
    }
    g_object_unref(unit);
}

void blocks_draw_wifi_setup(cairo_t *cr, PangoContext *context) {
    set_ink(cr, INK_BLACK);
    for (int row = 0; row < WIFI_QR_SIZE; row++) {
        for (int column = 0; column < WIFI_QR_SIZE; column++) {
            if (WIFI_QR_MODULES[row * WIFI_QR_SIZE + column] != 0) {
                cairo_rectangle(cr, QR_LEFT + column * QR_MODULE, QR_TOP + row * QR_MODULE,
                                QR_MODULE, QR_MODULE);
            }
        }
    }
    cairo_fill(cr);
    show_text(cr, context, WIFI_TITLE_SIZE, WIFI_TITLE, WIFI_TEXT_LEFT, WIFI_TITLE_BASELINE);
    for (size_t step = 0; step < G_N_ELEMENTS(WIFI_STEPS); step++) {
        const double baseline = WIFI_STEPS[step].baseline;
        const double centre_y = layout_snap(baseline - HAN_CENTRE_ABOVE_BASELINE * WIFI_STEP_SIZE);
        cairo_new_sub_path(cr);
        cairo_arc(cr, WIFI_STEP_CIRCLE_X, centre_y, WIFI_STEP_CIRCLE_RADIUS, 0, 2 * G_PI);
        cairo_fill(cr);
        const char number[] = {(char)('1' + step), '\0'};
        PangoLayout *digit = layout_text(context, FONT_FAMILY_DIGITS, WIFI_STEP_NUMBER_SIZE,
                                         DIGITS_PROPORTIONAL, number);
        set_ink(cr, INK_WHITE);
        show_centred(cr, digit, WIFI_STEP_CIRCLE_X, centre_y);
        set_ink(cr, INK_BLACK);
        g_object_unref(digit);
        show_text(cr, context, WIFI_STEP_SIZE, WIFI_STEPS[step].text, WIFI_STEP_TEXT_LEFT,
                  baseline);
    }
    show_text(cr, context, WIFI_FOOTER_SIZE, WIFI_FOOTER, WIFI_TEXT_LEFT, WIFI_FOOTER_BASELINE);
}
