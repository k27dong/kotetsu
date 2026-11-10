#include <stdio.h>
#include <string.h>
#include <time.h>

#include "date.h"
#include "lunar_table.h"
#include "stderr_capture.h"
#include "unity.h"

static const char *const LUNAR_DAY_PREFIX = "农历";

static const char *const LUNAR_MONTHS[] = {"正月", "二月", "三月", "四月", "五月", "六月",
                                           "七月", "八月", "九月", "十月", "冬月", "腊月"};

static const char *const LUNAR_DAYS[] = {
        "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
        "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
        "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十",
};

static const char *const SOLAR_TERMS[] = {
        "小寒", "大寒", "立春", "雨水", "惊蛰", "春分", "清明", "谷雨",
        "立夏", "小满", "芒种", "夏至", "小暑", "大暑", "立秋", "处暑",
        "白露", "秋分", "寒露", "霜降", "立冬", "小雪", "大雪", "冬至",
};

void setUp(void) {
}

void tearDown(void) {
}

static time_t warsaw_time(struct civil_date day, int hour, int minute) {
    struct tm local = {
            .tm_year = day.year - 1900,
            .tm_mon = day.month - 1,
            .tm_mday = day.day,
            .tm_hour = hour,
            .tm_min = minute,
            .tm_isdst = -1,
    };
    const time_t moment = mktime(&local);
    TEST_ASSERT_NOT_EQUAL(-1, moment);
    return moment;
}

static struct date on(int year, int month, int day) {
    return date_for(warsaw_time((struct civil_date){year, month, day}, 12, 0));
}

static struct date on_outside_lunar_table(int year, int month, int day) {
    char captured[256];
    start_capture();
    const struct date date = on(year, month, day);
    finish_capture(captured, sizeof captured);
    char expected[64];
    snprintf(expected, sizeof expected, "<3>date: no lunar date for %04d-%02d-%02d", year, month,
             day);
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE(expected, captured, strlen(expected), captured);
    TEST_ASSERT_FALSE(date.ok);
    return date;
}

static const char *without_lunar_prefix(const char *text) {
    const size_t length = strlen(LUNAR_DAY_PREFIX);
    return strncmp(text, LUNAR_DAY_PREFIX, length) == 0 ? text + length : text;
}

static void lunar_text(char *out, size_t size, int month, bool leap, int day) {
    snprintf(out, size, "%s%s%s", leap ? "闰" : "", LUNAR_MONTHS[month - 1], LUNAR_DAYS[day - 1]);
}

static void assert_lunar(int year, int month, int day, const char *expected) {
    char label[16];
    snprintf(label, sizeof label, "%04d-%02d-%02d", year, month, day);
    const struct date date = on(year, month, day);
    TEST_ASSERT_TRUE_MESSAGE(date.ok, label);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, without_lunar_prefix(date.line.lunar), label);
}

static void test_counter_days(void) {
    TEST_ASSERT_EQUAL_INT(1000, on_outside_lunar_table(2024, 5, 28).counter_days);
    TEST_ASSERT_EQUAL_INT(1880, on(2026, 10, 25).counter_days);
    TEST_ASSERT_EQUAL_INT(1881, on(2026, 10, 26).counter_days);
    TEST_ASSERT_EQUAL_INT(3000, on(2029, 11, 18).counter_days);
    TEST_ASSERT_EQUAL_INT(10000, on(2049, 1, 17).counter_days);
}

static void test_counter_2027_02_22_is_2000(void) {
    TEST_ASSERT_EQUAL_INT(2000, on(2027, 2, 22).counter_days);
}

static void test_counter_follows_warsaw_dates_across_dst(void) {
    const struct civil_date saturday = {2026, 3, 28};
    const struct civil_date sunday = {2026, 3, 29};
    const struct civil_date monday = {2026, 3, 30};
    const int before = date_for(warsaw_time(saturday, 23, 30)).counter_days;
    TEST_ASSERT_EQUAL_INT(before + 1, date_for(warsaw_time(sunday, 0, 30)).counter_days);
    TEST_ASSERT_EQUAL_INT(before + 1, date_for(warsaw_time(sunday, 23, 30)).counter_days);
    TEST_ASSERT_EQUAL_INT(before + 2, date_for(warsaw_time(monday, 0, 30)).counter_days);
}

static void test_weekdays(void) {
    TEST_ASSERT_EQUAL_STRING("周一", on(2026, 10, 26).weekday_name);
    TEST_ASSERT_EQUAL_STRING("周日", on(2026, 10, 25).weekday_name);
    TEST_ASSERT_EQUAL_STRING("周四", on_outside_lunar_table(1970, 1, 1).weekday_name);
    TEST_ASSERT_EQUAL_STRING("周四", on(2026, 11, 5).weekday_name);
}

static void test_today_is_the_warsaw_date(void) {
    const struct date date = on(2026, 10, 26);
    TEST_ASSERT_EQUAL_INT(2026, date.today.year);
    TEST_ASSERT_EQUAL_INT(10, date.today.month);
    TEST_ASSERT_EQUAL_INT(26, date.today.day);
}

static void test_day_arithmetic_across_months_and_leap_days(void) {
    TEST_ASSERT_EQUAL_INT(2, on(2028, 3, 1).counter_days - on(2028, 2, 28).counter_days);
    TEST_ASSERT_EQUAL_INT(1, on(2027, 3, 1).counter_days - on(2027, 2, 28).counter_days);
    TEST_ASSERT_EQUAL_INT(1, on(2027, 1, 1).counter_days - on(2026, 12, 31).counter_days);
}

static void test_easter_and_holidays_2026(void) {
    TEST_ASSERT_TRUE(on(2026, 4, 5).line.rest_day);
    const struct date easter_sunday = on(2027, 3, 28);
    TEST_ASSERT_EQUAL_STRING("复活节", easter_sunday.line.prefix);
    TEST_ASSERT_TRUE(easter_sunday.line.rest_day);
    TEST_ASSERT_EQUAL_STRING("复活节次日", on(2026, 4, 6).line.prefix);
    TEST_ASSERT_EQUAL_STRING("圣灵降临节", on(2026, 5, 24).line.prefix);
    TEST_ASSERT_EQUAL_STRING("基督圣体节", on(2026, 6, 4).line.prefix);
    TEST_ASSERT_EQUAL_STRING("平安夜", on(2026, 12, 24).line.prefix);
    TEST_ASSERT_EQUAL_STRING("独立日", on(2026, 11, 11).line.prefix);
    TEST_ASSERT_NULL(on(2026, 11, 12).line.prefix);
    TEST_ASSERT_FALSE(on(2026, 11, 12).line.rest_day);
}

struct expected_line {
    struct civil_date date;
    const char *pill;
    const char *prefix;
    const char *lunar;
    bool rest_day;
};

static void test_date_lines(void) {
    static const struct expected_line lines[] = {
            {{2026, 10, 26}, nullptr, nullptr, "农历九月十七", false},
            {{2026, 11, 3}, nullptr, nullptr, "农历九月廿五", false},
            {{2026, 11, 7}, nullptr, "立冬", "九月廿九", false},
            {{2027, 9, 15}, "中秋节", nullptr, "八月十五", false},
            {{2027, 2, 6}, "春节", nullptr, "正月初一", false},
            {{2027, 2, 5}, "除夕", nullptr, "腊月廿九", false},
            {{2026, 12, 22}, "冬至", nullptr, "冬月十四", false},
            {{2026, 11, 11}, nullptr, "独立日", "十月初三", true},
            {{2027, 1, 1}, "元旦", nullptr, "冬月廿四", true},
            {{2026, 12, 24}, nullptr, "平安夜", "冬月十六", true},
            {{2028, 6, 30}, nullptr, nullptr, "农历闰五月初八", false},
            {{2027, 10, 1}, "国庆节", nullptr, "九月初二", false},
            {{2026, 4, 5}, "清明", nullptr, "二月十八", true},
            {{2028, 1, 6}, nullptr, "主显节", "腊月初十", true},
            {{2028, 6, 27}, nullptr, nullptr, "农历闰五月初五", false},
    };
    for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++) {
        const struct expected_line *expected = &lines[i];
        char label[16];
        snprintf(label, sizeof label, "%04d-%02d-%02d", expected->date.year, expected->date.month,
                 expected->date.day);
        const struct date date = on(expected->date.year, expected->date.month, expected->date.day);
        TEST_ASSERT_TRUE_MESSAGE(date.ok, label);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(expected->pill, date.line.pill, label);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(expected->prefix, date.line.prefix, label);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(expected->lunar, date.line.lunar, label);
        TEST_ASSERT_EQUAL_MESSAGE(expected->rest_day, date.line.rest_day, label);
    }
}

static void test_rest_day_on_polish_holiday(void) {
    const struct date holiday = on(2026, 11, 11);
    TEST_ASSERT_TRUE(holiday.line.rest_day);
    TEST_ASSERT_EQUAL_STRING("独立日", holiday.line.prefix);
    TEST_ASSERT_FALSE(on(2026, 11, 10).line.rest_day);
    TEST_ASSERT_TRUE(on(2027, 1, 1).line.rest_day);
}

static void test_leap_month_text(void) {
    const struct date first_day = on(2028, 6, 23);
    TEST_ASSERT_EQUAL_STRING("农历闰五月初一", first_day.line.lunar);
    assert_lunar(2028, 6, 22, "五月三十");
    assert_lunar(2028, 7, 21, "闰五月廿九");
    assert_lunar(2028, 7, 22, "六月初一");
}

static void test_solar_term_prefix(void) {
    TEST_ASSERT_EQUAL_STRING("小寒", on(2027, 1, 5).line.prefix);
    TEST_ASSERT_NULL(on(2026, 11, 8).line.prefix);
    TEST_ASSERT_NULL(on(2026, 11, 8).line.pill);
}

static void test_date_line_outside_lunar_table(void) {
    const struct date after = on_outside_lunar_table(2061, 1, 1);
    TEST_ASSERT_EQUAL_INT(2061, after.today.year);
    TEST_ASSERT_EQUAL_STRING("一月", after.month_name);
    (void)on_outside_lunar_table(2024, 12, 31);
}

static void test_2049_01_18_and_17_match_old_program(void) {
    const struct date seventeenth = on(2049, 1, 17);
    TEST_ASSERT_TRUE(seventeenth.ok);
    TEST_ASSERT_NULL(seventeenth.line.pill);
    TEST_ASSERT_NULL(seventeenth.line.prefix);
    TEST_ASSERT_EQUAL_STRING("农历腊月十四", seventeenth.line.lunar);
    TEST_ASSERT_FALSE(seventeenth.line.rest_day);
    TEST_ASSERT_EQUAL_INT(10000, seventeenth.counter_days);

    const struct date eighteenth = on(2049, 1, 18);
    TEST_ASSERT_TRUE(eighteenth.ok);
    TEST_ASSERT_NULL(eighteenth.line.pill);
    TEST_ASSERT_NULL(eighteenth.line.prefix);
    TEST_ASSERT_EQUAL_STRING("农历腊月十五", eighteenth.line.lunar);
    TEST_ASSERT_FALSE(eighteenth.line.rest_day);
    TEST_ASSERT_EQUAL_INT(10001, eighteenth.counter_days);
}

static void test_names(void) {
    TEST_ASSERT_EQUAL_STRING("十月", on(2026, 10, 26).month_name);
    TEST_ASSERT_EQUAL_STRING("十一月", on(2026, 11, 5).month_name);
    TEST_ASSERT_EQUAL_STRING("周一", on(2026, 10, 26).weekday_name);
    assert_lunar(2026, 11, 20, "十月十二");
    assert_lunar(2026, 12, 29, "冬月廿一");
    assert_lunar(2026, 11, 19, "十月十一");
    assert_lunar(2026, 11, 28, "十月二十");
}

static void test_lunar_new_years_eve_across_a_month_end(void) {
    const struct date eve = on(2052, 1, 31);
    TEST_ASSERT_EQUAL_STRING("除夕", eve.line.pill);
    TEST_ASSERT_EQUAL_STRING("腊月三十", eve.line.lunar);
    const struct date new_year = on(2052, 2, 1);
    TEST_ASSERT_EQUAL_STRING("春节", new_year.line.pill);
    TEST_ASSERT_EQUAL_STRING("正月初一", new_year.line.lunar);
}

static void test_lunar_matches_hko(void) {
    FILE *fixture = fopen("fixtures/hko_lunar_months.csv", "r");
    TEST_ASSERT_NOT_NULL(fixture);
    char line[64];
    TEST_ASSERT_NOT_NULL(fgets(line, sizeof line, fixture));
    TEST_ASSERT_EQUAL_STRING("gregorian_start,lunar_month,leap\n", line);
    int rows = 0;
    time_t previous_start = 0;
    int previous_month = 0;
    bool previous_leap = false;
    while (fgets(line, sizeof line, fixture) != nullptr) {
        struct civil_date start;
        int month;
        int leap;
        TEST_ASSERT_EQUAL_INT_MESSAGE(5,
                                      sscanf(line, "%d-%d-%d,%d,%d", &start.year, &start.month,
                                             &start.day, &month, &leap),
                                      line);
        const time_t start_moment = warsaw_time(start, 12, 0);
        const struct date first_day = date_for(start_moment);
        TEST_ASSERT_TRUE_MESSAGE(first_day.ok, line);
        char text[32];
        lunar_text(text, sizeof text, month, leap == 1, 1);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(text, without_lunar_prefix(first_day.line.lunar), line);
        if (rows > 0) {
            const int previous_length =
                    (int)((start_moment - previous_start + SECONDS_PER_DAY / 2) / SECONDS_PER_DAY);
            const struct date last_day = date_for(start_moment - SECONDS_PER_DAY);
            lunar_text(text, sizeof text, previous_month, previous_leap, previous_length);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(text, without_lunar_prefix(last_day.line.lunar), line);
        }
        previous_start = start_moment;
        previous_month = month;
        previous_leap = leap == 1;
        rows++;
    }
    TEST_ASSERT_EQUAL_INT(0, fclose(fixture));
    TEST_ASSERT_EQUAL_INT(445, rows);
}

static void test_lunar_2033_leap_eleventh(void) {
    assert_lunar(2033, 11, 22, "冬月初一");
    assert_lunar(2033, 12, 22, "闰冬月初一");
}

static void test_lunar_table_edges(void) {
    assert_lunar(2025, 1, 1, "腊月初二");
    assert_lunar(2060, 12, 31, "腊月初九");
}

static int solar_term_index(const char *name) {
    for (int i = 0; name != nullptr && i < SOLAR_TERMS_PER_YEAR; i++) {
        if (strcmp(name, SOLAR_TERMS[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static int count_visible_solar_terms(int year, int shown[SOLAR_TERMS_PER_YEAR]) {
    int visible = 0;
    memset(shown, 0, sizeof(int) * SOLAR_TERMS_PER_YEAR);
    time_t moment = warsaw_time((struct civil_date){year, 1, 1}, 12, 0);
    while (time_warsaw_date(moment).year == year) {
        const struct date date = date_for(moment);
        const int term =
                solar_term_index(date.line.pill != nullptr ? date.line.pill : date.line.prefix);
        if (term >= 0) {
            TEST_ASSERT_EQUAL_INT_MESSAGE(term / 2 + 1, date.today.month, SOLAR_TERMS[term]);
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, shown[term], SOLAR_TERMS[term]);
            shown[term] = 1;
            visible++;
        }
        moment += SECONDS_PER_DAY;
    }
    return visible;
}

static void test_each_visible_solar_term_shows_once_in_its_month(void) {
    int shown[SOLAR_TERMS_PER_YEAR];
    for (int year = 2025; year <= 2060; year++) {
        (void)count_visible_solar_terms(year, shown);
    }
}

static void test_2026_shows_all_24_solar_terms(void) {
    int shown[SOLAR_TERMS_PER_YEAR];
    TEST_ASSERT_EQUAL_INT(24, count_visible_solar_terms(2026, shown));
}

static void test_2038_festivals_hide_three_solar_terms(void) {
    int shown[SOLAR_TERMS_PER_YEAR];
    TEST_ASSERT_EQUAL_INT(21, count_visible_solar_terms(2038, shown));
    TEST_ASSERT_EQUAL_STRING("春节", on(2038, 2, 4).line.pill);
    TEST_ASSERT_EQUAL_STRING("元宵节", on(2038, 2, 18).line.pill);
    TEST_ASSERT_EQUAL_STRING("七夕", on(2038, 8, 7).line.pill);
    TEST_ASSERT_EQUAL_INT(0, shown[solar_term_index("立春")]);
    TEST_ASSERT_EQUAL_INT(0, shown[solar_term_index("雨水")]);
    TEST_ASSERT_EQUAL_INT(0, shown[solar_term_index("立秋")]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_counter_days);
    RUN_TEST(test_counter_2027_02_22_is_2000);
    RUN_TEST(test_counter_follows_warsaw_dates_across_dst);
    RUN_TEST(test_weekdays);
    RUN_TEST(test_today_is_the_warsaw_date);
    RUN_TEST(test_day_arithmetic_across_months_and_leap_days);
    RUN_TEST(test_easter_and_holidays_2026);
    RUN_TEST(test_date_lines);
    RUN_TEST(test_rest_day_on_polish_holiday);
    RUN_TEST(test_leap_month_text);
    RUN_TEST(test_solar_term_prefix);
    RUN_TEST(test_date_line_outside_lunar_table);
    RUN_TEST(test_2049_01_18_and_17_match_old_program);
    RUN_TEST(test_names);
    RUN_TEST(test_lunar_new_years_eve_across_a_month_end);
    RUN_TEST(test_lunar_matches_hko);
    RUN_TEST(test_lunar_2033_leap_eleventh);
    RUN_TEST(test_lunar_table_edges);
    RUN_TEST(test_each_visible_solar_term_shows_once_in_its_month);
    RUN_TEST(test_2026_shows_all_24_solar_terms);
    RUN_TEST(test_2038_festivals_hide_three_solar_terms);
    return UNITY_END();
}
