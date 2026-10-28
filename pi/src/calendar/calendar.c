#include "calendar/calendar.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "calendar/lunar.h"
#include "log.h"
#include "model.h"

static constexpr struct civil_date COUNTER_DAY_ZERO = {2021, 9, 1};
static constexpr int DAYS_PER_400_YEARS = 146'097;
static constexpr int DAYS_FROM_0000_03_01_TO_1970_01_01 = 719'468;

struct named_day {
    int month;
    int day;
    const char *name;
};

struct easter_holiday {
    int days_after_easter;
    const char *name;
};

static const struct named_day POLISH_FIXED_HOLIDAYS[] = {
        {1, 1, "元旦"},
        {1, 6, "主显节"},
        {5, 1, "劳动节"},
        {5, 3, "宪法日"},
        {8, 15, "圣母升天节"},
        {11, 1, "诸圣节"},
        {11, 11, "独立日"},
        {12, 24, "平安夜"},
        {12, 25, "圣诞节"},
        {12, 26, "圣诞节次日"},
        {},
};

static const struct easter_holiday POLISH_EASTER_HOLIDAYS[] = {
        {0, "复活节"}, {1, "复活节次日"}, {49, "圣灵降临节"}, {60, "基督圣体节"}, {},
};

static const struct named_day LUNAR_FESTIVALS[] = {
        {1, 1, "春节"},    {1, 15, "元宵节"}, {5, 5, "端午节"},  {7, 7, "七夕"},
        {8, 15, "中秋节"}, {9, 9, "重阳节"},  {12, 8, "腊八节"}, {},
};

static const struct named_day GREGORIAN_FESTIVALS[] = {
        {1, 1, "元旦"},
        {2, 14, "情人节"},
        {10, 1, "国庆节"},
        {},
};

static const char *const MONTH_NAMES[] = {"一月", "二月", "三月", "四月", "五月",   "六月",
                                          "七月", "八月", "九月", "十月", "十一月", "十二月"};

static const char *const WEEKDAY_NAMES[] = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};

static int day_number(struct civil_date date) {
    int year = date.year - (date.month <= 2); // years start on 1 March, so 29 February ends one
    int era = (year >= 0 ? year : year - 399) / 400;
    int year_of_era = year - era * 400;
    int day_of_year = (153 * ((date.month + 9) % 12) + 2) / 5 + date.day - 1;
    int day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * DAYS_PER_400_YEARS + day_of_era - DAYS_FROM_0000_03_01_TO_1970_01_01;
}

static struct civil_date date_of_day_number(int number) {
    int days = number + DAYS_FROM_0000_03_01_TO_1970_01_01;
    int era = (days >= 0 ? days : days - (DAYS_PER_400_YEARS - 1)) / DAYS_PER_400_YEARS;
    int day_of_era = days - era * DAYS_PER_400_YEARS;
    int leap_days = day_of_era / 1460 - day_of_era / 36'524 + day_of_era / (DAYS_PER_400_YEARS - 1);
    int year_of_era = (day_of_era - leap_days) / 365;
    int day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    int month_from_march = (5 * day_of_year + 2) / 153;
    int month = month_from_march < 10 ? month_from_march + 3 : month_from_march - 9;
    return (struct civil_date){
            .year = year_of_era + era * 400 + (month <= 2),
            .month = month,
            .day = day_of_year - (153 * month_from_march + 2) / 5 + 1,
    };
}

static const char *name_on(const struct named_day *days, int month, int day) {
    for (; days->name != nullptr; days++) {
        if (days->month == month && days->day == day) {
            return days->name;
        }
    }
    return nullptr;
}

int calendar_weekday(struct civil_date date) {
    int days_since_monday = (day_number(date) + 3) % 7; // 1970-01-01 was a Thursday
    return (days_since_monday < 0 ? days_since_monday + 7 : days_since_monday) + 1;
}

int calendar_days_between(struct civil_date from, struct civil_date to) {
    return day_number(to) - day_number(from);
}

struct civil_date calendar_add_days(struct civil_date date, int days) {
    return date_of_day_number(day_number(date) + days);
}

int calendar_counter_days(struct civil_date today) {
    return calendar_days_between(COUNTER_DAY_ZERO, today);
}

struct civil_date calendar_easter(int year) {
    int a = year % 19; // Meeus/Jones/Butcher, keeping its published letter names
    int b = year / 100;
    int c = year % 100;
    int d = b / 4;
    int e = b % 4;
    int f = (b + 8) / 25;
    int g = (b - f + 1) / 3;
    int h = (19 * a + b - d - g + 15) % 30;
    int i = c / 4;
    int k = c % 4;
    int l = (32 + 2 * e + 2 * i - h - k) % 7;
    int m = (a + 11 * h + 22 * l) / 451;
    int month_and_day = h + l - 7 * m + 114;
    return (struct civil_date){year, month_and_day / 31, month_and_day % 31 + 1};
}

const char *calendar_polish_holiday(struct civil_date date) {
    const char *fixed = name_on(POLISH_FIXED_HOLIDAYS, date.month, date.day);
    if (fixed != nullptr) {
        return fixed;
    }
    int days_after_easter = calendar_days_between(calendar_easter(date.year), date);
    for (const struct easter_holiday *holiday = POLISH_EASTER_HOLIDAYS; holiday->name != nullptr;
         holiday++) {
        if (holiday->days_after_easter == days_after_easter) {
            return holiday->name;
        }
    }
    return nullptr;
}

const char *calendar_month_name(int month) {
    return month >= 1 && month <= 12 ? MONTH_NAMES[month - 1] : nullptr;
}

const char *calendar_weekday_name(int weekday) {
    return weekday >= 1 && weekday <= 7 ? WEEKDAY_NAMES[weekday - 1] : nullptr;
}

static bool ends_lunar_month(struct civil_date date) {
    struct lunar_date tomorrow;
    return lunar_from_civil(calendar_add_days(date, 1), &tomorrow) && tomorrow.day == 1;
}

static const char *festival(struct civil_date date, struct lunar_date lunar, const char *term) {
    if (!lunar.leap) {
        const char *lunar_festival = name_on(LUNAR_FESTIVALS, lunar.month, lunar.day);
        if (lunar_festival != nullptr) {
            return lunar_festival;
        }
        if (lunar.month == 12 && ends_lunar_month(date)) {
            return "除夕";
        }
    }
    if (term != nullptr && (strcmp(term, "清明") == 0 || strcmp(term, "冬至") == 0)) {
        return term;
    }
    return name_on(GREGORIAN_FESTIVALS, date.month, date.day);
}

bool calendar_date_line(struct civil_date date, struct date_line *out) {
    struct lunar_date lunar;
    if (!lunar_from_civil(date, &lunar)) {
        log_error("calendar: no lunar date for %04d-%02d-%02d", date.year, date.month, date.day);
        return false;
    }
    const char *holiday = calendar_polish_holiday(date);
    const char *term = lunar_solar_term(date);
    const char *pill = festival(date, lunar, term);
    const char *prefix = nullptr;
    if (pill == nullptr) {
        prefix = holiday != nullptr ? holiday : term;
    }
    *out = (struct date_line){.pill = pill, .prefix = prefix, .rest_day = holiday != nullptr};
    int length =
            snprintf(out->lunar, sizeof out->lunar, "%s%s%s%s",
                     pill == nullptr && prefix == nullptr ? "农历" : "", lunar.leap ? "闰" : "",
                     lunar_month_name(lunar.month), lunar_day_name(lunar.day));
    if (length < 0 || (size_t)length >= sizeof out->lunar) {
        log_error("calendar: lunar date for %04d-%02d-%02d doesn't fit", date.year, date.month,
                  date.day);
        return false;
    }
    return true;
}
