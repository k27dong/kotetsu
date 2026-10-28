#pragma once

#include <stddef.h>

#include "panel/refresh.h"
#include "render/frame.h"

static constexpr size_t STATE_APP_JSON_BYTES = 8192;

struct saved_state {
    struct frame on_glass;
    struct refresh_history history;
    char app_json[STATE_APP_JSON_BYTES];
};

[[nodiscard]] bool state_load(const char *path, struct saved_state *out);
[[nodiscard]] bool state_save(const char *path, const struct saved_state *state);
