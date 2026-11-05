#include "sources/wifi.h"

#include <stddef.h>
#include <string.h>

#include <glib.h>

#include "log.h"

#ifdef __linux__
#include <linux/nl80211.h>
#include <stdint.h>

#include <netlink/attr.h>
#include <netlink/errno.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>
#include <netlink/handlers.h>
#include <netlink/msg.h>
#include <netlink/netlink.h>
#include <netlink/socket.h>
#endif

static const char *const STATE_WORDS[] = {
        [WIFI_STATE_HOTSPOT] = "HOTSPOT",
        [WIFI_STATE_CONNECTING] = "CONNECTING",
        [WIFI_STATE_CONNECTED] = "CONNECTED",
};

static gchar *last_garbled; // the content last warned about, read again every 15 s

static bool read_file(const char *path, gchar **contents) {
    GError *error = nullptr;
    if (g_file_get_contents(path, contents, nullptr, &error)) {
        return true;
    }
    if (!g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
        log_error("can't read %s: %s", path, error->message);
    }
    g_error_free(error);
    return false;
}

enum wifi_state wifi_read_state(const char *path) {
    gchar *contents = nullptr;
    if (!read_file(path, &contents)) {
        return WIFI_STATE_UNKNOWN;
    }
    const char *word = g_strstrip(contents);
    enum wifi_state state = WIFI_STATE_UNKNOWN;
    for (size_t i = 0; i < G_N_ELEMENTS(STATE_WORDS); i++) {
        if (STATE_WORDS[i] != nullptr && strcmp(word, STATE_WORDS[i]) == 0) {
            state = (enum wifi_state)i;
        }
    }
    if (state != WIFI_STATE_UNKNOWN) {
        g_free(last_garbled);
        last_garbled = nullptr;
    } else if (g_strcmp0(word, last_garbled) != 0) {
        log_warning("%s holds \"%s\", not a Comitup state", path, word);
        g_free(last_garbled);
        last_garbled = g_strdup(word);
    }
    g_free(contents);
    return state;
}

const char *wifi_state_name(enum wifi_state state) {
    const bool named = (size_t)state < G_N_ELEMENTS(STATE_WORDS) && STATE_WORDS[state] != nullptr;
    return named ? STATE_WORDS[state] : "UNKNOWN";
}

#ifdef __linux__
#define NL80211_FAMILY "nl80211"

static constexpr int WEAKEST_DBM = -110; // the kernel's 0–70 link quality is dBm + 110 in between
static constexpr int STRONGEST_DBM = -40;

struct access_point_signal {
    bool found;
    int dbm;
};

static int link_quality(int dbm) {
    if (dbm < WEAKEST_DBM) {
        return 0;
    }
    if (dbm > STRONGEST_DBM) {
        return STRONGEST_DBM - WEAKEST_DBM;
    }
    return dbm - WEAKEST_DBM;
}

static bool parse_attributes(struct nl_msg *message, struct nlattr **attributes) {
    struct genlmsghdr *header = nlmsg_data(nlmsg_hdr(message));
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

static bool dump(struct nl_sock *socket, int family, uint8_t command, uint32_t ifindex,
                 nl_recvmsg_msg_cb_t keep, void *argument) {
    struct nl_msg *message = nlmsg_alloc();
    if (message == nullptr || genlmsg_put(message, NL_AUTO_PORT, NL_AUTO_SEQ, family, 0, NLM_F_DUMP,
                                          command, 0) == nullptr) {
        log_error("can't build nl80211 command %d", command);
        nlmsg_free(message);
        return false;
    }
    int status = ifindex != 0 ? nla_put_u32(message, NL80211_ATTR_IFINDEX, ifindex) : 0;
    if (status == 0) {
        status = nl_socket_modify_cb(socket, NL_CB_VALID, NL_CB_CUSTOM, keep, argument);
    }
    if (status == 0) {
        status = nl_send_auto(socket, message);
    }
    nlmsg_free(message);
    if (status >= 0) {
        status = nl_recvmsgs_default(socket);
    }
    if (status < 0) {
        log_error("nl80211 command %d failed: %s", command, nl_geterror(status));
        return false;
    }
    return true;
}

bool wifi_read_signal(double *out) {
    struct nl_sock *socket = nl_socket_alloc();
    if (socket == nullptr) {
        log_error("can't allocate a netlink socket");
        return false;
    }
    const int connected = genl_connect(socket);
    const int family = connected < 0 ? connected : genl_ctrl_resolve(socket, NL80211_FAMILY);
    if (family < 0 && family != -NLE_OBJ_NOTFOUND) { // not found: no Wi-Fi driver, as on a cable
        log_error("can't reach nl80211: %s", nl_geterror(family));
    }
    uint32_t ifindex = 0;
    struct access_point_signal signal = {};
    const bool read = family >= 0 &&
                      dump(socket, family, NL80211_CMD_GET_INTERFACE, 0,
                           keep_first_station_interface, &ifindex) &&
                      ifindex != 0 &&
                      dump(socket, family, NL80211_CMD_GET_STATION, ifindex,
                           keep_access_point_signal, &signal) &&
                      signal.found;
    nl_socket_free(socket);
    if (read) {
        *out = link_quality(signal.dbm);
    }
    return read;
}
#else
bool wifi_read_signal([[maybe_unused]] double *out) {
    return false;
}
#endif
