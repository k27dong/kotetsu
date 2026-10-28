#pragma once

#include <stddef.h>
#include <stdint.h>

#include "model.h"

static constexpr int SOLAR_TERMS_PER_YEAR = 24;

struct lunar_date {
    int month;
    int day;
    bool leap;
};

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

[[nodiscard]] bool lunar_from_civil(struct civil_date date, struct lunar_date *out);
const char *lunar_month_name(int month);
const char *lunar_day_name(int day);
const char *lunar_solar_term(struct civil_date date);
