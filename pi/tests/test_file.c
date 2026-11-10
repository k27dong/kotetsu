#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "shared/file.h"
#include "unity.h"

static gchar *dir;

void setUp(void) {
    dir = g_dir_make_tmp("kotetsu-file-XXXXXX", nullptr);
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
    if (g_rmdir(dir) != 0) { // not an assert, which would cut tearDown short
        fprintf(stderr, "can't remove %s\n", dir);
    }
    g_free(dir);
}

static gchar *path_in_dir(const char *name) {
    return g_build_filename(dir, name, nullptr);
}

static unsigned entries_in_dir(void) {
    GDir *entries = g_dir_open(dir, 0, nullptr);
    TEST_ASSERT_NOT_NULL(entries);
    unsigned count = 0;
    while (g_dir_read_name(entries) != nullptr) {
        count++;
    }
    g_dir_close(entries);
    return count;
}

static void test_read_whole_small_file(void) {
    gchar *path = path_in_dir("temperature");
    TEST_ASSERT_TRUE(g_file_set_contents(path, "48100\n", -1, nullptr));
    char text[16];
    size_t length = 0;
    TEST_ASSERT_TRUE(file_read(path, text, sizeof text, &length));
    TEST_ASSERT_EQUAL_size_t(6, length);
    TEST_ASSERT_EQUAL_MEMORY("48100\n", text, 6);
    length = 0;
    TEST_ASSERT_TRUE(file_read(path, text, 6, &length));
    TEST_ASSERT_EQUAL_size_t(6, length);
    g_free(path);
}

static void test_read_empty_file(void) {
    gchar *path = path_in_dir("empty");
    TEST_ASSERT_TRUE(g_file_set_contents(path, "", 0, nullptr));
    char text[4];
    size_t length = 99;
    TEST_ASSERT_TRUE(file_read(path, text, sizeof text, &length));
    TEST_ASSERT_EQUAL_size_t(0, length);
    g_free(path);
}

static void test_read_fails_when_file_is_larger(void) {
    gchar *path = path_in_dir("temperature");
    TEST_ASSERT_TRUE(g_file_set_contents(path, "48100\n", -1, nullptr));
    char text[5];
    size_t length = 0;
    errno = 0;
    TEST_ASSERT_FALSE(file_read(path, text, sizeof text, &length));
    TEST_ASSERT_EQUAL_INT(EFBIG, errno);
    g_free(path);
}

static void test_read_missing_file_fails_with_enoent(void) {
    gchar *path = path_in_dir("missing");
    char text[4];
    size_t length = 0;
    errno = 0;
    TEST_ASSERT_FALSE(file_read(path, text, sizeof text, &length));
    TEST_ASSERT_EQUAL_INT(ENOENT, errno);
    g_free(path);
}

static void test_write_atomic_replaces_and_leaves_no_temp(void) {
    gchar *path = path_in_dir("state");
    TEST_ASSERT_TRUE(file_write_atomic(path, "first version", 13));
    TEST_ASSERT_TRUE(file_write_atomic(path, "second", 6));
    gchar *contents = nullptr;
    gsize length = 0;
    TEST_ASSERT_TRUE(g_file_get_contents(path, &contents, &length, nullptr));
    TEST_ASSERT_EQUAL_size_t(6, length);
    TEST_ASSERT_EQUAL_MEMORY("second", contents, 6);
    TEST_ASSERT_EQUAL_UINT(1, entries_in_dir());
    GStatBuf status;
    TEST_ASSERT_EQUAL_INT(0, g_stat(path, &status));
    TEST_ASSERT_EQUAL_HEX(0600, status.st_mode & 0777);
    g_free(contents);
    g_free(path);
}

static void test_write_atomic_fails_in_missing_directory(void) {
    gchar *path = g_build_filename(dir, "missing", "state", nullptr);
    errno = 0;
    TEST_ASSERT_FALSE(file_write_atomic(path, "x", 1));
    TEST_ASSERT_EQUAL_INT(ENOENT, errno);
    TEST_ASSERT_EQUAL_UINT(0, entries_in_dir());
    g_free(path);
}

static void test_write_atomic_fails_when_directory_cannot_sync(void) {
    gchar *locked = path_in_dir("locked");
    TEST_ASSERT_EQUAL_INT(0, g_mkdir(locked, 0300)); // without read permission it can't be synced
    gchar *path = g_build_filename(locked, "state", nullptr);
    errno = 0;
    const bool written = file_write_atomic(path, "new", 3);
    const int reason = errno;
    TEST_ASSERT_EQUAL_INT(0, g_chmod(locked, 0700)); // restored first, for tearDown to clear it
    gchar *contents = nullptr;
    const bool replaced = g_file_get_contents(path, &contents, nullptr, nullptr);
    const bool removed = g_remove(path) == 0;
    TEST_ASSERT_FALSE(written);
    TEST_ASSERT_EQUAL_INT(EACCES, reason);
    TEST_ASSERT_TRUE(replaced);
    TEST_ASSERT_EQUAL_STRING("new", contents);
    TEST_ASSERT_TRUE(removed);
    g_free(contents);
    g_free(path);
    g_free(locked);
}

static void test_binary_data_round_trips(void) {
    gchar *path = path_in_dir("state");
    const uint8_t written[] = {0x00, 0xFF, 0x00, 0x7F, 0x80, 0x0A, 0x00};
    TEST_ASSERT_TRUE(file_write_atomic(path, written, sizeof written));
    uint8_t read[sizeof written];
    size_t length = 0;
    TEST_ASSERT_TRUE(file_read(path, read, sizeof read, &length));
    TEST_ASSERT_EQUAL_size_t(sizeof written, length);
    TEST_ASSERT_EQUAL_MEMORY(written, read, sizeof written);
    g_free(path);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_read_whole_small_file);
    RUN_TEST(test_read_empty_file);
    RUN_TEST(test_read_fails_when_file_is_larger);
    RUN_TEST(test_read_missing_file_fails_with_enoent);
    RUN_TEST(test_write_atomic_replaces_and_leaves_no_temp);
    RUN_TEST(test_write_atomic_fails_in_missing_directory);
    RUN_TEST(test_write_atomic_fails_when_directory_cannot_sync);
    RUN_TEST(test_binary_data_round_trips);
    return UNITY_END();
}
