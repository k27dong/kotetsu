#include "panel/state.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <glib.h>

#include "log.h"
#include "panel/refresh.h"
#include "render/frame.h"

#define STATE_MAGIC "KTS2"

static constexpr uint8_t STATE_VERSION = 1;
static constexpr size_t DIGEST_BYTES = 32;

struct state_file {
    char magic[4];
    uint8_t version;
    uint8_t on_glass[FRAME_BYTES];
    uint8_t glass_known;
    uint8_t partials_since_full[sizeof(int32_t)];
    uint8_t last_refresh_s[sizeof(int64_t)];
    char app_json[STATE_APP_JSON_BYTES];
    uint8_t digest[DIGEST_BYTES];
};

static constexpr size_t PAYLOAD_START = offsetof(struct state_file, on_glass);
static constexpr size_t PAYLOAD_BYTES = offsetof(struct state_file, digest) - PAYLOAD_START;

static void digest_payload(const struct state_file *file, uint8_t *digest) {
    GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
    g_checksum_update(checksum, (const guchar *)file + PAYLOAD_START, (gssize)PAYLOAD_BYTES);
    gsize length = DIGEST_BYTES;
    g_checksum_get_digest(checksum, digest, &length);
    g_checksum_free(checksum);
}

static void encode(const struct saved_state *state, struct state_file *file) {
    memcpy(file->magic, STATE_MAGIC, sizeof file->magic);
    file->version = STATE_VERSION;
    memcpy(file->on_glass, state->on_glass.bits, sizeof file->on_glass);
    file->glass_known = state->history.glass_known;
    const int32_t partials_since_full = state->history.partials_since_full;
    memcpy(file->partials_since_full, &partials_since_full, sizeof file->partials_since_full);
    memcpy(file->last_refresh_s, &state->history.last_refresh_s, sizeof file->last_refresh_s);
    const size_t json_length = strnlen(state->app_json, sizeof state->app_json);
    memcpy(file->app_json, state->app_json, json_length);
    memset(file->app_json + json_length, 0, sizeof file->app_json - json_length);
    digest_payload(file, file->digest);
}

static void decode(const struct state_file *file, struct saved_state *out) {
    memcpy(out->on_glass.bits, file->on_glass, sizeof out->on_glass.bits);
    int32_t partials_since_full;
    memcpy(&partials_since_full, file->partials_since_full, sizeof partials_since_full);
    int64_t last_refresh_s;
    memcpy(&last_refresh_s, file->last_refresh_s, sizeof last_refresh_s);
    out->history = (struct refresh_history){
            .glass_known = file->glass_known != 0,
            .partials_since_full = partials_since_full,
            .last_refresh_s = last_refresh_s,
    };
    memcpy(out->app_json, file->app_json, sizeof out->app_json);
}

static bool is_intact(const char *path, const struct state_file *file, size_t length) {
    if (length != sizeof *file) {
        log_warning("saved state %s has %zu bytes, not %zu", path, length, sizeof *file);
        return false;
    }
    if (memcmp(file->magic, STATE_MAGIC, sizeof file->magic) != 0) {
        log_warning("%s isn't a kotetsu state file", path);
        return false;
    }
    if (file->version != STATE_VERSION) {
        log_warning("saved state %s is version %d, not %d", path, file->version, STATE_VERSION);
        return false;
    }
    uint8_t digest[DIGEST_BYTES];
    digest_payload(file, digest);
    if (memcmp(digest, file->digest, sizeof digest) != 0) {
        log_warning("saved state %s fails its checksum", path);
        return false;
    }
    if (memchr(file->app_json, '\0', sizeof file->app_json) == nullptr) {
        log_warning("saved state %s holds an app state that isn't a terminated string", path);
        return false;
    }
    return true;
}

bool state_load(const char *path, struct saved_state *out) {
    gchar *contents = nullptr;
    gsize length = 0;
    GError *error = nullptr;
    if (!g_file_get_contents(path, &contents, &length, &error)) {
        if (g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            log_info("no saved state at %s", path);
        } else {
            log_error("can't read the saved state: %s", error->message);
        }
        g_error_free(error);
        return false;
    }
    const struct state_file *file = (const struct state_file *)contents;
    const bool intact = is_intact(path, file, length);
    if (intact) {
        decode(file, out);
    }
    g_free(contents);
    return intact;
}

bool state_save(const char *path, const struct saved_state *state) {
    if (memchr(state->app_json, '\0', sizeof state->app_json) == nullptr) {
        log_error("can't save %s: the app state isn't a terminated string", path);
        return false;
    }
    struct state_file *file = g_new(struct state_file, 1);
    encode(state, file);
    GError *error = nullptr;
    const bool saved = g_file_set_contents_full(
            path, (const gchar *)file, (gssize)sizeof *file,
            G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, &error);
    if (!saved) {
        log_error("can't save the state: %s", error->message);
        g_error_free(error);
    }
    g_free(file);
    return saved;
}
