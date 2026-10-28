#include "calendar/lunar.h"

#include <stddef.h>

#include "calendar/calendar.h"
#include "model.h"

static constexpr int FIRST_YEAR = 2025;
static constexpr int LAST_YEAR = 2060;

static const char *const MONTH_NAMES[] = {"正月", "二月", "三月", "四月", "五月", "六月",
                                          "七月", "八月", "九月", "十月", "冬月", "腊月"};

static const char *const DAY_NAMES[] = {
        "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
        "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
        "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十",
};

static const char *const SOLAR_TERM_NAMES[SOLAR_TERMS_PER_YEAR] = {
        "小寒", "大寒", "立春", "雨水", "惊蛰", "春分", "清明", "谷雨",
        "立夏", "小满", "芒种", "夏至", "小暑", "大暑", "立秋", "处暑",
        "白露", "秋分", "寒露", "霜降", "立冬", "小雪", "大雪", "冬至",
};

bool lunar_from_civil(struct civil_date date, struct lunar_date *out) {
    if (date.year < FIRST_YEAR || date.year > LAST_YEAR) {
        return false;
    }
    const struct lunar_month *month = nullptr;
    for (size_t i = 0;
         i < lunar_month_count && calendar_days_between(lunar_months[i].start, date) >= 0; i++) {
        month = &lunar_months[i];
    }
    if (month == nullptr) {
        return false;
    }
    *out = (struct lunar_date){
            .month = month->month,
            .day = calendar_days_between(month->start, date) + 1,
            .leap = month->leap,
    };
    return true;
}

const char *lunar_month_name(int month) {
    return month >= 1 && month <= 12 ? MONTH_NAMES[month - 1] : nullptr;
}

const char *lunar_day_name(int day) {
    return day >= 1 && day <= 30 ? DAY_NAMES[day - 1] : nullptr;
}

const char *lunar_solar_term(struct civil_date date) {
    if (date.month < 1 || date.month > 12) {
        return nullptr;
    }
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
