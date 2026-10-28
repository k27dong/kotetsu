#pragma once

#include <stddef.h>

static constexpr int SCREEN_WIDTH = 800;
static constexpr int SCREEN_HEIGHT = 480;

static constexpr int TICK_SECONDS = 15;
static constexpr int WEATHER_PERIOD_SECONDS = 1800;

static constexpr int MAX_PARTIALS = 4;
static constexpr int MIN_REFRESH_GAP_SECONDS = 180;

static constexpr int WEATHER_STALE_SECONDS = 7200;
static constexpr int WEATHER_OFFLINE_SECONDS = 43'200;

static constexpr int MIDNIGHT_GREETING_WAIT_SECONDS = 60;

static constexpr int HTTP_CONNECT_TIMEOUT_SECONDS = 5;
static constexpr int HTTP_TOTAL_TIMEOUT_SECONDS = 10;

static constexpr int BUSY_TIMEOUT_MS = 10'000;

static constexpr size_t PHRASE_TEXT_MAX_BYTES = 2048;

#define WARSAW_LATITUDE    "52.23"
#define WARSAW_LONGITUDE   "21.01"
#define WARSAW_COUNTY_CODE "1465"

#define DASHBOARD_URL_DEFAULT "https://kotetsu.k27dong.com"

#define FONT_DIR_DEFAULT "/usr/local/share/kotetsu/fonts"
#define FONT_FILE_CJK    "LXGWWenKaiGBScreen.ttf"
#define FONT_FILE_DIGITS "Inter-SemiBold.ttf"

#define STATE_PATH_DEFAULT      "/var/lib/kotetsu/state"
#define WIFI_STATE_PATH_DEFAULT "/run/kotetsu/wifi-state"

#define KOTETSU_VERSION "2.0.0"
