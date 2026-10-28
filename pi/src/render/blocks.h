#pragma once

#include <cairo.h>
#include <pango/pango.h>

#include "model.h"

void blocks_draw_date(cairo_t *cr, PangoContext *context, const struct screen_model *model);
void blocks_draw_weather(cairo_t *cr, PangoContext *context, const struct weather_view *weather);
void blocks_draw_counter(cairo_t *cr, PangoContext *context, const struct screen_model *model);
void blocks_draw_wifi_setup(cairo_t *cr, PangoContext *context);
