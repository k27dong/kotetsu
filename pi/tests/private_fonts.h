#pragma once

#include <stddef.h>
#include <stdio.h>

#include <fontconfig/fontconfig.h>
#include <glib.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>

#define FONT_FILE_CJK    "LXGWWenKaiGBScreen.ttf"
#define FONT_FILE_DIGITS "Inter-SemiBold.ttf"

// the two font files alone, as the program loads them; nullptr when one is missing
[[maybe_unused]] static PangoFontMap *private_fonts(const char *font_dir) {
    if (font_dir == nullptr) {
        return nullptr;
    }
    FcConfig *config = FcConfigCreate();
    const char *const files[] = {FONT_FILE_CJK, FONT_FILE_DIGITS};
    bool added = true;
    for (size_t i = 0; i < G_N_ELEMENTS(files) && added; i++) {
        gchar *path = g_build_filename(font_dir, files[i], nullptr);
        added = FcConfigAppFontAddFile(config, (const FcChar8 *)path);
        g_free(path);
    }
    PangoFontMap *fonts = nullptr;
    if (added) {
        fonts = pango_cairo_font_map_new_for_font_type(CAIRO_FONT_TYPE_FT);
        pango_fc_font_map_set_config(PANGO_FC_FONT_MAP(fonts), config);
    }
    FcConfigDestroy(config);
    return fonts;
}

[[maybe_unused]] static int skip_without_fonts(void) {
    puts("no fonts in KOTETSU_FONT_DIR");
    return 77;
}
