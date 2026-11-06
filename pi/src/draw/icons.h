#pragma once

#include <cairo.h>

#include "phrase.h"
#include "weather.h"

void icons_draw_weather(cairo_t *cr, enum weather_icon icon, cairo_rectangle_t box);
void icons_draw_greeting(cairo_t *cr, enum greeting_icon icon, double x, double y);
