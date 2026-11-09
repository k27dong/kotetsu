/* the screen: the refresh rules, and the panel that shows the frame */
#include "screen/screen.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "draw/frame.h"
#include "screen/board.h"
#include "screen/epd.h"
#include "shared/log.h"
#include "shared/time.h"

static constexpr int REFRESH_GAP_SECONDS = 180;
static constexpr int PARTIALS_BEFORE_FULL = 4;
static constexpr int GREETING_WAIT_SECONDS = 60;
static constexpr int CLEANUP_MINUTE_DEFAULT = 600;

struct screen {
    struct board *board;
    bool panel;
};

struct wall screen_new_wall(void) {
    return (struct wall){.cleanup_minute = CLEANUP_MINUTE_DEFAULT};
}

static struct decision refresh_for(const struct wall *wall, const struct wanted *wanted) {
    /* full for unknown glass */
    if (!wall->known) {
        return (struct decision){REFRESH_FULL, REASON_UNKNOWN_GLASS};
    }

    /* full when the dashboard asks */
    if (wanted->full_refresh != 0 && wanted->full_refresh != wall->full_refresh_done) {
        return (struct decision){REFRESH_FULL, REASON_FULL_REFRESH_ASKED};
    }

    /* full for a new phrase */
    const bool same_frame = frame_equal(&wall->frame, wanted->frame);
    const char *text = wanted->phrase_text != nullptr ? wanted->phrase_text : "";
    if (wanted->phrase_id != wall->phrase_id || strcmp(text, wall->phrase_text) != 0) {
        return same_frame ? (struct decision){REFRESH_NONE, REASON_SAME_FRAME}
                          : (struct decision){REFRESH_FULL, REASON_NEW_PHRASE};
    }

    /* full for a new day */
    if (!time_same_date(wanted->date, wall->date)) {
        return same_frame ? (struct decision){REFRESH_NONE, REASON_SAME_FRAME}
                          : (struct decision){REFRESH_FULL, REASON_NEW_DAY};
    }

    /* full clean-up after a partial */
    if (wall->cleanup_due) {
        return wall->partials_since_full > 0 ? (struct decision){REFRESH_FULL, REASON_CLEANUP}
                                             : (struct decision){REFRESH_NONE, REASON_CLEANUP};
    }

    /* nothing for the same frame */
    if (same_frame) {
        return (struct decision){REFRESH_NONE, REASON_SAME_FRAME};
    }

    /* partial, or full after four */
    return wall->partials_since_full < PARTIALS_BEFORE_FULL
                   ? (struct decision){REFRESH_PARTIAL, REASON_CHANGE}
                   : (struct decision){REFRESH_FULL, REASON_CHANGE};
}

static struct wall wall_at_tick(struct wall wall, const struct wanted *wanted, time_t now) {
    /* clamp a future refresh */
    if (wall.last_refresh > now) {
        wall.last_refresh = now; // the clock went back
    }

    /* note a clean-up crossing */
    wall.cleanup_minute = wanted->cleanup_minute;
    if (wall.last_tick != 0 &&
        time_same_date(time_warsaw_date(wall.last_tick), time_warsaw_date(now)) &&
        time_warsaw_minute_of_day(wall.last_tick) < wall.cleanup_minute &&
        time_warsaw_minute_of_day(now) >= wall.cleanup_minute) {
        wall.cleanup_due = true;
    }
    wall.last_tick = now;
    return wall;
}

struct decision screen_decide(const struct wall *wall, const struct wanted *wanted, time_t now) {
    /* see what the frame needs */
    const struct wall ticked = wall_at_tick(*wall, wanted, now);
    const struct decision decision = refresh_for(&ticked, wanted);
    if (decision.kind == REFRESH_NONE) {
        return decision;
    }

    /* wait for the greeting */
    const time_t since = ticked.greeting_wait_since;
    if (decision.reason == REASON_NEW_DAY && !wanted->answered_today &&
        (since == 0 || now - since < GREETING_WAIT_SECONDS)) { // even with a start in the future
        return (struct decision){REFRESH_NONE, REASON_GREETING_WAIT};
    }

    /* keep 180 s between refreshes */
    if (now - ticked.last_refresh < REFRESH_GAP_SECONDS) {
        return (struct decision){REFRESH_NONE, REASON_TOO_SOON};
    }
    return decision;
}

struct wall screen_note_tick(struct wall wall, const struct wanted *wanted,
                             struct decision decision, time_t now) {
    /* do this tick's bookkeeping */
    wall = wall_at_tick(wall, wanted, now);

    /* start the greeting wait */
    const time_t since = wall.greeting_wait_since;
    if (decision.reason == REASON_GREETING_WAIT &&
        (since == 0 || since > now)) { // a start in the future means the clock went back
        wall.greeting_wait_since = now;
    }

    /* end a needless clean-up */
    if (decision.kind == REFRESH_NONE && decision.reason == REASON_CLEANUP) {
        wall.cleanup_due = false;
    }
    return wall;
}

struct wall screen_note_refresh(struct wall wall, const struct wanted *wanted,
                                struct refresh_result result, time_t now) {
    if (result.kind == REFRESH_NONE) {
        return wall;
    }

    /* forget the glass on failure */
    if (!result.ok) {
        wall.known = false;
        wall.last_refresh = now; // the retry waits the gap too
        return wall;
    }

    /* update the history */
    wall.partials_since_full = result.kind == REFRESH_PARTIAL ? wall.partials_since_full + 1 : 0;
    wall.last_kind = result.kind;
    wall.last_refresh = now;

    /* note what the glass shows */
    const char *text = wanted->phrase_text != nullptr ? wanted->phrase_text : "";
    const size_t length = strlen(text);
    if (length >= sizeof wall.phrase_text) {
        log_error("the drawn phrase is %zu bytes; the wall holds %zu", length,
                  sizeof wall.phrase_text - 1);
        abort();
    }
    wall.known = true;
    wall.frame = *wanted->frame;
    wall.phrase_id = wanted->phrase_id;
    memcpy(wall.phrase_text, text, length + 1);
    wall.date = wanted->date;

    /* mark clean-up and request done */
    if (result.kind == REFRESH_FULL) {
        wall.cleanup_due = false;
        if (wanted->full_refresh != 0) {
            wall.full_refresh_done = wanted->full_refresh;
        }
    }

    /* end the greeting wait */
    wall.greeting_wait_since = 0;
    return wall;
}

const char *screen_reason_text(enum refresh_reason reason) {
    switch (reason) {
    case REASON_SAME_FRAME:
        return "same frame";
    case REASON_UNKNOWN_GLASS:
        return "unknown glass";
    case REASON_FULL_REFRESH_ASKED:
        return "full refresh asked";
    case REASON_NEW_PHRASE:
        return "new phrase";
    case REASON_NEW_DAY:
        return "new day";
    case REASON_CLEANUP:
        return "clean-up";
    case REASON_CHANGE:
        return "change";
    case REASON_TOO_SOON:
        return "too soon";
    case REASON_GREETING_WAIT:
        return "greeting wait";
    }
    log_error("unknown refresh reason %d", (int)reason);
    abort();
}

[[nodiscard]] static struct busy_reading busy_after_sleep(struct board *board) {
    const struct epd_steps sleep = epd_sleep_steps();
    if (!board_run(board, &sleep)) {
        return (struct busy_reading){};
    }
    return board_read_busy(board); // only a panel holds BUSY high in deep sleep
}

struct screen *screen_open(void) {
    /* open the board */
    struct board *board = board_open();
    if (board == nullptr) {
        log_info("no panel: the board can't be opened");
        return nullptr;
    }

    /* make the screen */
    struct screen *screen = malloc(sizeof *screen);
    if (screen == nullptr) {
        log_error("out of memory opening the screen");
        abort();
    }
    screen->board = board; // kept even without a panel

    /* look for the panel */
    const struct busy_reading busy = busy_after_sleep(board);
    screen->panel = busy.ok && busy.busy;
    if (!screen->panel) {
        log_warning("no panel: BUSY reads %d after deep sleep", busy.ok ? 0 : -1);
    }
    return screen;
}

struct panel_search screen_find_panel(struct screen *screen) {
    /* nothing to look for */
    if (screen == nullptr || screen->panel) {
        return (struct panel_search){.found = screen != nullptr};
    }

    /* look for it again */
    const struct busy_reading busy = busy_after_sleep(screen->board); // never touches the glass
    if (!busy.ok || !busy.busy) {
        return (struct panel_search){};
    }
    log_info("panel found: BUSY reads 1 after deep sleep");
    screen->panel = true;
    return (struct panel_search){.found = true, .just_found = true};
}

bool screen_refresh(struct screen *screen, const struct frame *on_glass, const struct frame *next,
                    enum refresh_kind kind) {
    const struct epd_steps steps = epd_refresh_steps(on_glass, next, kind);
    return board_run(screen->board, &steps);
}

bool screen_clear(struct screen *screen) {
    if (screen == nullptr || !screen->panel) {
        log_error("no panel to clear");
        return false;
    }

    /* paint the glass white */
    struct frame white;
    frame_fill_white(&white);
    const struct epd_steps steps = epd_refresh_steps(&white, &white, REFRESH_FULL);
    return board_run(screen->board, &steps);
}

void screen_close(struct screen *screen) {
    if (screen == nullptr) {
        return;
    }
    board_close(screen->board);
    free(screen);
}
