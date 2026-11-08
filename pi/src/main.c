#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
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
#include "shared/log.h"
#include "shared/time.h"
#include "warnings.h"
#include "weather.h"

#define USAGE "usage: kotetsu [--clear] [--version]"

static constexpr int TICK_SECONDS = 15;
static constexpr long NANOSECONDS_PER_SECOND = 1'000'000'000;

struct state { // what each module remembers between ticks
    struct phrase_state phrase;
    struct weather_state weather;
    struct wall wall;
    struct record_state record;
    int64_t too_long_logged; // the phrase already warned about
};

struct inputs { // what one tick collected
    time_t now;
    struct phrase phrase;
    const char *weather; // "weather fetched", "weather not due" or "weather failed"
    struct date date;
};

struct drawn { // one tick's frame, and what the wall holds once it shows
    time_t at;
    struct frame frame;
    int64_t phrase_id; // 0 for none
    char phrase_text[PHRASE_TEXT_MAX_BYTES];
    struct civil_date date;
    int64_t full_refresh;
    int cleanup_minute;
    bool answered_today;
    int64_t too_long_id; // the wanted phrase, when it didn't fit
    int missing_glyphs;
};

struct outcome { // what output did with the frame
    struct decision decision;
    bool refreshed;
    bool panel;         // a panel showed it; without one the wall just took the frame
    const char *upload; // "upload ok", "no upload" or "upload failed"
};

struct fetch { // the collect threads' arguments and results; each thread writes one field
    const struct config *config;
    struct phrase_state *phrase_state;
    const struct weather_state *weather_state;
    time_t now;
    struct phrase phrase;
    struct weather_fetched weather;
};

struct refresh_job {
    struct screen *screen;
    const struct frame *on_glass;
    const struct frame *next;
    enum refresh_kind kind;
    bool ok;
};

struct upload_job {
    struct record_state *record;
    const struct config *config;
    const struct frame *frame;
    struct report report;
    bool ok;
};

static volatile sig_atomic_t stop_requested;

static void request_stop([[maybe_unused]] int signal_number) {
    stop_requested = 1;
}

static bool stop_on(int signal_number) {
    struct sigaction action = {
            .sa_handler = request_stop,
            .sa_flags = SA_RESTART, // a refresh or save under way finishes; the sleep still wakes
    };
    if (sigemptyset(&action.sa_mask) != 0 || sigaction(signal_number, &action, nullptr) != 0) {
        log_error("can't handle signal %d: %s", signal_number, strerror(errno));
        return false;
    }
    return true;
}

static pthread_t start_thread(void *(*run)(void *), void *argument) {
    pthread_t thread;
    const int error = pthread_create(&thread, nullptr, run, argument);
    if (error != 0) {
        log_error("can't start a thread: %s", strerror(error));
        abort();
    }
    return thread;
}

static void *fetch_phrase(void *argument) {
    struct fetch *fetch = argument;
    fetch->phrase = phrase_fetch(fetch->phrase_state, fetch->config, fetch->now);
    return nullptr;
}

static void *fetch_forecast(void *argument) {
    struct fetch *fetch = argument;
    fetch->weather.forecast = weather_fetch_forecast(fetch->weather_state, fetch->now);
    return nullptr;
}

static void *fetch_air(void *argument) {
    struct fetch *fetch = argument;
    fetch->weather.air = weather_fetch_air();
    return nullptr;
}

static void *fetch_warnings(void *argument) {
    struct fetch *fetch = argument;
    fetch->weather.warnings = warnings_fetch(fetch->now);
    return nullptr;
}

static struct inputs collect(const struct config *config, struct state *state, time_t now) {
    /* the dashboard every tick, and the three weather sources when they are due */
    struct fetch fetch = {
            .config = config,
            .phrase_state = &state->phrase,
            .weather_state = &state->weather,
            .now = now,
    };
    void *(*const sources[])(void *) = {fetch_phrase, fetch_forecast, fetch_air, fetch_warnings};
    const bool fetching_weather = weather_due(&state->weather, now);
    const size_t count = fetching_weather ? sizeof sources / sizeof *sources : 1;
    pthread_t threads[sizeof sources / sizeof *sources];
    for (size_t i = 0; i < count; i++) {
        threads[i] = start_thread(sources[i], &fetch);
    }

    /* the date, on this thread meanwhile */
    struct inputs inputs = {.now = now, .weather = "weather not due", .date = date_for(now)};
    for (size_t i = 0; i < count; i++) {
        pthread_join(threads[i], nullptr);
    }

    /* what the dashboard said */
    inputs.phrase = fetch.phrase;
    if (inputs.phrase.status != DASHBOARD_OK) {
        record_error(&state->record, dashboard_problem(inputs.phrase.status));
    }

    /* what the weather sources said, kept for the next 30 minutes */
    if (fetching_weather) {
        weather_absorb(&state->weather, &fetch.weather, now);
        inputs.weather = fetch.weather.forecast.ok ? "weather fetched" : "weather failed";
        if (!fetch.weather.forecast.ok) {
            record_error(&state->record, "weather fetch failed");
        }
    }
    return inputs;
}

static bool draw_frame(struct drawing *drawing, const struct inputs *inputs, struct state *state,
                       struct drawn *out) {
    /* nothing is drawn without a date line */
    if (!inputs->date.ok) {
        record_error(&state->record, "no date line for today");
        return false;
    }

    /* the dashboard's phrase, or the glass's own until the dashboard has answered */
    const struct phrase *phrase = &inputs->phrase;
    const struct wall *wall = &state->wall;
    const int64_t wanted_id = phrase->have_answer ? phrase->answer.id : wall->phrase_id;
    const char *wanted_text = phrase->have_answer ? phrase->answer.text : wall->phrase_text;
    const char *glass_text = wall->phrase_id != 0 ? wall->phrase_text : nullptr;
    const struct draw_input input = {
            .phrase = wanted_text,
            .glass_phrase = glass_text,
            .date = inputs->date,
            .weather = weather_view(&state->weather, inputs->now),
            .show_greeting = phrase->answered_today && phrase->answer.has_greeting,
            .greeting_icon = phrase->answer.greeting_icon,
            .greeting = phrase->answer.greeting,
    };
    const struct draw_result result = draw(drawing, &input, &out->frame);
    if (!result.ok) {
        record_error(&state->record, "drawing failed");
        return false;
    }

    /* the phrase on the frame: the wanted one, the glass's own instead, or none */
    out->at = inputs->now;
    out->phrase_id = 0;
    out->phrase_text[0] = '\0';
    if (result.phrase_drawn) {
        out->phrase_id = result.drew_glass_phrase ? wall->phrase_id : wanted_id;
        memcpy(out->phrase_text, result.drew_glass_phrase ? wall->phrase_text : wanted_text,
               sizeof out->phrase_text);
    }
    out->date = inputs->date.today;
    out->full_refresh = phrase->have_answer ? phrase->answer.full_refresh : 0;
    out->cleanup_minute =
            phrase->have_answer ? phrase->answer.cleanup_minute : wall->cleanup_minute;
    out->answered_today = phrase->answered_today;
    out->too_long_id = result.too_long ? wanted_id : 0;
    out->missing_glyphs = result.missing_glyphs;

    /* say once which phrase is too long */
    if (out->too_long_id != state->too_long_logged) {
        if (out->too_long_id != 0) {
            log_warning("phrase %" PRId64 " doesn't fit the screen; the glass keeps its phrase",
                        out->too_long_id);
        }
        state->too_long_logged = out->too_long_id;
    }
    return true;
}

static void *refresh_panel(void *argument) {
    struct refresh_job *job = argument;
    job->ok = screen_refresh(job->screen, job->on_glass, job->next, job->kind);
    return nullptr;
}

static void *upload_frame(void *argument) {
    struct upload_job *job = argument;
    job->ok = record_upload(job->record, job->config, job->frame, &job->report);
    return nullptr;
}

static struct outcome output(struct screen *screen, const struct config *config,
                             struct state *state, const struct drawn *drawn) {
    /* the panel, looked for again while none has answered; one just found shows nothing of ours */
    const struct panel_search panel = screen_find_panel(screen);
    if (panel.just_found) {
        state->wall.known = false;
    }
    if (screen != nullptr && !panel.found) {
        record_error(&state->record, "no panel"); // an upload clears it, so set it every tick
    }

    /* what the frame calls for */
    const struct wanted wanted = {
            .frame = &drawn->frame,
            .phrase_id = drawn->phrase_id,
            .phrase_text = drawn->phrase_id != 0 ? drawn->phrase_text : nullptr,
            .date = drawn->date,
            .full_refresh = drawn->full_refresh,
            .cleanup_minute = drawn->cleanup_minute,
            .answered_today = drawn->answered_today,
    };
    const struct decision decision = screen_decide(&state->wall, &wanted, drawn->at);
    const bool refreshing = decision.kind != REFRESH_NONE;

    /* the report goes with the frame refreshed now, or with the one on the glass */
    const struct wall *wall = &state->wall;
    const int64_t missing_glyphs_id = drawn->missing_glyphs > 0 ? drawn->phrase_id : 0;
    const struct report report = {
            .refresh = refreshing ? decision.kind : wall->last_kind,
            .at = refreshing ? drawn->at : wall->last_refresh,
            .phrase_id = refreshing ? drawn->phrase_id : wall->phrase_id,
            .weather_at = state->weather.have_report ? state->weather.report.fetched_at : 0,
            .too_long_id = drawn->too_long_id,
            .missing_glyphs_id = missing_glyphs_id,
            .last_error = state->record.last_error[0] != '\0' ? state->record.last_error : nullptr,
    };
    struct upload_job upload = {
            .record = &state->record,
            .config = config,
            .frame = refreshing ? &drawn->frame : &wall->frame,
            .report = report,
    };
    const bool uploading =
            record_should_upload(&state->record, refreshing, drawn->too_long_id, wall->known);

    /* the panel and the dashboard side by side; without a panel the wall just takes the frame */
    struct refresh_job refresh = {
            .screen = screen,
            .on_glass = &wall->frame,
            .next = &drawn->frame,
            .kind = decision.kind,
            .ok = !panel.found,
    };
    pthread_t threads[2];
    size_t count = 0;
    if (refreshing && panel.found) {
        threads[count++] = start_thread(refresh_panel, &refresh);
    }
    if (uploading) {
        threads[count++] = start_thread(upload_frame, &upload);
    }
    for (size_t i = 0; i < count; i++) {
        pthread_join(threads[i], nullptr);
    }

    /* what the wall holds now, saved once the panel shows it */
    screen_record(&state->wall, &wanted, (struct refresh_result){decision.kind, refresh.ok},
                  drawn->at);
    if (refreshing && !refresh.ok) {
        record_error(&state->record, "panel refresh failed");
    }
    if (refreshing && refresh.ok) {
        if (missing_glyphs_id != 0) {
            log_warning("phrase %" PRId64 " has %d characters the font lacks", missing_glyphs_id,
                        drawn->missing_glyphs);
        }
        if (!record_save(config->state_path, &state->wall, &state->weather)) {
            record_error(&state->record, "state save failed");
        }
    }
    return (struct outcome){
            .decision = decision,
            .refreshed = refreshing && refresh.ok,
            .panel = panel.found,
            .upload = !uploading  ? "no upload"
                      : upload.ok ? "upload ok"
                                  : "upload failed",
    };
}

static void sleep_until_next_tick(const struct timespec *started) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    const long long remaining =
            (started->tv_sec + TICK_SECONDS - now.tv_sec) * (long long)NANOSECONDS_PER_SECOND +
            (started->tv_nsec - now.tv_nsec);
    if (remaining <= 0) {
        return;
    }
    const struct timespec wait = {
            .tv_sec = (time_t)(remaining / NANOSECONDS_PER_SECOND),
            .tv_nsec = (long)(remaining % NANOSECONDS_PER_SECOND),
    };
    nanosleep(&wait, nullptr); // a stop signal cuts it short
}

static int clear_panel(void) {
    struct screen *screen = screen_open();
    const bool cleared = screen_clear(screen); // which says when there is no panel
    screen_close(screen);
    return cleared ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, char *argv[]) {
    /* the options, each of which ends the run on its own */
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
    if (clear) {
        return clear_panel();
    }

    /* Warsaw time, curl, the configuration, the fonts, the panel and the saved state */
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
    struct drawing *drawing = draw_open(config.font_dir);
    if (drawing == nullptr) {
        return EXIT_FAILURE;
    }
    struct screen *screen = screen_open();
    struct state state = {.weather = {.eaqi = -1}};
    if (record_restore(config.state_path, &state.wall, &state.weather)) {
        log_info("restored the saved state from %s", config.state_path);
    }
    if (!stop_on(SIGTERM) || !stop_on(SIGINT)) {
        return EXIT_FAILURE;
    }
    signal(SIGPIPE, SIG_IGN); // a dashboard that drops an upload mid-way must not end the program
    log_info("kotetsu %s: dashboard %s, state %s", KOTETSU_VERSION, config.dashboard_url,
             config.state_path);

    /* nothing is drawn until the clock is right; a system without timesyncd counts as synced */
    if (!time_clock_synced()) {
        log_info("waiting for the clock to synchronise before drawing");
        const struct timespec tick = {.tv_sec = TICK_SECONDS};
        while (!stop_requested && !time_clock_synced()) {
            nanosleep(&tick, nullptr);
        }
    }

    while (!stop_requested) {
        struct timespec started;
        clock_gettime(CLOCK_MONOTONIC, &started);
        const time_t now = time(nullptr);

        /* 1. collect: each network source in its own thread */
        const struct inputs inputs = collect(&config, &state, now);

        /* 2. start the tick line from what was collected, then draw one frame */
        struct tick_summary summary = {
                .dashboard = inputs.phrase.status == DASHBOARD_OK
                                     ? "dashboard ok"
                                     : dashboard_problem(inputs.phrase.status),
                .weather = inputs.weather,
                .upload = "no upload",
        };
        struct drawn drawn;
        if (draw_frame(drawing, &inputs, &state, &drawn)) {

            /* 3. output: the panel and the dashboard side by side, then the record */
            const struct outcome outcome = output(screen, &config, &state, &drawn);
            summary.drawn = true;
            summary.phrase_id = drawn.phrase_id;
            summary.too_long = drawn.too_long_id != 0;
            summary.missing_glyphs = drawn.missing_glyphs;
            summary.decision = outcome.decision;
            summary.refreshed = outcome.refreshed;
            summary.panel = outcome.panel;
            summary.upload = outcome.upload;
        }

        /* 4. one line for the whole tick, drawn or not */
        record_log_tick(&summary);

        sleep_until_next_tick(&started);
    }

    screen_close(screen);
    draw_close(drawing);
    curl_global_cleanup();
    return EXIT_SUCCESS;
}
