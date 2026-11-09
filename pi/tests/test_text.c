#include <stddef.h>
#include <string.h>

#include <glib.h>

#include "shared/text.h"
#include "unity.h"

struct polish_name {
    const char *composed;
    const char *decomposed;
    const char *shouted; // capitals, decomposed
};

// the IMGW warning names with letters that Unicode can also spell as a base and a combining mark
static const struct polish_name POLISH_NAMES[] = {
        {"Intensywne opady śniegu", "Intensywne opady s\u0301niegu",
         "INTENSYWNE OPADY S\u0301NIEGU"},
        {"Silny mróz", "Silny mro\u0301z", "SILNY MRO\u0301Z"},
        {"Opady marznące", "Opady marzna\u0328ce", "OPADY MARZNA\u0328CE"},
        {"Gęsta mgła", "Ge\u0328sta mgła", "GE\u0328STA MGŁA"},
        {"Mgła intensywnie osadzająca szadź", "Mgła intensywnie osadzaja\u0328ca szadz\u0301",
         "MGŁA INTENSYWNIE OSADZAJA\u0328CA SZADZ\u0301"},
};

void setUp(void) {
}

void tearDown(void) {
}

static void test_bytes_vs_characters(void) {
    const char *text = "农历九月十七";
    TEST_ASSERT_EQUAL_size_t(18, strlen(text));
    TEST_ASSERT_EQUAL_INT(6, g_utf8_strlen(text, -1));
    TEST_ASSERT_TRUE(text_is_valid_utf8(text, strlen(text)));
}

static void test_four_byte_character(void) {
    const char *text = "𠮷";
    TEST_ASSERT_EQUAL_size_t(4, strlen(text));
    TEST_ASSERT_TRUE(text_is_valid_utf8(text, strlen(text)));
    TEST_ASSERT_FALSE(text_is_valid_utf8(text, 3));
}

static void test_invalid_utf8(void) {
    TEST_ASSERT_FALSE(text_is_valid_utf8("\xe5\x86", 2));
    TEST_ASSERT_FALSE(text_is_valid_utf8("\xff", 1));
    TEST_ASSERT_FALSE(text_is_valid_utf8("农历", 4));
}

static void test_bytes_past_length_are_ignored(void) {
    TEST_ASSERT_TRUE(text_is_valid_utf8("农\xff", 3));
}

static void test_invalid_text_is_never_equal(void) {
    TEST_ASSERT_FALSE(text_equal_folded("\xff", "\xff"));
    TEST_ASSERT_FALSE(text_equal_folded("Burze", "\xff"));
}

static void test_equal_folded(void) {
    const char *nfd = "S\xcc\x81"
                      "nieg";
    const char *nfc = "\xc5\x9a"
                      "nieg";
    TEST_ASSERT_TRUE(text_equal_folded("Silny wiatr", "SILNY WIATR"));
    TEST_ASSERT_NOT_EQUAL_INT(0, strcmp(nfd, nfc));
    TEST_ASSERT_TRUE(text_equal_folded(nfd, nfc));
    TEST_ASSERT_FALSE(text_equal_folded(nfc, "Snieg"));
    TEST_ASSERT_FALSE(text_equal_folded("Silny wiatr", "Silny mróz"));
}

static void test_polish_names_composed_and_decomposed(void) {
    for (size_t i = 0; i < G_N_ELEMENTS(POLISH_NAMES); i++) {
        const struct polish_name *name = &POLISH_NAMES[i];
        TEST_ASSERT_NOT_EQUAL_INT_MESSAGE(0, strcmp(name->composed, name->decomposed),
                                          name->composed);
        TEST_ASSERT_TRUE_MESSAGE(text_equal_folded(name->composed, name->decomposed),
                                 name->composed);
        TEST_ASSERT_TRUE_MESSAGE(text_equal_folded(name->decomposed, name->composed),
                                 name->composed);
        TEST_ASSERT_TRUE_MESSAGE(text_equal_folded(name->shouted, name->composed), name->composed);
    }
    TEST_ASSERT_FALSE(text_equal_folded("Silny mróz", "Silny mroz"));
    TEST_ASSERT_FALSE(text_equal_folded("Gęsta mgła", "Gesta mgla"));
    TEST_ASSERT_TRUE(text_equal_folded("Upał", "UPAŁ"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_bytes_vs_characters);
    RUN_TEST(test_four_byte_character);
    RUN_TEST(test_invalid_utf8);
    RUN_TEST(test_bytes_past_length_are_ignored);
    RUN_TEST(test_invalid_text_is_never_equal);
    RUN_TEST(test_equal_folded);
    RUN_TEST(test_polish_names_composed_and_decomposed);
    return UNITY_END();
}
