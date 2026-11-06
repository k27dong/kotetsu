#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "config.h"
#include "shared/dashboard.h"
#include "shared/time.h"

static constexpr size_t GREETING_TEXT_BYTES = 64;

enum greeting_icon {
    GREETING_HEART,
    GREETING_RINGS,
    GREETING_CAKE,
    GREETING_CAT,
    GREETING_HEARTS,
    GREETING_STAR,
};

struct phrase_answer {
    int64_t id;
    char text[PHRASE_TEXT_MAX_BYTES];
    time_t until;
    bool has_greeting;
    enum greeting_icon greeting_icon;
    char greeting[GREETING_TEXT_BYTES];
    int64_t full_refresh; // 0 when the dashboard sent null
    int cleanup_minute;   // of the Warsaw day
};

struct phrase_state {
    bool have_answer;
    struct phrase_answer answer;
    struct civil_date answered_on;
};

struct phrase {
    enum dashboard_status status;
    bool have_answer;
    struct phrase_answer answer;
    bool answered_today;
};

// keeps the last good answer on failure
struct phrase phrase_fetch(struct phrase_state *state, const struct config *config, time_t now);
[[nodiscard]] enum dashboard_status phrase_parse(const char *json, struct phrase_answer *out);
