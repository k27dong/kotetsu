#pragma once

#include <time.h>

#include "shared/time.h"

struct date_line {
    const char *pill;
    const char *prefix;
    char lunar[32];
    bool rest_day;
};

struct date {
    bool ok; // false when the date line can't be worked out
    struct civil_date today;
    const char *month_name;
    const char *weekday_name;
    struct date_line line;
    int counter_days; // since 2021-09-01, in Warsaw dates
};

struct date date_for(time_t now);
