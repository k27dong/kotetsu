#pragma once

#include <cairo.h>
#include <pango/pango.h>

#define FONT_FAMILY_CJK    "LXGW WenKai GB Screen"
#define FONT_FAMILY_DIGITS "Inter"

enum digit_figures {
    DIGITS_PROPORTIONAL,
    DIGITS_TABULAR,
};

struct text_style {
    const char *family;
    double size_px;
    enum digit_figures figures;
};

struct phrase_fit {
    bool fits;
    int missing_glyphs;
};

double typeset_snap(double value);
PangoContext *typeset_context(PangoFontMap *fonts, cairo_t *cr);
PangoLayout *typeset_line(PangoContext *context, struct text_style style, const char *text);
void typeset_show(cairo_t *cr, PangoLayout *layout, double x, double baseline);
// draws at the largest size that fits, or not at all
struct phrase_fit typeset_phrase(PangoContext *context, cairo_t *cr, const char *text);
