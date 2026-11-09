#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <cairo.h>
#include <fontconfig/fontconfig.h>
#include <glib.h>
#include <pango/pango.h>
#include <pango/pangofc-font.h>

#include "config.h"
#include "draw/frame.h"
#include "draw/typeset.h"
#include "private_fonts.h"
#include "stderr_capture.h"
#include "unity.h"

static constexpr int PHRASE_LEFT = 32;
static constexpr int PHRASE_TOP = 24;
static constexpr int PHRASE_RIGHT = 767;
static constexpr int PHRASE_BOTTOM = 323;
static constexpr int MAX_BANDS = 8;

static const struct text_style LABEL = {FONT_FAMILY_CJK, 26, DIGITS_PROPORTIONAL};
static const struct text_style BIG_LABEL = {FONT_FAMILY_CJK, 64, DIGITS_PROPORTIONAL};
static const struct text_style NUMBER = {FONT_FAMILY_DIGITS, 64, DIGITS_PROPORTIONAL};
static const struct text_style TABULAR_NUMBER = {FONT_FAMILY_DIGITS, 64, DIGITS_TABULAR};

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

static struct ink_box ink_anywhere(const struct frame *frame) {
    return ink_in(frame, 0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
}

static int ink_width(const struct ink_box box) {
    return box.found ? box.right - box.left + 1 : 0;
}

static int ink_height(const struct ink_box box) {
    return box.found ? box.bottom - box.top + 1 : 0;
}

static int ink_bands(const struct frame *frame, struct ink_box *bands) {
    int count = 0;
    bool inside = false;
    for (int y = 0; y <= SCREEN_HEIGHT; y++) {
        bool inked = false;
        for (int x = 0; x < SCREEN_WIDTH && y < SCREEN_HEIGHT && !inked; x++) {
            inked = frame_is_black(frame, x, y);
        }
        if (inked && !inside) {
            TEST_ASSERT_LESS_THAN_INT(MAX_BANDS, count);
            bands[count].top = y;
            inside = true;
        } else if (!inked && inside) {
            bands[count] = ink_in(frame, 0, bands[count].top, SCREEN_WIDTH - 1, y - 1);
            count++;
            inside = false;
        }
    }
    return count;
}

static void render_on(cairo_surface_t **surface, cairo_t **cr, PangoContext **context) {
    *surface = cairo_image_surface_create(CAIRO_FORMAT_A8, SCREEN_WIDTH, SCREEN_HEIGHT);
    *cr = cairo_create(*surface);
    *context = typeset_context(fonts, *cr);
}

static void frame_of(cairo_surface_t *surface, cairo_t *cr, PangoContext *context,
                     struct frame *out) {
    TEST_ASSERT_EQUAL_INT(CAIRO_STATUS_SUCCESS, cairo_status(cr));
    cairo_surface_flush(surface);
    frame_from_a8(cairo_image_surface_get_data(surface), cairo_image_surface_get_stride(surface),
                  out);
    g_object_unref(context);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

static struct phrase_fit render_phrase(const char *text, struct frame *out) {
    cairo_surface_t *surface;
    cairo_t *cr;
    PangoContext *context;
    render_on(&surface, &cr, &context);
    const struct phrase_fit fit = typeset_phrase(context, cr, text);
    frame_of(surface, cr, context, out);
    return fit;
}

static struct ink_box line_ink(const char *text, double size) {
    cairo_surface_t *surface;
    cairo_t *cr;
    PangoContext *context;
    render_on(&surface, &cr, &context);
    PangoLayout *layout = typeset_line(
            context, (struct text_style){FONT_FAMILY_CJK, size, DIGITS_PROPORTIONAL}, text);
    typeset_show(cr, layout, PHRASE_LEFT, 240);
    g_object_unref(layout);
    struct frame frame;
    frame_of(surface, cr, context, &frame); // the ink that survives the half-coverage cut
    return ink_anywhere(&frame);
}

static void assert_line(struct ink_box band, const char *text, double size) {
    const struct ink_box reference = line_ink(text, size);
    TEST_ASSERT_TRUE_MESSAGE(reference.found, text);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ink_width(reference), ink_width(band), text);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ink_height(reference), ink_height(band), text);
}

static gchar *repeated(const char *unit, int times, const char *separator) {
    GString *text = g_string_new(nullptr);
    for (int i = 0; i < times; i++) {
        if (i > 0 && separator != nullptr) {
            g_string_append(text, separator);
        }
        g_string_append(text, unit);
    }
    return g_string_free(text, false);
}

static void test_six_han_fit_120(void) {
    struct frame frame;
    const struct phrase_fit fit = render_phrase("一二三四五六", &frame);
    TEST_ASSERT_TRUE(fit.fits);
    TEST_ASSERT_EQUAL_INT(0, fit.missing_glyphs);
    struct ink_box bands[MAX_BANDS];
    TEST_ASSERT_EQUAL_INT(1, ink_bands(&frame, bands));
    assert_line(bands[0], "一二三四五六", 120);
    const struct ink_box box = bands[0];
    TEST_ASSERT_GREATER_OR_EQUAL_INT(PHRASE_LEFT, box.left);
    TEST_ASSERT_LESS_OR_EQUAL_INT(PHRASE_RIGHT, box.right);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(PHRASE_TOP, box.top);
    TEST_ASSERT_LESS_OR_EQUAL_INT(PHRASE_BOTTOM, box.bottom);
    TEST_ASSERT_INT_WITHIN(4, 400, (box.left + box.right + 1) / 2);
    TEST_ASSERT_INT_WITHIN(8, (PHRASE_TOP + PHRASE_BOTTOM + 1) / 2, (box.top + box.bottom + 1) / 2);
}

static void test_poem_drops_to_32(void) {
    gchar *poem = repeated("一二三四五六七八九十甲乙丙", 6, "\n");
    struct frame frame;
    const struct phrase_fit fit = render_phrase(poem, &frame);
    g_free(poem);
    TEST_ASSERT_TRUE(fit.fits);
    struct ink_box bands[MAX_BANDS];
    TEST_ASSERT_EQUAL_INT(6, ink_bands(&frame, bands));
    assert_line(bands[0], "一二三四五六七八九十甲乙丙", 32);
    assert_line(bands[5], "一二三四五六七八九十甲乙丙", 32);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(PHRASE_TOP, bands[0].top);
    TEST_ASSERT_LESS_OR_EQUAL_INT(PHRASE_BOTTOM, bands[5].bottom);
}

static void test_too_long_reports_not_fit(void) {
    gchar *wall = repeated("永", 400, nullptr);
    struct frame frame;
    const struct phrase_fit fit = render_phrase(wall, &frame);
    g_free(wall);
    TEST_ASSERT_FALSE(fit.fits);
    TEST_ASSERT_FALSE(ink_anywhere(&frame).found);
}

static void test_missing_glyph_is_drawn_and_reported(void) {
    struct frame frame;
    const struct phrase_fit fit = render_phrase("晚安😀", &frame);
    TEST_ASSERT_TRUE(fit.fits);
    TEST_ASSERT_EQUAL_INT(1, fit.missing_glyphs);
    TEST_ASSERT_TRUE(ink_anywhere(&frame).found);
}

static void test_punctuation_break_preferred(void) {
    struct frame frame;
    const struct phrase_fit fit = render_phrase("今天天气很好，我们去公园散步吧", &frame);
    TEST_ASSERT_TRUE(fit.fits);
    struct ink_box bands[MAX_BANDS];
    TEST_ASSERT_EQUAL_INT(2, ink_bands(&frame, bands));
    assert_line(bands[0], "今天天气很好，", 92);
    assert_line(bands[1], "我们去公园散步吧", 92);
}

static void test_break_stays_without_nearby_punctuation(void) {
    struct frame frame;
    const struct phrase_fit fit = render_phrase("今天，天气很好我们去公园散步吧", &frame);
    TEST_ASSERT_TRUE(fit.fits);
    struct ink_box bands[MAX_BANDS];
    TEST_ASSERT_EQUAL_INT(2, ink_bands(&frame, bands));
    assert_line(bands[0], "今天，天气很好我", 92);
    assert_line(bands[1], "们去公园散步吧", 92);
}

static void test_space_after_punctuation_is_dropped(void) {
    struct frame frame;
    const struct phrase_fit fit = render_phrase("天地人， 水1火", &frame);
    TEST_ASSERT_TRUE(fit.fits);
    struct ink_box bands[MAX_BANDS];
    TEST_ASSERT_EQUAL_INT(2, ink_bands(&frame, bands));
    assert_line(bands[0], "天地人，", 100);
    TEST_ASSERT_INT_WITHIN(1, 400, (bands[1].left + bands[1].right + 1) / 2);
}

static void assert_same_frame(const char *automatic, const char *typed) {
    struct frame automatic_frame;
    struct frame typed_frame;
    const struct phrase_fit automatic_fit = render_phrase(automatic, &automatic_frame);
    const struct phrase_fit typed_fit = render_phrase(typed, &typed_frame);
    TEST_ASSERT_TRUE_MESSAGE(automatic_fit.fits, automatic);
    TEST_ASSERT_TRUE_MESSAGE(typed_fit.fits, typed);
    TEST_ASSERT_TRUE_MESSAGE(frame_equal(&automatic_frame, &typed_frame), automatic);
}

static void test_automatic_break_matches_typed_break(void) {
    assert_same_frame("今天天气很好，我们去公园散步吧", "今天天气很好，\n我们去公园散步吧");
    assert_same_frame("一二三四五六！七八九十甲乙丙丁", "一二三四五六！\n七八九十甲乙丙丁");
    assert_same_frame("一二三四五六， 2十甲乙丙丁戊己庚", "一二三四五六，\n2十甲乙丙丁戊己庚");
}

static void test_english_word_not_split(void) {
    struct frame frame;
    const struct phrase_fit fit = render_phrase("Congratulations!", &frame);
    TEST_ASSERT_TRUE(fit.fits);
    struct ink_box bands[MAX_BANDS];
    TEST_ASSERT_EQUAL_INT(1, ink_bands(&frame, bands));
    assert_line(bands[0], "Congratulations!", 92);
}

static void test_trailing_punctuation_shift(void) {
    struct frame with_stop;
    const struct phrase_fit stop_fit = render_phrase("一二三。", &with_stop);
    struct frame plain;
    const struct phrase_fit plain_fit = render_phrase("一二三四", &plain);
    TEST_ASSERT_TRUE(stop_fit.fits);
    TEST_ASSERT_TRUE(plain_fit.fits);
    const struct ink_box stop_box = ink_anywhere(&with_stop);
    const struct ink_box plain_box = ink_anywhere(&plain);
    assert_line(stop_box, "一二三。", 128);
    assert_line(plain_box, "一二三四", 128);
    TEST_ASSERT_INT_WITHIN(2, 32, stop_box.left - plain_box.left);
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

static int advance_units(PangoContext *context, struct text_style style, const char *text) {
    PangoLayout *layout = typeset_line(context, style, text);
    PangoRectangle logical;
    pango_layout_get_extents(layout, nullptr, &logical);
    g_object_unref(layout);
    return logical.width;
}

static void test_digit_gap_is_quarter_em(void) {
    cairo_surface_t *surface;
    cairo_t *cr;
    PangoContext *context = scratch_context(&surface, &cr);
    const double sizes[] = {64, 26};
    for (size_t i = 0; i < G_N_ELEMENTS(sizes); i++) {
        const struct text_style style = {FONT_FAMILY_CJK, sizes[i], DIGITS_PROPORTIONAL};
        const int gap = advance_units(context, style, "1天") - advance_units(context, style, "1") -
                        advance_units(context, style, "天");
        TEST_ASSERT_INT_WITHIN(PANGO_SCALE / 2, (int)(sizes[i] / 4 * PANGO_SCALE), gap);
    }
    TEST_ASSERT_EQUAL_INT(advance_units(context, LABEL, "1 天"),
                          advance_units(context, LABEL, "1") +
                                  advance_units(context, LABEL, " 天"));
    drop_scratch(context, surface, cr);
}

static int gaps_in(PangoContext *context, const char *text) {
    int stretches = 0;
    for (const char *cursor = text; *cursor != '\0';) {
        const char *start = cursor;
        const bool digits = g_unichar_isdigit(g_utf8_get_char(cursor));
        do {
            cursor = g_utf8_next_char(cursor);
        } while (*cursor != '\0' && g_unichar_isdigit(g_utf8_get_char(cursor)) == digits);
        gchar *stretch = g_strndup(start, (gsize)(cursor - start));
        stretches += advance_units(context, BIG_LABEL, stretch);
        g_free(stretch);
    }
    const int gap = (int)(BIG_LABEL.size_px / 4 * PANGO_SCALE);
    const int extra = advance_units(context, BIG_LABEL, text) - stretches; // what the gaps add
    TEST_ASSERT_INT_WITHIN_MESSAGE(PANGO_SCALE / 2, (extra + gap / 2) / gap * gap, extra, text);
    return (extra + gap / 2) / gap;
}

static void test_gaps_only_between_digits_and_han(void) {
    cairo_surface_t *surface;
    cairo_t *cr;
    PangoContext *context = scratch_context(&surface, &cr);
    TEST_ASSERT_EQUAL_INT(2, gaps_in(context, "结婚1周年"));
    TEST_ASSERT_EQUAL_INT(1, gaps_in(context, "520快乐"));
    TEST_ASSERT_EQUAL_INT(1, gaps_in(context, "14:00更新"));
    TEST_ASSERT_EQUAL_INT(0, gaps_in(context, "6 周年"));
    TEST_ASSERT_EQUAL_INT(0, gaps_in(context, "abc中文"));
    TEST_ASSERT_EQUAL_INT(1, gaps_in(context, "1𠮷"));
    TEST_ASSERT_EQUAL_INT(1, gaps_in(context, "𠮷2"));
    TEST_ASSERT_EQUAL_INT(2, gaps_in(context, "1年2"));
    drop_scratch(context, surface, cr);
}

static const char *run_font_file(PangoLayout *layout, const char *needle) {
    const char *text = pango_layout_get_text(layout);
    const char *found = strstr(text, needle);
    TEST_ASSERT_NOT_NULL(found);
    const int offset = (int)(found - text);
    for (GSList *runs = pango_layout_get_line_readonly(layout, 0)->runs; runs != nullptr;
         runs = runs->next) {
        const PangoItem *item = ((PangoGlyphItem *)runs->data)->item;
        if (offset >= item->offset && offset < item->offset + item->length) {
            FcChar8 *file = nullptr;
            TEST_ASSERT_EQUAL_INT(FcResultMatch,
                                  FcPatternGetString(pango_fc_font_get_pattern(
                                                             PANGO_FC_FONT(item->analysis.font)),
                                                     FC_FILE, 0, &file));
            return g_path_get_basename((const char *)file);
        }
    }
    TEST_FAIL_MESSAGE(needle);
    return nullptr;
}

static void assert_run_file(PangoLayout *layout, const char *needle, const char *file) {
    const char *found = run_font_file(layout, needle);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(file, found, needle);
    g_free((char *)found);
}

static void test_digit_runs_use_inter(void) {
    cairo_surface_t *surface;
    cairo_t *cr;
    PangoContext *context = scratch_context(&surface, &cr);
    PangoLayout *layout = typeset_line(context, LABEL, "结婚1周年 520");
    assert_run_file(layout, "1", FONT_FILE_DIGITS);
    assert_run_file(layout, "520", FONT_FILE_DIGITS);
    assert_run_file(layout, "周", FONT_FILE_CJK);
    assert_run_file(layout, " ", FONT_FILE_CJK);
    g_object_unref(layout);
    drop_scratch(context, surface, cr);
}

static void test_tabular_figures(void) {
    cairo_surface_t *surface;
    cairo_t *cr;
    PangoContext *context = scratch_context(&surface, &cr);
    TEST_ASSERT_EQUAL_INT(advance_units(context, TABULAR_NUMBER, "11"),
                          advance_units(context, TABULAR_NUMBER, "00"));
    TEST_ASSERT_LESS_THAN_INT(advance_units(context, NUMBER, "00"),
                              advance_units(context, NUMBER, "11"));
    drop_scratch(context, surface, cr);
}

static void test_text_that_is_not_utf8_is_logged_and_still_laid_out(void) {
    cairo_surface_t *surface;
    cairo_t *cr;
    PangoContext *context = scratch_context(&surface, &cr);
    char captured[1024];
    start_capture();
    PangoLayout *layout = typeset_line(context, LABEL, "1\xe5\x86");
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_NOT_NULL(layout);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(captured, "<3>can't typeset text that isn't UTF-8"),
                                 captured);
    g_object_unref(layout);
    drop_scratch(context, surface, cr);
}

int main(void) {
    fonts = private_fonts(getenv("KOTETSU_FONT_DIR"));
    if (fonts == nullptr) {
        return skip_without_fonts();
    }
    UNITY_BEGIN();
    RUN_TEST(test_six_han_fit_120);
    RUN_TEST(test_poem_drops_to_32);
    RUN_TEST(test_too_long_reports_not_fit);
    RUN_TEST(test_missing_glyph_is_drawn_and_reported);
    RUN_TEST(test_punctuation_break_preferred);
    RUN_TEST(test_break_stays_without_nearby_punctuation);
    RUN_TEST(test_space_after_punctuation_is_dropped);
    RUN_TEST(test_automatic_break_matches_typed_break);
    RUN_TEST(test_english_word_not_split);
    RUN_TEST(test_trailing_punctuation_shift);
    RUN_TEST(test_digit_gap_is_quarter_em);
    RUN_TEST(test_gaps_only_between_digits_and_han);
    RUN_TEST(test_digit_runs_use_inter);
    RUN_TEST(test_tabular_figures);
    RUN_TEST(test_text_that_is_not_utf8_is_logged_and_still_laid_out);
    g_object_unref(fonts);
    return UNITY_END();
}
