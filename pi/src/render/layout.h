#pragma once

#include <cairo.h>
#include <pango/pango.h>

struct phrase_fit {
    bool fits;
    int size_px;
    int lines;
    int unknown_glyphs;
};

enum digit_figures {
    DIGITS_PROPORTIONAL,
    DIGITS_TABULAR,
};

double layout_snap(double value);
PangoContext *layout_context_new(PangoFontMap *fonts, cairo_t *cr);
PangoLayout *layout_text(PangoContext *context, const char *family, double size_px,
                         enum digit_figures figures, const char *text);
void layout_show(cairo_t *cr, PangoLayout *layout, double x, double baseline);
struct phrase_fit layout_phrase(PangoFontMap *fonts, cairo_t *cr, const char *text);
