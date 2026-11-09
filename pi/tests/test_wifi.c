#include <string.h>

#include <glib.h>

#include "unity.h"
#include "wifi.h"

#define KERNEL_LINK_QUALITY "/proc/net/wireless" // deprecated, but the kernel's own figure

void setUp(void) {
}

void tearDown(void) {
}

#ifdef __linux__
static bool kernel_link_quality(double *out) {
    gchar *contents = nullptr;
    if (!g_file_get_contents(KERNEL_LINK_QUALITY, &contents, nullptr, nullptr)) {
        return false;
    }
    bool found = false;
    gchar **lines = g_strsplit(contents, "\n", -1);
    for (gchar **line = lines; *line != nullptr && !found; line++) {
        const char *after_name = strchr(*line, ':');
        if (after_name == nullptr) {
            continue;
        }
        const char *quality = after_name + 1 + strspn(after_name + 1, " ");
        quality += strcspn(quality, " ");
        char *end = nullptr;
        *out = g_ascii_strtod(quality, &end);
        found = end != quality;
    }
    g_strfreev(lines);
    g_free(contents);
    return found;
}
#endif

static void test_wifi_signal_on_0_to_70_scale(void) {
    const struct wifi_signal signal = wifi_read_signal();
#ifdef __linux__
    double expected = 0;
    const bool linked = kernel_link_quality(&expected);
    TEST_ASSERT_EQUAL_INT(linked, signal.ok);
    if (linked) {
        TEST_ASSERT_FLOAT_WITHIN(5.0f, (float)expected, (float)signal.quality);
        TEST_ASSERT_TRUE(signal.quality >= 0 && signal.quality <= 70);
    }
#else
    TEST_ASSERT_FALSE(signal.ok);
#endif
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_wifi_signal_on_0_to_70_scale);
    return UNITY_END();
}
