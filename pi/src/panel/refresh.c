#include "panel/refresh.h"

#include <stdint.h>
#include <stdlib.h>

#include "config.h"
#include "log.h"

static enum refresh_kind kind_for(const struct refresh_history *history, bool frames_equal,
                                  enum refresh_reason reason) {
    if (!history->glass_known) {
        return REFRESH_FULL;
    }
    switch (reason) {
    case REFRESH_REASON_FORCED:
        return REFRESH_FULL;
    case REFRESH_REASON_CLEANUP:
        return history->partials_since_full > 0 ? REFRESH_FULL : REFRESH_NONE;
    case REFRESH_REASON_NEW_PHRASE:
    case REFRESH_REASON_NEW_DAY:
    case REFRESH_REASON_SCREEN_SWITCH:
        return frames_equal ? REFRESH_NONE : REFRESH_FULL;
    case REFRESH_REASON_CHANGE:
        if (frames_equal) {
            return REFRESH_NONE;
        }
        return history->partials_since_full < MAX_PARTIALS ? REFRESH_PARTIAL : REFRESH_FULL;
    }
    log_error("unknown refresh reason %d", (int)reason);
    abort();
}

struct refresh_decision refresh_decide(const struct refresh_history *history, bool frames_equal,
                                       enum refresh_reason reason, int64_t now_s) {
    int64_t gap_start_s = history->last_refresh_s;
    if (gap_start_s > now_s) {
        gap_start_s = now_s; // the clock was set back; waiting for the old time could take years
    }
    return (struct refresh_decision){
            .kind = kind_for(history, frames_equal, reason),
            .not_before_s = gap_start_s + MIN_REFRESH_GAP_SECONDS,
    };
}

void refresh_record(struct refresh_history *history, enum refresh_kind done, int64_t now_s) {
    switch (done) {
    case REFRESH_NONE:
        return;
    case REFRESH_PARTIAL:
        history->partials_since_full++;
        break;
    case REFRESH_FULL:
        history->partials_since_full = 0;
        break;
    }
    history->glass_known = true;
    history->last_refresh_s = now_s;
}
