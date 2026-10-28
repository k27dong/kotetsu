#pragma once

#include <pango/pango.h>

#include "model.h"
#include "render/frame.h"
#include "render/layout.h"

struct render_report {
    struct phrase_fit phrase;
};

[[nodiscard]] bool screen_render(PangoFontMap *fonts, const struct screen_model *model,
                                 struct frame *out, struct render_report *report);
