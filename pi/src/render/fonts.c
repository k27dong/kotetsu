#include "render/fonts.h"

#include <fontconfig/fontconfig.h>
#include <glib.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>

#include "config.h"
#include "log.h"

static bool add_font_file(FcConfig *config, const char *font_dir, const char *file) {
    gchar *path = g_build_filename(font_dir, file, nullptr);
    const bool added = FcConfigAppFontAddFile(config, (const FcChar8 *)path);
    if (!added) {
        log_error("can't load font %s", path);
    }
    g_free(path);
    return added;
}

PangoFontMap *fonts_create_map(const char *font_dir) {
    FcConfig *config = FcConfigCreate();
    if (config == nullptr) {
        log_error("can't create a fontconfig configuration");
        return nullptr;
    }
    const bool cjk_added = add_font_file(config, font_dir, FONT_FILE_CJK);
    const bool digits_added = add_font_file(config, font_dir, FONT_FILE_DIGITS);
    PangoFontMap *map = nullptr;
    if (cjk_added && digits_added) {
        map = pango_cairo_font_map_new_for_font_type(CAIRO_FONT_TYPE_FT);
        if (map == nullptr) {
            log_error("Pango has no FreeType font map");
        } else {
            pango_fc_font_map_set_config(PANGO_FC_FONT_MAP(map), config);
        }
    }
    FcConfigDestroy(config); // the font map holds its own reference
    return map;
}
