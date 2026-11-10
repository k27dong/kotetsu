#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "config.h"
#include "stderr_capture.h"
#include "unity.h"

#define DEV_DASHBOARD "http://127.0.0.1:8787"

static const char *const VARIABLES[] = {
        "KOTETSU_DASHBOARD_URL", "KOTETSU_STATE",         "KOTETSU_FONT_DIR",
        "KOTETSU_PI_KEY_FILE",   "CREDENTIALS_DIRECTORY",
};

static gchar *dir;

void setUp(void) {
    for (size_t i = 0; i < G_N_ELEMENTS(VARIABLES); i++) {
        TEST_ASSERT_EQUAL_INT(0, unsetenv(VARIABLES[i]));
    }
    dir = g_dir_make_tmp("kotetsu-config-XXXXXX", nullptr);
    TEST_ASSERT_NOT_NULL(dir);
}

void tearDown(void) {
    GDir *entries = g_dir_open(dir, 0, nullptr);
    if (entries != nullptr) {
        const gchar *name;
        while ((name = g_dir_read_name(entries)) != nullptr) {
            gchar *entry = g_build_filename(dir, name, nullptr);
            if (g_remove(entry) != 0) {
                fprintf(stderr, "can't remove %s\n", entry);
            }
            g_free(entry);
        }
        g_dir_close(entries);
    }
    if (g_rmdir(dir) != 0) { // after a failed test, an assert would cut the clean-up short
        fprintf(stderr, "can't remove %s\n", dir);
    }
    g_free(dir);
}

static gchar *write_file(const char *name, const char *contents, gssize length) {
    gchar *path = g_build_filename(dir, name, nullptr);
    TEST_ASSERT_TRUE(g_file_set_contents(path, contents, length, nullptr));
    return path;
}

static void use_key_file(const char *contents, gssize length) {
    gchar *path = write_file("key", contents, length);
    TEST_ASSERT_EQUAL_INT(0, setenv("KOTETSU_PI_KEY_FILE", path, 1));
    TEST_ASSERT_EQUAL_INT(
            0, setenv("KOTETSU_DASHBOARD_URL", DEV_DASHBOARD, 1)); // a key file by hand needs one
    g_free(path);
}

static void use_systemd_key(const char *contents) {
    gchar *key = write_file("pi-key", contents, -1);
    TEST_ASSERT_EQUAL_INT(0, setenv("CREDENTIALS_DIRECTORY", dir, 1)); // as systemd runs it
    g_free(key);
}

static void assert_not_ok_with_error(void) {
    char captured[1024];
    start_capture();
    const struct config config = config_load();
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_FALSE(config.ok);
    TEST_ASSERT_TRUE_MESSAGE(g_str_has_prefix(captured, "<3>"), captured);
}

static void test_defaults_under_systemd(void) {
    use_systemd_key("pi-secret\n");
    const struct config config = config_load();
    TEST_ASSERT_TRUE(config.ok);
    TEST_ASSERT_EQUAL_STRING("https://kotetsu.k27dong.com", config.dashboard_url);
    TEST_ASSERT_EQUAL_STRING("/var/lib/kotetsu/state", config.state_path);
    TEST_ASSERT_EQUAL_STRING("/usr/local/share/kotetsu/fonts", config.font_dir);
    TEST_ASSERT_EQUAL_STRING("pi-secret", config.pi_key);
}

static void test_key_file_needs_a_dashboard_url(void) {
    use_key_file("pi-secret", -1);
    TEST_ASSERT_TRUE(config_load().ok);
    static const char *const unset_or_empty[] = {nullptr, ""};
    for (size_t i = 0; i < G_N_ELEMENTS(unset_or_empty); i++) {
        if (unset_or_empty[i] == nullptr) {
            TEST_ASSERT_EQUAL_INT(0, unsetenv("KOTETSU_DASHBOARD_URL"));
        } else {
            TEST_ASSERT_EQUAL_INT(0, setenv("KOTETSU_DASHBOARD_URL", unset_or_empty[i], 1));
        }
        char captured[1024];
        start_capture();
        const bool ok = config_load().ok;
        finish_capture(captured, sizeof captured);
        TEST_ASSERT_FALSE(ok);
        TEST_ASSERT_EQUAL_STRING("<3>set KOTETSU_DASHBOARD_URL: a run with KOTETSU_PI_KEY_FILE has "
                                 "no default dashboard, so it can't replace the wall's picture by "
                                 "mistake\n",
                                 captured);
    }
    use_systemd_key("from-systemd\n"); // a systemd key doesn't lift the rule
    TEST_ASSERT_FALSE(config_load().ok);
}

static void test_environment_overrides_paths(void) {
    use_key_file("pi-secret", -1);
    TEST_ASSERT_EQUAL_INT(0, setenv("KOTETSU_DASHBOARD_URL", "http://127.0.0.1:8787", 1));
    TEST_ASSERT_EQUAL_INT(0, setenv("KOTETSU_STATE", "/tmp/kotetsu/state", 1));
    TEST_ASSERT_EQUAL_INT(0, setenv("KOTETSU_FONT_DIR", "/tmp/kotetsu/fonts", 1));
    const struct config config = config_load();
    TEST_ASSERT_TRUE(config.ok);
    TEST_ASSERT_EQUAL_STRING("http://127.0.0.1:8787", config.dashboard_url);
    TEST_ASSERT_EQUAL_STRING("/tmp/kotetsu/state", config.state_path);
    TEST_ASSERT_EQUAL_STRING("/tmp/kotetsu/fonts", config.font_dir);
}

static void test_empty_variable_means_default(void) {
    use_key_file("pi-secret", -1);
    TEST_ASSERT_EQUAL_INT(0, setenv("KOTETSU_STATE", "", 1));
    const struct config config = config_load();
    TEST_ASSERT_TRUE(config.ok);
    TEST_ASSERT_EQUAL_STRING("/var/lib/kotetsu/state", config.state_path);
}

static void test_key_from_credentials_directory(void) {
    use_systemd_key("from-systemd\n");
    const struct config config = config_load();
    TEST_ASSERT_TRUE(config.ok);
    TEST_ASSERT_EQUAL_STRING("from-systemd", config.pi_key);
}

static void test_key_file_variable_comes_first(void) {
    use_systemd_key("from-systemd\n");
    use_key_file("from-variable\n", -1);
    const struct config config = config_load();
    TEST_ASSERT_TRUE(config.ok);
    TEST_ASSERT_EQUAL_STRING("from-variable", config.pi_key);
}

static void test_key_surrounding_whitespace_trimmed(void) {
    const char *const files[] = {" pi-secret\r\n", "pi-secret \n\n", "\tpi-secret", "pi-secret"};
    for (size_t i = 0; i < G_N_ELEMENTS(files); i++) {
        use_key_file(files[i], -1);
        const struct config config = config_load();
        TEST_ASSERT_TRUE_MESSAGE(config.ok, files[i]);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("pi-secret", config.pi_key, files[i]);
    }
    use_key_file(" pi secret\n", -1);
    const struct config config = config_load();
    TEST_ASSERT_TRUE(config.ok);
    TEST_ASSERT_EQUAL_STRING("pi secret", config.pi_key);
}

static void test_no_key_is_not_ok(void) {
    assert_not_ok_with_error();
    gchar *missing = g_build_filename(dir, "missing", nullptr);
    TEST_ASSERT_EQUAL_INT(0, setenv("KOTETSU_PI_KEY_FILE", missing, 1));
    TEST_ASSERT_EQUAL_INT(0, setenv("KOTETSU_DASHBOARD_URL", DEV_DASHBOARD, 1));
    assert_not_ok_with_error();
    use_key_file("", 0);
    assert_not_ok_with_error();
    use_key_file("\n", -1);
    assert_not_ok_with_error();
    use_key_file(" \r\n\t", -1);
    assert_not_ok_with_error();
    TEST_ASSERT_EQUAL_INT(0, unsetenv("KOTETSU_PI_KEY_FILE"));
    TEST_ASSERT_EQUAL_INT(0, setenv("CREDENTIALS_DIRECTORY", dir, 1)); // with no pi-key in it
    assert_not_ok_with_error();
    g_free(missing);
}

static void test_overlong_value_is_not_ok(void) {
    use_systemd_key("pi-secret"); // so the dashboard URL can come and go with the others
    gchar *longest = g_strnfill(255, 'a');
    gchar *too_long = g_strnfill(256, 'a');
    for (size_t i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_INT(0, setenv(VARIABLES[i], longest, 1));
        TEST_ASSERT_TRUE_MESSAGE(config_load().ok, VARIABLES[i]);
        TEST_ASSERT_EQUAL_INT(0, setenv(VARIABLES[i], too_long, 1));
        assert_not_ok_with_error();
        TEST_ASSERT_EQUAL_INT(0, unsetenv(VARIABLES[i]));
    }
    g_free(too_long);
    g_free(longest);
}

static void test_overlong_key_is_not_ok(void) {
    gchar *longest = g_strnfill(PI_KEY_MAX_BYTES - 1, 'k');
    gchar *longest_padded = g_strconcat("  ", longest, "\r\n", nullptr); // over 256 bytes in all
    use_key_file(longest_padded, -1);
    const struct config config = config_load();
    TEST_ASSERT_TRUE(config.ok);
    TEST_ASSERT_EQUAL_STRING(longest, config.pi_key);
    gchar *too_long = g_strnfill(PI_KEY_MAX_BYTES, 'k');
    use_key_file(too_long, -1);
    assert_not_ok_with_error();
    gchar *far_too_long = g_strnfill(4 * PI_KEY_MAX_BYTES, 'k');
    use_key_file(far_too_long, -1);
    assert_not_ok_with_error();
    gchar *huge_file = g_strnfill(8 * PI_KEY_MAX_BYTES, 'k');
    use_key_file(huge_file, -1);
    assert_not_ok_with_error();
    g_free(huge_file);
    g_free(far_too_long);
    g_free(too_long);
    g_free(longest_padded);
    g_free(longest);
}

static void test_overlong_credentials_directory_is_not_ok(void) {
    gchar *longest = g_strnfill(4'088, 'c'); // 4,096 bytes less "/pi-key" and the string's end
    gchar *too_long = g_strnfill(4'089, 'c');
    char captured[1024];
    TEST_ASSERT_EQUAL_INT(0, setenv("CREDENTIALS_DIRECTORY", too_long, 1));
    start_capture();
    const bool too_long_ok = config_load().ok;
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_FALSE(too_long_ok);
    TEST_ASSERT_EQUAL_STRING("<3>CREDENTIALS_DIRECTORY is over 4088 bytes\n", captured);
    TEST_ASSERT_EQUAL_INT(0, setenv("CREDENTIALS_DIRECTORY", longest, 1));
    start_capture();
    const bool longest_ok = config_load().ok;
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_FALSE(longest_ok); // fails on the missing directory, not its length
    TEST_ASSERT_TRUE_MESSAGE(g_str_has_prefix(captured, "<3>can't read the Pi key from ccc"),
                             captured);
    g_free(too_long);
    g_free(longest);
}

static void test_errors_never_show_the_key(void) {
    GString *too_long = g_string_new(nullptr);
    while (too_long->len < PI_KEY_MAX_BYTES) {
        g_string_append_c(too_long, "SECRET"[too_long->len % 6]);
    }
    use_key_file(too_long->str, -1);
    char captured[1024];
    start_capture();
    const bool ok = config_load().ok;
    finish_capture(captured, sizeof captured);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_NULL_MESSAGE(strstr(captured, "SECRET"), captured);
    g_string_free(too_long, TRUE);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_under_systemd);
    RUN_TEST(test_key_file_needs_a_dashboard_url);
    RUN_TEST(test_environment_overrides_paths);
    RUN_TEST(test_empty_variable_means_default);
    RUN_TEST(test_key_from_credentials_directory);
    RUN_TEST(test_key_file_variable_comes_first);
    RUN_TEST(test_key_surrounding_whitespace_trimmed);
    RUN_TEST(test_no_key_is_not_ok);
    RUN_TEST(test_overlong_value_is_not_ok);
    RUN_TEST(test_overlong_key_is_not_ok);
    RUN_TEST(test_overlong_credentials_directory_is_not_ok);
    RUN_TEST(test_errors_never_show_the_key);
    return UNITY_END();
}
