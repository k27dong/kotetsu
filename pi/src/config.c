#include "config.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shared/file.h"
#include "shared/log.h"

#define PI_KEY_CREDENTIAL "pi-key"

static bool copy_setting(const char *variable, const char *fallback, char *out, size_t size) {
    const char *value = getenv(variable);
    if (value == nullptr || value[0] == '\0') {
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

static bool read_pi_key(struct config *config) {
    /* find the key file: named by hand, or the credential systemd passes in */
    char credential[sizeof config->state_path];
    const char *path = getenv("KOTETSU_PI_KEY_FILE");
    if (path == nullptr || path[0] == '\0') {
        const char *credentials = getenv("CREDENTIALS_DIRECTORY");
        if (credentials == nullptr || credentials[0] == '\0') {
            log_error("set KOTETSU_PI_KEY_FILE, or run under systemd with "
                      "LoadCredential=" PI_KEY_CREDENTIAL);
            return false;
        }
        const int printed =
                snprintf(credential, sizeof credential, "%s/" PI_KEY_CREDENTIAL, credentials);
        if (printed < 0 || (size_t)printed >= sizeof credential) {
            log_error("CREDENTIALS_DIRECTORY is over %zu bytes", sizeof credential - 1);
            return false;
        }
        path = credential;
    }

    /* read the key, without the newline that ends the file */
    char *key = config->pi_key;
    size_t length = 0;
    if (!file_read(path, key, sizeof config->pi_key, &length)) {
        log_error("can't read the Pi key from %s: %s", path, strerror(errno));
        return false;
    }
    if (length > 0 && key[length - 1] == '\n') {
        length--;
    }
    if (length == 0 || length == sizeof config->pi_key) {
        log_error("the Pi key in %s must be 1 to %zu bytes", path, sizeof config->pi_key - 1);
        return false;
    }
    key[length] = '\0';
    return true;
}

struct config config_load(void) {
    struct config config = {};
    config.ok = copy_setting("KOTETSU_DASHBOARD_URL", DASHBOARD_URL_DEFAULT, config.dashboard_url,
                             sizeof config.dashboard_url) &&
                copy_setting("KOTETSU_STATE", STATE_PATH_DEFAULT, config.state_path,
                             sizeof config.state_path) &&
                copy_setting("KOTETSU_FONT_DIR", FONT_DIR_DEFAULT, config.font_dir,
                             sizeof config.font_dir) &&
                read_pi_key(&config);
    return config;
}
