#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "config.h"
#include "shared/dashboard.h"
#include "shared/http.h"
#include "shared/time.h"

#define PHRASE_NOW_PATH "/api/pi/now"

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
    enum dashboard_status status; // this tick's answer
    bool answered_today;          // the last good answer came on this Warsaw day
    bool have_answer;             // the fields below hold the last good answer
    struct phrase_answer answer;
    struct civil_date answered_on;
};

[[nodiscard]] struct phrase_state phrase_read(struct phrase_state previous, const char *url,
                                              const struct http_response *response, time_t now);
