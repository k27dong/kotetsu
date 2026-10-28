#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "model.h"
#include "panel/refresh.h"

static constexpr size_t GREETING_TEXT_BYTES = 64;

struct dashboard_now {
    int64_t phrase_id;
    char phrase_text[PHRASE_TEXT_MAX_BYTES];
    int64_t until_s;
    bool has_greeting;
    enum small_icon greeting_icon;
    char greeting_text[GREETING_TEXT_BYTES];
    int64_t full_refresh; // 0 when the dashboard sent null
};

enum dashboard_status {
    DASHBOARD_OK,
    DASHBOARD_UNREACHABLE,
    DASHBOARD_UNAUTHORIZED,
    DASHBOARD_BAD_RESPONSE,
};

struct dashboard_report {
    enum refresh_kind refresh;
    int64_t at_s;
    int64_t phrase_id; // 0 when nothing is on the glass
    bool has_weather_at;
    int64_t weather_at_s;
    int64_t too_long_id; // 0 when none
    int64_t missing_glyphs_id;
    bool has_wifi_signal;
    double wifi_signal;
    bool has_pi_temperature;
    double pi_temperature_c;
    const char *last_error; // nullptr when none
};

[[nodiscard]] enum dashboard_status dashboard_parse_now(const char *json,
                                                        struct dashboard_now *out);
[[nodiscard]] enum dashboard_status dashboard_fetch_now(const char *base_url, const char *pi_key,
                                                        struct dashboard_now *out);
[[nodiscard]] enum dashboard_status dashboard_upload(const char *base_url, const char *pi_key,
                                                     const uint8_t *png, size_t png_length,
                                                     const char *report_json);
[[nodiscard]] bool dashboard_report_json(const struct dashboard_report *report, char *out,
                                         size_t size);
