/* the date block: the Warsaw date, lunar date, holidays and day counter */
#include "date.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lunar_table.h"
#include "shared/log.h"
#include "shared/time.h"

static constexpr struct civil_date COUNTER_DAY_ZERO = {2021, 9, 1};
static constexpr int DAYS_PER_400_YEARS = 146'097;
static constexpr int DAYS_FROM_0000_03_01_TO_1970_01_01 = 719'468;
static constexpr int DAYS_PER_WEEK = 7;
static constexpr int THURSDAY_INDEX_FROM_MONDAY = 3;

struct named_day {
    int month;
    int day;
    const char *name;
};

struct easter_holiday {
    int days_after_easter;
    const char *name;
};

struct lunar_date {
    int month;
    int day;
    bool leap;
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

static const char *const LUNAR_MONTH_NAMES[] = {"正月", "二月", "三月", "四月", "五月", "六月",
                                                "七月", "八月", "九月", "十月", "冬月", "腊月"};

static const char *const LUNAR_DAY_NAMES[] = {
        "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
        "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
        "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十",
};

static const char *const SOLAR_TERM_NAMES[SOLAR_TERMS_PER_YEAR] = {
        "小寒", "大寒", "立春", "雨水", "惊蛰", "春分", "清明", "谷雨",
        "立夏", "小满", "芒种", "夏至", "小暑", "大暑", "立秋", "处暑",
        "白露", "秋分", "寒露", "霜降", "立冬", "小雪", "大雪", "冬至",
};

static int day_number(struct civil_date date) {
    int year = date.year - (date.month <= 2); // years run from 1 March, putting 29 February last
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

static int days_between(struct civil_date from, struct civil_date to) {
    return day_number(to) - day_number(from);
}

static const char *name_on(const struct named_day *days, int month, int day) {
    for (; days->name != nullptr; days++) {
        if (days->month == month && days->day == day) {
            return days->name;
        }
    }
    return nullptr;
}

static struct civil_date easter(int year) {
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

static const char *polish_holiday(struct civil_date date) {
    /* fixed-date holidays */
    const char *fixed = name_on(POLISH_FIXED_HOLIDAYS, date.month, date.day);
    if (fixed != nullptr) {
        return fixed;
    }

    /* Easter holidays */
    int days_after_easter = days_between(easter(date.year), date);
    for (const struct easter_holiday *holiday = POLISH_EASTER_HOLIDAYS; holiday->name != nullptr;
         holiday++) {
        if (holiday->days_after_easter == days_after_easter) {
            return holiday->name;
        }
    }
    return nullptr;
}

static bool lunar_from_civil(struct civil_date date, struct lunar_date *out) {
    /* find the lunar month */
    if (date.year < lunar_first_year || date.year > lunar_last_year) {
        return false;
    }
    const struct lunar_month *month = nullptr;
    for (size_t i = 0; i < lunar_month_count && days_between(lunar_months[i].start, date) >= 0;
         i++) {
        month = &lunar_months[i];
    }
    if (month == nullptr) {
        return false;
    }

    /* count the day */
    *out = (struct lunar_date){
            .month = month->month,
            .day = days_between(month->start, date) + 1,
            .leap = month->leap,
    };
    return true;
}

static const char *solar_term_on(struct civil_date date) {
    for (size_t i = 0; i < solar_term_year_count; i++) {
        if (solar_term_years[i].year != date.year) {
            continue;
        }
        for (int term = 2 * (date.month - 1); term < 2 * date.month; term++) {
            if (solar_term_years[i].days[term] == date.day) {
                return SOLAR_TERM_NAMES[term];
            }
        }
    }
    return nullptr;
}

static const char *festival(struct civil_date date, struct lunar_date lunar, const char *term) {
    /* lunar festivals and 除夕 */
    if (!lunar.leap) {
        const char *lunar_festival = name_on(LUNAR_FESTIVALS, lunar.month, lunar.day);
        if (lunar_festival != nullptr) {
            return lunar_festival;
        }
        struct lunar_date tomorrow;
        if (lunar.month == 12 &&
            lunar_from_civil(date_of_day_number(day_number(date) + 1), &tomorrow) &&
            tomorrow.day == 1) {
            return "除夕";
        }
    }

    /* 清明 and 冬至 */
    if (term != nullptr && (strcmp(term, "清明") == 0 || strcmp(term, "冬至") == 0)) {
        return term;
    }

    /* Gregorian festivals */
    return name_on(GREGORIAN_FESTIVALS, date.month, date.day);
}

static bool build_date_line(struct civil_date date, struct date_line *out) {
    /* find the lunar date */
    struct lunar_date lunar;
    if (!lunar_from_civil(date, &lunar)) {
        log_error("date: no lunar date for %04d-%02d-%02d", date.year, date.month, date.day);
        return false;
    }

    /* choose the pill or prefix */
    const char *holiday = polish_holiday(date);
    const char *term = solar_term_on(date);
    const char *pill = festival(date, lunar, term);
    const char *prefix = nullptr;
    if (pill == nullptr) {
        prefix = holiday != nullptr ? holiday : term;
    }
    *out = (struct date_line){.pill = pill, .prefix = prefix, .rest_day = holiday != nullptr};

    /* write the lunar date */
    int length =
            snprintf(out->lunar, sizeof out->lunar, "%s%s%s%s",
                     pill == nullptr && prefix == nullptr ? "农历" : "", lunar.leap ? "闰" : "",
                     LUNAR_MONTH_NAMES[lunar.month - 1], LUNAR_DAY_NAMES[lunar.day - 1]);
    if (length < 0 || (size_t)length >= sizeof out->lunar) {
        log_error("date: lunar date for %04d-%02d-%02d doesn't fit", date.year, date.month,
                  date.day);
        return false;
    }
    return true;
}

struct date date_for(time_t now) {
    /* find today's weekday */
    struct civil_date today = time_warsaw_date(now);
    int days_since_monday = (day_number(today) + THURSDAY_INDEX_FROM_MONDAY) %
                            DAYS_PER_WEEK; // 1970-01-01 was a Thursday
    if (days_since_monday < 0) {
        days_since_monday += DAYS_PER_WEEK;
    }

    /* fill in the date */
    struct date date = {
            .today = today,
            .month_name = MONTH_NAMES[today.month - 1],
            .weekday_name = WEEKDAY_NAMES[days_since_monday],
            .counter_days = days_between(COUNTER_DAY_ZERO, today),
    };
    date.ok = build_date_line(today, &date.line);
    return date;
}
