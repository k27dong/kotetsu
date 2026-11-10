#pragma once

#include "screen/screen.h"
#include "weather.h"

[[nodiscard]] bool state_save(const char *path, const struct wall *wall,
                              const struct weather_state *weather);

struct saved_state {
    bool ok;          // when false, wall is screen_new_wall()
    struct wall wall; // known is always false after a restart
    bool have_weather;
    struct weather_report weather;
};

[[nodiscard]] struct saved_state state_restore(const char *path);
