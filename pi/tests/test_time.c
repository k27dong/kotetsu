#include <time.h>
#include <unistd.h>

#include "shared/time.h"
#include "unity.h"

void setUp(void) {
}

void tearDown(void) {
}

static time_t at(const char *text) {
    time_t moment = 0;
    TEST_ASSERT_TRUE_MESSAGE(time_parse_iso8601(text, &moment), text);
    return moment;
}

static void assert_rejected(const char *text) {
    time_t moment = 0;
    TEST_ASSERT_FALSE_MESSAGE(time_parse_iso8601(text, &moment), text);
}

static void assert_warsaw_date(int year, int month, int day, const char *moment) {
    const struct civil_date date = time_warsaw_date(at(moment));
    TEST_ASSERT_EQUAL_INT_MESSAGE(year, date.year, moment);
    TEST_ASSERT_EQUAL_INT_MESSAGE(month, date.month, moment);
    TEST_ASSERT_EQUAL_INT_MESSAGE(day, date.day, moment);
}

static void assert_warsaw_minute(int minute, const char *moment) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(minute, time_warsaw_minute_of_day(at(moment)), moment);
}

static void test_time_units(void) {
    TEST_ASSERT_EQUAL_INT(60, SECONDS_PER_MINUTE);
    TEST_ASSERT_EQUAL_INT(3'600, SECONDS_PER_HOUR);
    TEST_ASSERT_EQUAL_INT(86'400, SECONDS_PER_DAY);
    TEST_ASSERT_EQUAL_INT(60, MINUTES_PER_HOUR);
    TEST_ASSERT_EQUAL_INT(1'440, MINUTES_PER_DAY);
}

static void test_same_date_needs_year_month_and_day(void) {
    const struct civil_date day = {2026, 11, 5};
    TEST_ASSERT_TRUE(time_same_date(day, (struct civil_date){2026, 11, 5}));
    TEST_ASSERT_FALSE(time_same_date(day, (struct civil_date){2027, 11, 5}));
    TEST_ASSERT_FALSE(time_same_date(day, (struct civil_date){2026, 12, 5}));
    TEST_ASSERT_FALSE(time_same_date(day, (struct civil_date){2026, 11, 6}));
}

static void test_parse_iso8601(void) {
    TEST_ASSERT_EQUAL_INT64(1'792'888'200, at("2026-10-25T00:30:00Z"));
    TEST_ASSERT_EQUAL_INT64(1'793'005'200, at("2026-10-26T10:00:00+01:00"));
    TEST_ASSERT_EQUAL_INT64(1'793'005'200, at("2026-10-26T10:00+01:00"));
    TEST_ASSERT_EQUAL_INT64(1'792'985'400, at("2026-10-26T01:00:00-02:30"));
    TEST_ASSERT_EQUAL_INT64(1'793'008'800, at("2026-10-26T10:00:00.250Z"));
    TEST_ASSERT_EQUAL_INT64(1'835'481'599, at("2028-02-29T23:59:59Z"));
    TEST_ASSERT_EQUAL_INT64(0, at("1970-01-01T01:00:00+01:00"));
    assert_rejected("2026-10-26 10:00:00Z");
    assert_rejected("2026-02-29T10:00:00Z");
    assert_rejected("2026-04-31T10:00:00Z");
    assert_rejected("2026-00-10T10:00:00Z");
    assert_rejected("2026-10-00T10:00:00Z");
    assert_rejected("2026-13-01T10:00:00Z");
    assert_rejected("2026-10-26T24:00:00Z");
    assert_rejected("2026-10-26T10:60:00Z");
    assert_rejected("2026-10-26T10:00:60Z");
    assert_rejected("2026-10-26T10:00:00+1:00");
    assert_rejected("2026-10-26T10:00:00+24:00");
    assert_rejected("2026-10-26T10:00:00.25Z");
    assert_rejected("2026-10-26T10:00:00Zx");
    assert_rejected("");
}

static void test_parse_needs_offset(void) {
    assert_rejected("2026-10-26T10:00:00");
    assert_rejected("2026-10-26T10:00:00.250");
    assert_rejected("2026-10-26T10:00");
    assert_rejected("2026-10-26");
    assert_rejected("2026-10-26Z");
    assert_rejected("10:00:00Z");
}

static void test_warsaw_autumn_change_2026_10_25(void) {
    assert_warsaw_minute(150, "2026-10-25T00:30:00Z");
    assert_warsaw_minute(150, "2026-10-25T01:30:00Z");
    assert_warsaw_minute(179, "2026-10-25T00:59:00Z");
    assert_warsaw_minute(120, "2026-10-25T01:00:00Z");
    assert_warsaw_date(2026, 10, 25, "2026-10-24T22:00:00Z");
    assert_warsaw_date(2026, 10, 24, "2026-10-24T21:59:00Z");
    assert_warsaw_date(2026, 10, 25, "2026-10-25T22:59:00Z");
    assert_warsaw_date(2026, 10, 26, "2026-10-25T23:00:00Z");
    assert_warsaw_minute(0, "2026-10-25T23:00:00Z");
}

static void test_warsaw_spring_change_2026_03_29(void) {
    assert_warsaw_minute(119, "2026-03-29T00:59:00Z");
    assert_warsaw_minute(180, "2026-03-29T01:00:00Z");
    assert_warsaw_date(2026, 3, 28, "2026-03-28T22:59:00Z");
    assert_warsaw_date(2026, 3, 29, "2026-03-28T23:00:00Z");
    assert_warsaw_minute(0, "2026-03-28T23:00:00Z");
    assert_warsaw_date(2026, 3, 29, "2026-03-29T21:59:00Z");
    assert_warsaw_date(2026, 3, 30, "2026-03-29T22:00:00Z");
    assert_warsaw_minute(0, "2026-03-29T22:00:00Z");
}

static void test_warsaw_spring_change_2027_03_28(void) {
    assert_warsaw_minute(119, "2027-03-28T00:59:00Z");
    assert_warsaw_minute(180, "2027-03-28T01:00:00Z");
}

static void test_format_matches_old_report(void) {
    char text[32];
    TEST_ASSERT_TRUE(time_format_iso8601(at("2026-10-26T12:00:00+01:00"), text, sizeof text));
    TEST_ASSERT_EQUAL_STRING("2026-10-26T11:00:00Z", text);
    TEST_ASSERT_TRUE(time_format_iso8601(at("2026-10-27T00:01:05+01:00"), text, sizeof text));
    TEST_ASSERT_EQUAL_STRING("2026-10-26T23:01:05Z", text);
    TEST_ASSERT_TRUE(time_format_iso8601(at("2026-07-01T00:00:00+02:00"), text, sizeof text));
    TEST_ASSERT_EQUAL_STRING("2026-06-30T22:00:00Z", text);
}

static void test_format_needs_room_for_the_whole_time(void) {
    char text[sizeof "2026-10-26T11:00:00Z"];
    TEST_ASSERT_TRUE(time_format_iso8601(at("2026-10-26T12:00:00+01:00"), text, sizeof text));
    TEST_ASSERT_EQUAL_STRING("2026-10-26T11:00:00Z", text);
    TEST_ASSERT_FALSE(time_format_iso8601(at("2026-10-26T12:00:00+01:00"), text, sizeof text - 1));
}

static void test_time_left_is_the_rest_of_the_tick(void) {
    const struct timespec started = {.tv_sec = 100};
    const struct timespec now = {.tv_sec = 104, .tv_nsec = 500'000'000};
    const struct timespec left = time_left_in_tick(started, now, 15);
    TEST_ASSERT_EQUAL_INT64(10, left.tv_sec);
    TEST_ASSERT_EQUAL_INT64(500'000'000, left.tv_nsec);
}

static void test_time_left_is_zero_after_an_overrun(void) {
    const struct timespec started = {.tv_sec = 100};
    const struct timespec now = {.tv_sec = 116};
    const struct timespec left = time_left_in_tick(started, now, 15);
    TEST_ASSERT_EQUAL_INT64(0, left.tv_sec);
    TEST_ASSERT_EQUAL_INT64(0, left.tv_nsec);
}

static void test_time_left_borrows_nanoseconds(void) {
    const struct timespec started = {.tv_sec = 100, .tv_nsec = 900'000'000};
    const struct timespec now = {.tv_sec = 101, .tv_nsec = 100'000'000};
    const struct timespec left = time_left_in_tick(started, now, 15);
    TEST_ASSERT_EQUAL_INT64(14, left.tv_sec);
    TEST_ASSERT_EQUAL_INT64(800'000'000, left.tv_nsec);
}

static void test_clock_synced_matches_timesyncd(void) {
    const bool flagged = access("/run/systemd/timesync/synchronized", F_OK) == 0;
    const bool timesyncd = access("/run/systemd/timesync", F_OK) == 0;
    TEST_ASSERT_EQUAL(flagged || !timesyncd, time_clock_synced()); // no timesyncd: trusted
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_time_units);
    RUN_TEST(test_same_date_needs_year_month_and_day);
    RUN_TEST(test_parse_iso8601);
    RUN_TEST(test_parse_needs_offset);
    RUN_TEST(test_warsaw_autumn_change_2026_10_25);
    RUN_TEST(test_warsaw_spring_change_2026_03_29);
    RUN_TEST(test_warsaw_spring_change_2027_03_28);
    RUN_TEST(test_format_matches_old_report);
    RUN_TEST(test_format_needs_room_for_the_whole_time);
    RUN_TEST(test_time_left_is_the_rest_of_the_tick);
    RUN_TEST(test_time_left_is_zero_after_an_overrun);
    RUN_TEST(test_time_left_borrows_nanoseconds);
    RUN_TEST(test_clock_synced_matches_timesyncd);
    return UNITY_END();
}
