#pragma once

enum wifi_state {
    WIFI_STATE_UNKNOWN,
    WIFI_STATE_HOTSPOT,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
};

// UNKNOWN when the file is missing, as before Comitup's first callback and on the Mac
enum wifi_state wifi_read_state(const char *path);
const char *wifi_state_name(enum wifi_state state);
// the connected Wi-Fi link's quality on the kernel's 0–70 scale; false without one, as on the Mac
[[nodiscard]] bool wifi_read_signal(double *out);
