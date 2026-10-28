#include "render/layout.h"

#include <stddef.h>
#include <string.h>

#include <cairo.h>
#include <glib.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

#include "render/fonts.h"
#include "render/text.h"

static constexpr double PHRASE_LEFT = 32;
static constexpr double PHRASE_TOP = 24;
static constexpr double PHRASE_WIDTH = 736;
static constexpr double PHRASE_HEIGHT = 300;
static constexpr int PHRASE_SIZE_MAX = 128;
static constexpr int PHRASE_SIZE_MIN = 32;
static constexpr int PHRASE_SIZE_STEP = 4;
static constexpr double PHRASE_LINE_HEIGHT = 1.5;
static constexpr double DIGIT_GAP_EM = 0.25;
static constexpr double HANGING_SHIFT_EM = 0.25;

#define LANGUAGE         "zh-cn"
#define FEATURES         "chws"
#define FEATURES_TABULAR "chws, tnum"
#define GAP_PLACEHOLDER  "⁠"

static constexpr gunichar GAP_PLACEHOLDER_CHAR = 0x2060;
static const gunichar HANGING_PUNCTUATION[] = {U'。', U'，', U'！', U'？'};

double layout_snap(double value) {
    return (double)(long)(value + 0.5);
}

PangoContext *layout_context_new(PangoFontMap *fonts, cairo_t *cr) {
    PangoContext *context = pango_font_map_create_context(fonts);
    pango_context_set_language(context, pango_language_from_string(LANGUAGE));
    pango_cairo_update_context(cr, context);
    cairo_font_options_t *options = cairo_font_options_create();
    cairo_font_options_set_antialias(options, CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(options, CAIRO_HINT_STYLE_SLIGHT);
    cairo_font_options_set_hint_metrics(options, CAIRO_HINT_METRICS_ON);
    pango_cairo_context_set_font_options(context, options);
    cairo_font_options_destroy(options);
    return context;
}

// The offsets are byte offsets of the placeholders in the returned copy, not in text.
static char *with_gap_placeholders(const char *text, size_t **offsets_out, size_t *count_out) {
    const size_t length = strlen(text);
    size_t *offsets = g_new(size_t, length + 1);
    const size_t count = text_gap_offsets(text, offsets, length + 1);
    GString *gapped = g_string_sized_new(length + count * strlen(GAP_PLACEHOLDER));
    size_t copied = 0;
    for (size_t i = 0; i < count; i++) {
        const size_t gap = offsets[i];
        g_string_append_len(gapped, text + copied, (gssize)(gap - copied));
        copied = gap;
        offsets[i] = gapped->len;
        g_string_append(gapped, GAP_PLACEHOLDER);
    }
    g_string_append(gapped, text + copied);
    *offsets_out = offsets;
    *count_out = count;
    return g_string_free(gapped, false);
}

static PangoAttrList *attributes_for(const char *gapped, const size_t *gaps, size_t gap_count,
                                     double size_px, const char *features, bool digits_in_inter) {
    PangoAttrList *attributes = pango_attr_list_new();
    pango_attr_list_insert(attributes, pango_attr_font_features_new(features));
    const PangoRectangle no_ink = {};
    const PangoRectangle gap_box = {.width = (int)(size_px * DIGIT_GAP_EM * PANGO_SCALE + 0.5)};
    for (size_t i = 0; i < gap_count; i++) {
        PangoAttribute *gap = pango_attr_shape_new(&no_ink, &gap_box);
        gap->start_index = (guint)gaps[i];
        gap->end_index = (guint)(gaps[i] + strlen(GAP_PLACEHOLDER));
        pango_attr_list_insert(attributes, gap);
    }
    if (digits_in_inter) {
        const size_t max_runs = strlen(gapped) / 2 + 1;
        struct byte_range *runs = g_new(struct byte_range, max_runs);
        const size_t run_count = text_digit_runs(gapped, runs, max_runs);
        for (size_t i = 0; i < run_count; i++) {
            PangoAttribute *family = pango_attr_family_new(FONT_FAMILY_DIGITS);
            family->start_index = (guint)runs[i].start;
            family->end_index = (guint)runs[i].end;
            pango_attr_list_insert(attributes, family);
            PangoAttribute *weight = pango_attr_weight_new(PANGO_WEIGHT_SEMIBOLD);
            weight->start_index = (guint)runs[i].start;
            weight->end_index = (guint)runs[i].end;
            pango_attr_list_insert(attributes, weight);
        }
        g_free(runs);
    }
    return attributes;
}

static void set_font(PangoLayout *layout, const char *family, double size_px) {
    PangoFontDescription *description = pango_font_description_new();
    pango_font_description_set_family(description, family);
    pango_font_description_set_absolute_size(description, size_px * PANGO_SCALE);
    if (strcmp(family, FONT_FAMILY_DIGITS) == 0) {
        pango_font_description_set_weight(description, PANGO_WEIGHT_SEMIBOLD);
    }
    pango_layout_set_font_description(layout, description);
    pango_font_description_free(description);
}

PangoLayout *layout_text(PangoContext *context, const char *family, double size_px,
                         enum digit_figures figures, const char *text) {
    size_t *gaps;
    size_t gap_count;
    char *gapped = with_gap_placeholders(text, &gaps, &gap_count);
    PangoLayout *layout = pango_layout_new(context);
    pango_layout_set_text(layout, gapped, -1);
    set_font(layout, family, size_px);
    PangoAttrList *attributes =
            attributes_for(gapped, gaps, gap_count, size_px,
                           figures == DIGITS_TABULAR ? FEATURES_TABULAR : FEATURES, true);
    pango_layout_set_attributes(layout, attributes);
    pango_attr_list_unref(attributes);
    g_free(gaps);
    g_free(gapped);
    return layout;
}

void layout_show(cairo_t *cr, PangoLayout *layout, double x, double baseline) {
    cairo_move_to(cr, layout_snap(x), layout_snap(baseline));
    pango_cairo_show_layout_line(cr, pango_layout_get_line_readonly(layout, 0));
}

static bool block_fits(PangoLayout *layout, int size_px) {
    if (pango_layout_get_line_count(layout) * size_px * PHRASE_LINE_HEIGHT > PHRASE_HEIGHT) {
        return false;
    }
    PangoLayoutIter *iter = pango_layout_get_iter(layout);
    bool fits = true;
    do {
        PangoRectangle logical;
        pango_layout_iter_get_line_extents(iter, nullptr, &logical);
        if (logical.width > PHRASE_WIDTH * PANGO_SCALE) {
            fits = false;
            break;
        }
    } while (pango_layout_iter_next_line(iter));
    pango_layout_iter_free(iter);
    return fits;
}

static bool ends_with_hanging_punctuation(const char *text, PangoLayoutLine *line) {
    const char *start = text + pango_layout_line_get_start_index(line);
    const char *cursor = start + pango_layout_line_get_length(line);
    while (cursor > start) {
        cursor = g_utf8_prev_char(cursor);
        const gunichar character = g_utf8_get_char(cursor);
        if (character == GAP_PLACEHOLDER_CHAR || g_unichar_isspace(character)) {
            continue;
        }
        for (size_t i = 0; i < G_N_ELEMENTS(HANGING_PUNCTUATION); i++) {
            if (character == HANGING_PUNCTUATION[i]) {
                return true;
            }
        }
        return false;
    }
    return false;
}

static void draw_block(cairo_t *cr, PangoLayout *layout, int size_px) {
    const double line_height = size_px * PHRASE_LINE_HEIGHT;
    const int lines = pango_layout_get_line_count(layout);
    const double block_top = PHRASE_TOP + (PHRASE_HEIGHT - lines * line_height) / 2;
    const char *text = pango_layout_get_text(layout);
    PangoLayoutIter *iter = pango_layout_get_iter(layout);
    int index = 0;
    do {
        PangoRectangle logical;
        pango_layout_iter_get_line_extents(iter, nullptr, &logical);
        const double width = (double)logical.width / PANGO_SCALE;
        const double height = (double)logical.height / PANGO_SCALE;
        const double ascent =
                (double)(pango_layout_iter_get_baseline(iter) - logical.y) / PANGO_SCALE;
        PangoLayoutLine *line = pango_layout_iter_get_line_readonly(iter);
        double x = PHRASE_LEFT + (PHRASE_WIDTH - width) / 2;
        if (ends_with_hanging_punctuation(text, line)) {
            x += size_px * HANGING_SHIFT_EM;
        }
        const double line_top = block_top + index * line_height;
        cairo_move_to(cr, layout_snap(x),
                      layout_snap(line_top + (line_height - height) / 2 + ascent));
        pango_cairo_show_layout_line(cr, line);
        index++;
    } while (pango_layout_iter_next_line(iter));
    pango_layout_iter_free(iter);
}

struct phrase_fit layout_phrase(PangoFontMap *fonts, cairo_t *cr, const char *text) {
    PangoContext *context = layout_context_new(fonts, cr);
    size_t *gaps;
    size_t gap_count;
    char *gapped = with_gap_placeholders(text, &gaps, &gap_count);
    PangoLayout *layout = pango_layout_new(context);
    pango_layout_set_text(layout, gapped, -1);
    pango_layout_set_width(layout, (int)(PHRASE_WIDTH * PANGO_SCALE));
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    struct phrase_fit fit = {};
    for (int size_px = PHRASE_SIZE_MAX; size_px >= PHRASE_SIZE_MIN; size_px -= PHRASE_SIZE_STEP) {
        set_font(layout, FONT_FAMILY_CJK, size_px);
        PangoAttrList *attributes =
                attributes_for(gapped, gaps, gap_count, size_px, FEATURES, false);
        pango_layout_set_attributes(layout, attributes);
        pango_attr_list_unref(attributes);
        if (block_fits(layout, size_px)) {
            fit.fits = true;
            fit.size_px = size_px;
            fit.lines = pango_layout_get_line_count(layout);
            draw_block(cr, layout, size_px);
            break;
        }
    }
    fit.unknown_glyphs = pango_layout_get_unknown_glyphs_count(layout);
    g_object_unref(layout);
    g_object_unref(context);
    g_free(gaps);
    g_free(gapped);
    return fit;
}
