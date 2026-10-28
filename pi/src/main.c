#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <curl/curl.h>
#include <glib.h>

#include "app.h"
#include "calendar/clock.h"
#include "config.h"
#include "log.h"
#include "panel/display.h"
#include "panel/refresh.h"
#include "render/frame.h"
#include "sources/dashboard.h"
#include "sources/weather.h"
#include "sources/wifi.h"

#define USAGE            "usage: kotetsu [--once] [--now <ISO-8601>] [--clear] [--version]"
#define PNG_DIR_DEFAULT  "pi/build/png"
#define PI_KEY_FILE_NAME "pi-key"

struct runtime {
    struct display *display;
    const char *dashboard_url;
    char *pi_key;
    const char *wifi_state_path;
    struct weather_source weather;
};

static volatile sig_atomic_t stop_requested;

static void request_stop([[maybe_unused]] int signal_number) {
    stop_requested = 1;
}

static enum dashboard_status fetch_now(void *context, struct dashboard_now *out) {
    const struct runtime *runtime = context;
    return dashboard_fetch_now(runtime->dashboard_url, runtime->pi_key, out);
}

static enum dashboard_status upload(void *context, const uint8_t *png, size_t length,
                                    const char *report_json) {
    const struct runtime *runtime = context;
    return dashboard_upload(runtime->dashboard_url, runtime->pi_key, png, length, report_json);
}

static bool fetch_weather(void *context, int64_t now_s, struct weather_inputs *out) {
    struct runtime *runtime = context;
    return weather_fetch(&runtime->weather, now_s, out);
}

static enum wifi_state read_wifi(void *context) {
    const struct runtime *runtime = context;
    return wifi_read_state(runtime->wifi_state_path);
}

static bool show(void *context, const struct frame *on_glass, const struct frame *next,
                 enum refresh_kind kind) {
    const struct runtime *runtime = context;
    return display_show(runtime->display, on_glass, next, kind);
}

static const char *setting(const char *variable, const char *fallback) {
    const char *value = getenv(variable);
    return value != nullptr && value[0] != '\0' ? value : fallback;
}

static char *read_pi_key(void) {
    const char *given = getenv("KOTETSU_PI_KEY_FILE");
    const char *credentials = getenv("CREDENTIALS_DIRECTORY");
    gchar *path = nullptr;
    if (given != nullptr && given[0] != '\0') {
        path = g_strdup(given);
    } else if (credentials != nullptr) {
        path = g_build_filename(credentials, PI_KEY_FILE_NAME, nullptr);
    } else {
        log_error("set KOTETSU_PI_KEY_FILE, or run under systemd with "
                  "LoadCredential=" PI_KEY_FILE_NAME);
        return nullptr;
    }
    gchar *key = nullptr;
    GError *error = nullptr;
    if (!g_file_get_contents(path, &key, nullptr, &error)) {
        log_error("can't read the Pi key: %s", error->message);
        g_error_free(error);
    } else if (g_strstrip(key)[0] == '\0') {
        log_error("the Pi key file %s is empty", path);
        g_free(key);
        key = nullptr;
    }
    g_free(path);
    return key;
}

static bool stop_on(int signal_number) {
    if (signal(signal_number, request_stop) == SIG_ERR) {
        log_error("can't handle signal %d", signal_number);
        return false;
    }
    return true;
}

static int run(struct runtime *runtime, bool once) {
    const struct app_config config = {
            .state_path = setting("KOTETSU_STATE", STATE_PATH_DEFAULT),
            .font_dir = setting("KOTETSU_FONT_DIR", FONT_DIR_DEFAULT),
    };
    const struct app_ports ports = {
            .context = runtime,
            .fetch_now = fetch_now,
            .upload = upload,
            .fetch_weather = fetch_weather,
            .read_wifi = read_wifi,
            .show = show,
    };
    struct app *app = app_create(&config, &ports);
    if (app == nullptr) {
        return EXIT_FAILURE;
    }
    int status = EXIT_FAILURE;
    if (stop_on(SIGTERM) && stop_on(SIGINT)) {
        log_info("kotetsu %s: dashboard %s, state %s", KOTETSU_VERSION, runtime->dashboard_url,
                 config.state_path);
        while (!stop_requested) {
            app_tick(app, clock_now());
            if (once) {
                break;
            }
            sleep((unsigned)TICK_SECONDS); // a stop signal cuts the sleep short
        }
        status = EXIT_SUCCESS;
    }
    app_destroy(app);
    return status;
}

int main(int argc, char *argv[]) {
    bool once = false;
    bool clear = false;
    const char *now_text = nullptr;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            puts("kotetsu " KOTETSU_VERSION);
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "--once") == 0) {
            once = true;
        } else if (strcmp(argv[i], "--clear") == 0) {
            clear = true;
        } else if (strcmp(argv[i], "--now") == 0 && i + 1 < argc) {
            now_text = argv[++i];
        } else {
            log_error(USAGE);
            return EXIT_FAILURE;
        }
    }
    if (now_text != nullptr) {
        int64_t now_s = 0;
        if (!clock_parse_iso8601(now_text, &now_s)) {
            log_error("--now needs an ISO 8601 time with an offset, not %s", now_text);
            return EXIT_FAILURE;
        }
        clock_set_override(now_s);
    }
    if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
        log_error("can't initialise libcurl");
        return EXIT_FAILURE;
    }
    struct runtime runtime = {
            .dashboard_url = setting("KOTETSU_DASHBOARD_URL", DASHBOARD_URL_DEFAULT),
            .wifi_state_path = setting("KOTETSU_WIFI_STATE", WIFI_STATE_PATH_DEFAULT),
    };
    int status = EXIT_FAILURE;
    runtime.display = display_open(setting("KOTETSU_PNG_DIR", PNG_DIR_DEFAULT));
    if (runtime.display == nullptr) {
        status = EXIT_FAILURE;
    } else if (clear) {
        status = display_clear(runtime.display) ? EXIT_SUCCESS : EXIT_FAILURE;
    } else if ((runtime.pi_key = read_pi_key()) != nullptr) {
        status = run(&runtime, once);
    }
    g_free(runtime.pi_key);
    display_close(runtime.display);
    curl_global_cleanup();
    return status;
}
