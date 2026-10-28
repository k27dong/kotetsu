#include "app.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>
#include <glib-object.h>
#include <glib.h>
#include <pango/pango.h>

#include "calendar/calendar.h"
#include "calendar/clock.h"
#include "config.h"
#include "log.h"
#include "model.h"
#include "panel/refresh.h"
#include "panel/state.h"
#include "render/fonts.h"
#include "render/frame.h"
#include "render/screen.h"
#include "render/text.h"
#include "sources/dashboard.h"
#include "sources/weather.h"
#include "sources/wifi.h"

#define WIRELESS_PATH    "/proc/net/wireless"
#define TEMPERATURE_PATH "/sys/class/thermal/thermal_zone0/temp"

static constexpr int CLEANUP_MINUTE_OF_DAY = 10 * 60;
static constexpr double LARGEST_EXACT_INTEGER = 9'007'199'254'740'991.0;
static constexpr size_t LAST_ERROR_BYTES = 128;
static constexpr size_t REPORT_JSON_BYTES = 1024;

struct phrase {
    int64_t id; // 0 when there's none
    char text[PHRASE_TEXT_MAX_BYTES];
};

struct greeting {
    bool has;
    enum small_icon icon;
    char text[GREETING_TEXT_BYTES];
};

struct glass {
    struct phrase phrase;
    struct civil_date date;
    bool wifi_setup;
    enum refresh_kind last_refresh;
};

struct app {
    struct app_ports ports;
    char *state_path;
    PangoFontMap *fonts;
    struct saved_state state;
    struct glass glass;
    struct phrase wanted;
    struct greeting greeting;
    bool answered;
    struct civil_date answered_on;
    int64_t full_refresh_asked;
    int64_t full_refresh_done;
    struct weather_inputs weather;
    int64_t next_weather_s;
    enum wifi_state wifi;
    bool ticked;
    struct civil_date last_tick_date;
    int last_tick_minute;
    bool cleanup_due;
    bool waiting_for_greeting;
    int64_t waiting_since_s;
    int64_t reported_too_long_id;
    bool clock_wait_logged;
    char last_error[LAST_ERROR_BYTES];
};

static const struct phrase NO_PHRASE = {};

static const char *const WIFI_STATE_NAMES[] = {
        [WIFI_STATE_UNKNOWN] = "unknown",
        [WIFI_STATE_HOTSPOT] = "hotspot",
        [WIFI_STATE_CONNECTING] = "connecting",
        [WIFI_STATE_CONNECTED] = "connected",
};

[[gnu::format(printf, 2, 3)]] static void note_error(struct app *app, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(app->last_error, sizeof app->last_error, format, arguments);
    va_end(arguments);
}

static const char *status_name(enum dashboard_status status) {
    switch (status) {
    case DASHBOARD_OK:
        return "ok";
    case DASHBOARD_UNREACHABLE:
        return "unreachable";
    case DASHBOARD_UNAUTHORIZED:
        return "unauthorized";
    case DASHBOARD_BAD_RESPONSE:
        return "bad response";
    }
    return "unknown";
}

static bool same_date(struct civil_date a, struct civil_date b) {
    return a.year == b.year && a.month == b.month && a.day == b.day;
}

static bool usable_text(const char *text, size_t size) {
    const size_t length = strnlen(text, size);
    return length > 0 && length < size && text_is_valid_utf8(text, length);
}

static const cJSON *member(const cJSON *object, const char *name) {
    return cJSON_GetObjectItemCaseSensitive(object, name);
}

static bool read_integer(const cJSON *item, int64_t *out) {
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    const double value = item->valuedouble;
    if (value < -LARGEST_EXACT_INTEGER || value > LARGEST_EXACT_INTEGER ||
        (double)(int64_t)value != value) {
        return false;
    }
    *out = (int64_t)value;
    return true;
}

static void restore(struct app *app) {
    cJSON *root = cJSON_Parse(app->state.app_json);
    if (!cJSON_IsObject(root)) {
        log_warning("the saved app state isn't a JSON object; starting without it");
        cJSON_Delete(root);
        return;
    }
    int64_t id = 0;
    const char *text = cJSON_GetStringValue(member(root, "phraseText"));
    if (member(root, "phraseId") != nullptr) {
        if (read_integer(member(root, "phraseId"), &id) && id > 0 && text != nullptr &&
            usable_text(text, PHRASE_TEXT_MAX_BYTES)) {
            app->glass.phrase.id = id;
            g_strlcpy(app->glass.phrase.text, text, sizeof app->glass.phrase.text);
            app->wanted = app->glass.phrase;
        } else {
            log_warning("the saved phrase is unusable; starting without one");
        }
    }
    int64_t full_refresh = 0;
    if (read_integer(member(root, "fullRefresh"), &full_refresh)) {
        app->full_refresh_done = full_refresh;
    }
    const cJSON *weather = member(root, "weather");
    int64_t fetched_s = 0;
    int64_t now_c = 0;
    int64_t low_c = 0;
    int64_t high_c = 0;
    int64_t code = 0;
    if (cJSON_IsObject(weather) && read_integer(member(weather, "fetchedAt"), &fetched_s) &&
        read_integer(member(weather, "nowC"), &now_c) &&
        read_integer(member(weather, "lowC"), &low_c) &&
        read_integer(member(weather, "highC"), &high_c) &&
        read_integer(member(weather, "code"), &code)) {
        app->weather.have_report = true;
        app->weather.report = (struct weather_report){
                .fetched_s = fetched_s,
                .now_c = (int)now_c,
                .low_c = (int)low_c,
                .high_c = (int)high_c,
                .code = (int)code,
        };
    }
    cJSON_Delete(root);
}

static void save_state(struct app *app) {
    cJSON *root = cJSON_CreateObject();
    if (app->glass.phrase.id != 0) {
        cJSON_AddNumberToObject(root, "phraseId", (double)app->glass.phrase.id);
        cJSON_AddStringToObject(root, "phraseText", app->glass.phrase.text);
    }
    if (app->full_refresh_done != 0) {
        cJSON_AddNumberToObject(root, "fullRefresh", (double)app->full_refresh_done);
    }
    if (app->weather.have_report) {
        const struct weather_report *report = &app->weather.report;
        cJSON *weather = cJSON_AddObjectToObject(root, "weather");
        cJSON_AddNumberToObject(weather, "fetchedAt", (double)report->fetched_s);
        cJSON_AddNumberToObject(weather, "nowC", report->now_c);
        cJSON_AddNumberToObject(weather, "lowC", report->low_c);
        cJSON_AddNumberToObject(weather, "highC", report->high_c);
        cJSON_AddNumberToObject(weather, "code", report->code);
    }
    if (!cJSON_PrintPreallocated(root, app->state.app_json, (int)sizeof app->state.app_json,
                                 false)) {
        log_error("the app state doesn't fit the state file; saving without it");
        g_strlcpy(app->state.app_json, "{}", sizeof app->state.app_json);
    }
    cJSON_Delete(root);
    if (!state_save(app->state_path, &app->state)) {
        note_error(app, "state save failed");
    }
}

struct app *app_create(const struct app_config *config, const struct app_ports *ports) {
    PangoFontMap *fonts = fonts_create_map(config->font_dir);
    if (fonts == nullptr) {
        return nullptr;
    }
    struct app *app = g_new0(struct app, 1);
    app->ports = *ports;
    app->state_path = g_strdup(config->state_path);
    app->fonts = fonts;
    if (state_load(app->state_path, &app->state)) {
        restore(app);
    }
    app->state.history.glass_known = false; // the panel's memory after a boot is unknown
    return app;
}

void app_destroy(struct app *app) {
    if (app == nullptr) {
        return;
    }
    g_object_unref(app->fonts);
    g_free(app->state_path);
    g_free(app);
}

static void poll_dashboard(struct app *app, int64_t now_s) {
    struct dashboard_now answer = {};
    const enum dashboard_status status = app->ports.fetch_now(app->ports.context, &answer);
    if (status != DASHBOARD_OK) {
        note_error(app, "dashboard %s", status_name(status));
        return;
    }
    if (answer.phrase_id <= 0 || !usable_text(answer.phrase_text, sizeof answer.phrase_text) ||
        (answer.has_greeting && !usable_text(answer.greeting_text, sizeof answer.greeting_text))) {
        log_error("phrase %" PRId64 " from the dashboard isn't usable text", answer.phrase_id);
        note_error(app, "dashboard sent unusable text");
        return;
    }
    app->wanted.id = answer.phrase_id;
    memcpy(app->wanted.text, answer.phrase_text, sizeof app->wanted.text);
    app->greeting.has = answer.has_greeting;
    app->greeting.icon = answer.greeting_icon;
    memcpy(app->greeting.text, answer.greeting_text, sizeof app->greeting.text);
    app->full_refresh_asked = answer.full_refresh;
    app->answered = true;
    app->answered_on = clock_warsaw_date(now_s);
}

static void fetch_weather(struct app *app, int64_t now_s) {
    app->next_weather_s = now_s + WEATHER_PERIOD_SECONDS;
    struct weather_inputs fetched = {};
    if (app->ports.fetch_weather(app->ports.context, now_s, &fetched)) {
        app->weather = fetched;
    } else {
        note_error(app, "weather fetch failed");
    }
}

static void track_time(struct app *app, int64_t now_s, struct civil_date today) {
    const int minute = clock_warsaw_minute_of_day(now_s);
    if (!app->ticked) {
        app->ticked = true;
        if (app->state.history.last_refresh_s > now_s) {
            app->state.history.last_refresh_s = now_s; // the clock went back while the Pi was off
        }
    } else if (same_date(app->last_tick_date, today) &&
               app->last_tick_minute < CLEANUP_MINUTE_OF_DAY && minute >= CLEANUP_MINUTE_OF_DAY) {
        app->cleanup_due = true;
    }
    app->last_tick_date = today;
    app->last_tick_minute = minute;
}

static bool wifi_setup_screen(struct app *app) {
    const enum wifi_state state = app->ports.read_wifi(app->ports.context);
    if (state != app->wifi) {
        log_info("wi-fi %s", WIFI_STATE_NAMES[state]);
        app->wifi = state;
    }
    // a connection attempt made from the hotspot keeps the setup screen until it succeeds
    return state == WIFI_STATE_HOTSPOT || (state == WIFI_STATE_CONNECTING && app->glass.wifi_setup);
}

static bool compose(struct app *app, struct civil_date today, bool wifi_setup,
                    struct screen_model *model) {
    *model = (struct screen_model){
            .wifi_setup = wifi_setup,
            .day_of_month = today.day,
            .month_name = calendar_month_name(today.month),
            .weekday_name = calendar_weekday_name(calendar_weekday(today)),
            .weather = weather_view_for(&app->weather),
            .counter_days = calendar_counter_days(today),
    };
    if (app->answered && same_date(app->answered_on, today) && app->greeting.has) {
        model->has_greeting = true;
        model->greeting_icon = app->greeting.icon;
        model->greeting = app->greeting.text;
    }
    return calendar_date_line(today, &model->date_line);
}

static bool render(struct app *app, struct screen_model *model, const struct phrase *phrase,
                   struct frame *out, struct render_report *report) {
    model->phrase = phrase->id != 0 ? phrase->text : nullptr;
    return screen_render(app->fonts, model, out, report);
}

// the phrase drawn, or nullptr when rendering failed; *too_long_id names a wanted phrase that
// had to give way to the one on the glass
static const struct phrase *render_fitting(struct app *app, struct screen_model *model,
                                           struct frame *out, struct render_report *report,
                                           int64_t *too_long_id) {
    *too_long_id = 0;
    const struct phrase *drawn = &app->wanted;
    if (!render(app, model, drawn, out, report)) {
        return nullptr;
    }
    if (model->wifi_setup || drawn->id == 0 || report->phrase.fits) {
        return drawn;
    }
    *too_long_id = drawn->id;
    log_warning("phrase %" PRId64 " doesn't fit the screen; the glass keeps its phrase", drawn->id);
    drawn = app->glass.phrase.id != app->wanted.id ? &app->glass.phrase : &NO_PHRASE;
    if (!render(app, model, drawn, out, report)) {
        return nullptr;
    }
    if (drawn->id == 0 || report->phrase.fits) {
        return drawn;
    }
    log_error("phrase %" PRId64 " on the glass no longer fits the screen", drawn->id);
    drawn = &NO_PHRASE;
    return render(app, model, drawn, out, report) ? drawn : nullptr;
}

static enum refresh_reason reason_for(const struct app *app, const struct screen_model *model,
                                      const struct phrase *drawn, struct civil_date today) {
    if (!app->state.history.glass_known) {
        return REFRESH_REASON_CHANGE; // every reason is a full refresh on an unknown glass
    }
    if (model->wifi_setup != app->glass.wifi_setup) {
        return REFRESH_REASON_SCREEN_SWITCH;
    }
    if (app->full_refresh_asked != 0 && app->full_refresh_asked != app->full_refresh_done) {
        return REFRESH_REASON_FORCED;
    }
    if (!model->wifi_setup) {
        if (drawn->id != app->glass.phrase.id || strcmp(drawn->text, app->glass.phrase.text) != 0) {
            return REFRESH_REASON_NEW_PHRASE;
        }
        if (!same_date(today, app->glass.date)) {
            return REFRESH_REASON_NEW_DAY;
        }
    }
    if (app->cleanup_due) {
        return REFRESH_REASON_CLEANUP;
    }
    return REFRESH_REASON_CHANGE;
}

static bool holding_for_greeting(struct app *app, int64_t now_s, struct civil_date today) {
    if (app->answered && same_date(app->answered_on, today)) {
        return false;
    }
    if (!app->waiting_for_greeting) {
        app->waiting_for_greeting = true;
        app->waiting_since_s = now_s;
    }
    return now_s - app->waiting_since_s < MIDNIGHT_GREETING_WAIT_SECONDS;
}

static bool show(struct app *app, int64_t now_s, const struct frame *next, enum refresh_kind kind,
                 const struct screen_model *model, const struct phrase *drawn,
                 struct civil_date today) {
    if (!app->ports.show(app->ports.context, &app->state.on_glass, next, kind)) {
        note_error(app, "panel refresh failed");
        app->state.history.glass_known = false; // the glass and the panel's memory are unknown now
        return false;
    }
    refresh_record(&app->state.history, kind, now_s);
    app->state.on_glass = *next;
    app->glass.last_refresh = kind;
    app->glass.date = today;
    app->glass.wifi_setup = model->wifi_setup;
    if (!model->wifi_setup && drawn != &app->glass.phrase) {
        app->glass.phrase = *drawn;
    }
    if (kind == REFRESH_FULL) {
        app->cleanup_due = false;
        if (app->answered) {
            app->full_refresh_done = app->full_refresh_asked;
        }
    }
    app->waiting_for_greeting = false;
    save_state(app);
    return true;
}

static bool read_temperature(double *out) {
    gchar *text = nullptr;
    GError *error = nullptr;
    if (!g_file_get_contents(TEMPERATURE_PATH, &text, nullptr, &error)) {
        if (!g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            log_error("can't read %s: %s", TEMPERATURE_PATH, error->message);
        }
        g_error_free(error);
        return false;
    }
    char *end = nullptr;
    const long millidegrees = strtol(text, &end, 10);
    const bool parsed = end != text;
    g_free(text);
    if (parsed) {
        *out = (double)millidegrees / 1000.0;
    }
    return parsed;
}

static void upload_report(struct app *app, const struct frame *frame, int64_t too_long_id,
                          int64_t missing_glyphs_id) {
    uint8_t *png = nullptr;
    size_t length = 0;
    if (!frame_encode_png(frame, &png, &length)) {
        note_error(app, "PNG encoding failed");
        return;
    }
    struct dashboard_report report = {
            .refresh = app->glass.last_refresh,
            .at_s = app->state.history.last_refresh_s,
            .phrase_id = app->glass.wifi_setup ? 0 : app->glass.phrase.id,
            .has_weather_at = app->weather.have_report,
            .weather_at_s = app->weather.report.fetched_s,
            .too_long_id = too_long_id,
            .missing_glyphs_id = missing_glyphs_id,
            .last_error = app->last_error[0] != '\0' ? app->last_error : nullptr,
    };
    report.has_wifi_signal = wifi_read_signal(WIRELESS_PATH, &report.wifi_signal);
    report.has_pi_temperature = read_temperature(&report.pi_temperature_c);
    char json[REPORT_JSON_BYTES];
    if (dashboard_report_json(&report, json, sizeof json)) {
        const enum dashboard_status status =
                app->ports.upload(app->ports.context, png, length, json);
        if (status == DASHBOARD_OK) {
            app->reported_too_long_id = too_long_id;
        } else {
            note_error(app, "upload %s", status_name(status));
        }
    }
    free(png);
}

void app_tick(struct app *app, int64_t now_s) {
    if (!clock_is_synced()) {
        if (!app->clock_wait_logged) {
            log_info("waiting for the clock to synchronise before drawing");
            app->clock_wait_logged = true;
        }
        return;
    }
    const struct civil_date today = clock_warsaw_date(now_s);
    track_time(app, now_s, today);
    poll_dashboard(app, now_s);
    if (now_s >= app->next_weather_s) {
        fetch_weather(app, now_s);
    }
    app->weather.now_s = now_s;
    struct screen_model model;
    if (!compose(app, today, wifi_setup_screen(app), &model)) {
        note_error(app, "no date line for today");
        return;
    }
    struct frame next;
    struct render_report report;
    int64_t too_long_id = 0;
    const struct phrase *drawn = render_fitting(app, &model, &next, &report, &too_long_id);
    if (drawn == nullptr) {
        note_error(app, "render failed");
        return;
    }
    const int64_t missing_glyphs_id =
            !model.wifi_setup && drawn->id != 0 && report.phrase.unknown_glyphs > 0 ? drawn->id : 0;
    const enum refresh_reason reason = reason_for(app, &model, drawn, today);
    bool refreshed = false;
    if (reason != REFRESH_REASON_NEW_DAY || !holding_for_greeting(app, now_s, today)) {
        const struct refresh_decision decision = refresh_decide(
                &app->state.history, frame_equal(&app->state.on_glass, &next), reason, now_s);
        if (reason == REFRESH_REASON_CLEANUP && decision.kind == REFRESH_NONE) {
            app->cleanup_due = false;
        }
        if (decision.kind != REFRESH_NONE && now_s >= decision.not_before_s) {
            refreshed = show(app, now_s, &next, decision.kind, &model, drawn, today);
        }
    }
    if (refreshed) {
        if (missing_glyphs_id != 0) {
            log_warning("phrase %" PRId64 " has %d characters the font lacks", missing_glyphs_id,
                        report.phrase.unknown_glyphs);
        }
        upload_report(app, &next, too_long_id, missing_glyphs_id);
    } else if (too_long_id != 0 && too_long_id != app->reported_too_long_id &&
               app->state.history.glass_known) {
        upload_report(app, &app->state.on_glass, too_long_id, missing_glyphs_id);
    }
}
