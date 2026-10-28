#include "sources/wifi.h"

#include <stddef.h>
#include <string.h>

#include <glib.h>

#include "log.h"

static const char *const STATE_WORDS[] = {
        [WIFI_STATE_HOTSPOT] = "HOTSPOT",
        [WIFI_STATE_CONNECTING] = "CONNECTING",
        [WIFI_STATE_CONNECTED] = "CONNECTED",
};

static bool read_file(const char *path, gchar **contents) {
    GError *error = nullptr;
    if (g_file_get_contents(path, contents, nullptr, &error)) {
        return true;
    }
    if (!g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
        log_error("can't read %s: %s", path, error->message);
    }
    g_error_free(error);
    return false;
}

enum wifi_state wifi_read_state(const char *path) {
    gchar *contents = nullptr;
    if (!read_file(path, &contents)) {
        return WIFI_STATE_UNKNOWN;
    }
    const char *word = g_strstrip(contents);
    enum wifi_state state = WIFI_STATE_UNKNOWN;
    for (size_t i = 0; i < G_N_ELEMENTS(STATE_WORDS); i++) {
        if (STATE_WORDS[i] != nullptr && strcmp(word, STATE_WORDS[i]) == 0) {
            state = (enum wifi_state)i;
        }
    }
    if (state == WIFI_STATE_UNKNOWN) {
        log_warning("%s holds \"%s\", not a Comitup state", path, word);
    }
    g_free(contents);
    return state;
}

bool wifi_read_signal(const char *path, double *out) {
    gchar *contents = nullptr;
    if (!read_file(path, &contents)) {
        return false;
    }
    bool found = false;
    gchar **lines = g_strsplit(contents, "\n", -1);
    for (gchar **line = lines; *line != nullptr && !found; line++) {
        // an interface line reads "wlan0: 0000   70.  -40.  -256 ..."; the headings have no colon
        const char *after_name = strchr(*line, ':');
        if (after_name == nullptr) {
            continue;
        }
        const char *after_status = after_name + 1;
        after_status += strspn(after_status, " ");
        after_status += strcspn(after_status, " ");
        char *end = nullptr;
        const double quality = g_ascii_strtod(after_status, &end);
        found = end != after_status;
        if (found) {
            *out = quality;
        }
    }
    g_strfreev(lines);
    g_free(contents);
    return found;
}
