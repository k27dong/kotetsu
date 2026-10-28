#include "render/screen.h"

#include <stddef.h>

#include <cairo.h>
#include <glib-object.h>
#include <pango/pango.h>

#include "config.h"
#include "log.h"
#include "model.h"
#include "render/blocks.h"
#include "render/frame.h"
#include "render/layout.h"

static constexpr double RULE_LEFT = 32;
static constexpr double RULE_TOP = 336;
static constexpr double RULE_WIDTH = 736;
static constexpr double LINE_THICKNESS = 2;
static constexpr double DIVIDER_TOP = 356;
static constexpr double DIVIDER_HEIGHT = 100;
static const double DIVIDERS[] = {276, 522};

static void draw_strip(cairo_t *cr, PangoContext *context, const struct screen_model *model) {
    cairo_rectangle(cr, RULE_LEFT, RULE_TOP, RULE_WIDTH, LINE_THICKNESS);
    for (size_t i = 0; i < G_N_ELEMENTS(DIVIDERS); i++) {
        cairo_rectangle(cr, DIVIDERS[i], DIVIDER_TOP, LINE_THICKNESS, DIVIDER_HEIGHT);
    }
    cairo_fill(cr);
    blocks_draw_date(cr, context, model);
    blocks_draw_weather(cr, context, &model->weather);
    blocks_draw_counter(cr, context, model);
}

bool screen_render(PangoFontMap *fonts, const struct screen_model *model, struct frame *out,
                   struct render_report *report) {
    *report = (struct render_report){};
    cairo_surface_t *surface =
            cairo_image_surface_create(CAIRO_FORMAT_A8, SCREEN_WIDTH, SCREEN_HEIGHT);
    cairo_t *cr = cairo_create(surface);
    PangoContext *context = layout_context_new(fonts, cr);
    if (model->wifi_setup) {
        blocks_draw_wifi_setup(cr, context);
    } else {
        if (model->phrase != nullptr && model->phrase[0] != '\0') {
            report->phrase = layout_phrase(fonts, cr, model->phrase);
        }
        draw_strip(cr, context, model);
    }
    const cairo_status_t status = cairo_status(cr);
    const bool rendered = status == CAIRO_STATUS_SUCCESS;
    if (rendered) {
        cairo_surface_flush(surface);
        frame_from_a8(cairo_image_surface_get_data(surface),
                      cairo_image_surface_get_stride(surface), out);
    } else {
        log_error("can't render the screen: %s", cairo_status_to_string(status));
    }
    g_object_unref(context);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return rendered;
}
