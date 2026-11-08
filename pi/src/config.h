#pragma once

#include <stddef.h>

static constexpr int SCREEN_WIDTH = 800;
static constexpr int SCREEN_HEIGHT = 480;

static constexpr size_t PHRASE_TEXT_MAX_BYTES = 2048;

#define DASHBOARD_URL_DEFAULT "https://kotetsu.k27dong.com"

#define FONT_DIR_DEFAULT "/usr/local/share/kotetsu/fonts"

#define STATE_PATH_DEFAULT "/var/lib/kotetsu/state"

#define KOTETSU_VERSION "2.0.0"

static constexpr size_t PI_KEY_MAX_BYTES = 256;

struct config {
    bool ok; // false when the key can't be read, a value is too long or a key file comes without
             // a dashboard URL
    char dashboard_url[256];
    char state_path[256];
    char font_dir[256];
    char pi_key[PI_KEY_MAX_BYTES];
};

struct config config_load(void);
