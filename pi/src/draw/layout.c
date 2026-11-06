#include "draw/layout.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cairo.h>
#include <fontconfig/fontconfig.h>
#include <glib-object.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>

#include "config.h"
#include "date.h"
#include "draw/frame.h"
#include "draw/icons.h"
#include "draw/typeset.h"
#include "phrase.h"
#include "shared/log.h"
#include "weather.h"

#define FONT_FILE_CJK    "LXGWWenKaiGBScreen.ttf"
#define FONT_FILE_DIGITS "Inter-SemiBold.ttf"

static constexpr double RULE_LEFT = 32;
static constexpr double RULE_TOP = 336;
static constexpr double RULE_WIDTH = 736;
static constexpr double LINE_THICKNESS = 2;
static constexpr double DIVIDER_TOP = 356;
static constexpr double DIVIDER_HEIGHT = 100;
static const double DIVIDERS[] = {276, 522};

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

#define COUNTER_UNIT "天"
#define REST_DAY     "休"

static const struct text_style NUMBER = {FONT_FAMILY_DIGITS, NUMBER_SIZE, DIGITS_PROPORTIONAL};
static const struct text_style TABULAR_NUMBER = {FONT_FAMILY_DIGITS, NUMBER_SIZE, DIGITS_TABULAR};
static const struct text_style LABEL = {FONT_FAMILY_CJK, LABEL_SIZE, DIGITS_PROPORTIONAL};
static const struct text_style LINE = {FONT_FAMILY_CJK, LINE_SIZE, DIGITS_PROPORTIONAL};
static const struct text_style TABULAR_LINE = {FONT_FAMILY_CJK, LINE_SIZE, DIGITS_TABULAR};
static const struct text_style RANGE = {FONT_FAMILY_DIGITS, LINE_SIZE, DIGITS_TABULAR};
static const struct text_style PILL_LABEL = {FONT_FAMILY_CJK, PILL_TEXT_SIZE, DIGITS_PROPORTIONAL};

struct drawing {
    PangoFontMap *fonts;
};

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

struct shrinkable {
    const char *purpose;
    struct text_style style;
    double size_min;
};

struct counter {
    const char *days;
    PangoLayout *unit;
};

struct greeting {
    enum greeting_icon icon;
    const char *text;
};

struct drawing *draw_open(const char *font_dir) {
    /* a fontconfig of the two files alone, so nothing else on the system is ever drawn */
    FcConfig *config = FcConfigCreate();
    if (config == nullptr) {
        log_error("can't create a fontconfig configuration");
        return nullptr;
    }
    const char *const files[] = {FONT_FILE_CJK, FONT_FILE_DIGITS};
    bool added = true;
    for (size_t i = 0; i < sizeof files / sizeof *files; i++) {
        char path[512];
        const int length = snprintf(path, sizeof path, "%s/%s", font_dir, files[i]);
        if (length < 0 || (size_t)length >= sizeof path ||
            !FcConfigAppFontAddFile(config, (const FcChar8 *)path)) {
            log_error("can't load font %s/%s", font_dir, files[i]);
            added = false;
        }
    }

    /* a FreeType font map over it, which keeps its own reference */
    PangoFontMap *fonts = nullptr;
    if (added) {
        fonts = pango_cairo_font_map_new_for_font_type(CAIRO_FONT_TYPE_FT);
        if (fonts == nullptr) {
            log_error("Pango has no FreeType font map");
        } else {
            pango_fc_font_map_set_config(PANGO_FC_FONT_MAP(fonts), config);
        }
    }
    FcConfigDestroy(config);
    if (fonts == nullptr) {
        return nullptr;
    }
    struct drawing *drawing = malloc(sizeof *drawing);
    if (drawing == nullptr) {
        log_error("can't allocate the drawing state");
        g_object_unref(fonts);
        return nullptr;
    }
    drawing->fonts = fonts;
    return drawing;
}

void draw_close(struct drawing *drawing) {
    if (drawing == nullptr) {
        return;
    }
    g_object_unref(drawing->fonts);
    free(drawing);
}

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

static void fill_rounded_rect(cairo_t *cr, cairo_rectangle_t box, double radius) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, box.x + box.width - radius, box.y + radius, radius, -M_PI / 2, 0);
    cairo_arc(cr, box.x + box.width - radius, box.y + box.height - radius, radius, 0, M_PI / 2);
    cairo_arc(cr, box.x + radius, box.y + box.height - radius, radius, M_PI / 2, M_PI);
    cairo_arc(cr, box.x + radius, box.y + radius, radius, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
    cairo_fill(cr);
}

static PangoLayout *shrunk_to_fit(PangoContext *context, struct shrinkable text, const char *value,
                                  double max_advance) {
    for (;; text.style.size_px -= 1) {
        PangoLayout *layout = typeset_line(context, text.style, value);
        if (measure(layout).advance <= max_advance) {
            return layout;
        }
        if (text.style.size_px <= text.size_min) {
            log_warning("the %s doesn't fit at %.0f px", text.purpose, text.style.size_px);
            return layout;
        }
        g_object_unref(layout);
    }
}

static void show_right_aligned(cairo_t *cr, PangoLayout *layout, double right, double baseline) {
    typeset_show(cr, layout, right - measure(layout).ink_right, baseline);
}

static double pill_width(PangoLayout *label) {
    return measure(label).advance + 2 * PILL_PADDING;
}

static void draw_pill(cairo_t *cr, PangoLayout *label, double left) {
    set_ink(cr, INK_BLACK);
    fill_rounded_rect(cr, (cairo_rectangle_t){left, PILL_TOP, pill_width(label), PILL_HEIGHT},
                      PILL_HEIGHT / 2);
    set_ink(cr, INK_WHITE);
    typeset_show(cr, label, left + PILL_PADDING, LINE_BASELINE);
    set_ink(cr, INK_BLACK);
}

static void draw_date_line(cairo_t *cr, PangoContext *context, const struct date_line *line) {
    double x = DATE_LEFT;
    if (line->pill != nullptr) {
        PangoLayout *label = typeset_line(context, PILL_LABEL, line->pill);
        draw_pill(cr, label, x);
        x += pill_width(label) + DATE_PILL_GAP;
        g_object_unref(label);
    }
    char text[64];
    snprintf(text, sizeof text, "%s%s%s", line->prefix == nullptr ? "" : line->prefix,
             line->prefix == nullptr ? "" : " ", line->lunar);
    PangoLayout *layout = shrunk_to_fit(
            context, (struct shrinkable){"date line", LINE, LINE_SIZE_MIN}, text, DATE_RIGHT - x);
    typeset_show(cr, layout, x, LINE_BASELINE);
    g_object_unref(layout);
}

static void draw_rest_badge(cairo_t *cr, PangoContext *context) {
    set_ink(cr, INK_BLACK);
    fill_rounded_rect(
            cr,
            (cairo_rectangle_t){REST_BADGE_LEFT, REST_BADGE_TOP, REST_BADGE_SIZE, REST_BADGE_SIZE},
            REST_BADGE_RADIUS);
    PangoLayout *label = typeset_line(context, LABEL, REST_DAY);
    set_ink(cr, INK_WHITE);
    typeset_show(cr, label, REST_BADGE_LEFT + (REST_BADGE_SIZE - measure(label).advance) / 2,
                 REST_BADGE_TOP + REST_BADGE_SIZE / 2 + HAN_CENTRE_ABOVE_BASELINE * LABEL_SIZE);
    set_ink(cr, INK_BLACK);
    g_object_unref(label);
}

static void draw_date(cairo_t *cr, PangoContext *context, const struct date *date) {
    /* the day's number, with the month and weekday stacked beside it */
    char day[16];
    snprintf(day, sizeof day, "%d", date->today.day);
    PangoLayout *number = typeset_line(context, NUMBER, day);
    typeset_show(cr, number, DATE_LEFT, NUMBER_BASELINE);
    const double column_left = DATE_LEFT + measure(number).advance + DATE_COLUMN_GAP;
    g_object_unref(number);
    PangoLayout *month = typeset_line(context, LABEL, date->month_name);
    typeset_show(cr, month, column_left, DATE_MONTH_BASELINE);
    g_object_unref(month);
    PangoLayout *weekday = typeset_line(context, LABEL, date->weekday_name);
    typeset_show(cr, weekday, column_left, NUMBER_BASELINE);
    g_object_unref(weekday);

    /* the lunar date line under them, and 休 in the corner on a Polish holiday */
    draw_date_line(cr, context, &date->line);
    if (date->line.rest_day) {
        draw_rest_badge(cr, context);
    }
}

static void draw_weather(cairo_t *cr, PangoContext *context, const struct weather_view *weather) {
    /* the icon on the left and the temperature against the right edge */
    icons_draw_weather(cr, weather->icon,
                       (cairo_rectangle_t){WEATHER_ICON_LEFT, WEATHER_ICON_TOP, WEATHER_ICON_SIZE,
                                           WEATHER_ICON_SIZE});
    PangoLayout *temperature =
            shrunk_to_fit(context, (struct shrinkable){"temperature", TABULAR_NUMBER, LINE_SIZE},
                          weather->temperature, TEMPERATURE_MAX_WIDTH);
    show_right_aligned(cr, temperature, WEATHER_RIGHT, NUMBER_BASELINE);
    g_object_unref(temperature);

    /* the line under the icon: a badge or a word, centred on the icon but never past the left */
    double line_right;
    if (weather->line_kind == WEATHER_LINE_BADGE) {
        PangoLayout *label = typeset_line(context, PILL_LABEL, weather->line);
        const double centred = typeset_snap(WEATHER_ICON_CENTRE - pill_width(label) / 2);
        const double left = centred > WEATHER_LEFT ? centred : WEATHER_LEFT;
        draw_pill(cr, label, left);
        line_right = left + pill_width(label);
        g_object_unref(label);
    } else {
        PangoLayout *line = typeset_line(context, TABULAR_LINE, weather->line);
        const double centred = WEATHER_ICON_CENTRE - measure(line).advance / 2;
        const double left = centred > WEATHER_LEFT ? centred : WEATHER_LEFT;
        typeset_show(cr, line, left, LINE_BASELINE);
        line_right = left + measure(line).advance;
        g_object_unref(line);
    }

    /* the day's range after it, shrunk to keep clear of the line */
    if (weather->range[0] != '\0') {
        PangoLayout *range =
                shrunk_to_fit(context, (struct shrinkable){"weather range", RANGE, LINE_SIZE_MIN},
                              weather->range, WEATHER_RIGHT - line_right - RANGE_CLEARANCE);
        show_right_aligned(cr, range, WEATHER_RIGHT, LINE_BASELINE);
        g_object_unref(range);
    }
}

static void draw_counter_number(cairo_t *cr, PangoContext *context, struct counter counter) {
    const double unit_advance = measure(counter.unit).advance;
    PangoLayout *number = shrunk_to_fit(
            context, (struct shrinkable){"counter", NUMBER, LINE_SIZE}, counter.days,
            COUNTER_RIGHT_LIMIT - COUNTER_TEXT_LEFT - COUNTER_UNIT_GAP - unit_advance);
    const struct text_metrics metrics = measure(number);
    const double centred_left = COUNTER_CENTRE_X - (metrics.ink_left + metrics.ink_right) / 2;
    const double pair_width = metrics.advance + COUNTER_UNIT_GAP + unit_advance;
    const double right_most = COUNTER_RIGHT_LIMIT - pair_width;
    const double left = centred_left < right_most ? centred_left : right_most;
    const double baseline = COUNTER_CENTRE_Y - (metrics.ink_top + metrics.ink_bottom) / 2;
    typeset_show(cr, number, left, baseline);
    typeset_show(cr, counter.unit, left + metrics.advance + COUNTER_UNIT_GAP, baseline);
    g_object_unref(number);
}

static void draw_counter_tile(cairo_t *cr, PangoContext *context, struct counter counter,
                              struct greeting greeting) {
    /* a black tile with the counter in white along its top */
    set_ink(cr, INK_BLACK);
    fill_rounded_rect(cr, (cairo_rectangle_t){TILE_LEFT, TILE_TOP, TILE_WIDTH, TILE_HEIGHT},
                      TILE_RADIUS);
    set_ink(cr, INK_WHITE);
    const double unit_advance = measure(counter.unit).advance;
    PangoLayout *number = shrunk_to_fit(
            context, (struct shrinkable){"tile counter", NUMBER, LINE_SIZE}, counter.days,
            TILE_RIGHT_LIMIT - COUNTER_TEXT_LEFT - COUNTER_UNIT_GAP - unit_advance);
    typeset_show(cr, number, COUNTER_TEXT_LEFT, NUMBER_BASELINE);
    typeset_show(cr, counter.unit, COUNTER_TEXT_LEFT + measure(number).advance + COUNTER_UNIT_GAP,
                 NUMBER_BASELINE);
    g_object_unref(number);

    /* the greeting's icon and text along its bottom */
    icons_draw_greeting(cr, greeting.icon, COUNTER_TEXT_LEFT, TILE_ICON_TOP);
    PangoLayout *text =
            shrunk_to_fit(context, (struct shrinkable){"greeting", LABEL, LINE_SIZE_MIN},
                          greeting.text, TILE_RIGHT_LIMIT - GREETING_LEFT);
    typeset_show(cr, text, GREETING_LEFT, LINE_BASELINE);
    g_object_unref(text);
    set_ink(cr, INK_BLACK);
}

static void draw_counter(cairo_t *cr, PangoContext *context, int counter_days,
                         const struct greeting *greeting) {
    char days[16];
    snprintf(days, sizeof days, "%d", counter_days);
    const struct counter counter = {days, typeset_line(context, LABEL, COUNTER_UNIT)};
    if (greeting != nullptr) {
        draw_counter_tile(cr, context, counter, *greeting);
    } else {
        draw_counter_number(cr, context, counter);
    }
    g_object_unref(counter.unit);
}

static bool has_text(const char *text) {
    return text != nullptr && text[0] != '\0';
}

struct draw_result draw(struct drawing *drawing, const struct draw_input *input,
                        struct frame *out) {
    /* a coverage surface: Cairo paints ink into it, and the frame keeps what is half covered */
    cairo_surface_t *surface =
            cairo_image_surface_create(CAIRO_FORMAT_A8, SCREEN_WIDTH, SCREEN_HEIGHT);
    cairo_t *cr = cairo_create(surface);
    PangoContext *context = typeset_context(drawing->fonts, cr);

    /* the wanted phrase, or the one on the glass when the wanted one is too long */
    struct draw_result result = {};
    struct phrase_fit fit = {};
    if (has_text(input->phrase)) {
        fit = typeset_phrase(context, cr, input->phrase);
        result.too_long = !fit.fits;
    }
    if (result.too_long && has_text(input->glass_phrase) &&
        strcmp(input->glass_phrase, input->phrase) != 0) {
        fit = typeset_phrase(context, cr, input->glass_phrase);
        result.drew_glass_phrase = fit.fits;
        if (!fit.fits) {
            log_error("the phrase on the glass no longer fits the screen either; drawing none");
        }
    }
    result.phrase_drawn = fit.fits;
    result.missing_glyphs = fit.fits ? fit.missing_glyphs : 0;

    /* the rule, the two dividers and the three blocks between them */
    cairo_rectangle(cr, RULE_LEFT, RULE_TOP, RULE_WIDTH, LINE_THICKNESS);
    for (size_t i = 0; i < sizeof DIVIDERS / sizeof *DIVIDERS; i++) {
        cairo_rectangle(cr, DIVIDERS[i], DIVIDER_TOP, LINE_THICKNESS, DIVIDER_HEIGHT);
    }
    cairo_fill(cr);
    draw_date(cr, context, &input->date);
    draw_weather(cr, context, &input->weather);
    const struct greeting greeting = {input->greeting_icon, input->greeting};
    draw_counter(cr, context, input->date.counter_days, input->show_greeting ? &greeting : nullptr);

    /* the frame, unless Cairo gave up somewhere along the way */
    const cairo_status_t status = cairo_status(cr);
    result.ok = status == CAIRO_STATUS_SUCCESS;
    if (result.ok) {
        cairo_surface_flush(surface);
        frame_from_a8(cairo_image_surface_get_data(surface),
                      cairo_image_surface_get_stride(surface), out);
    } else {
        log_error("can't draw the screen: %s", cairo_status_to_string(status));
    }
    g_object_unref(context);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return result;
}
