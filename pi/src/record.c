/* the record: the screen report and its upload, and the tick's journal line */
#include "record.h"

#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cjson/cJSON.h>
#include <glib.h>

#include "config.h"
#include "draw/frame.h"
#include "screen/screen.h"
#include "shared/dashboard.h"
#include "shared/file.h"
#include "shared/http.h"
#include "shared/log.h"
#include "shared/time.h"
#include "wifi.h"

#define SCREEN_PATH      "/api/pi/screen"
#define TEMPERATURE_PATH "/sys/class/thermal/thermal_zone0/temp"

static constexpr long HTTP_NO_CONTENT = 204; // the dashboard's answer once it has stored a screen
static constexpr size_t REPORT_JSON_BYTES = 8'192; // fits a last error of 1000 escaped characters
static constexpr glong REPORT_ERROR_MAX_CHARACTERS = 1'000; // the most the dashboard takes
static constexpr size_t TEMPERATURE_TEXT_BYTES = 32;
static constexpr double MILLIDEGREES_PER_DEGREE = 1'000.0;
static constexpr size_t TICK_PART_BYTES = 64;

static void keep_valid_utf8(char *text) {
    const char *end = nullptr;
    if (!g_utf8_validate(text, -1, &end)) {
        text[end - text] = '\0';
    }
}

struct record_state record_keep_error(struct record_state record, const char *text) {
    snprintf(record.last_error, sizeof record.last_error, "%s", text);
    keep_valid_utf8(record.last_error); // the cut can split a character
    return record;
}

struct readings record_read_device(void) {
    /* read the Wi-Fi signal */
    const struct wifi_signal wifi = wifi_read_signal();
    struct readings readings = {.has_wifi_signal = wifi.ok, .wifi_signal = wifi.quality};

    /* read the SoC temperature */
    char text[TEMPERATURE_TEXT_BYTES];
    size_t length = 0;
    if (!file_read(TEMPERATURE_PATH, text, sizeof text - 1, &length)) {
        if (errno != ENOENT) { // no thermal zone, as on the Mac
            log_error("can't read %s: %s", TEMPERATURE_PATH, strerror(errno));
        }
        return readings;
    }

    /* convert it to degrees */
    text[length] = '\0';
    char *end = nullptr;
    const long millidegrees = strtol(text, &end, 10);
    if (end != text) {
        readings.has_pi_temperature = true;
        readings.pi_temperature_c = (double)millidegrees / MILLIDEGREES_PER_DEGREE;
    }
    return readings;
}

static void add_time(cJSON *object, const char *key, time_t moment) {
    char text[sizeof "2026-10-26T11:00:00Z"];
    if (!time_format_iso8601(moment, text, sizeof text)) {
        log_error("%lld s is outside the times this system can write", (long long)moment);
        abort();
    }
    cJSON_AddStringToObject(object, key, text);
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

bool record_report_json(const struct report *report, const struct readings *readings, char *out,
                        size_t size) {
    /* check there was a refresh */
    if (report->refresh == REFRESH_NONE) {
        log_error("a screen report needs the refresh that was done");
        return false;
    }

    /* add what was refreshed */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "refresh", report->refresh == REFRESH_FULL ? "full" : "partial");
    add_time(root, "at", report->at);
    add_number_or_null(root, "phraseId", report->phrase_id != 0, (double)report->phrase_id);
    if (report->weather_at != 0) {
        add_time(root, "weatherAt", report->weather_at);
    } else {
        cJSON_AddNullToObject(root, "weatherAt");
    }
    add_ids(root, "tooLong", report->too_long_id);
    add_ids(root, "missingGlyphs", report->missing_glyphs_id);

    /* add the readings */
    add_number_or_null(root, "wifiSignal", readings->has_wifi_signal, readings->wifi_signal);
    add_number_or_null(root, "piTemperatureC", readings->has_pi_temperature,
                       readings->pi_temperature_c);

    /* add the last error */
    if (report->last_error == nullptr) {
        cJSON_AddNullToObject(root, "lastError");
    } else {
        cJSON *error = cJSON_AddStringToObject(root, "lastError", report->last_error);
        if (error != nullptr) {
            keep_valid_utf8(error->valuestring); // counting a broken character runs past the end
            if (g_utf8_strlen(error->valuestring, -1) > REPORT_ERROR_MAX_CHARACTERS) {
                *g_utf8_offset_to_pointer(error->valuestring, REPORT_ERROR_MAX_CHARACTERS) = '\0';
            }
        }
    }

    /* write it out */
    const bool printed = cJSON_PrintPreallocated(root, out, (int)size, false);
    cJSON_Delete(root);
    if (!printed) {
        log_error("the screen report doesn't fit in %zu bytes", size);
    }
    return printed;
}

bool record_should_upload(const struct record_state *state, bool refresh_decided,
                          int64_t too_long_id, bool glass_known) {
    const bool new_too_long = too_long_id != 0 && too_long_id != state->reported_too_long_id;
    return refresh_decided || (new_too_long && glass_known); // so the dashboard can drop it
}

struct upload_result record_upload(const struct config *config, const struct frame *frame,
                                   const struct report *report) {
    /* build the report */
    const struct readings readings = record_read_device();
    char json[REPORT_JSON_BYTES];
    if (!record_report_json(report, &readings, json, sizeof json)) {
        return (struct upload_result){};
    }

    /* encode the picture */
    const struct png png = frame_encode_png(frame);
    if (!png.ok) {
        return (struct upload_result){.error = "PNG encoding failed"};
    }

    /* put both on the dashboard */
    char url[sizeof config->dashboard_url + sizeof SCREEN_PATH];
    snprintf(url, sizeof url, "%s" SCREEN_PATH, config->dashboard_url);
    const struct http_part parts[] = {
            {.name = "screen", .content_type = "image/png", .data = png.data, .length = png.length},
            {.name = "report",
             .content_type = "application/json",
             .data = (const uint8_t *)json,
             .length = strlen(json)},
    };
    struct http_response response =
            http_put_multipart(url, config->pi_key, parts, sizeof parts / sizeof *parts);
    free(png.data);

    /* name any problem */
    const bool stored = response.ok && response.status == HTTP_NO_CONTENT;
    enum dashboard_status problem = DASHBOARD_UNREACHABLE;
    if (response.ok && !stored) {
        problem = dashboard_log_answer(url, &response);
    }
    http_response_free(&response);

    /* say how it went */
    struct upload_result result = {.ok = stored};
    if (!stored) {
        snprintf(result.error, sizeof result.error, "upload failed: %s",
                 dashboard_problem(problem));
    }
    return result;
}

struct record_state record_after_upload(struct record_state record,
                                        const struct upload_result *upload, int64_t too_long_id) {
    if (upload->ok) {
        record.last_error[0] = '\0';
        record.reported_too_long_id = too_long_id;
    } else if (upload->error[0] != '\0') { // empty when the report couldn't be built
        record = record_keep_error(record, upload->error);
    }
    return record;
}

void record_log_tick(const struct tick_summary *tick) {
    /* say nothing was drawn */
    if (!tick->drawn) {
        log_info("%s; %s; nothing drawn; no refresh; no upload", tick->dashboard, tick->weather);
        return;
    }

    /* name the phrase */
    char phrase[TICK_PART_BYTES] = "no phrase";
    if (tick->phrase_id != 0) {
        snprintf(phrase, sizeof phrase, "phrase %" PRId64, tick->phrase_id);
    }

    /* describe the refresh */
    char refresh[TICK_PART_BYTES];
    const char *reason = screen_reason_text(tick->decision.reason);
    if (tick->decision.kind == REFRESH_NONE) {
        snprintf(refresh, sizeof refresh, "no refresh (%s)", reason);
    } else {
        snprintf(refresh, sizeof refresh, "%s refresh (%s) %s",
                 tick->decision.kind == REFRESH_FULL ? "full" : "partial", reason,
                 !tick->refreshed ? "failed"
                 : tick->panel    ? "done"
                                  : "done (no panel)");
    }

    /* log the whole tick */
    log_info("%s; %s; %s%s, %d missing glyph%s; %s; %s", tick->dashboard, tick->weather, phrase,
             tick->too_long ? " instead of a too-long one" : "", tick->missing_glyphs,
             tick->missing_glyphs == 1 ? "" : "s", refresh, tick->upload);
}
