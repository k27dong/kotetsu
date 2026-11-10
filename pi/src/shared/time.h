#pragma once

#include <stddef.h>
#include <time.h>

static constexpr int SECONDS_PER_MINUTE = 60;
static constexpr int SECONDS_PER_HOUR = 3'600;
static constexpr int SECONDS_PER_DAY = 86'400;
static constexpr int MINUTES_PER_HOUR = 60;
static constexpr int MINUTES_PER_DAY = 1'440;

struct civil_date {
    int year;
    int month;
    int day;
};

// in local time, which main sets to Europe/Warsaw
struct civil_date time_warsaw_date(time_t now);
int time_warsaw_minute_of_day(time_t now);
bool time_same_date(struct civil_date a, struct civil_date b);
// needs the date, the time and an offset
[[nodiscard]] bool time_parse_iso8601(const char *text, time_t *out);
[[nodiscard]] bool time_format_iso8601(time_t moment, char *out, size_t size);
// true once timesyncd has synced the clock, or without timesyncd
bool time_clock_synced(void);
// zero once the tick has run its length
[[nodiscard]] struct timespec time_left_in_tick(struct timespec started, struct timespec now,
                                                int tick_seconds);
