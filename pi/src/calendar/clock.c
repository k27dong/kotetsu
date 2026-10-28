#include "calendar/clock.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "calendar/calendar.h"
#include "log.h"
#include "model.h"

#define TIMESYNC_SYNCHRONIZED_PATH "/run/systemd/timesync/synchronized"

#ifdef __APPLE__
static constexpr bool ALWAYS_SYNCED = true; // macOS keeps its own clock and has no timesyncd
#else
static constexpr bool ALWAYS_SYNCED = false;
#endif

static constexpr struct civil_date UNIX_EPOCH = {1970, 1, 1};
static constexpr int64_t SECONDS_PER_DAY = 86'400;
static constexpr int64_t SECONDS_PER_HOUR = 3600;
static constexpr int64_t SECONDS_PER_MINUTE = 60;
static constexpr int64_t NANOSECONDS_PER_SECOND = 1'000'000'000;

static bool override_active;
static int64_t override_start_s;
static struct timespec override_set_at;

static struct timespec read_clock(clockid_t clock_id) {
    struct timespec now;
    if (clock_gettime(clock_id, &now) != 0) {
        log_error("clock: clock_gettime failed: %s", strerror(errno));
        abort();
    }
    return now;
}

int64_t clock_now(void) {
    if (!override_active) {
        return read_clock(CLOCK_REALTIME).tv_sec;
    }
    struct timespec now = read_clock(CLOCK_MONOTONIC);
    int64_t elapsed_ns = (now.tv_sec - override_set_at.tv_sec) * NANOSECONDS_PER_SECOND +
                         (now.tv_nsec - override_set_at.tv_nsec);
    return override_start_s + elapsed_ns / NANOSECONDS_PER_SECOND;
}

void clock_set_override(int64_t start_s) {
    override_start_s = start_s;
    override_set_at = read_clock(CLOCK_MONOTONIC);
    override_active = true;
}

bool clock_is_overridden(void) {
    return override_active;
}

static bool read_digits(const char **cursor, int count, int *out) {
    int value = 0;
    for (int i = 0; i < count; i++) {
        char digit = (*cursor)[i];
        if (digit < '0' || digit > '9') {
            return false;
        }
        value = value * 10 + (digit - '0');
    }
    *cursor += count;
    *out = value;
    return true;
}

static bool read_char(const char **cursor, char expected) {
    if (**cursor != expected) {
        return false;
    }
    (*cursor)++;
    return true;
}

static bool read_offset(const char **cursor, int *offset_s) {
    if (read_char(cursor, 'Z')) {
        *offset_s = 0;
        return true;
    }
    int sign = 0;
    if (read_char(cursor, '+')) {
        sign = 1;
    } else if (read_char(cursor, '-')) {
        sign = -1;
    } else {
        return false;
    }
    int hours = 0;
    int minutes = 0;
    if (!read_digits(cursor, 2, &hours) || !read_char(cursor, ':') ||
        !read_digits(cursor, 2, &minutes) || hours > 23 || minutes > 59) {
        return false;
    }
    *offset_s = sign * (hours * 3600 + minutes * 60);
    return true;
}

bool clock_parse_iso8601(const char *text, int64_t *out_s) {
    const char *cursor = text;
    struct civil_date date = {};
    int hour = 0;
    int minute = 0;
    if (!read_digits(&cursor, 4, &date.year) || !read_char(&cursor, '-') ||
        !read_digits(&cursor, 2, &date.month) || !read_char(&cursor, '-') ||
        !read_digits(&cursor, 2, &date.day) || !read_char(&cursor, 'T') ||
        !read_digits(&cursor, 2, &hour) || !read_char(&cursor, ':') ||
        !read_digits(&cursor, 2, &minute)) {
        return false;
    }
    int second = 0;
    int milliseconds = 0;
    if (read_char(&cursor, ':') &&
        (!read_digits(&cursor, 2, &second) ||
         (read_char(&cursor, '.') && !read_digits(&cursor, 3, &milliseconds)))) {
        return false;
    }
    int offset_s = 0;
    if (!read_offset(&cursor, &offset_s) || *cursor != '\0' || hour > 23 || minute > 59 ||
        second > 59) {
        return false;
    }
    struct civil_date rolled = calendar_add_days(date, 0); // an impossible date like 02-30 rolls on
    if (rolled.year != date.year || rolled.month != date.month || rolled.day != date.day) {
        return false;
    }
    *out_s = calendar_days_between(UNIX_EPOCH, date) * SECONDS_PER_DAY + hour * SECONDS_PER_HOUR +
             minute * SECONDS_PER_MINUTE + second - offset_s;
    return true;
}

bool clock_is_synced(void) {
    if (override_active || ALWAYS_SYNCED) {
        return true;
    }
    if (access(TIMESYNC_SYNCHRONIZED_PATH, F_OK) == 0) {
        return true;
    }
    if (errno != ENOENT) {
        log_error("clock: can't check %s: %s", TIMESYNC_SYNCHRONIZED_PATH, strerror(errno));
    }
    return false;
}

static int64_t floor_divide(int64_t dividend, int64_t divisor) {
    return dividend / divisor - (dividend % divisor < 0);
}

static struct civil_date date_of(int64_t seconds) {
    return calendar_add_days(UNIX_EPOCH, (int)floor_divide(seconds, SECONDS_PER_DAY));
}

static int64_t clock_change_s(int year, int month) {
    struct civil_date last_sunday = {year, month, 31}; // March and October both have 31 days
    last_sunday.day -= calendar_weekday(last_sunday) % 7;
    return calendar_days_between(UNIX_EPOCH, last_sunday) * SECONDS_PER_DAY + SECONDS_PER_HOUR;
}

static int64_t warsaw_local_s(int64_t now_s) {
    int year = date_of(now_s).year;
    bool summer = now_s >= clock_change_s(year, 3) && now_s < clock_change_s(year, 10);
    return now_s + (summer ? 2 : 1) * SECONDS_PER_HOUR; // CET-1CEST,M3.5.0,M10.5.0/3
}

struct civil_date clock_warsaw_date(int64_t now_s) {
    return date_of(warsaw_local_s(now_s));
}

int clock_warsaw_minute_of_day(int64_t now_s) {
    int64_t local_s = warsaw_local_s(now_s);
    int64_t second_of_day = local_s - floor_divide(local_s, SECONDS_PER_DAY) * SECONDS_PER_DAY;
    return (int)(second_of_day / SECONDS_PER_MINUTE);
}
