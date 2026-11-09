#include "phrase.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <cjson/cJSON.h>

#include "shared/dashboard.h"
#include "shared/http.h"
#include "shared/json.h"
#include "shared/log.h"
#include "shared/text.h"
#include "shared/time.h"

static const char *const GREETING_ICON_NAMES[] = {
        [GREETING_HEART] = "heart", [GREETING_RINGS] = "rings",   [GREETING_CAKE] = "cake",
        [GREETING_CAT] = "cat",     [GREETING_HEARTS] = "hearts", [GREETING_STAR] = "star",
};

static bool read_id(const cJSON *item, int64_t *out) {
    int64_t id = 0;
    if (!json_whole_number(item, &id) || id < 1) {
        return false;
    }
    *out = id;
    return true;
}

static bool read_text(const cJSON *item, const char *what, char *out, size_t size) {
    const char *text = cJSON_GetStringValue(item);
    if (text == nullptr) {
        log_error("the dashboard's %s isn't text", what);
        return false;
    }
    const size_t length = strlen(text);
    if (length == 0 || length >= size) {
        log_error("the dashboard's %s has %zu bytes, not 1 to %zu", what, length, size - 1);
        return false;
    }
    if (!text_is_valid_utf8(text, length)) {
        log_error("the dashboard's %s isn't UTF-8", what);
        return false;
    }
    memcpy(out, text, length + 1);
    return true;
}

static bool read_answer(const cJSON *root, struct phrase_answer *answer) {
    /* the phrase: a positive id, its text and when it stops being shown */
    if (!cJSON_IsObject(root)) {
        log_error("the dashboard's answer isn't a JSON object");
        return false;
    }
    const cJSON *phrase = json_member(root, "phrase");
    if (!cJSON_IsObject(phrase)) {
        log_error("the dashboard's answer has no phrase");
        return false;
    }
    if (!read_id(json_member(phrase, "id"), &answer->id)) {
        log_error("the dashboard's phrase id isn't a positive whole number");
        return false;
    }
    if (!read_text(json_member(phrase, "text"), "phrase text", answer->text, sizeof answer->text)) {
        return false;
    }
    const char *until = cJSON_GetStringValue(json_member(phrase, "until"));
    if (until == nullptr || !time_parse_iso8601(until, &answer->until)) {
        log_error("phrase %" PRId64 " comes without a readable until time", answer->id);
        return false;
    }

    /* the greeting: null, or one of the six icons with its text */
    const cJSON *greeting = json_member(root, "greeting");
    if (cJSON_IsObject(greeting)) {
        const char *icon = cJSON_GetStringValue(json_member(greeting, "icon"));
        bool known = false;
        if (icon != nullptr) {
            for (size_t i = 0; i < sizeof GREETING_ICON_NAMES / sizeof *GREETING_ICON_NAMES; i++) {
                if (strcmp(icon, GREETING_ICON_NAMES[i]) == 0) {
                    answer->greeting_icon = (enum greeting_icon)i;
                    known = true;
                    break;
                }
            }
        }
        if (!known) {
            log_error("the dashboard's greeting icon isn't one of the six");
            return false;
        }
        if (!read_text(json_member(greeting, "text"), "greeting", answer->greeting,
                       sizeof answer->greeting)) {
            return false;
        }
        answer->has_greeting = true;
    } else if (!cJSON_IsNull(greeting)) {
        log_error("the dashboard's greeting is neither an object nor null");
        return false;
    }

    /* the full-refresh request: null, or a positive id */
    const cJSON *full_refresh = json_member(root, "full_refresh");
    if (cJSON_IsNumber(full_refresh)) {
        if (!read_id(full_refresh, &answer->full_refresh)) {
            log_error("the dashboard's full_refresh isn't a positive whole number");
            return false;
        }
    } else if (!cJSON_IsNull(full_refresh)) {
        log_error("the dashboard's full_refresh is neither a number nor null");
        return false;
    }

    /* the clean-up minute of the Warsaw day */
    int64_t cleanup_minute = 0;
    if (!json_whole_number(json_member(root, "cleanupMinute"), &cleanup_minute) ||
        cleanup_minute < 0 || cleanup_minute >= MINUTES_PER_DAY) {
        log_error("the dashboard's cleanupMinute isn't a minute of the day");
        return false;
    }
    answer->cleanup_minute = (int)cleanup_minute;
    return true;
}

[[nodiscard]] static enum dashboard_status phrase_parse(const char *json,
                                                        struct phrase_answer *out) {
    cJSON *root = cJSON_Parse(json);
    struct phrase_answer answer = {};
    const bool good = read_answer(root, &answer);
    cJSON_Delete(root);
    if (!good) {
        return DASHBOARD_BAD_RESPONSE;
    }
    *out = answer;
    return DASHBOARD_OK;
}

struct phrase_state phrase_read(struct phrase_state previous, const char *url,
                                const struct http_response *response, time_t now) {
    /* read the answer */
    struct phrase_answer answer = {};
    enum dashboard_status status = DASHBOARD_UNREACHABLE;
    if (response->ok && response->status == HTTP_OK) {
        status = phrase_parse(response->body, &answer);
    } else if (response->ok) {
        status = dashboard_log_answer(url, response);
        if (response->status == 503) { // the dashboard's own answer when it has nothing to show
            status = DASHBOARD_NO_PHRASE;
        }
    }

    /* keep a good answer */
    const struct civil_date today = time_warsaw_date(now);
    struct phrase_state state = previous;
    state.status = status;
    if (status == DASHBOARD_OK) {
        state.have_answer = true;
        state.answer = answer;
        state.answered_on = today;
    }
    state.answered_today = state.have_answer && time_same_date(state.answered_on, today);
    return state;
}
