// Host tests for named fan levels (fork #21 F6; spec §4.2). The texts belong
// to the caller (PAYLOAD_FAN_* in MHI-AC-Ctrl.h); this module only maps them
// to levels 1..4 and Auto.

#include <string.h>
#include <unity.h>

#include "mhi_fan.h"

void setUp(void) {}
void tearDown(void) {}

static const MhiFanNames kNames = {{"1", "2", "3", "4"}, "Auto"};

static void test_text_and_parse_round_trip(void) {
  TEST_ASSERT_EQUAL_STRING("1", mhi_fan_text(&kNames, 1));
  TEST_ASSERT_EQUAL_STRING("4", mhi_fan_text(&kNames, 4));
  TEST_ASSERT_EQUAL_STRING("Auto", mhi_fan_text(&kNames, MHI_FAN_AUTO));
  TEST_ASSERT_NULL(mhi_fan_text(&kNames, 0));
  TEST_ASSERT_NULL(mhi_fan_text(&kNames, 6));
  TEST_ASSERT_EQUAL_INT(1, mhi_fan_parse(&kNames, "1"));
  TEST_ASSERT_EQUAL_INT(4, mhi_fan_parse(&kNames, "4"));
  TEST_ASSERT_EQUAL_INT(MHI_FAN_AUTO, mhi_fan_parse(&kNames, "Auto"));
}

static void test_parse_with_custom_names_still_accepts_the_digits(void) {
  const MhiFanNames named = {{"Low", "Medium", "High", "Turbo"}, "Auto"};
  TEST_ASSERT_EQUAL_INT(3, mhi_fan_parse(&named, "High"));
  TEST_ASSERT_EQUAL_INT(3, mhi_fan_parse(&named, "3"));  // v2.8's numbers still work
}

static void test_parse_rejects_everything_else(void) {
  TEST_ASSERT_EQUAL_INT(MHI_FAN_NONE, mhi_fan_parse(&kNames, "0"));
  TEST_ASSERT_EQUAL_INT(MHI_FAN_NONE, mhi_fan_parse(&kNames, "5"));
  TEST_ASSERT_EQUAL_INT(MHI_FAN_NONE, mhi_fan_parse(&kNames, "auto"));  // case-sensitive
  TEST_ASSERT_EQUAL_INT(MHI_FAN_NONE, mhi_fan_parse(&kNames, ""));
  TEST_ASSERT_EQUAL_INT(MHI_FAN_NONE, mhi_fan_parse(&kNames, NULL));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_text_and_parse_round_trip);
  RUN_TEST(test_parse_with_custom_names_still_accepts_the_digits);
  RUN_TEST(test_parse_rejects_everything_else);
  return UNITY_END();
}
