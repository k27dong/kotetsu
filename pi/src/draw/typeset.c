/* typesetting: lines of text, and the phrase fitted to its box */
#include "draw/typeset.h"

#include <stddef.h>
#include <string.h>

#include <cairo.h>
#include <glib.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

#include "config.h"
#include "shared/log.h"

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
#define GAP_PLACEHOLDER  "\u2060"

static constexpr size_t GAPPED_TEXT_BYTES = PHRASE_TEXT_MAX_BYTES * 3;  // a 3-byte gap per 2 bytes
static constexpr size_t DIGIT_RUNS_MAX = PHRASE_TEXT_MAX_BYTES / 2 + 1; // a non-digit between runs

static constexpr gunichar GAP_PLACEHOLDER_CHAR = 0x2060;
static const gunichar HANGING_PUNCTUATION[] = {U'。', U'，', U'！', U'？', 0};
static const gunichar BREAK_PUNCTUATION[] = {U'。', U'，', U'！', U'？', U'、', U'；', U'：', 0};

struct byte_range {
    size_t start;
    size_t end;
};

struct gapped_text {
    char text[GAPPED_TEXT_BYTES]; // with a placeholder wherever a digit meets a Han character
    size_t length;
    size_t gaps[PHRASE_TEXT_MAX_BYTES]; // byte offsets of the placeholders in text
    size_t gap_count;
};

double typeset_snap(double value) {
    return (double)(long)(value + 0.5);
}

PangoContext *typeset_context(PangoFontMap *fonts, cairo_t *cr) {
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

static bool is_han(gunichar character) {
    return g_unichar_get_script(character) == G_UNICODE_SCRIPT_HAN;
}

static void with_gap_placeholders(const char *text, struct gapped_text *gapped) {
    /* cap and validate the text */
    const char *limit = memchr(text, '\0', PHRASE_TEXT_MAX_BYTES);
    if (limit == nullptr) {
        log_error("can't typeset text over %zu bytes; drawing the start",
                  PHRASE_TEXT_MAX_BYTES - 1);
        limit = text + PHRASE_TEXT_MAX_BYTES - 1;
    }
    const char *end;
    if (!g_utf8_validate_len(text, (gsize)(limit - text), &end)) {
        log_error("can't typeset text that isn't UTF-8; drawing what is");
    }
    const size_t length = (size_t)(end - text);

    /* find where digits meet Han */
    gapped->gap_count = 0;
    gunichar previous = 0; // neither a digit nor Han
    for (const char *cursor = text; cursor < end; cursor = g_utf8_next_char(cursor)) {
        const gunichar current = g_utf8_get_char(cursor);
        if ((g_unichar_isdigit(previous) && is_han(current)) ||
            (is_han(previous) && g_unichar_isdigit(current))) {
            gapped->gaps[gapped->gap_count++] = (size_t)(cursor - text);
        }
        previous = current;
    }

    /* copy it with placeholders */
    const size_t placeholder_bytes = strlen(GAP_PLACEHOLDER);
    gapped->length = 0;
    size_t copied = 0;
    for (size_t i = 0; i < gapped->gap_count; i++) {
        const size_t gap = gapped->gaps[i];
        memcpy(gapped->text + gapped->length, text + copied, gap - copied);
        gapped->length += gap - copied;
        copied = gap;
        gapped->gaps[i] = gapped->length; // now an offset in the copy
        memcpy(gapped->text + gapped->length, GAP_PLACEHOLDER, placeholder_bytes);
        gapped->length += placeholder_bytes;
    }
    memcpy(gapped->text + gapped->length, text + copied, length - copied);
    gapped->length += length - copied;
    gapped->text[gapped->length] = '\0';
}

static void splice(struct gapped_text *gapped, struct byte_range old, const char *with,
                   size_t with_bytes) {
    memmove(gapped->text + old.start + with_bytes, gapped->text + old.end,
            gapped->length - old.end + 1);
    memcpy(gapped->text + old.start, with, with_bytes);
    gapped->length = gapped->length - (old.end - old.start) + with_bytes;
}

static size_t digit_runs(const char *text, struct byte_range *out) {
    size_t count = 0;
    const char *cursor = text;
    while (*cursor != '\0') {
        if (!g_unichar_isdigit(g_utf8_get_char(cursor))) {
            cursor = g_utf8_next_char(cursor);
            continue;
        }
        const char *start = cursor;
        do {
            cursor = g_utf8_next_char(cursor);
        } while (*cursor != '\0' && g_unichar_isdigit(g_utf8_get_char(cursor)));
        out[count++] = (struct byte_range){(size_t)(start - text), (size_t)(cursor - text)};
    }
    return count;
}

static PangoAttrList *attributes_for(const struct gapped_text *gapped, double size_px,
                                     const char *features, bool digits_in_inter) {
    /* features and fixed gaps */
    PangoAttrList *attributes = pango_attr_list_new();
    pango_attr_list_insert(attributes, pango_attr_font_features_new(features));
    const PangoRectangle no_ink = {};
    const PangoRectangle gap_box = {.width = (int)(size_px * DIGIT_GAP_EM * PANGO_SCALE + 0.5)};
    for (size_t i = 0; i < gapped->gap_count; i++) {
        PangoAttribute *gap = pango_attr_shape_new(&no_ink, &gap_box);
        gap->start_index = (guint)gapped->gaps[i];
        gap->end_index = (guint)(gapped->gaps[i] + strlen(GAP_PLACEHOLDER));
        pango_attr_list_insert(attributes, gap);
    }
    if (!digits_in_inter) {
        return attributes;
    }

    /* digits in Inter semibold */
    struct byte_range runs[DIGIT_RUNS_MAX];
    const size_t run_count = digit_runs(gapped->text, runs);
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

PangoLayout *typeset_line(PangoContext *context, struct text_style style, const char *text) {
    struct gapped_text gapped;
    with_gap_placeholders(text, &gapped);
    PangoLayout *layout = pango_layout_new(context);
    pango_layout_set_text(layout, gapped.text, (int)gapped.length);
    set_font(layout, style.family, style.size_px);
    PangoAttrList *attributes =
            attributes_for(&gapped, style.size_px,
                           style.figures == DIGITS_TABULAR ? FEATURES_TABULAR : FEATURES, true);
    pango_layout_set_attributes(layout, attributes);
    pango_attr_list_unref(attributes);
    return layout;
}

void typeset_show(cairo_t *cr, PangoLayout *layout, double x, double baseline) {
    cairo_move_to(cr, typeset_snap(x), typeset_snap(baseline));
    pango_cairo_show_layout_line(cr, pango_layout_get_line_readonly(layout, 0));
}

static bool block_fits(PangoLayout *layout, int size_px) {
    /* all lines within the height */
    if (pango_layout_get_line_count(layout) * size_px * PHRASE_LINE_HEIGHT > PHRASE_HEIGHT) {
        return false;
    }

    /* each line within the width */
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

static bool is_one_of(gunichar character, const gunichar *set) {
    for (; *set != 0; set++) {
        if (character == *set) {
            return true;
        }
    }
    return false;
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
        return is_one_of(character, HANGING_PUNCTUATION);
    }
    return false;
}

static void set_phrase_text(PangoLayout *layout, const struct gapped_text *gapped, int size_px) {
    pango_layout_set_text(layout, gapped->text, (int)gapped->length);
    PangoAttrList *attributes = attributes_for(gapped, size_px, FEATURES, false);
    pango_layout_set_attributes(layout, attributes);
    pango_attr_list_unref(attributes);
}

static void shift_gaps(struct gapped_text *gapped, size_t from, int by) {
    for (size_t i = 0; i < gapped->gap_count; i++) {
        if (gapped->gaps[i] >= from) {
            gapped->gaps[i] = (size_t)((long)gapped->gaps[i] + by);
        }
    }
}

struct cut {
    size_t after_punctuation;
    size_t whitespace_bytes;
};

static struct cut punctuation_cut(PangoLayout *layout, int line_index) {
    /* the line's bytes and width */
    PangoLayoutLine *line = pango_layout_get_line_readonly(layout, line_index);
    const char *text = pango_layout_get_text(layout);
    const char *start = text + pango_layout_line_get_start_index(line);
    const char *end = start + pango_layout_line_get_length(line);
    PangoRectangle logical;
    pango_layout_line_get_extents(line, nullptr, &logical);

    /* the last late punctuation */
    struct cut cut = {};
    for (const char *cursor = start; cursor < end; cursor = g_utf8_next_char(cursor)) {
        /* punctuation and what follows */
        if (!is_one_of(g_utf8_get_char(cursor), BREAK_PUNCTUATION)) {
            continue;
        }
        const char *after = g_utf8_next_char(cursor);
        const char *resume = after;
        while (resume < end && g_unichar_isspace(g_utf8_get_char(resume))) {
            resume = g_utf8_next_char(resume);
        }
        if (resume >= end) {
            return (struct cut){}; // the line already ends with it
        }
        if (g_unichar_ispunct(g_utf8_get_char(resume))) {
            continue; // a closing quote or bracket stays with its punctuation
        }

        /* in the final quarter */
        int trailing_x;
        pango_layout_line_index_to_x(line, (int)(cursor - text), true, &trailing_x);
        if (trailing_x * 4 >= logical.width * 3) {
            cut = (struct cut){(size_t)(after - text), (size_t)(resume - after)};
        }
    }
    return cut;
}

static void prefer_punctuation_breaks(PangoLayout *layout, struct gapped_text *gapped,
                                      int size_px) {
    const int lines = pango_layout_get_line_count(layout);
    for (int i = 0; i + 1 < lines; i++) {
        /* find late punctuation */
        if (pango_layout_line_is_paragraph_start(pango_layout_get_line_readonly(layout, i + 1))) {
            continue;
        }
        const struct cut cut = punctuation_cut(layout, i);
        if (cut.after_punctuation == 0) {
            continue;
        }

        /* break after it instead */
        const struct byte_range whitespace = {cut.after_punctuation,
                                              cut.after_punctuation + cut.whitespace_bytes};
        char dropped[GAPPED_TEXT_BYTES];
        memcpy(dropped, gapped->text + whitespace.start, cut.whitespace_bytes);
        splice(gapped, whitespace, "\n", 1);
        shift_gaps(gapped, whitespace.end, 1 - (int)cut.whitespace_bytes);
        set_phrase_text(layout, gapped, size_px);

        /* undo it when worse */
        if (!block_fits(layout, size_px) || pango_layout_get_line_count(layout) != lines) {
            splice(gapped, (struct byte_range){whitespace.start, whitespace.start + 1}, dropped,
                   cut.whitespace_bytes);
            shift_gaps(gapped, whitespace.start + 1, (int)cut.whitespace_bytes - 1);
            set_phrase_text(layout, gapped, size_px);
        }
    }
}

static void draw_block(cairo_t *cr, PangoLayout *layout, int size_px) {
    /* centre the block */
    const double line_height = size_px * PHRASE_LINE_HEIGHT;
    const int lines = pango_layout_get_line_count(layout);
    const double block_top = PHRASE_TOP + (PHRASE_HEIGHT - lines * line_height) / 2;

    /* centre each line in it */
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
            x += size_px * HANGING_SHIFT_EM; // the punctuation hangs out past the edge
        }
        const double line_top = block_top + index * line_height;
        cairo_move_to(cr, typeset_snap(x),
                      typeset_snap(line_top + (line_height - height) / 2 + ascent));
        pango_cairo_show_layout_line(cr, line);
        index++;
    } while (pango_layout_iter_next_line(iter));
    pango_layout_iter_free(iter);
}

struct phrase_fit typeset_phrase(PangoContext *context, cairo_t *cr, const char *text) {
    /* wrap at the box's width */
    struct gapped_text gapped;
    with_gap_placeholders(text, &gapped);
    PangoLayout *layout = pango_layout_new(context);
    pango_layout_set_width(layout, (int)(PHRASE_WIDTH * PANGO_SCALE));
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD);

    /* the largest size that fits */
    struct phrase_fit fit = {};
    for (int size_px = PHRASE_SIZE_MAX; size_px >= PHRASE_SIZE_MIN; size_px -= PHRASE_SIZE_STEP) {
        set_font(layout, FONT_FAMILY_CJK, size_px);
        set_phrase_text(layout, &gapped, size_px);
        if (block_fits(layout, size_px)) {
            prefer_punctuation_breaks(layout, &gapped, size_px);
            fit.fits = true;
            draw_block(cr, layout, size_px);
            break;
        }
    }

    /* count the missing characters */
    fit.missing_glyphs = pango_layout_get_unknown_glyphs_count(layout); // the same at every size
    g_object_unref(layout);
    return fit;
}
