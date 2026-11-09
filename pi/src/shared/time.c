#include "shared/time.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "shared/log.h"

#define TIMESYNC_DIRECTORY    "/run/systemd/timesync"
#define TIMESYNC_SYNCHRONIZED TIMESYNC_DIRECTORY "/synchronized"

static constexpr long NANOSECONDS_PER_SECOND = 1'000'000'000;

static struct tm warsaw_time(time_t now) {
    struct tm local;
    if (localtime_r(&now, &local) == nullptr) {
        log_error("%lld s is outside the times this system can convert", (long long)now);
        abort();
    }
    return local;
}

struct civil_date time_warsaw_date(time_t now) {
    const struct tm local = warsaw_time(now);
    return (struct civil_date){local.tm_year + 1900, local.tm_mon + 1, local.tm_mday};
}

int time_warsaw_minute_of_day(time_t now) {
    const struct tm local = warsaw_time(now);
    return local.tm_hour * MINUTES_PER_HOUR + local.tm_min;
}

bool time_same_date(struct civil_date a, struct civil_date b) {
    return a.year == b.year && a.month == b.month && a.day == b.day;
}

static bool read_digits(const char **cursor, int count, int *out) {
    int value = 0;
    for (int i = 0; i < count; i++) {
        const char digit = (*cursor)[i];
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

static bool read_offset(const char **cursor, int *offset_seconds) {
    /* read Z for UTC, or the sign of an offset */
    if (read_char(cursor, 'Z')) {
        *offset_seconds = 0;
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

    /* read the hours and minutes after the sign */
    int hours = 0;
    int minutes = 0;
    if (!read_digits(cursor, 2, &hours) || !read_char(cursor, ':') ||
        !read_digits(cursor, 2, &minutes) || hours > 23 || minutes > 59) {
        return false;
    }
    *offset_seconds = sign * (hours * SECONDS_PER_HOUR + minutes * SECONDS_PER_MINUTE);
    return true;
}

bool time_parse_iso8601(const char *text, time_t *out) {
    /* read the date and the time to the minute */
    const char *cursor = text;
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    if (!read_digits(&cursor, 4, &year) || !read_char(&cursor, '-') ||
        !read_digits(&cursor, 2, &month) || !read_char(&cursor, '-') ||
        !read_digits(&cursor, 2, &day) || !read_char(&cursor, 'T') ||
        !read_digits(&cursor, 2, &hour) || !read_char(&cursor, ':') ||
        !read_digits(&cursor, 2, &minute)) {
        return false;
    }

    /* read the seconds and milliseconds, when given */
    int second = 0;
    int milliseconds = 0;
    if (read_char(&cursor, ':') &&
        (!read_digits(&cursor, 2, &second) ||
         (read_char(&cursor, '.') && !read_digits(&cursor, 3, &milliseconds)))) {
        return false;
    }

    /* read the offset, which ends the text */
    int offset_seconds = 0;
    if (!read_offset(&cursor, &offset_seconds) || *cursor != '\0') {
        return false;
    }

    /* refuse a time or a day that doesn't exist; timegm would move 02-30 on to March */
    if (hour > 23 || minute > 59 || second > 59) {
        return false;
    }
    struct tm fields = {
            .tm_year = year - 1900,
            .tm_mon = month - 1,
            .tm_mday = day,
            .tm_hour = hour,
            .tm_min = minute,
            .tm_sec = second,
    };
    const time_t moment = timegm(&fields);
    if (fields.tm_year != year - 1900 || fields.tm_mon != month - 1 || fields.tm_mday != day) {
        return false;
    }
    *out = moment - offset_seconds;
    return true;
}

bool time_format_iso8601(time_t moment, char *out, size_t size) {
    struct tm utc;
    return gmtime_r(&moment, &utc) != nullptr &&
           strftime(out, size, "%Y-%m-%dT%H:%M:%SZ", &utc) > 0;
}

bool time_clock_synced(void) {
    /* look for the file timesyncd writes once the clock is synchronised */
    struct stat status;
    if (stat(TIMESYNC_SYNCHRONIZED, &status) == 0) {
        return true;
    }
    if (errno == ENOENT && stat(TIMESYNC_DIRECTORY, &status) == 0) {
        return false;
    }

    /* without timesyncd, the system keeps its own clock */
    if (errno == ENOENT) {
        return true;
    }
    log_error("can't tell whether the clock is synchronised: %s", strerror(errno));
    return false;
}

struct timespec time_left_in_tick(struct timespec started, struct timespec now, int tick_seconds) {
    struct timespec left = {
            .tv_sec = started.tv_sec + tick_seconds - now.tv_sec,
            .tv_nsec = started.tv_nsec - now.tv_nsec,
    };
    if (left.tv_nsec < 0) {
        left.tv_sec -= 1;
        left.tv_nsec += NANOSECONDS_PER_SECOND;
    }
    if (left.tv_sec < 0) {
        return (struct timespec){};
    }
    return left;
}
