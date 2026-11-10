#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cairo.h>
#include <glib.h>
#include <pango/pango.h>

#include "config.h"
#include "draw/frame.h"
#include "draw/layout.h"
#include "draw/typeset.h"
#include "private_fonts.h"
#include "stderr_capture.h"
#include "unity.h"

#define TWO_LINES "慢慢来，\n比较快"
#define NEITHER_FITS_LINE \
    "<3>the phrase on the glass no longer fits the screen either; drawing none\n"

static const struct text_style LABEL = {FONT_FAMILY_CJK, 26, DIGITS_PROPORTIONAL};
static const struct text_style NUMBER = {FONT_FAMILY_DIGITS, 64, DIGITS_PROPORTIONAL};
static const struct text_style TABULAR_NUMBER = {FONT_FAMILY_DIGITS, 64, DIGITS_TABULAR};

static const char *font_dir;
static struct drawing *drawing;
static PangoFontMap *fonts;

void setUp(void) {
}

void tearDown(void) {
}

struct ink_box {
    int left;
    int top;
    int right;
    int bottom;
    bool found;
};

static struct ink_box ink_in(const struct frame *frame, int x0, int y0, int x1, int y1) {
    struct ink_box box = {.left = x1, .top = y1, .right = x0, .bottom = y0};
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (frame_is_black(frame, x, y)) {
                box.left = MIN(box.left, x);
                box.right = MAX(box.right, x);
                box.top = MIN(box.top, y);
                box.bottom = MAX(box.bottom, y);
            }
        }
    }
    box.found = box.right >= box.left;
    return box;
}

static bool column_has_black(const struct frame *frame, int x, int y0, int y1) {
    for (int y = y0; y <= y1; y++) {
        if (frame_is_black(frame, x, y)) {
            return true;
        }
    }
    return false;
}

static int first_white_x(const struct frame *frame, int x0, int y0, int x1, int y1) {
    for (int x = x0; x <= x1; x++) {
        for (int y = y0; y <= y1; y++) {
            if (!frame_is_black(frame, x, y)) {
                return x;
            }
        }
    }
    return -1;
}

static struct draw_input ordinary_monday(void) {
    return (struct draw_input){
            .phrase = TWO_LINES,
            .date = {.ok = true,
                     .today = {2026, 10, 26},
                     .month_name = "十月",
                     .weekday_name = "周一",
                     .line = {.lunar = "农历九月十七"},
                     .counter_days = 1881},
            .weather = {.icon = WEATHER_ICON_RAIN,
                        .temperature = "8°",
                        .range = "7~10°",
                        .line = "小雨",
                        .line_kind = WEATHER_LINE_WORD},
    };
}

static struct draw_result render(const struct draw_input *input, struct frame *out) {
    const struct draw_result result = draw(drawing, input, out);
    TEST_ASSERT_TRUE(result.ok);
    return result;
}

static gchar *repeated(const char *unit, int times) {
    GString *text = g_string_new(nullptr);
    for (int i = 0; i < times; i++) {
        g_string_append(text, unit);
    }
    return g_string_free(text, false);
}

static void test_rule_and_dividers(void) {
    const struct draw_input input = ordinary_monday();
    struct frame frame;
    const struct draw_result result = render(&input, &frame);
    TEST_ASSERT_TRUE(result.phrase_drawn);
    TEST_ASSERT_FALSE(result.too_long);
    TEST_ASSERT_FALSE(frame_is_black(&frame, 400, 335));
    TEST_ASSERT_TRUE(frame_is_black(&frame, 400, 336));
    TEST_ASSERT_TRUE(frame_is_black(&frame, 400, 337));
    TEST_ASSERT_FALSE(frame_is_black(&frame, 400, 338));
    TEST_ASSERT_TRUE(frame_is_black(&frame, 32, 336));
    TEST_ASSERT_TRUE(frame_is_black(&frame, 767, 336));
    TEST_ASSERT_FALSE(frame_is_black(&frame, 31, 336));
    TEST_ASSERT_FALSE(frame_is_black(&frame, 768, 336));
    TEST_ASSERT_FALSE(frame_is_black(&frame, 275, 400));
    TEST_ASSERT_TRUE(frame_is_black(&frame, 276, 400));
    TEST_ASSERT_TRUE(frame_is_black(&frame, 277, 400));
    TEST_ASSERT_FALSE(frame_is_black(&frame, 278, 400));
    TEST_ASSERT_TRUE(frame_is_black(&frame, 522, 356));
    TEST_ASSERT_TRUE(frame_is_black(&frame, 523, 455));
    TEST_ASSERT_FALSE(frame_is_black(&frame, 522, 355));
    TEST_ASSERT_FALSE(frame_is_black(&frame, 522, 456));
}

static void test_no_phrase_leaves_the_top_white(void) {
    struct draw_input input = ordinary_monday();
    input.phrase = nullptr;
    struct frame frame;
    struct draw_result result = render(&input, &frame);
    TEST_ASSERT_FALSE(result.phrase_drawn);
    TEST_ASSERT_FALSE(result.too_long);
    TEST_ASSERT_FALSE(ink_in(&frame, 0, 0, SCREEN_WIDTH - 1, 335).found);
    TEST_ASSERT_TRUE(frame_is_black(&frame, 400, 336));
    input.phrase = "";
    struct frame empty_frame;
    result = render(&input, &empty_frame);
    TEST_ASSERT_FALSE(result.phrase_drawn);
    TEST_ASSERT_TRUE(frame_equal(&frame, &empty_frame));
}

static PangoContext *scratch_context(cairo_surface_t **surface, cairo_t **cr) {
    *surface = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    *cr = cairo_create(*surface);
    return typeset_context(fonts, *cr);
}

static void drop_scratch(PangoContext *context, cairo_surface_t *surface, cairo_t *cr) {
    g_object_unref(context);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

static void test_normal_counter_centred(void) {
    const struct draw_input input = ordinary_monday();
    struct frame frame;
    render(&input, &frame);
    cairo_surface_t *surface;
    cairo_t *cr;
    PangoContext *context = scratch_context(&surface, &cr);
    PangoLayout *layout = typeset_line(context, NUMBER, "1881");
    PangoRectangle ink;
    PangoRectangle logical;
    pango_layout_get_pixel_extents(layout, &ink, &logical);
    g_object_unref(layout);
    drop_scratch(context, surface, cr);

    const struct ink_box all = ink_in(&frame, 536, 356, 767, 455);
    TEST_ASSERT_TRUE(all.found);
    const struct ink_box number = ink_in(&frame, all.left, 356, all.left + ink.width - 1, 455);
    TEST_ASSERT_INT_WITHIN(3, 652, (number.left + number.right + 1) / 2);
    TEST_ASSERT_INT_WITHIN(2, 406, (number.top + number.bottom + 1) / 2);
    int tian_left = number.right + 1;
    while (!column_has_black(&frame, tian_left, 356, 455)) {
        tian_left++;
    }
    TEST_ASSERT_INT_WITHIN(3, number.left - ink.x + logical.width + 10, tian_left);
    TEST_ASSERT_LESS_OR_EQUAL_INT(762, all.right + 1);
}

struct glyph_box {
    int advance;
    int width;
    int height;
};

static struct glyph_box number_ink(double size, const char *days) {
    cairo_surface_t *surface =
            cairo_image_surface_create(CAIRO_FORMAT_A8, SCREEN_WIDTH, SCREEN_HEIGHT);
    cairo_t *cr = cairo_create(surface);
    PangoContext *context = typeset_context(fonts, cr);
    PangoLayout *layout = typeset_line(
            context, (struct text_style){FONT_FAMILY_DIGITS, size, DIGITS_PROPORTIONAL}, days);
    PangoRectangle logical;
    pango_layout_get_pixel_extents(layout, nullptr, &logical);
    typeset_show(cr, layout, 100, 300);
    cairo_surface_flush(surface);
    struct frame frame; // the ink that survives the half-coverage cut
    frame_from_a8(cairo_image_surface_get_data(surface), cairo_image_surface_get_stride(surface),
                  &frame);
    const struct ink_box box = ink_in(&frame, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
    g_object_unref(layout);
    drop_scratch(context, surface, cr);
    return (struct glyph_box){logical.width, box.right - box.left + 1, box.bottom - box.top + 1};
}

static int advance_px(struct text_style style, const char *text) {
    cairo_surface_t *surface;
    cairo_t *cr;
    PangoContext *context = scratch_context(&surface, &cr);
    PangoLayout *layout = typeset_line(context, style, text);
    PangoRectangle logical;
    pango_layout_get_pixel_extents(layout, nullptr, &logical);
    g_object_unref(layout);
    drop_scratch(context, surface, cr);
    return logical.width;
}

static void assert_counter_drawn_at(int counter_days, double size) {
    struct draw_input input = ordinary_monday();
    input.date.counter_days = counter_days;
    struct frame frame;
    render(&input, &frame);
    char days[16];
    snprintf(days, sizeof days, "%d", counter_days);
    const struct glyph_box expected = number_ink(size, days);
    const struct ink_box all = ink_in(&frame, 536, 356, 767, 455);
    TEST_ASSERT_TRUE_MESSAGE(all.found, days);
    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(548, all.left, days);
    TEST_ASSERT_LESS_OR_EQUAL_INT_MESSAGE(762, all.right + 1, days);
    const struct ink_box number = ink_in(&frame, all.left, 356, all.left + expected.width - 1, 455);
    TEST_ASSERT_EQUAL_INT_MESSAGE(expected.height, number.bottom - number.top + 1, days);
}

static void test_four_digit_counters_keep_64px(void) {
    assert_counter_drawn_at(1882, 64);
    assert_counter_drawn_at(2000, 64);
    assert_counter_drawn_at(8888, 64);
}

static void test_five_digit_counter_shrinks(void) {
    const int unit_advance = advance_px(LABEL, "天");
    double size = 64;
    while (number_ink(size, "10001").advance + 10 + unit_advance > 762 - 548) {
        size -= 1;
    }
    TEST_ASSERT_LESS_THAN_INT(64, (int)size);
    assert_counter_drawn_at(10001, size);
}

static void test_range_clears_badge(void) {
    struct draw_input input = ordinary_monday();
    input.weather = (struct weather_view){.icon = WEATHER_ICON_CLOUD,
                                          .temperature = "−4°",
                                          .range = "−21~−12°",
                                          .line = "严寒预警",
                                          .line_kind = WEATHER_LINE_BADGE};
    struct frame frame;
    render(&input, &frame);
    TEST_ASSERT_TRUE(frame_is_black(&frame, 286, 443));
    TEST_ASSERT_TRUE(frame_is_black(&frame, 393, 443));
    TEST_ASSERT_FALSE(frame_is_black(&frame, 394, 443));
    for (int x = 394; x < 402; x++) {
        TEST_ASSERT_FALSE_MESSAGE(column_has_black(&frame, x, 428, 457), "the 8 px after the pill");
    }
    const struct ink_box range = ink_in(&frame, 402, 428, 520, 460);
    TEST_ASSERT_TRUE(range.found);
    TEST_ASSERT_INT_WITHIN(3, 508, range.right + 1);
}

static void test_weather_digits_tabular(void) {
    struct draw_input input = ordinary_monday();
    input.weather = (struct weather_view){.icon = WEATHER_ICON_SUN,
                                          .temperature = "11°",
                                          .range = "6~15°",
                                          .line = "晴",
                                          .line_kind = WEATHER_LINE_WORD};
    struct frame frame;
    render(&input, &frame);
    cairo_surface_t *surface;
    cairo_t *cr;
    PangoContext *context = scratch_context(&surface, &cr);
    PangoRectangle tabular;
    PangoRectangle proportional;
    PangoLayout *layout = typeset_line(context, TABULAR_NUMBER, "11°");
    pango_layout_get_pixel_extents(layout, &tabular, nullptr);
    g_object_unref(layout);
    layout = typeset_line(context, NUMBER, "11°");
    pango_layout_get_pixel_extents(layout, &proportional, nullptr);
    g_object_unref(layout);
    drop_scratch(context, surface, cr);
    TEST_ASSERT_NOT_EQUAL_INT(proportional.width, tabular.width);
    const struct ink_box temperature = ink_in(&frame, 353, 356, 520, 424);
    TEST_ASSERT_TRUE(temperature.found);
    TEST_ASSERT_INT_WITHIN(1, tabular.width, temperature.right - temperature.left + 1);
}

static void test_special_counter_tile(void) {
    struct draw_input input = ordinary_monday();
    input.show_greeting = true;
    input.greeting_icon = GREETING_CAKE;
    input.greeting = "生日快乐";
    struct frame frame;
    render(&input, &frame);
    TEST_ASSERT_TRUE(frame_is_black(&frame, 540, 356));
    TEST_ASSERT_TRUE(frame_is_black(&frame, 763, 455));
    TEST_ASSERT_FALSE(frame_is_black(&frame, 535, 400));
    TEST_ASSERT_FALSE(frame_is_black(&frame, 652, 351));
    TEST_ASSERT_NOT_EQUAL_INT(-1, first_white_x(&frame, 582, 428, 755, 457));
    TEST_ASSERT_NOT_EQUAL_INT(-1, first_white_x(&frame, 548, 428, 573, 453));
    TEST_ASSERT_NOT_EQUAL_INT(-1, first_white_x(&frame, 548, 366, 700, 414));
}

static void test_greeting_tile_only_when_shown(void) {
    const struct draw_input plain = ordinary_monday();
    struct frame plain_frame;
    render(&plain, &plain_frame);
    struct draw_input input = plain;
    input.greeting_icon = GREETING_CAKE;
    input.greeting = "生日快乐";
    struct frame hidden_frame;
    render(&input, &hidden_frame);
    TEST_ASSERT_FALSE(frame_is_black(&hidden_frame, 540, 356));
    TEST_ASSERT_TRUE(frame_equal(&plain_frame, &hidden_frame));
    input.show_greeting = true;
    struct frame shown_frame;
    render(&input, &shown_frame);
    TEST_ASSERT_TRUE(frame_is_black(&shown_frame, 540, 356));
    TEST_ASSERT_FALSE(frame_equal(&plain_frame, &shown_frame));
}

static void test_badge_clamped(void) {
    struct draw_input input = ordinary_monday();
    input.weather = (struct weather_view){.icon = WEATHER_ICON_CLOUD,
                                          .temperature = "5°",
                                          .range = "2~6°",
                                          .line = "大风预警",
                                          .line_kind = WEATHER_LINE_BADGE};
    struct frame frame;
    render(&input, &frame);
    TEST_ASSERT_FALSE(frame_is_black(&frame, 285, 443));
    TEST_ASSERT_TRUE(frame_is_black(&frame, 286, 443));
    TEST_ASSERT_TRUE(frame_is_black(&frame, 300, 428));
    TEST_ASSERT_TRUE(frame_is_black(&frame, 300, 457));
    TEST_ASSERT_FALSE(frame_is_black(&frame, 300, 458));
    TEST_ASSERT_GREATER_OR_EQUAL_INT(294, first_white_x(&frame, 288, 438, 400, 448));
}

static void test_minus_17_shrinks(void) {
    struct draw_input input = ordinary_monday();
    input.weather = (struct weather_view){.icon = WEATHER_ICON_SUN,
                                          .temperature = "−17°",
                                          .range = "−21~−12°",
                                          .line = "晴",
                                          .line_kind = WEATHER_LINE_WORD};
    struct frame frame;
    render(&input, &frame);
    const struct ink_box temperature = ink_in(&frame, 353, 356, 520, 424);
    TEST_ASSERT_TRUE(temperature.found);
    TEST_ASSERT_INT_WITHIN(3, 508, temperature.right + 1);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(358, temperature.left);
    const struct ink_box range = ink_in(&frame, 353, 430, 520, 460);
    TEST_ASSERT_TRUE(range.found);
    TEST_ASSERT_INT_WITHIN(3, 508, range.right + 1);
}

static void test_rest_day_badge(void) {
    struct draw_input input = ordinary_monday();
    input.date.today = (struct civil_date){2026, 11, 11};
    input.date.month_name = "十一月";
    input.date.weekday_name = "周三";
    input.date.line = (struct date_line){.prefix = "独立日", .lunar = "十月初三", .rest_day = true};
    struct frame frame;
    render(&input, &frame);
    TEST_ASSERT_TRUE(frame_is_black(&frame, 224, 379));
    TEST_ASSERT_TRUE(frame_is_black(&frame, 258, 379));
    TEST_ASSERT_FALSE(frame_is_black(&frame, 221, 379));
    TEST_ASSERT_FALSE(frame_is_black(&frame, 261, 379));
    TEST_ASSERT_FALSE(frame_is_black(&frame, 241, 359));
    TEST_ASSERT_FALSE(frame_is_black(&frame, 241, 398));
    TEST_ASSERT_NOT_EQUAL_INT(-1, first_white_x(&frame, 228, 368, 254, 392));
}

static void test_too_long_draws_glass_phrase(void) {
    gchar *wall = repeated("永", 400);
    struct draw_input input = ordinary_monday();
    input.phrase = wall;
    input.glass_phrase = TWO_LINES;
    struct frame frame;
    const struct draw_result result = render(&input, &frame);
    g_free(wall);
    TEST_ASSERT_TRUE(result.too_long);
    TEST_ASSERT_TRUE(result.drew_glass_phrase);
    TEST_ASSERT_TRUE(result.phrase_drawn);
    TEST_ASSERT_EQUAL_INT(0, result.missing_glyphs);
    const struct draw_input as_wanted = ordinary_monday();
    struct frame wanted_frame;
    render(&as_wanted, &wanted_frame);
    TEST_ASSERT_TRUE(frame_equal(&wanted_frame, &frame));
}

static void test_too_long_same_as_glass_draws_no_phrase(void) {
    gchar *wall = repeated("永", 400);
    struct draw_input input = ordinary_monday();
    input.phrase = wall;
    input.glass_phrase = wall;
    struct frame frame;
    char logged[256];
    start_capture();
    const struct draw_result result = render(&input, &frame);
    finish_capture(logged, sizeof logged);
    g_free(wall);
    TEST_ASSERT_TRUE(result.too_long);
    TEST_ASSERT_FALSE(result.drew_glass_phrase);
    TEST_ASSERT_FALSE(result.phrase_drawn);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", logged,
                                     "the same phrase isn't tried, so nothing is logged");
    struct draw_input none = ordinary_monday();
    none.phrase = nullptr;
    struct frame none_frame;
    render(&none, &none_frame);
    TEST_ASSERT_TRUE(frame_equal(&none_frame, &frame));
}

static void test_neither_fits_draws_no_phrase(void) {
    gchar *wall = repeated("永", 400);
    gchar *other_wall = repeated("安", 400);
    struct draw_input input = ordinary_monday();
    input.phrase = wall;
    input.glass_phrase = other_wall;
    struct frame frame;
    char logged[256];
    start_capture();
    const struct draw_result result = render(&input, &frame);
    finish_capture(logged, sizeof logged);
    g_free(other_wall);
    g_free(wall);
    TEST_ASSERT_TRUE(result.too_long);
    TEST_ASSERT_FALSE(result.drew_glass_phrase);
    TEST_ASSERT_FALSE(result.phrase_drawn);
    TEST_ASSERT_EQUAL_INT(0, result.missing_glyphs);
    TEST_ASSERT_EQUAL_STRING(NEITHER_FITS_LINE, logged);
    TEST_ASSERT_FALSE(ink_in(&frame, 0, 0, SCREEN_WIDTH - 1, 335).found);
    TEST_ASSERT_TRUE(frame_is_black(&frame, 400, 336));
}

static void test_no_glass_phrase_draws_no_phrase(void) {
    gchar *wall = repeated("永", 400);
    struct draw_input input = ordinary_monday();
    input.phrase = wall;
    struct frame frame;
    char logged[256];
    start_capture();
    const struct draw_result result = render(&input, &frame);
    finish_capture(logged, sizeof logged);
    g_free(wall);
    TEST_ASSERT_TRUE(result.too_long);
    TEST_ASSERT_FALSE(result.drew_glass_phrase);
    TEST_ASSERT_FALSE(result.phrase_drawn);
    TEST_ASSERT_FALSE(ink_in(&frame, 0, 0, SCREEN_WIDTH - 1, 335).found);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", logged, "no glass phrase to try, so nothing is logged");
}

static void test_missing_glyphs_counted(void) {
    struct draw_input input = ordinary_monday();
    input.phrase = "今天也很可爱🐱";
    struct frame frame;
    struct draw_result result = render(&input, &frame);
    TEST_ASSERT_TRUE(result.phrase_drawn);
    TEST_ASSERT_EQUAL_INT(1, result.missing_glyphs);
    input.phrase = "结婚1周年 𠮷";
    result = render(&input, &frame);
    TEST_ASSERT_EQUAL_INT(0, result.missing_glyphs);

    gchar *wall = repeated("永", 400);
    input.phrase = wall;
    input.glass_phrase = "晚安😀";
    result = render(&input, &frame);
    g_free(wall);
    TEST_ASSERT_TRUE(result.drew_glass_phrase);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, result.missing_glyphs, "counted for the phrase drawn");
}

static void assert_open_fails_with_only(const char *file) {
    gchar *dir = g_dir_make_tmp("kotetsu-fonts-XXXXXX", nullptr);
    TEST_ASSERT_NOT_NULL(dir);
    gchar *source = g_build_filename(font_dir, file, nullptr);
    gchar *link = g_build_filename(dir, file, nullptr);
    TEST_ASSERT_EQUAL_INT(0, symlink(source, link));
    struct drawing *opened = draw_open(dir);
    TEST_ASSERT_EQUAL_INT(0, unlink(link));
    TEST_ASSERT_EQUAL_INT(0, rmdir(dir));
    TEST_ASSERT_NULL(opened);
    g_free(link);
    g_free(source);
    g_free(dir);
}

static void test_open_needs_both_font_files(void) {
    assert_open_fails_with_only(FONT_FILE_CJK);
    assert_open_fails_with_only(FONT_FILE_DIGITS);
    TEST_ASSERT_NULL(draw_open("/nonexistent"));
}

int main(void) {
    font_dir = getenv("KOTETSU_FONT_DIR");
    fonts = private_fonts(font_dir);
    if (fonts == nullptr) {
        return skip_without_fonts();
    }
    drawing = draw_open(font_dir);
    if (drawing == nullptr) {
        puts("draw_open failed with the fonts present");
        return EXIT_FAILURE;
    }
    UNITY_BEGIN();
    RUN_TEST(test_rule_and_dividers);
    RUN_TEST(test_no_phrase_leaves_the_top_white);
    RUN_TEST(test_normal_counter_centred);
    RUN_TEST(test_four_digit_counters_keep_64px);
    RUN_TEST(test_five_digit_counter_shrinks);
    RUN_TEST(test_range_clears_badge);
    RUN_TEST(test_weather_digits_tabular);
    RUN_TEST(test_special_counter_tile);
    RUN_TEST(test_greeting_tile_only_when_shown);
    RUN_TEST(test_badge_clamped);
    RUN_TEST(test_minus_17_shrinks);
    RUN_TEST(test_rest_day_badge);
    RUN_TEST(test_too_long_draws_glass_phrase);
    RUN_TEST(test_too_long_same_as_glass_draws_no_phrase);
    RUN_TEST(test_neither_fits_draws_no_phrase);
    RUN_TEST(test_no_glass_phrase_draws_no_phrase);
    RUN_TEST(test_missing_glyphs_counted);
    RUN_TEST(test_open_needs_both_font_files);
    draw_close(drawing);
    g_object_unref(fonts);
    return UNITY_END();
}
