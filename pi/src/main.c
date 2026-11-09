/* the program: set up once, then one tick every 15 seconds */
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <curl/curl.h>

#include "config.h"
#include "date.h"
#include "draw/frame.h"
#include "draw/layout.h"
#include "phrase.h"
#include "record.h"
#include "screen/screen.h"
#include "shared/dashboard.h"
#include "shared/http.h"
#include "shared/log.h"
#include "shared/time.h"
#include "state.h"
#include "warnings.h"
#include "weather.h"

#define USAGE "usage: kotetsu [--clear] [--version]"

static constexpr int TICK_SECONDS = 15;

static volatile sig_atomic_t stop_requested;

static void request_stop([[maybe_unused]] int signal_number) {
    stop_requested = 1;
}

static bool stop_on(int signal_number) {
    struct sigaction action = {
            .sa_handler = request_stop,
            .sa_flags =
                    SA_RESTART, // a refresh or save under way finishes, and the sleep still wakes
    };
    if (sigemptyset(&action.sa_mask) != 0 || sigaction(signal_number, &action, nullptr) != 0) {
        log_error("can't handle signal %d: %s", signal_number, strerror(errno));
        return false;
    }
    return true;
}

int main(int argc, char *argv[]) {
    /* read the options */
    bool clear = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            puts("kotetsu " KOTETSU_VERSION);
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "--clear") != 0) {
            log_error(USAGE);
            return EXIT_FAILURE;
        }
        clear = true;
    }

    /* clear the panel instead */
    if (clear) {
        struct screen *screen = screen_open();
        const bool cleared = screen_clear(screen); // which says when there is no panel
        screen_close(screen);
        return cleared ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    /* Warsaw time, curl and config */
    setenv("TZ", "Europe/Warsaw", 1);
    tzset();
    if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
        log_error("can't initialise libcurl");
        return EXIT_FAILURE;
    }
    const struct config config = config_load();
    if (!config.ok) {
        return EXIT_FAILURE;
    }
    char phrase_url[sizeof config.dashboard_url + sizeof PHRASE_NOW_PATH];
    snprintf(phrase_url, sizeof phrase_url, "%s" PHRASE_NOW_PATH, config.dashboard_url);

    /* open the fonts and panel */
    struct drawing *drawing = draw_open(config.font_dir);
    if (drawing == nullptr) {
        return EXIT_FAILURE;
    }
    struct screen *screen = screen_open();

    /* restore the saved state */
    const struct saved_state saved = state_restore(config.state_path);
    if (saved.ok) {
        log_info("restored the saved state from %s", config.state_path);
    }
    struct phrase_state phrase = {};
    struct weather_state weather = {
            .have_report = saved.have_weather, .report = saved.weather, .eaqi = -1};
    struct wall wall = saved.wall;
    struct record_state record = {};
    int64_t too_long_logged = 0;

    /* handle the stop signals */
    if (!stop_on(SIGTERM) || !stop_on(SIGINT)) {
        return EXIT_FAILURE;
    }
    signal(SIGPIPE, SIG_IGN); // a dashboard that drops an upload mid-way must not end the program

    /* log the start */
    log_info("kotetsu %s: dashboard %s, state %s", KOTETSU_VERSION, config.dashboard_url,
             config.state_path);

    /* wait for the clock */
    if (!time_clock_synced()) {
        log_info("waiting for the clock to synchronise before drawing");
        const struct timespec tick = {.tv_sec = TICK_SECONDS};
        while (!stop_requested && !time_clock_synced()) {
            nanosleep(&tick, nullptr);
        }
    }

    while (!stop_requested) {
        /* fetch phrase and weather */
        struct timespec started;
        clock_gettime(CLOCK_MONOTONIC, &started);
        const time_t now = time(nullptr);
        const bool weather_due = now >= weather.next_fetch;
        const struct http_request requests[] = {
                {.url = phrase_url, .bearer = config.pi_key},
                {.url = WEATHER_OPEN_METEO_URL},
                {.url = WEATHER_AIR_QUALITY_URL},
                {.url = WARNINGS_URL},
        };
        struct http_batch answers =
                http_get_batch(requests, weather_due ? sizeof requests / sizeof *requests : 1);

        /* read the answers */
        phrase = phrase_read(phrase, phrase_url, &answers.responses[0], now);
        const char *dashboard_outcome = "dashboard ok";
        if (phrase.status != DASHBOARD_OK) {
            dashboard_outcome = dashboard_problem(phrase.status);
            record = record_keep_error(record, dashboard_outcome);
        }
        const char *weather_outcome = "weather not due";
        if (weather_due) {
            struct forecast forecast = weather_read_open_meteo(&answers.responses[1], now);
            if (weather_wants_met_norway(&weather, &forecast)) {
                struct http_response met = http_get(WEATHER_MET_NORWAY_URL, nullptr);
                forecast = weather_read_met_norway(&met, now);
                http_response_free(&met);
            }
            weather = weather_update(weather, &forecast, &answers.responses[2],
                                     &answers.responses[3], now);
            weather_outcome = forecast.ok ? "weather fetched" : "weather failed";
            if (!forecast.ok) {
                record = record_keep_error(record, "weather fetch failed");
            }
        }
        http_batch_free(&answers);

        /* work out the date */
        const struct date date = date_for(now);
        if (!date.ok) {
            record = record_keep_error(record, "no date line for today");
            record_log_tick(&(struct tick_summary){.dashboard = dashboard_outcome,
                                                   .weather = weather_outcome});
            goto next_tick;
        }

        /* draw the frame */
        const int64_t wanted_id = phrase.have_answer ? phrase.answer.id : wall.phrase_id;
        const char *wanted_text = phrase.have_answer ? phrase.answer.text : wall.phrase_text;
        const struct draw_input input = {
                .phrase = wanted_text,
                .glass_phrase = wall.phrase_id != 0 ? wall.phrase_text : nullptr,
                .date = date,
                .weather = weather_view(&weather, now),
                .show_greeting = phrase.answered_today && phrase.answer.has_greeting,
                .greeting_icon = phrase.answer.greeting_icon,
                .greeting = phrase.answer.greeting,
        };
        struct frame frame;
        const struct draw_result drawn = draw(drawing, &input, &frame);
        if (!drawn.ok) {
            record = record_keep_error(record, "drawing failed");
            record_log_tick(&(struct tick_summary){.dashboard = dashboard_outcome,
                                                   .weather = weather_outcome});
            goto next_tick;
        }
        const int64_t phrase_id = !drawn.phrase_drawn       ? 0
                                  : drawn.drew_glass_phrase ? wall.phrase_id
                                                            : wanted_id;
        const char *phrase_text = !drawn.phrase_drawn       ? nullptr
                                  : drawn.drew_glass_phrase ? wall.phrase_text
                                                            : wanted_text;
        const int64_t too_long_id = drawn.too_long ? wanted_id : 0;
        if (too_long_id != too_long_logged) {
            if (too_long_id != 0) {
                log_warning("phrase %" PRId64 " doesn't fit the screen; the glass keeps its phrase",
                            too_long_id);
            }
            too_long_logged = too_long_id;
        }

        /* decide on a refresh */
        const struct panel_search panel = screen_find_panel(screen);
        if (panel.just_found) {
            wall.known = false; // it shows nothing of ours yet
        }
        if (screen != nullptr && !panel.found) { // an upload clears it, so set it every tick
            record = record_keep_error(record, "no panel");
        }
        const struct wanted wanted = {
                .frame = &frame,
                .phrase_id = phrase_id,
                .phrase_text = phrase_text,
                .date = date.today,
                .full_refresh = phrase.have_answer ? phrase.answer.full_refresh : 0,
                .cleanup_minute =
                        phrase.have_answer ? phrase.answer.cleanup_minute : wall.cleanup_minute,
                .answered_today = phrase.answered_today,
        };
        const struct decision decision = screen_decide(&wall, &wanted, now);
        wall = screen_note_tick(wall, &wanted, decision, now);

        /* refresh the panel */
        const bool refreshing = decision.kind != REFRESH_NONE;
        bool refreshed = refreshing; // without a panel the wall just takes the frame
        if (refreshing && panel.found) {
            refreshed = screen_refresh(screen, &wall.frame, &frame, decision.kind);
        }
        wall = screen_note_refresh(wall, &wanted, (struct refresh_result){decision.kind, refreshed},
                                   now);
        if (refreshing && !refreshed) {
            record = record_keep_error(record, "panel refresh failed");
        }

        /* save the state */
        const int64_t missing_glyphs_id = drawn.missing_glyphs > 0 ? phrase_id : 0;
        if (refreshed) {
            if (missing_glyphs_id != 0) {
                log_warning("phrase %" PRId64 " has %d characters the font lacks",
                            missing_glyphs_id, drawn.missing_glyphs);
            }
            if (!state_save(config.state_path, &wall, &weather)) {
                record = record_keep_error(record, "state save failed");
            }
        }

        /* upload picture and report */
        const struct report report = {
                .refresh = refreshing ? decision.kind : wall.last_kind,
                .at = refreshing ? now : wall.last_refresh,
                .phrase_id = refreshing ? phrase_id : wall.phrase_id,
                .weather_at = weather.have_report ? weather.report.fetched_at : 0,
                .too_long_id = too_long_id,
                .missing_glyphs_id = missing_glyphs_id,
                .last_error = record.last_error[0] != '\0' ? record.last_error : nullptr,
        };
        const char *upload_outcome = "no upload";
        if (record_should_upload(&record, refreshing, too_long_id, wall.known)) {
            const struct upload_result upload =
                    record_upload(&config, refreshing ? &frame : &wall.frame, &report);
            record = record_after_upload(record, &upload, too_long_id);
            upload_outcome = upload.ok ? "upload ok" : "upload failed";
        }

        /* log the tick */
        const struct tick_summary summary = {
                .dashboard = dashboard_outcome,
                .weather = weather_outcome,
                .drawn = true,
                .phrase_id = phrase_id,
                .too_long = too_long_id != 0,
                .missing_glyphs = drawn.missing_glyphs,
                .decision = decision,
                .refreshed = refreshed,
                .panel = panel.found,
                .upload = upload_outcome,
        };
        record_log_tick(&summary);

next_tick:
        /* sleep until next tick */
        struct timespec ended;
        clock_gettime(CLOCK_MONOTONIC, &ended);
        const struct timespec left = time_left_in_tick(started, ended, TICK_SECONDS);
        nanosleep(&left, nullptr); // a stop signal cuts it short
    }

    /* close down */
    screen_close(screen);
    draw_close(drawing);
    curl_global_cleanup();
    return EXIT_SUCCESS;
}
