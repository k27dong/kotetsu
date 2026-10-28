#pragma once

#include <cairo.h>

#include "model.h"

void icons_draw_weather(cairo_t *cr, enum weather_icon icon, double x, double y, double size);
void icons_draw_small(cairo_t *cr, enum small_icon icon, double x, double y);
[[nodiscard]] bool icons_small_from_name(const char *name, enum small_icon *out);
