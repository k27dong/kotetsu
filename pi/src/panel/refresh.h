#pragma once

#include <stdint.h>

enum refresh_kind {
    REFRESH_NONE,
    REFRESH_PARTIAL,
    REFRESH_FULL,
};

enum refresh_reason {
    REFRESH_REASON_CHANGE,
    REFRESH_REASON_NEW_PHRASE,
    REFRESH_REASON_NEW_DAY,
    REFRESH_REASON_CLEANUP,
    REFRESH_REASON_FORCED,
    REFRESH_REASON_SCREEN_SWITCH,
};

struct refresh_history {
    bool glass_known;
    int partials_since_full;
    int64_t last_refresh_s;
};

struct refresh_decision {
    enum refresh_kind kind;
    int64_t not_before_s;
};

struct refresh_decision refresh_decide(const struct refresh_history *history, bool frames_equal,
                                       enum refresh_reason reason, int64_t now_s);
void refresh_record(struct refresh_history *history, enum refresh_kind done, int64_t now_s);
