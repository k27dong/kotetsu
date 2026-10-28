#include "sources/dashboard.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>
#include <glib.h>

#include "calendar/clock.h"
#include "log.h"
#include "panel/refresh.h"
#include "render/icons.h"
#include "render/text.h"
#include "sources/http.h"

#define NOW_PATH    "/api/pi/now"
#define SCREEN_PATH "/api/pi/screen"

static constexpr double LARGEST_EXACT_INTEGER = 9'007'199'254'740'991.0;
static constexpr glong REPORT_ERROR_MAX_CHARACTERS = 1000;

static const cJSON *member(const cJSON *object, const char *name) {
    return cJSON_GetObjectItemCaseSensitive(object, name);
}

static bool read_id(const cJSON *item, int64_t *out) {
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    const double value = item->valuedouble;
    if (value < 1 || value > LARGEST_EXACT_INTEGER || (double)(int64_t)value != value) {
        return false;
    }
    *out = (int64_t)value;
    return true;
}

static bool read_text(const cJSON *item, const char *what, char *out, size_t size) {
    const char *text = cJSON_GetStringValue(item);
    if (text == nullptr) {
        log_error("the dashboard's %s isn't text", what);
        return false;
    }
    const size_t length = strlen(text);
    if (length == 0 || length >= size) {
        log_error("the dashboard's %s has %zu bytes, not 1 to %zu", what, length, size - 1);
        return false;
    }
    if (!text_is_valid_utf8(text, length)) {
        log_error("the dashboard's %s isn't UTF-8", what);
        return false;
    }
    memcpy(out, text, length + 1);
    return true;
}

static bool read_now(const cJSON *root, struct dashboard_now *now) {
    if (!cJSON_IsObject(root)) {
        log_error("the dashboard's answer isn't a JSON object");
        return false;
    }
    const cJSON *phrase = member(root, "phrase");
    if (!cJSON_IsObject(phrase)) {
        log_error("the dashboard's answer has no phrase");
        return false;
    }
    if (!read_id(member(phrase, "id"), &now->phrase_id)) {
        log_error("the dashboard's phrase id isn't a positive whole number");
        return false;
    }
    if (!read_text(member(phrase, "text"), "phrase text", now->phrase_text,
                   sizeof now->phrase_text)) {
        return false;
    }
    const char *until = cJSON_GetStringValue(member(phrase, "until"));
    if (until == nullptr || !clock_parse_iso8601(until, &now->until_s)) {
        log_error("phrase %" PRId64 " comes without a readable until time", now->phrase_id);
        return false;
    }
    const cJSON *greeting = member(root, "greeting");
    if (cJSON_IsObject(greeting)) {
        const char *icon = cJSON_GetStringValue(member(greeting, "icon"));
        if (icon == nullptr || !icons_small_from_name(icon, &now->greeting_icon)) {
            log_error("the dashboard's greeting icon isn't one of the six");
            return false;
        }
        if (!read_text(member(greeting, "text"), "greeting", now->greeting_text,
                       sizeof now->greeting_text)) {
            return false;
        }
        now->has_greeting = true;
    } else if (!cJSON_IsNull(greeting)) {
        log_error("the dashboard's greeting is neither an object nor null");
        return false;
    }
    const cJSON *full_refresh = member(root, "full_refresh");
    if (cJSON_IsNumber(full_refresh)) {
        if (!read_id(full_refresh, &now->full_refresh)) {
            log_error("the dashboard's full_refresh isn't a positive whole number");
            return false;
        }
    } else if (!cJSON_IsNull(full_refresh)) {
        log_error("the dashboard's full_refresh is neither a number nor null");
        return false;
    }
    return true;
}

enum dashboard_status dashboard_parse_now(const char *json, struct dashboard_now *out) {
    cJSON *root = cJSON_Parse(json);
    struct dashboard_now now = {};
    const bool parsed = read_now(root, &now);
    cJSON_Delete(root);
    if (!parsed) {
        return DASHBOARD_BAD_RESPONSE;
    }
    *out = now;
    return DASHBOARD_OK;
}

static gchar *iso_time(int64_t seconds) {
    GDateTime *when = g_date_time_new_from_unix_utc(seconds);
    if (when == nullptr) {
        log_error("%" PRId64 " s is outside the times GLib can write", seconds);
        abort();
    }
    gchar *text = g_date_time_format_iso8601(when);
    g_date_time_unref(when);
    return text;
}

static gchar *now_header(void) {
    return clock_is_overridden() ? iso_time(clock_now()) : nullptr;
}

static enum dashboard_status status_for(const char *url, const struct http_response *response) {
    cJSON *root = cJSON_Parse(response->body);
    const char *error = cJSON_GetStringValue(member(root, "error"));
    log_error("%s answered %ld%s%s", url, response->status, error != nullptr ? ": " : "",
              error != nullptr ? error : "");
    cJSON_Delete(root);
    if (response->status == 401 || response->status == 403) {
        return DASHBOARD_UNAUTHORIZED;
    }
    if (response->status >= 500) {
        return DASHBOARD_UNREACHABLE;
    }
    return DASHBOARD_BAD_RESPONSE;
}

enum dashboard_status dashboard_fetch_now(const char *base_url, const char *pi_key,
                                          struct dashboard_now *out) {
    gchar *url = g_strconcat(base_url, NOW_PATH, nullptr);
    gchar *header = now_header();
    struct http_response response;
    enum dashboard_status status = DASHBOARD_UNREACHABLE;
    if (http_get(url, pi_key, header, &response)) {
        status = response.status == 200 ? dashboard_parse_now(response.body, out)
                                        : status_for(url, &response);
        http_response_free(&response);
    }
    g_free(header);
    g_free(url);
    return status;
}

enum dashboard_status dashboard_upload(const char *base_url, const char *pi_key, const uint8_t *png,
                                       size_t png_length, const char *report_json) {
    const struct http_part parts[] = {
            {.name = "screen", .content_type = "image/png", .data = png, .length = png_length},
            {.name = "report",
             .content_type = "application/json",
             .data = (const uint8_t *)report_json,
             .length = strlen(report_json)},
    };
    gchar *url = g_strconcat(base_url, SCREEN_PATH, nullptr);
    gchar *header = now_header();
    struct http_response response;
    enum dashboard_status status = DASHBOARD_UNREACHABLE;
    if (http_put_multipart(url, pi_key, header, parts, G_N_ELEMENTS(parts), &response)) {
        status = response.status == 204 ? DASHBOARD_OK : status_for(url, &response);
        http_response_free(&response);
    }
    g_free(header);
    g_free(url);
    return status;
}

static void add_time(cJSON *object, const char *key, int64_t seconds) {
    gchar *text = iso_time(seconds);
    cJSON_AddStringToObject(object, key, text);
    g_free(text);
}

static void add_ids(cJSON *object, const char *key, int64_t id) {
    cJSON *ids = cJSON_AddArrayToObject(object, key);
    if (id != 0) {
        cJSON_AddItemToArray(ids, cJSON_CreateNumber((double)id));
    }
}

static void add_number_or_null(cJSON *object, const char *key, bool present, double value) {
    if (present) {
        cJSON_AddNumberToObject(object, key, value);
    } else {
        cJSON_AddNullToObject(object, key);
    }
}

bool dashboard_report_json(const struct dashboard_report *report, char *out, size_t size) {
    if (report->refresh == REFRESH_NONE) {
        log_error("a screen report needs the refresh that was done");
        return false;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "refresh", report->refresh == REFRESH_FULL ? "full" : "partial");
    add_time(root, "at", report->at_s);
    add_number_or_null(root, "phraseId", report->phrase_id != 0, (double)report->phrase_id);
    if (report->has_weather_at) {
        add_time(root, "weatherAt", report->weather_at_s);
    } else {
        cJSON_AddNullToObject(root, "weatherAt");
    }
    add_ids(root, "tooLong", report->too_long_id);
    add_ids(root, "missingGlyphs", report->missing_glyphs_id);
    add_number_or_null(root, "wifiSignal", report->has_wifi_signal, report->wifi_signal);
    add_number_or_null(root, "piTemperatureC", report->has_pi_temperature,
                       report->pi_temperature_c);
    if (report->last_error == nullptr) {
        cJSON_AddNullToObject(root, "lastError");
    } else if (g_utf8_strlen(report->last_error, -1) <= REPORT_ERROR_MAX_CHARACTERS) {
        cJSON_AddStringToObject(root, "lastError", report->last_error);
    } else {
        gchar *capped = g_strndup(
                report->last_error,
                (gsize)(g_utf8_offset_to_pointer(report->last_error, REPORT_ERROR_MAX_CHARACTERS) -
                        report->last_error));
        cJSON_AddStringToObject(root, "lastError", capped);
        g_free(capped);
    }
    const bool printed = cJSON_PrintPreallocated(root, out, (int)size, false);
    cJSON_Delete(root);
    if (!printed) {
        log_error("the screen report doesn't fit in %zu bytes", size);
    }
    return printed;
}
