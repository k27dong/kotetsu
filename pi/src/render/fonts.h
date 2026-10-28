#pragma once

#include <pango/pango.h>

#define FONT_FAMILY_CJK    "LXGW WenKai GB Screen"
#define FONT_FAMILY_DIGITS "Inter"

[[nodiscard]] PangoFontMap *fonts_create_map(const char *font_dir);
