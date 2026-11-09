#include <stdint.h>

#include <cjson/cJSON.h>

#include "shared/json.h"
#include "unity.h"

void setUp(void) {
}

void tearDown(void) {
}

static cJSON *parse(const char *json) {
    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL_MESSAGE(root, json);
    return root;
}

static void test_whole_number_accepts_2_pow_53_minus_1(void) {
    cJSON *numbers = parse("[9007199254740991, -9007199254740991]");
    int64_t value = 0;
    TEST_ASSERT_TRUE(json_whole_number(cJSON_GetArrayItem(numbers, 0), &value));
    TEST_ASSERT_EQUAL_INT64(INT64_C(9'007'199'254'740'991), value);
    TEST_ASSERT_TRUE(json_whole_number(cJSON_GetArrayItem(numbers, 1), &value));
    TEST_ASSERT_EQUAL_INT64(-INT64_C(9'007'199'254'740'991), value);
    cJSON_Delete(numbers);
}

static void test_whole_number_rejects_2_pow_53(void) {
    // 2^53 + 1 reads as the double 2^53, so 2^53 itself can't be trusted
    const char *const texts[] = {"9007199254740992", "-9007199254740992", "9007199254740993",
                                 "-9007199254740993", "1e400"};
    for (size_t i = 0; i < sizeof texts / sizeof texts[0]; i++) {
        cJSON *number = parse(texts[i]);
        int64_t value = 7;
        TEST_ASSERT_FALSE_MESSAGE(json_whole_number(number, &value), texts[i]);
        TEST_ASSERT_EQUAL_INT64_MESSAGE(7, value, texts[i]);
        cJSON_Delete(number);
    }
}

static void test_whole_number_rejects_fraction(void) {
    cJSON *items = parse("[7.5, -0.25, 7, 7.0, -3, \"7\", null, true]");
    int64_t value = 0;
    TEST_ASSERT_FALSE(json_whole_number(cJSON_GetArrayItem(items, 0), &value));
    TEST_ASSERT_FALSE(json_whole_number(cJSON_GetArrayItem(items, 1), &value));
    TEST_ASSERT_TRUE(json_whole_number(cJSON_GetArrayItem(items, 2), &value));
    TEST_ASSERT_EQUAL_INT64(7, value);
    TEST_ASSERT_TRUE(json_whole_number(cJSON_GetArrayItem(items, 3), &value));
    TEST_ASSERT_EQUAL_INT64(7, value);
    TEST_ASSERT_TRUE(json_whole_number(cJSON_GetArrayItem(items, 4), &value));
    TEST_ASSERT_EQUAL_INT64(-3, value);
    TEST_ASSERT_FALSE(json_whole_number(cJSON_GetArrayItem(items, 5), &value));
    TEST_ASSERT_FALSE(json_whole_number(cJSON_GetArrayItem(items, 6), &value));
    TEST_ASSERT_FALSE(json_whole_number(cJSON_GetArrayItem(items, 7), &value));
    TEST_ASSERT_FALSE(json_whole_number(nullptr, &value));
    cJSON_Delete(items);
}

static void test_member_of_non_object_is_null(void) {
    cJSON *root = parse("{\"phrase\":{\"id\":7},\"list\":[{\"id\":1}],\"text\":\"id\"}");
    TEST_ASSERT_NOT_NULL(json_member(json_member(root, "phrase"), "id"));
    TEST_ASSERT_NULL(json_member(json_member(root, "list"), "id"));
    TEST_ASSERT_NULL(json_member(json_member(root, "text"), "id"));
    TEST_ASSERT_NULL(json_member(json_member(root, "missing"), "id"));
    TEST_ASSERT_NULL(json_member(nullptr, "id"));
    cJSON_Delete(root);
}

static void test_member_names_are_case_sensitive(void) {
    cJSON *root = parse("{\"cleanupMinute\":600}");
    TEST_ASSERT_NOT_NULL(json_member(root, "cleanupMinute"));
    TEST_ASSERT_NULL(json_member(root, "cleanupminute"));
    cJSON_Delete(root);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_whole_number_accepts_2_pow_53_minus_1);
    RUN_TEST(test_whole_number_rejects_2_pow_53);
    RUN_TEST(test_whole_number_rejects_fraction);
    RUN_TEST(test_member_of_non_object_is_null);
    RUN_TEST(test_member_names_are_case_sensitive);
    return UNITY_END();
}
