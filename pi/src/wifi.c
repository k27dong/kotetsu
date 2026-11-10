/* the Wi-Fi signal, read over nl80211 on Linux */
#include "wifi.h"

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

#include "shared/log.h"

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
    /* build the request */
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

    /* send it, keep each answer */
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

struct wifi_signal wifi_read_signal(void) {
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

    /* find the access point's signal */
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

    /* scale it to 0–70 */
    const int dbm = signal.dbm < WEAKEST_DBM     ? WEAKEST_DBM
                    : signal.dbm > STRONGEST_DBM ? STRONGEST_DBM
                                                 : signal.dbm;
    return (struct wifi_signal){.ok = true, .quality = dbm - WEAKEST_DBM};
}
#else
struct wifi_signal wifi_read_signal(void) {
    return (struct wifi_signal){}; // only Linux has nl80211
}
#endif
