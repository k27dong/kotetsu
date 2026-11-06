#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shared/file.h"
#include "shared/log.h"

#define PI_KEY_CREDENTIAL "pi-key"

static constexpr size_t CREDENTIAL_PATH_BYTES = 4'096; // the longest path Linux takes
static constexpr size_t KEY_FILE_BYTES = 1'024;        // the key and any whitespace around it

static const char *setting(const char *variable) { // nullptr when unset or empty
    const char *value = getenv(variable);
    return value != nullptr && value[0] != '\0' ? value : nullptr;
}

static bool copy_setting(const char *variable, const char *fallback, char *out, size_t size) {
    const char *value = setting(variable);
    if (value == nullptr) {
        value = fallback;
    }
    const size_t length = strlen(value);
    if (length >= size) {
        log_error("%s is over %zu bytes", variable, size - 1);
        return false;
    }
    memcpy(out, value, length + 1);
    return true;
}

static bool read_pi_key(char *out, size_t size) {
    /* find the key file: named by hand, or the credential systemd passes in */
    char credential[CREDENTIAL_PATH_BYTES];
    const char *path = setting("KOTETSU_PI_KEY_FILE");
    if (path == nullptr) {
        const char *credentials = setting("CREDENTIALS_DIRECTORY");
        if (credentials == nullptr) {
            log_error("set KOTETSU_PI_KEY_FILE, or run under systemd with "
                      "LoadCredential=" PI_KEY_CREDENTIAL);
            return false;
        }
        const int printed =
                snprintf(credential, sizeof credential, "%s/" PI_KEY_CREDENTIAL, credentials);
        if (printed < 0 || (size_t)printed >= sizeof credential) {
            log_error("CREDENTIALS_DIRECTORY is over %zu bytes",
                      sizeof credential - sizeof "/" PI_KEY_CREDENTIAL);
            return false;
        }
        path = credential;
    }

    /* read the file, and leave out the whitespace around the key */
    char contents[KEY_FILE_BYTES];
    size_t end = 0;
    if (!file_read(path, contents, sizeof contents, &end)) {
        log_error("can't read the Pi key from %s: %s", path, strerror(errno));
        return false;
    }
    size_t start = 0;
    while (start < end && isspace((unsigned char)contents[start])) {
        start++;
    }
    while (end > start && isspace((unsigned char)contents[end - 1])) {
        end--;
    }

    /* copy the key out, when there is one and it fits */
    const size_t length = end - start;
    if (length == 0 || length >= size) {
        log_error("the Pi key in %s must be 1 to %zu bytes", path, size - 1);
        return false;
    }
    memcpy(out, contents + start, length);
    out[length] = '\0';
    return true;
}

struct config config_load(void) {
    struct config config = {};

    /* a key file means a run by hand, which must name its dashboard: the default is the wall's */
    if (setting("KOTETSU_PI_KEY_FILE") != nullptr && setting("KOTETSU_DASHBOARD_URL") == nullptr) {
        log_error("set KOTETSU_DASHBOARD_URL: a run with KOTETSU_PI_KEY_FILE has no default "
                  "dashboard, so it can't replace the wall's picture by mistake");
        return config;
    }

    /* the settings, each with the Pi's default, and the key */
    config.ok = copy_setting("KOTETSU_DASHBOARD_URL", DASHBOARD_URL_DEFAULT, config.dashboard_url,
                             sizeof config.dashboard_url) &&
                copy_setting("KOTETSU_STATE", STATE_PATH_DEFAULT, config.state_path,
                             sizeof config.state_path) &&
                copy_setting("KOTETSU_FONT_DIR", FONT_DIR_DEFAULT, config.font_dir,
                             sizeof config.font_dir) &&
                read_pi_key(config.pi_key, sizeof config.pi_key);
    return config;
}
