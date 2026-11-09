/* the state file: the wall and the last weather report, kept across restarts */
#include "state.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cjson/cJSON.h>

#include "draw/frame.h"
#include "screen/screen.h"
#include "shared/file.h"
#include "shared/json.h"
#include "shared/log.h"
#include "shared/text.h"
#include "shared/time.h"
#include "weather.h"

#define STATE_MAGIC "KTS2"

static constexpr uint8_t STATE_VERSION = 2;
static constexpr size_t STATE_JSON_BYTES = 16'384; // fits the longest phrase even fully escaped
static constexpr uint64_t CHECKSUM_START = 0xcbf2'9ce4'8422'2325; // FNV-1a, 64-bit
static constexpr uint64_t CHECKSUM_PRIME = 0x100'0000'01b3;

struct state_file { // bytes only, so the layout has no padding
    char magic[4];
    uint8_t version;
    uint8_t frame[FRAME_BYTES];
    uint8_t glass_known;
    uint8_t partials_since_full[sizeof(int32_t)];
    uint8_t last_refresh[sizeof(int64_t)];
    char json[STATE_JSON_BYTES];
    uint8_t checksum[sizeof(uint64_t)];
};

static uint64_t checksum_of(const struct state_file *file) {
    const uint8_t *bytes = (const uint8_t *)file;
    uint64_t sum = CHECKSUM_START;
    for (size_t i = 0; i < offsetof(struct state_file, checksum); i++) {
        sum = (sum ^ bytes[i]) * CHECKSUM_PRIME;
    }
    return sum;
}

bool state_save(const char *path, const struct wall *wall, const struct weather_state *weather) {
    /* print the JSON part */
    cJSON *root = cJSON_CreateObject();
    if (wall->phrase_id != 0) {
        cJSON_AddNumberToObject(root, "phraseId", (double)wall->phrase_id);
        cJSON_AddStringToObject(root, "phraseText", wall->phrase_text);
    }
    if (wall->full_refresh_done != 0) {
        cJSON_AddNumberToObject(root, "fullRefresh", (double)wall->full_refresh_done);
    }
    cJSON_AddNumberToObject(root, "cleanupMinute", wall->cleanup_minute);
    if (weather->have_report) {
        const struct weather_report *report = &weather->report;
        cJSON *saved = cJSON_AddObjectToObject(root, "weather");
        cJSON_AddNumberToObject(saved, "fetchedAt", (double)report->fetched_at);
        cJSON_AddNumberToObject(saved, "nowC", report->now_c);
        cJSON_AddNumberToObject(saved, "lowC", report->low_c);
        cJSON_AddNumberToObject(saved, "highC", report->high_c);
        cJSON_AddNumberToObject(saved, "code", report->code);
    }
    struct state_file file = {};
    const bool printed = cJSON_PrintPreallocated(root, file.json, (int)sizeof file.json, false);
    cJSON_Delete(root);
    if (!printed) {
        log_error("the state doesn't fit the %zu bytes of JSON in %s", sizeof file.json, path);
        return false;
    }

    /* fill the binary part */
    memcpy(file.magic, STATE_MAGIC, sizeof file.magic);
    file.version = STATE_VERSION;
    memcpy(file.frame, wall->frame.bits, sizeof file.frame);
    file.glass_known = wall->known;
    const int32_t partials_since_full = wall->partials_since_full;
    memcpy(file.partials_since_full, &partials_since_full, sizeof file.partials_since_full);
    const int64_t last_refresh = wall->last_refresh;
    memcpy(file.last_refresh, &last_refresh, sizeof file.last_refresh);
    const uint64_t checksum = checksum_of(&file);
    memcpy(file.checksum, &checksum, sizeof file.checksum);

    /* replace the file atomically */
    if (!file_write_atomic(path, &file, sizeof file)) { // a failure may still have replaced it
        log_error("can't save the state to %s: %s", path, strerror(errno));
        return false;
    }
    return true;
}

static bool is_intact(const char *path, const struct state_file *file, size_t length) {
    /* check size, magic and version */
    if (length != sizeof *file) {
        log_warning("ignoring the saved state %s: it has %zu bytes, not %zu", path, length,
                    sizeof *file);
        return false;
    }
    if (memcmp(file->magic, STATE_MAGIC, sizeof file->magic) != 0) {
        log_warning("ignoring %s: it isn't a kotetsu state file", path);
        return false;
    }
    if (file->version != STATE_VERSION) {
        log_warning("ignoring the saved state %s: it is version %d, not %d", path, file->version,
                    STATE_VERSION);
        return false;
    }

    /* check the checksum */
    uint64_t checksum = 0;
    memcpy(&checksum, file->checksum, sizeof checksum);
    if (checksum != checksum_of(file)) {
        log_warning("ignoring the saved state %s: it fails its checksum", path);
        return false;
    }
    return true;
}

static bool restore_json(const char *path, const cJSON *root, struct saved_state *state) {
    /* the glass phrase */
    if (!cJSON_IsObject(root)) {
        log_warning("ignoring the saved state %s: its JSON isn't an object", path);
        return false;
    }
    int64_t phrase_id = 0;
    const char *phrase_text = "";
    if (json_member(root, "phraseId") != nullptr) {
        phrase_text = cJSON_GetStringValue(json_member(root, "phraseText"));
        const size_t length = phrase_text != nullptr ? strlen(phrase_text) : 0;
        if (!json_whole_number(json_member(root, "phraseId"), &phrase_id) || phrase_id < 1 ||
            length == 0 || length >= sizeof state->wall.phrase_text ||
            !text_is_valid_utf8(phrase_text, length)) {
            log_warning("ignoring the saved state %s: its phrase is unusable", path);
            return false;
        }
    }

    /* full refresh and clean-up minute */
    int64_t full_refresh = 0;
    const cJSON *saved_full_refresh = json_member(root, "fullRefresh");
    if (saved_full_refresh != nullptr && !json_whole_number(saved_full_refresh, &full_refresh)) {
        log_warning("ignoring the saved state %s: its full refresh is unusable", path);
        return false;
    }
    int64_t cleanup_minute = 0;
    if (!json_whole_number(json_member(root, "cleanupMinute"), &cleanup_minute) ||
        cleanup_minute < 0 || cleanup_minute >= MINUTES_PER_DAY) {
        log_warning("ignoring the saved state %s: its clean-up minute is unusable", path);
        return false;
    }

    /* the last weather report */
    const cJSON *saved_weather = json_member(root, "weather");
    int64_t fetched_at = 0;
    int64_t now_c = 0;
    int64_t low_c = 0;
    int64_t high_c = 0;
    int64_t code = 0;
    if (saved_weather != nullptr &&
        !(json_whole_number(json_member(saved_weather, "fetchedAt"), &fetched_at) &&
          json_whole_number(json_member(saved_weather, "nowC"), &now_c) &&
          json_whole_number(json_member(saved_weather, "lowC"), &low_c) &&
          json_whole_number(json_member(saved_weather, "highC"), &high_c) &&
          json_whole_number(json_member(saved_weather, "code"), &code))) {
        log_warning("ignoring the saved state %s: its weather report is unusable", path);
        return false;
    }

    /* all good, so keep them */
    state->wall.phrase_id = phrase_id;
    memcpy(state->wall.phrase_text, phrase_text, strlen(phrase_text) + 1);
    state->wall.full_refresh_done = full_refresh;
    state->wall.cleanup_minute = (int)cleanup_minute;
    if (saved_weather != nullptr) {
        state->have_weather = true;
        state->weather = (struct weather_report){
                .fetched_at = fetched_at,
                .now_c = (int)now_c,
                .low_c = (int)low_c,
                .high_c = (int)high_c,
                .code = (int)code,
        };
    }
    return true;
}

struct saved_state state_restore(const char *path) {
    /* start from a new wall */
    struct saved_state saved = {.wall = screen_new_wall()};

    /* read the file */
    struct state_file file;
    size_t length = 0;
    if (!file_read(path, &file, sizeof file, &length)) {
        if (errno == ENOENT) { // none before the first save
            log_info("no saved state at %s", path);
        } else if (errno == EFBIG) {
            log_warning("ignoring the saved state %s: it has over %zu bytes", path, sizeof file);
        } else {
            log_error("can't read the saved state %s: %s", path, strerror(errno));
        }
        return saved;
    }
    if (!is_intact(path, &file, length)) {
        return saved;
    }

    /* JSON part, all or nothing */
    cJSON *root = cJSON_ParseWithLength(file.json, sizeof file.json);
    const bool restored = restore_json(path, root, &saved);
    cJSON_Delete(root);
    if (!restored) {
        return saved;
    }

    /* frame and refresh history */
    memcpy(saved.wall.frame.bits, file.frame, sizeof saved.wall.frame.bits);
    int32_t partials_since_full = 0;
    memcpy(&partials_since_full, file.partials_since_full, sizeof partials_since_full);
    int64_t last_refresh = 0;
    memcpy(&last_refresh, file.last_refresh, sizeof last_refresh);
    saved.wall.partials_since_full = partials_since_full;
    saved.wall.last_refresh = last_refresh;
    saved.ok = true; // the glass stays unknown after a restart
    return saved;
}
