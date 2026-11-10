#pragma once

struct wifi_signal {
    bool ok;
    double quality; // the kernel's 0–70 link quality
};

struct wifi_signal wifi_read_signal(void);
