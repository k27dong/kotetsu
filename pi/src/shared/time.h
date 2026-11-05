#pragma once

#include <stddef.h>
#include <time.h>

static constexpr int SECONDS_PER_MINUTE = 60;
static constexpr int SECONDS_PER_HOUR = 3'600;
static constexpr int SECONDS_PER_DAY = 86'400;
static constexpr int MINUTES_PER_DAY = 1'440;

struct civil_date {
    int year;
    int month;
    int day;
};

// Warsaw is the local time zone: the process runs with TZ=Europe/Warsaw
struct civil_date time_warsaw_date(time_t now);
int time_warsaw_minute_of_day(time_t now);
[[nodiscard]] bool time_parse_iso8601(const char *text, time_t *out); // needs date, time and offset
[[nodiscard]] bool time_format_iso8601(time_t moment, char *out, size_t size);
bool time_clock_synced(void); // no /run/systemd/timesync means no timesyncd, so synced
