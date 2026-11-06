#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "config.h"
#include "draw/frame.h"
#include "screen/screen.h"
#include "weather.h"

struct record_state {
    char last_error[128];
    int64_t reported_too_long_id;
};

// fills the wall and the weather report from the file; the glass comes back unknown
[[nodiscard]] bool record_restore(const char *path, struct wall *wall,
                                  struct weather_state *weather);
[[nodiscard]] bool record_save(const char *path, const struct wall *wall,
                               const struct weather_state *weather);
// keeps it for the next report; the caller has logged it where it happened
void record_error(struct record_state *state, const char *text);

struct report { // 0 or nullptr means none
    enum refresh_kind refresh;
    time_t at;
    int64_t phrase_id;
    time_t weather_at;
    int64_t too_long_id;
    int64_t missing_glyphs_id;
    const char *last_error;
};

struct readings {
    bool has_wifi_signal;
    double wifi_signal; // the kernel's 0–70 link quality
    bool has_pi_temperature;
    double pi_temperature_c;
};

struct readings record_read_device(void);
[[nodiscard]] bool record_report_json(const struct report *report, const struct readings *readings,
                                      char *out, size_t size);
bool record_should_upload(const struct record_state *state, bool refreshed, int64_t too_long_id,
                          bool glass_known);
// a success clears the kept error and marks the too-long phrase reported
[[nodiscard]] bool record_upload(struct record_state *state, const struct config *config,
                                 const struct frame *frame, const struct report *report);

struct tick_summary {
    const char *dashboard; // "dashboard ok", or the problem
    const char *weather;   // "weather fetched", "weather not due" or "weather failed"
    int64_t phrase_id;     // the phrase drawn; 0 for none
    bool too_long;         // the wanted phrase didn't fit
    int missing_glyphs;
    struct decision decision;
    bool refreshed;     // the decided refresh worked
    const char *upload; // "upload ok", "no upload" or "upload failed"
};

void record_log_tick(const struct tick_summary *tick);
