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
};

void screen_wall_init(struct wall *wall) {
    *wall = (struct wall){.cleanup_minute = CLEANUP_MINUTE_DEFAULT};
}

static bool same_date(struct civil_date a, struct civil_date b) {
    return a.year == b.year && a.month == b.month && a.day == b.day;
}

static struct decision refresh_for(const struct wall *wall, const struct wanted *wanted) {
    /* an unknown glass or the dashboard's request is full, whatever the frame */
    if (!wall->known) {
        return (struct decision){REFRESH_FULL, REASON_UNKNOWN_GLASS};
    }
    if (wanted->full_refresh != 0 && wanted->full_refresh != wall->full_refresh_done) {
        return (struct decision){REFRESH_FULL, REASON_FULL_REFRESH_ASKED};
    }

    /* a page turn, by phrase or by date, is full unless the picture is the same */
    const bool same_frame = frame_equal(&wall->frame, wanted->frame);
    const char *text = wanted->phrase_text != nullptr ? wanted->phrase_text : "";
    if (wanted->phrase_id != wall->phrase_id || strcmp(text, wall->phrase_text) != 0) {
        return same_frame ? (struct decision){REFRESH_NONE, REASON_SAME_FRAME}
                          : (struct decision){REFRESH_FULL, REASON_NEW_PHRASE};
    }
    if (!same_date(wanted->date, wall->date)) {
        return same_frame ? (struct decision){REFRESH_NONE, REASON_SAME_FRAME}
                          : (struct decision){REFRESH_FULL, REASON_NEW_DAY};
    }

    /* a clean-up is full after a partial, else there is nothing to clean */
    if (wall->cleanup_due) {
        return wall->partials_since_full > 0 ? (struct decision){REFRESH_FULL, REASON_CLEANUP}
                                             : (struct decision){REFRESH_NONE, REASON_CLEANUP};
    }

    /* a changed picture is partial, or full after four partials */
    if (same_frame) {
        return (struct decision){REFRESH_NONE, REASON_SAME_FRAME};
    }
    return wall->partials_since_full < PARTIALS_BEFORE_FULL
                   ? (struct decision){REFRESH_PARTIAL, REASON_CHANGE}
                   : (struct decision){REFRESH_FULL, REASON_CHANGE};
}

struct decision screen_decide(struct wall *wall, const struct wanted *wanted, time_t now) {
    /* a last refresh in the future means the clock went back: the wait starts from now */
    if (wall->last_refresh > now) {
        wall->last_refresh = now;
    }

    /* clean-up becomes due when two ticks on one day straddle the clean-up minute */
    wall->cleanup_minute = wanted->cleanup_minute;
    if (wall->last_tick != 0 &&
        same_date(time_warsaw_date(wall->last_tick), time_warsaw_date(now)) &&
        time_warsaw_minute_of_day(wall->last_tick) < wall->cleanup_minute &&
        time_warsaw_minute_of_day(now) >= wall->cleanup_minute) {
        wall->cleanup_due = true;
    }
    wall->last_tick = now;

    /* what the frame calls for; a clean-up with nothing to clean is over */
    const struct decision decision = refresh_for(wall, wanted);
    if (decision.kind == REFRESH_NONE) {
        if (decision.reason == REASON_CLEANUP) {
            wall->cleanup_due = false;
        }
        return decision;
    }

    /* a new day waits up to 60 s for the dashboard, so the greeting comes with the date */
    if (decision.reason == REASON_NEW_DAY && !wanted->answered_today) {
        if (wall->greeting_wait_since == 0 || wall->greeting_wait_since > now) {
            wall->greeting_wait_since = now;
        }
        if (now - wall->greeting_wait_since < GREETING_WAIT_SECONDS) {
            return (struct decision){REFRESH_NONE, REASON_GREETING_WAIT};
        }
    }

    /* never two refreshes within 180 s */
    if (now - wall->last_refresh < REFRESH_GAP_SECONDS) {
        return (struct decision){REFRESH_NONE, REASON_TOO_SOON};
    }
    return decision;
}

void screen_record(struct wall *wall, const struct wanted *wanted, struct refresh_result result,
                   time_t now) {
    if (result.kind == REFRESH_NONE) {
        return;
    }

    /* after a failure the panel's memory is unknown, and the retry waits the gap too */
    if (!result.ok) {
        wall->known = false;
        wall->last_refresh = now;
        return;
    }

    /* the history */
    wall->partials_since_full = result.kind == REFRESH_PARTIAL ? wall->partials_since_full + 1 : 0;
    wall->last_kind = result.kind;
    wall->last_refresh = now;

    /* what is on the glass now */
    const char *text = wanted->phrase_text != nullptr ? wanted->phrase_text : "";
    const size_t length = strlen(text);
    if (length >= sizeof wall->phrase_text) {
        log_error("the drawn phrase is %zu bytes; the wall holds %zu", length,
                  sizeof wall->phrase_text - 1);
        abort();
    }
    wall->known = true;
    wall->frame = *wanted->frame;
    wall->phrase_id = wanted->phrase_id;
    memcpy(wall->phrase_text, text, length + 1);
    wall->date = wanted->date;

    /* a full refresh settles the clean-up and the dashboard's request */
    if (result.kind == REFRESH_FULL) {
        wall->cleanup_due = false;
        if (wanted->full_refresh != 0) {
            wall->full_refresh_done = wanted->full_refresh;
        }
    }
    wall->greeting_wait_since = 0;
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

struct screen *screen_open(void) {
    /* the GPIO lines and SPI, which board_open explains when it can't open them */
    struct board *board = board_open();
    if (board == nullptr) {
        log_info("no panel: the board can't be opened");
        return nullptr;
    }

    /* a panel answers deep sleep by holding BUSY high; an empty header reads low */
    const struct epd_steps sleep = epd_sleep_steps();
    const int busy = board_run(board, &sleep) ? board_read_busy(board) : -1;
    if (busy != 1) {
        log_info("no panel: BUSY reads %d after deep sleep", busy);
        board_close(board);
        return nullptr;
    }

    /* the screen owns the board from here on */
    struct screen *screen = malloc(sizeof *screen);
    if (screen == nullptr) {
        log_error("out of memory opening the screen");
        abort();
    }
    screen->board = board;
    return screen;
}

bool screen_refresh(struct screen *screen, const struct frame *on_glass, const struct frame *next,
                    enum refresh_kind kind) {
    const struct epd_steps steps = epd_refresh_steps(on_glass, next, kind);
    return board_run(screen->board, &steps);
}

bool screen_clear(struct screen *screen) {
    struct frame white;
    frame_fill_white(&white);
    const struct epd_steps steps = epd_clear_steps(&white);
    return board_run(screen->board, &steps);
}

void screen_close(struct screen *screen) {
    if (screen == nullptr) {
        return;
    }
    board_close(screen->board);
    free(screen);
}
