#pragma once

#include <stddef.h>
#include <stdint.h>

#include "shared/time.h"

static constexpr int SOLAR_TERMS_PER_YEAR = 24;

struct lunar_month {
    struct civil_date start;
    int month;
    bool leap;
};

struct solar_term_year {
    int year;
    uint8_t days[SOLAR_TERMS_PER_YEAR]; // 小寒 to 冬至, two terms a month from January
};

extern const int lunar_first_year;
extern const int lunar_last_year;
extern const struct lunar_month lunar_months[];
extern const size_t lunar_month_count;
extern const struct solar_term_year solar_term_years[];
extern const size_t solar_term_year_count;
