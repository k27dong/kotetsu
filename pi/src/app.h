#pragma once

#include <stddef.h>
#include <stdint.h>

#include "panel/refresh.h"
#include "render/frame.h"
#include "sources/dashboard.h"
#include "sources/weather.h"
#include "sources/wifi.h"

struct app_ports {
    void *context;
    enum dashboard_status (*fetch_now)(void *context, struct dashboard_now *out);
    enum dashboard_status (*upload)(void *context, const uint8_t *png, size_t length,
                                    const char *report_json);
    bool (*fetch_weather)(void *context, int64_t now_s, struct weather_inputs *out);
    enum wifi_state (*read_wifi)(void *context);
    bool (*clock_synced)(void *context);
    bool (*show)(void *context, const struct frame *on_glass, const struct frame *next,
                 enum refresh_kind kind);
};

struct app_config {
    const char *state_path;
    const char *font_dir;
};

struct app;

[[nodiscard]] struct app *app_create(const struct app_config *config,
                                     const struct app_ports *ports);
void app_tick(struct app *app, int64_t now_s);
void app_destroy(struct app *app);
