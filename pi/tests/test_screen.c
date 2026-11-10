#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "draw/frame.h"
#include "screen/screen.h"
#include "shared/time.h"
#include "stderr_capture.h"
#include "unity.h"

#define PHRASE       "与其祈求生活平淡点，还不如自己强大点"
#define OTHER_PHRASE "新的一天，新的开始"

static constexpr int64_t PHRASE_ID = 5;
static constexpr int CLEANUP_AT_TEN = 600;
static constexpr int TICK_SECONDS = 15;
static constexpr int GAP_SECONDS = 180;
static constexpr int GREETING_WAIT_SECONDS = 60;

static struct frame glass_frame;
static struct frame other_frame;
static time_t noon;

static time_t at(const char *text) {
    time_t moment = 0;
    TEST_ASSERT_TRUE_MESSAGE(time_parse_iso8601(text, &moment), text);
    return moment;
}

void setUp(void) {
    for (size_t i = 0; i < FRAME_BYTES; i++) {
        glass_frame.bits[i] = (uint8_t)(i * 37 + i / 100);
        other_frame.bits[i] = (uint8_t)(i * 11 + 0x5A);
    }
    noon = at("2026-10-26T12:00:00+01:00");
}

void tearDown(void) {
}

static void known_wall(struct wall *wall, time_t full_refresh_at) {
    *wall = screen_new_wall();
    wall->known = true;
    wall->frame = glass_frame;
    wall->phrase_id = PHRASE_ID;
    memcpy(wall->phrase_text, PHRASE, sizeof PHRASE);
    wall->date = (struct civil_date){2026, 10, 26};
    wall->last_kind = REFRESH_FULL;
    wall->last_refresh = full_refresh_at;
}

static struct wanted as_on_glass(void) {
    return (struct wanted){
            .frame = &glass_frame,
            .phrase_id = PHRASE_ID,
            .phrase_text = PHRASE,
            .date = {2026, 10, 26},
            .cleanup_minute = CLEANUP_AT_TEN,
            .answered_today = true,
    };
}

static void assert_decision(enum refresh_kind kind, enum refresh_reason reason,
                            struct decision decision) {
    TEST_ASSERT_EQUAL_INT(kind, decision.kind);
    TEST_ASSERT_EQUAL_INT(reason, decision.reason);
}

static struct decision tick(struct wall *wall, const struct wanted *wanted, time_t now) {
    const struct decision decision = screen_decide(wall, wanted, now);
    *wall = screen_note_tick(*wall, wanted, decision, now);
    return decision;
}

static void refreshed(struct wall *wall, const struct wanted *wanted, enum refresh_kind kind,
                      time_t now) {
    *wall = screen_note_refresh(*wall, wanted, (struct refresh_result){.kind = kind, .ok = true},
                                now);
}

static void test_unknown_glass_is_full(void) {
    struct wall wall = screen_new_wall();
    TEST_ASSERT_FALSE(wall.known);
    TEST_ASSERT_EQUAL_INT(CLEANUP_AT_TEN, wall.cleanup_minute);
    TEST_ASSERT_EQUAL_INT64(0, wall.last_refresh);
    struct wanted wanted = as_on_glass();
    assert_decision(REFRESH_FULL, REASON_UNKNOWN_GLASS, tick(&wall, &wanted, noon));

    known_wall(&wall, noon);
    wall.known = false;
    assert_decision(REFRESH_FULL, REASON_UNKNOWN_GLASS, tick(&wall, &wanted, noon + 600));
    wanted.frame = &other_frame;
    assert_decision(REFRESH_FULL, REASON_UNKNOWN_GLASS, tick(&wall, &wanted, noon + 600));
}

static void test_new_full_refresh_id_is_full_once(void) {
    struct wall wall;
    known_wall(&wall, noon);
    struct wanted wanted = as_on_glass();
    wanted.full_refresh = 41;
    assert_decision(REFRESH_FULL, REASON_FULL_REFRESH_ASKED, tick(&wall, &wanted, noon + 600));
    refreshed(&wall, &wanted, REFRESH_FULL, noon + 600);
    TEST_ASSERT_EQUAL_INT64(41, wall.full_refresh_done);
    assert_decision(REFRESH_NONE, REASON_SAME_FRAME, tick(&wall, &wanted, noon + 1200));
    wanted.full_refresh = 42;
    assert_decision(REFRESH_FULL, REASON_FULL_REFRESH_ASKED, tick(&wall, &wanted, noon + 1500));
}

static void test_new_phrase_is_full(void) {
    struct wall wall;
    known_wall(&wall, noon);
    struct wanted wanted = as_on_glass();
    wanted.frame = &other_frame;
    wanted.phrase_id = 6;
    wanted.phrase_text = OTHER_PHRASE;
    assert_decision(REFRESH_FULL, REASON_NEW_PHRASE, tick(&wall, &wanted, noon + 600));
    refreshed(&wall, &wanted, REFRESH_FULL, noon + 600);
    TEST_ASSERT_EQUAL_INT64(6, wall.phrase_id);
    TEST_ASSERT_EQUAL_STRING(OTHER_PHRASE, wall.phrase_text);

    wanted.frame = &glass_frame; // the same id with new text is a new phrase too
    wanted.phrase_text = PHRASE;
    assert_decision(REFRESH_FULL, REASON_NEW_PHRASE, tick(&wall, &wanted, noon + 1200));
}

static void test_new_day_is_full(void) {
    struct wall wall;
    known_wall(&wall, noon);
    struct wanted wanted = as_on_glass();
    wanted.frame = &other_frame;
    wanted.date = (struct civil_date){2026, 10, 27};
    assert_decision(REFRESH_FULL, REASON_NEW_DAY, tick(&wall, &wanted, noon + 600));
    refreshed(&wall, &wanted, REFRESH_FULL, noon + 600);
    TEST_ASSERT_EQUAL_INT(27, wall.date.day);
    assert_decision(REFRESH_NONE, REASON_SAME_FRAME, tick(&wall, &wanted, noon + 1200));
}

static void test_cleanup_crossing_after_partial_is_full(void) {
    const time_t nine = at("2026-10-26T09:00:00+01:00");
    struct wall wall;
    known_wall(&wall, nine);
    struct wanted wanted = as_on_glass();
    wanted.frame = &other_frame;
    assert_decision(REFRESH_PARTIAL, REASON_CHANGE, tick(&wall, &wanted, nine + 1800));
    refreshed(&wall, &wanted, REFRESH_PARTIAL, nine + 1800);
    assert_decision(REFRESH_NONE, REASON_SAME_FRAME,
                    tick(&wall, &wanted, at("2026-10-26T09:59:50+01:00")));
    TEST_ASSERT_FALSE(wall.cleanup_due);
    wanted.frame = &glass_frame; // a changed frame at the crossing is still full, never partial
    assert_decision(REFRESH_FULL, REASON_CLEANUP,
                    tick(&wall, &wanted, at("2026-10-26T10:00:05+01:00")));
    TEST_ASSERT_TRUE(wall.cleanup_due);
    refreshed(&wall, &wanted, REFRESH_FULL, at("2026-10-26T10:00:05+01:00"));
    TEST_ASSERT_FALSE(wall.cleanup_due);
    TEST_ASSERT_EQUAL_INT(0, wall.partials_since_full);
}

static void test_cleanup_crossing_without_partial_clears_due(void) {
    const time_t nine = at("2026-10-26T09:00:00+01:00");
    struct wall wall;
    known_wall(&wall, nine);
    struct wanted wanted = as_on_glass();
    assert_decision(REFRESH_NONE, REASON_SAME_FRAME,
                    tick(&wall, &wanted, at("2026-10-26T09:59:50+01:00")));
    wanted.frame = &other_frame; // the change waits a tick: nothing to clean, so no partial either
    assert_decision(REFRESH_NONE, REASON_CLEANUP,
                    tick(&wall, &wanted, at("2026-10-26T10:00:05+01:00")));
    TEST_ASSERT_FALSE(wall.cleanup_due);
    assert_decision(REFRESH_PARTIAL, REASON_CHANGE,
                    tick(&wall, &wanted, at("2026-10-26T10:00:20+01:00")));
}

static void test_cleanup_crossing_needs_ticks_on_the_same_day(void) {
    const time_t nine = at("2026-10-26T09:00:00+01:00");
    struct wall wall;
    known_wall(&wall, nine);
    wall.partials_since_full = 1;
    wall.last_tick = at("2026-10-25T09:30:00+01:00");
    struct wanted wanted = as_on_glass();
    assert_decision(REFRESH_NONE, REASON_SAME_FRAME,
                    tick(&wall, &wanted, at("2026-10-26T10:00:05+01:00")));
    TEST_ASSERT_FALSE(wall.cleanup_due);
    assert_decision(REFRESH_NONE, REASON_SAME_FRAME,
                    tick(&wall, &wanted, at("2026-10-26T10:00:20+01:00")));
    TEST_ASSERT_FALSE(wall.cleanup_due);
}

static void test_cleanup_follows_the_dashboard_minute(void) {
    const time_t nine = at("2026-10-26T09:00:00+01:00");
    struct wall wall;
    known_wall(&wall, nine);
    struct wanted wanted = as_on_glass();
    wanted.cleanup_minute = 11 * 60;
    wanted.frame = &other_frame;
    refreshed(&wall, &wanted, REFRESH_PARTIAL, nine + 1800);
    assert_decision(REFRESH_NONE, REASON_SAME_FRAME,
                    tick(&wall, &wanted, at("2026-10-26T09:59:50+01:00")));
    assert_decision(REFRESH_NONE, REASON_SAME_FRAME,
                    tick(&wall, &wanted, at("2026-10-26T10:00:05+01:00")));
    TEST_ASSERT_EQUAL_INT(11 * 60, wall.cleanup_minute);
    assert_decision(REFRESH_NONE, REASON_SAME_FRAME,
                    tick(&wall, &wanted, at("2026-10-26T10:59:50+01:00")));
    assert_decision(REFRESH_FULL, REASON_CLEANUP,
                    tick(&wall, &wanted, at("2026-10-26T11:00:05+01:00")));
}

static void test_changed_frame_is_partial(void) {
    struct wall wall;
    known_wall(&wall, noon);
    struct wanted wanted = as_on_glass();
    wanted.frame = &other_frame;
    assert_decision(REFRESH_PARTIAL, REASON_CHANGE, tick(&wall, &wanted, noon + 600));
    refreshed(&wall, &wanted, REFRESH_PARTIAL, noon + 600);
    TEST_ASSERT_TRUE(frame_equal(&other_frame, &wall.frame));
    TEST_ASSERT_EQUAL_INT(REFRESH_PARTIAL, wall.last_kind);
    TEST_ASSERT_EQUAL_INT(1, wall.partials_since_full);
    TEST_ASSERT_EQUAL_INT64(noon + 600, wall.last_refresh);
}

static void test_fifth_change_is_full(void) {
    struct wall wall;
    known_wall(&wall, noon);
    struct wanted wanted = as_on_glass();
    time_t now = noon;
    for (int i = 0; i < 4; i++) {
        now += 1800;
        wanted.frame = i % 2 == 0 ? &other_frame : &glass_frame;
        assert_decision(REFRESH_PARTIAL, REASON_CHANGE, tick(&wall, &wanted, now));
        refreshed(&wall, &wanted, REFRESH_PARTIAL, now);
    }
    TEST_ASSERT_EQUAL_INT(4, wall.partials_since_full);
    now += 1800;
    wanted.frame = &other_frame;
    assert_decision(REFRESH_FULL, REASON_CHANGE, tick(&wall, &wanted, now));
    refreshed(&wall, &wanted, REFRESH_FULL, now);
    TEST_ASSERT_EQUAL_INT(0, wall.partials_since_full);
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, wall.last_kind);
}

static void test_identical_frame_is_none(void) {
    struct wall wall;
    known_wall(&wall, noon);
    struct wanted wanted = as_on_glass();
    assert_decision(REFRESH_NONE, REASON_SAME_FRAME, tick(&wall, &wanted, noon + 600));
    refreshed(&wall, &wanted, REFRESH_NONE, noon + 600);
    TEST_ASSERT_EQUAL_INT64(noon, wall.last_refresh);
    TEST_ASSERT_EQUAL_INT(0, wall.partials_since_full);
}

static void test_179_s_after_refresh_is_too_soon(void) {
    struct wall wall;
    known_wall(&wall, noon);
    struct wanted wanted = as_on_glass();
    wanted.frame = &other_frame;
    assert_decision(REFRESH_NONE, REASON_TOO_SOON, tick(&wall, &wanted, noon + GAP_SECONDS - 1));
    wanted.phrase_id = 6;
    wanted.phrase_text = OTHER_PHRASE;
    assert_decision(REFRESH_NONE, REASON_TOO_SOON, tick(&wall, &wanted, noon + GAP_SECONDS - 1));
}

static void test_180_s_after_refresh_refreshes(void) {
    struct wall wall;
    known_wall(&wall, noon);
    struct wanted wanted = as_on_glass();
    wanted.frame = &other_frame;
    assert_decision(REFRESH_PARTIAL, REASON_CHANGE, tick(&wall, &wanted, noon + GAP_SECONDS));
    wanted.phrase_id = 6;
    wanted.phrase_text = OTHER_PHRASE;
    assert_decision(REFRESH_FULL, REASON_NEW_PHRASE, tick(&wall, &wanted, noon + GAP_SECONDS));
}

static void test_clock_went_back_waits_from_now(void) {
    struct wall wall;
    known_wall(&wall, noon);
    struct wanted wanted = as_on_glass();
    wanted.frame = &other_frame;
    const time_t yesterday = noon - 86'400;
    assert_decision(REFRESH_NONE, REASON_TOO_SOON, tick(&wall, &wanted, yesterday));
    TEST_ASSERT_EQUAL_INT64(yesterday, wall.last_refresh);
    assert_decision(REFRESH_NONE, REASON_TOO_SOON,
                    tick(&wall, &wanted, yesterday + GAP_SECONDS - 1));
    assert_decision(REFRESH_PARTIAL, REASON_CHANGE, tick(&wall, &wanted, yesterday + GAP_SECONDS));
}

static void test_new_day_waits_60_s_for_greeting(void) {
    const time_t midnight = at("2026-10-27T00:00:00+01:00");
    struct wall wall;
    known_wall(&wall, at("2026-10-26T23:50:00+01:00"));
    struct wanted wanted = as_on_glass();
    wanted.frame = &other_frame;
    wanted.date = (struct civil_date){2026, 10, 27};
    wanted.answered_today = false;
    for (time_t after = 5; after < GREETING_WAIT_SECONDS + 5; after += TICK_SECONDS) {
        assert_decision(REFRESH_NONE, REASON_GREETING_WAIT, tick(&wall, &wanted, midnight + after));
    }
    TEST_ASSERT_EQUAL_INT64(midnight + 5, wall.greeting_wait_since);
    assert_decision(REFRESH_FULL, REASON_NEW_DAY,
                    tick(&wall, &wanted, midnight + GREETING_WAIT_SECONDS + 5));
    refreshed(&wall, &wanted, REFRESH_FULL, midnight + GREETING_WAIT_SECONDS + 5);
    TEST_ASSERT_EQUAL_INT64(0, wall.greeting_wait_since);
}

static void test_greeting_arriving_during_wait_refreshes_at_once(void) {
    const time_t midnight = at("2026-10-27T00:00:00+01:00");
    struct wall wall;
    known_wall(&wall, at("2026-10-26T23:50:00+01:00"));
    struct wanted wanted = as_on_glass();
    wanted.frame = &other_frame;
    wanted.date = (struct civil_date){2026, 10, 27};
    wanted.answered_today = false;
    assert_decision(REFRESH_NONE, REASON_GREETING_WAIT, tick(&wall, &wanted, midnight + 5));
    wanted.answered_today = true;
    assert_decision(REFRESH_FULL, REASON_NEW_DAY, tick(&wall, &wanted, midnight + 20));
}

static void test_new_phrase_on_a_new_day_does_not_wait(void) {
    const time_t midnight = at("2026-10-27T00:00:00+01:00");
    struct wall wall;
    known_wall(&wall, at("2026-10-26T23:50:00+01:00"));
    struct wanted wanted = as_on_glass();
    wanted.frame = &other_frame;
    wanted.date = (struct civil_date){2026, 10, 27};
    wanted.answered_today = false;
    wanted.phrase_id = 6;
    wanted.phrase_text = OTHER_PHRASE;
    assert_decision(REFRESH_FULL, REASON_NEW_PHRASE, tick(&wall, &wanted, midnight + 5));
    TEST_ASSERT_EQUAL_INT64(0, wall.greeting_wait_since);
}

static void test_greeting_wait_started_in_the_future_restarts_now(void) {
    const time_t midnight = at("2026-10-27T00:00:00+01:00");
    struct wall wall;
    known_wall(&wall, at("2026-10-26T23:50:00+01:00"));
    wall.greeting_wait_since = midnight + 3600;
    struct wanted wanted = as_on_glass();
    wanted.frame = &other_frame;
    wanted.date = (struct civil_date){2026, 10, 27};
    wanted.answered_today = false;
    assert_decision(REFRESH_NONE, REASON_GREETING_WAIT, tick(&wall, &wanted, midnight + 5));
    TEST_ASSERT_EQUAL_INT64(midnight + 5, wall.greeting_wait_since);
    assert_decision(REFRESH_FULL, REASON_NEW_DAY,
                    tick(&wall, &wanted, midnight + GREETING_WAIT_SECONDS + 5));
}

static void test_failed_refresh_makes_glass_unknown(void) {
    struct wall wall;
    known_wall(&wall, noon);
    struct wanted wanted = as_on_glass();
    wanted.frame = &other_frame;
    const time_t failed_at = noon + 600;
    assert_decision(REFRESH_PARTIAL, REASON_CHANGE, tick(&wall, &wanted, failed_at));
    wall = screen_note_refresh(wall, &wanted, (struct refresh_result){REFRESH_PARTIAL, false},
                               failed_at);
    TEST_ASSERT_FALSE(wall.known);
    TEST_ASSERT_EQUAL_INT64(failed_at, wall.last_refresh);
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, wall.last_kind);
    TEST_ASSERT_EQUAL_INT64(PHRASE_ID, wall.phrase_id);
    for (time_t now = failed_at; now < failed_at + GAP_SECONDS; now += TICK_SECONDS) {
        assert_decision(REFRESH_NONE, REASON_TOO_SOON, tick(&wall, &wanted, now));
    }
    assert_decision(REFRESH_FULL, REASON_UNKNOWN_GLASS,
                    tick(&wall, &wanted, failed_at + GAP_SECONDS));
}

static void test_full_success_clears_cleanup_and_marks_request_done(void) {
    struct wall wall;
    known_wall(&wall, noon);
    wall.cleanup_due = true;
    wall.partials_since_full = 3;
    wall.greeting_wait_since = noon;
    struct wanted wanted = as_on_glass();
    wanted.frame = &other_frame;
    wanted.phrase_id = 6;
    wanted.phrase_text = OTHER_PHRASE;
    wanted.date = (struct civil_date){2026, 10, 27};
    wanted.full_refresh = 41;
    refreshed(&wall, &wanted, REFRESH_FULL, noon + 600);
    TEST_ASSERT_TRUE(wall.known);
    TEST_ASSERT_TRUE(frame_equal(&other_frame, &wall.frame));
    TEST_ASSERT_EQUAL_INT64(6, wall.phrase_id);
    TEST_ASSERT_EQUAL_STRING(OTHER_PHRASE, wall.phrase_text);
    TEST_ASSERT_EQUAL_INT(2026, wall.date.year);
    TEST_ASSERT_EQUAL_INT(10, wall.date.month);
    TEST_ASSERT_EQUAL_INT(27, wall.date.day);
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, wall.last_kind);
    TEST_ASSERT_EQUAL_INT(0, wall.partials_since_full);
    TEST_ASSERT_EQUAL_INT64(noon + 600, wall.last_refresh);
    TEST_ASSERT_FALSE(wall.cleanup_due);
    TEST_ASSERT_EQUAL_INT64(41, wall.full_refresh_done);
    TEST_ASSERT_EQUAL_INT64(0, wall.greeting_wait_since);

    wanted.full_refresh = 0; // no request pending keeps the one already done
    refreshed(&wall, &wanted, REFRESH_FULL, noon + 1200);
    TEST_ASSERT_EQUAL_INT64(41, wall.full_refresh_done);
}

static void test_no_phrase_on_the_glass(void) {
    struct wall wall;
    known_wall(&wall, noon);
    struct wanted wanted = as_on_glass();
    wanted.frame = &other_frame;
    wanted.phrase_id = 0;
    wanted.phrase_text = nullptr;
    assert_decision(REFRESH_FULL, REASON_NEW_PHRASE, tick(&wall, &wanted, noon + 600));
    refreshed(&wall, &wanted, REFRESH_FULL, noon + 600);
    TEST_ASSERT_EQUAL_INT64(0, wall.phrase_id);
    TEST_ASSERT_EQUAL_STRING("", wall.phrase_text);
    assert_decision(REFRESH_NONE, REASON_SAME_FRAME, tick(&wall, &wanted, noon + 1200));
}

static void test_decide_leaves_the_wall_unchanged(void) {
    struct wall wall = screen_new_wall();
    const struct wall before = wall;
    const struct wanted wanted = as_on_glass();
    (void)screen_decide(&wall, &wanted, noon);
    TEST_ASSERT_EQUAL_MEMORY(&before, &wall, sizeof wall);

    known_wall(&wall, at("2026-10-26T09:00:00+01:00"));
    wall.partials_since_full = 1;
    wall.last_tick = at("2026-10-26T09:59:50+01:00");
    const struct wall due = wall;
    assert_decision(REFRESH_FULL, REASON_CLEANUP,
                    screen_decide(&wall, &wanted, at("2026-10-26T10:00:05+01:00")));
    TEST_ASSERT_EQUAL_MEMORY(&due, &wall, sizeof wall);
}

static void test_reason_texts(void) {
    TEST_ASSERT_EQUAL_STRING("same frame", screen_reason_text(REASON_SAME_FRAME));
    TEST_ASSERT_EQUAL_STRING("unknown glass", screen_reason_text(REASON_UNKNOWN_GLASS));
    TEST_ASSERT_EQUAL_STRING("full refresh asked", screen_reason_text(REASON_FULL_REFRESH_ASKED));
    TEST_ASSERT_EQUAL_STRING("new phrase", screen_reason_text(REASON_NEW_PHRASE));
    TEST_ASSERT_EQUAL_STRING("new day", screen_reason_text(REASON_NEW_DAY));
    TEST_ASSERT_EQUAL_STRING("clean-up", screen_reason_text(REASON_CLEANUP));
    TEST_ASSERT_EQUAL_STRING("change", screen_reason_text(REASON_CHANGE));
    TEST_ASSERT_EQUAL_STRING("too soon", screen_reason_text(REASON_TOO_SOON));
    TEST_ASSERT_EQUAL_STRING("greeting wait", screen_reason_text(REASON_GREETING_WAIT));
}

static void test_no_panel_on_this_machine(void) {
#ifdef __linux__
    TEST_IGNORE_MESSAGE("the panel on the Pi is live, so screen_open is never called here");
#else
    // without a board nothing is looked for again, and there is nothing to clear
    char captured[256];
    start_capture();
    struct screen *screen = screen_open();
    const struct panel_search panel = screen_find_panel(screen);
    const bool cleared = screen_clear(screen);
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_NULL(screen);
    TEST_ASSERT_FALSE(panel.found);
    TEST_ASSERT_FALSE(panel.just_found);
    TEST_ASSERT_FALSE(cleared);
    TEST_ASSERT_EQUAL_STRING("<6>no panel: the board can't be opened\n<3>no panel to clear\n",
                             captured);
    screen_close(nullptr);
#endif
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_unknown_glass_is_full);
    RUN_TEST(test_new_full_refresh_id_is_full_once);
    RUN_TEST(test_new_phrase_is_full);
    RUN_TEST(test_new_day_is_full);
    RUN_TEST(test_cleanup_crossing_after_partial_is_full);
    RUN_TEST(test_cleanup_crossing_without_partial_clears_due);
    RUN_TEST(test_cleanup_crossing_needs_ticks_on_the_same_day);
    RUN_TEST(test_cleanup_follows_the_dashboard_minute);
    RUN_TEST(test_changed_frame_is_partial);
    RUN_TEST(test_fifth_change_is_full);
    RUN_TEST(test_identical_frame_is_none);
    RUN_TEST(test_179_s_after_refresh_is_too_soon);
    RUN_TEST(test_180_s_after_refresh_refreshes);
    RUN_TEST(test_clock_went_back_waits_from_now);
    RUN_TEST(test_new_day_waits_60_s_for_greeting);
    RUN_TEST(test_greeting_arriving_during_wait_refreshes_at_once);
    RUN_TEST(test_new_phrase_on_a_new_day_does_not_wait);
    RUN_TEST(test_greeting_wait_started_in_the_future_restarts_now);
    RUN_TEST(test_failed_refresh_makes_glass_unknown);
    RUN_TEST(test_full_success_clears_cleanup_and_marks_request_done);
    RUN_TEST(test_no_phrase_on_the_glass);
    RUN_TEST(test_decide_leaves_the_wall_unchanged);
    RUN_TEST(test_reason_texts);
    RUN_TEST(test_no_panel_on_this_machine);
    return UNITY_END();
}
