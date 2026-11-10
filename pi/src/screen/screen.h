#pragma once

#include <stdint.h>
#include <time.h>

#include "config.h"
#include "draw/frame.h"
#include "shared/time.h"

enum refresh_kind {
    REFRESH_NONE,
    REFRESH_PARTIAL,
    REFRESH_FULL,
};

enum refresh_reason {
    REASON_SAME_FRAME,
    REASON_UNKNOWN_GLASS,
    REASON_FULL_REFRESH_ASKED,
    REASON_NEW_PHRASE,
    REASON_NEW_DAY,
    REASON_CLEANUP,
    REASON_CHANGE,
    REASON_TOO_SOON,
    REASON_GREETING_WAIT,
};

// what is on the wall, or would be without a panel
struct wall {
    bool known;
    struct frame frame;
    int64_t phrase_id; // 0 when no phrase is on the glass
    char phrase_text[PHRASE_TEXT_MAX_BYTES];
    struct civil_date date;
    enum refresh_kind last_kind;
    int partials_since_full;
    time_t last_refresh; // 0 before the first
    time_t last_tick;    // for the clean-up crossing
    int cleanup_minute;  // 600 until the dashboard says otherwise
    bool cleanup_due;
    int64_t full_refresh_done;
    time_t greeting_wait_since; // 0 when not waiting
};

struct wanted {
    const struct frame *frame;
    int64_t phrase_id;
    const char *phrase_text; // the phrase actually drawn, or nullptr
    struct civil_date date;
    int64_t full_refresh;
    int cleanup_minute; // the dashboard's, else the wall's own
    bool answered_today;
};

struct decision {
    enum refresh_kind kind;
    enum refresh_reason reason;
};

struct refresh_result {
    enum refresh_kind kind;
    bool ok;
};

// unknown glass, nothing refreshed, clean-up at 10:00
struct wall screen_new_wall(void);
struct decision screen_decide(const struct wall *wall, const struct wanted *wanted, time_t now);
[[nodiscard]] struct wall screen_note_tick(struct wall wall, const struct wanted *wanted,
                                           struct decision decision, time_t now);
[[nodiscard]] struct wall screen_note_refresh(struct wall wall, const struct wanted *wanted,
                                              struct refresh_result result, time_t now);
const char *screen_reason_text(enum refresh_reason reason);

struct screen;

struct panel_search {
    bool found;
    bool just_found; // its first answer since the board opened
};

// nullptr without a board, as on the Mac
struct screen *screen_open(void);
// looks again while none has answered
struct panel_search screen_find_panel(struct screen *screen);
[[nodiscard]] bool screen_refresh(struct screen *screen, const struct frame *on_glass,
                                  const struct frame *next, enum refresh_kind kind);
// false without a panel, which it says
[[nodiscard]] bool screen_clear(struct screen *screen);
void screen_close(struct screen *screen);
