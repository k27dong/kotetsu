#pragma once

#include <stddef.h>

static constexpr int SCREEN_WIDTH = 800;
static constexpr int SCREEN_HEIGHT = 480;

static constexpr size_t PHRASE_TEXT_MAX_BYTES = 2048;

#define DASHBOARD_URL_DEFAULT "https://kotetsu.k27dong.com"

#define FONT_DIR_DEFAULT "/usr/local/share/kotetsu/fonts"

#define STATE_PATH_DEFAULT "/var/lib/kotetsu/state"

static constexpr size_t PI_KEY_MAX_BYTES = 256;

struct config {
    bool ok;
    char dashboard_url[256];
    char state_path[256];
    char font_dir[256];
    char pi_key[PI_KEY_MAX_BYTES];
};

[[nodiscard]] struct config config_load(void);
