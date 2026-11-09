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

#ifdef __linux__
#include <linux/nl80211.h>

#include <netlink/attr.h>
#include <netlink/errno.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>
#include <netlink/handlers.h>
#include <netlink/msg.h>
#include <netlink/netlink.h>
#include <netlink/socket.h>
#endif

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

void record_error(struct record_state *state, const char *text) {
    snprintf(state->last_error, sizeof state->last_error, "%s", text);
    keep_valid_utf8(state->last_error); // the cut can split a character
}

struct wifi_signal {
    bool has;
    double quality;
};

#ifdef __linux__
#define NL80211_FAMILY "nl80211"

static constexpr int WEAKEST_DBM = -110; // the kernel's 0–70 link quality is dBm + 110 in between
static constexpr int STRONGEST_DBM = -40;

struct access_point_signal {
    bool found;
    int dbm;
};

struct dump_request {
    uint8_t command;
    uint32_t ifindex; // 0 for every interface
    nl_recvmsg_msg_cb_t keep;
    void *kept;
};

static bool parse_attributes(struct nl_msg *message, struct nlattr **attributes) {
    const struct genlmsghdr *header = nlmsg_data(nlmsg_hdr(message));
    return nla_parse(attributes, NL80211_ATTR_MAX, genlmsg_attrdata(header, 0),
                     genlmsg_attrlen(header, 0), nullptr) == 0;
}

static int keep_first_station_interface(struct nl_msg *message, void *argument) {
    uint32_t *ifindex = argument;
    struct nlattr *attributes[NL80211_ATTR_MAX + 1];
    if (*ifindex == 0 && parse_attributes(message, attributes) &&
        attributes[NL80211_ATTR_IFINDEX] != nullptr && attributes[NL80211_ATTR_IFTYPE] != nullptr &&
        nla_get_u32(attributes[NL80211_ATTR_IFTYPE]) == NL80211_IFTYPE_STATION) {
        *ifindex = nla_get_u32(attributes[NL80211_ATTR_IFINDEX]);
    }
    return NL_OK;
}

static int keep_access_point_signal(struct nl_msg *message, void *argument) {
    struct access_point_signal *signal = argument;
    struct nlattr *attributes[NL80211_ATTR_MAX + 1];
    struct nlattr *station[NL80211_STA_INFO_MAX + 1];
    if (!signal->found && parse_attributes(message, attributes) &&
        attributes[NL80211_ATTR_STA_INFO] != nullptr &&
        nla_parse_nested(station, NL80211_STA_INFO_MAX, attributes[NL80211_ATTR_STA_INFO],
                         nullptr) == 0 &&
        station[NL80211_STA_INFO_SIGNAL] != nullptr) {
        signal->dbm = (int8_t)nla_get_u8(station[NL80211_STA_INFO_SIGNAL]);
        signal->found = true;
    }
    return NL_OK;
}

static bool dump(struct nl_sock *socket, int family, const struct dump_request *request) {
    /* build the request, for one interface or for all */
    struct nl_msg *message = nlmsg_alloc();
    if (message == nullptr || genlmsg_put(message, NL_AUTO_PORT, NL_AUTO_SEQ, family, 0, NLM_F_DUMP,
                                          request->command, 0) == nullptr) {
        log_error("can't build nl80211 command %d", request->command);
        nlmsg_free(message);
        return false;
    }
    int status = request->ifindex != 0
                         ? nla_put_u32(message, NL80211_ATTR_IFINDEX, request->ifindex)
                         : 0;

    /* send it, and hand each answer to the request's callback */
    if (status == 0) {
        status = nl_socket_modify_cb(socket, NL_CB_VALID, NL_CB_CUSTOM, request->keep,
                                     request->kept);
    }
    if (status == 0) {
        status = nl_send_auto(socket, message);
    }
    nlmsg_free(message);
    if (status >= 0) {
        status = nl_recvmsgs_default(socket);
    }
    if (status < 0) {
        log_error("nl80211 command %d failed: %s", request->command, nl_geterror(status));
        return false;
    }
    return true;
}

static struct wifi_signal read_wifi_signal(void) {
    /* reach nl80211 */
    struct nl_sock *socket = nl_socket_alloc();
    if (socket == nullptr) {
        log_error("can't allocate a netlink socket");
        return (struct wifi_signal){};
    }
    const int connected = genl_connect(socket);
    const int family = connected < 0 ? connected : genl_ctrl_resolve(socket, NL80211_FAMILY);
    if (family < 0 && family != -NLE_OBJ_NOTFOUND) { // not found: no Wi-Fi driver, as on a cable
        log_error("can't reach nl80211: %s", nl_geterror(family));
    }

    /* find the first station interface, then the signal of its access point */
    uint32_t ifindex = 0;
    struct access_point_signal signal = {};
    const struct dump_request interfaces = {
            .command = NL80211_CMD_GET_INTERFACE,
            .keep = keep_first_station_interface,
            .kept = &ifindex,
    };
    bool read = family >= 0 && dump(socket, family, &interfaces) && ifindex != 0;
    if (read) {
        const struct dump_request stations = {
                .command = NL80211_CMD_GET_STATION,
                .ifindex = ifindex,
                .keep = keep_access_point_signal,
                .kept = &signal,
        };
        read = dump(socket, family, &stations) && signal.found;
    }
    nl_socket_free(socket);
    if (!read) {
        return (struct wifi_signal){};
    }

    /* put the signal on the kernel's 0–70 scale */
    const int dbm = signal.dbm < WEAKEST_DBM     ? WEAKEST_DBM
                    : signal.dbm > STRONGEST_DBM ? STRONGEST_DBM
                                                 : signal.dbm;
    return (struct wifi_signal){.has = true, .quality = dbm - WEAKEST_DBM};
}
#else
static struct wifi_signal read_wifi_signal(void) {
    return (struct wifi_signal){}; // only Linux has nl80211
}
#endif

struct readings record_read_device(void) {
    /* the Wi-Fi link's quality, when there is a link */
    const struct wifi_signal wifi = read_wifi_signal();
    struct readings readings = {.has_wifi_signal = wifi.has, .wifi_signal = wifi.quality};

    /* the SoC's temperature, which the kernel gives in millidegrees */
    char text[TEMPERATURE_TEXT_BYTES];
    size_t length = 0;
    if (!file_read(TEMPERATURE_PATH, text, sizeof text - 1, &length)) {
        if (errno != ENOENT) { // no thermal zone, as on the Mac
            log_error("can't read %s: %s", TEMPERATURE_PATH, strerror(errno));
        }
        return readings;
    }
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
    /* a report goes with a refresh, done now or before */
    if (report->refresh == REFRESH_NONE) {
        log_error("a screen report needs the refresh that was done");
        return false;
    }

    /* the refresh, and what it showed */
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

    /* how the Pi is doing */
    add_number_or_null(root, "wifiSignal", readings->has_wifi_signal, readings->wifi_signal);
    add_number_or_null(root, "piTemperatureC", readings->has_pi_temperature,
                       readings->pi_temperature_c);

    /* the last error, cut to the characters the dashboard takes */
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

bool record_should_upload(const struct record_state *state, bool refreshed, int64_t too_long_id,
                          bool glass_known) {
    // the dashboard takes a too-long phrase off the wall once told; the picture is the glass's
    return refreshed ||
           (too_long_id != 0 && too_long_id != state->reported_too_long_id && glass_known);
}

bool record_upload(struct record_state *state, const struct config *config,
                   const struct frame *frame, const struct report *report) {
    /* the report, with how the Pi is doing, and the picture */
    const struct readings readings = record_read_device();
    char json[REPORT_JSON_BYTES];
    if (!record_report_json(report, &readings, json, sizeof json)) {
        return false;
    }
    const struct png png = frame_encode_png(frame);
    if (!png.ok) {
        record_error(state, "PNG encoding failed");
        return false;
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

    /* any answer but a stored screen names the problem */
    const bool stored = response.ok && response.status == HTTP_NO_CONTENT;
    enum dashboard_status problem = DASHBOARD_UNREACHABLE;
    if (response.ok && !stored) {
        problem = dashboard_log_answer(url, &response);
    }
    http_response_free(&response);

    /* a stored screen clears the kept error; a failure becomes it */
    if (stored) {
        state->last_error[0] = '\0';
        state->reported_too_long_id = report->too_long_id;
    } else {
        char error[sizeof state->last_error];
        snprintf(error, sizeof error, "upload failed: %s", dashboard_problem(problem));
        record_error(state, error);
    }
    return stored;
}

void record_log_tick(const struct tick_summary *tick) {
    /* a tick that drew nothing has nothing more to tell */
    if (!tick->drawn) {
        log_info("%s; %s; nothing drawn; no refresh; no upload", tick->dashboard, tick->weather);
        return;
    }

    /* the phrase drawn */
    char phrase[TICK_PART_BYTES] = "no phrase";
    if (tick->phrase_id != 0) {
        snprintf(phrase, sizeof phrase, "phrase %" PRId64, tick->phrase_id);
    }

    /* the refresh decided, and how it went */
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

    /* one line for the whole tick */
    log_info("%s; %s; %s%s, %d missing glyph%s; %s; %s", tick->dashboard, tick->weather, phrase,
             tick->too_long ? " instead of a too-long one" : "", tick->missing_glyphs,
             tick->missing_glyphs == 1 ? "" : "s", refresh, tick->upload);
}
