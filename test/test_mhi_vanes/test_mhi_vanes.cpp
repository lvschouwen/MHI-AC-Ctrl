// Host tests for the named vane positions (fork issue #4, batch B; spec
// docs/superpowers/specs/2026-09-16-phase-4-batches-design.md §4.2).
//
// The texts belong to the caller (PAYLOAD_VANES_* in MHI-AC-Ctrl.h, overridable
// from config_defaults.h); this module only maps them to the core's numbers.

#include <string.h>
#include <unity.h>

#include "mhi_vanes.h"

void setUp(void) {}
void tearDown(void) {}

static const MhiVanesNames kNames = {{"Up", "UpCenter", "CenterDown", "Down"}, "Swing", "?"};

static void test_text_names_the_four_positions_and_swing(void) {
  TEST_ASSERT_EQUAL_STRING("Up", mhi_vanes_text(&kNames, 1));
  TEST_ASSERT_EQUAL_STRING("UpCenter", mhi_vanes_text(&kNames, 2));
  TEST_ASSERT_EQUAL_STRING("CenterDown", mhi_vanes_text(&kNames, 3));
  TEST_ASSERT_EQUAL_STRING("Down", mhi_vanes_text(&kNames, 4));
  TEST_ASSERT_EQUAL_STRING("Swing", mhi_vanes_text(&kNames, MHI_VANES_SWING));
}

static void test_text_is_unknown_for_anything_else(void) {
  // vanes_unknown: the last change came from the IR remote, the AC does not say where the vanes are.
  TEST_ASSERT_EQUAL_STRING("?", mhi_vanes_text(&kNames, MHI_VANES_UNKNOWN));
  TEST_ASSERT_EQUAL_STRING("?", mhi_vanes_text(&kNames, 6));
  TEST_ASSERT_EQUAL_STRING("?", mhi_vanes_text(&kNames, -1));
}

static void test_parse_accepts_the_names(void) {
  TEST_ASSERT_EQUAL_INT(1, mhi_vanes_parse(&kNames, "Up"));
  TEST_ASSERT_EQUAL_INT(2, mhi_vanes_parse(&kNames, "UpCenter"));
  TEST_ASSERT_EQUAL_INT(3, mhi_vanes_parse(&kNames, "CenterDown"));
  TEST_ASSERT_EQUAL_INT(4, mhi_vanes_parse(&kNames, "Down"));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_SWING, mhi_vanes_parse(&kNames, "Swing"));
}

static void test_parse_still_accepts_the_numbers_of_v2_8(void) {
  // Existing configurations write 1..4 and 5 for swing; they keep working.
  TEST_ASSERT_EQUAL_INT(1, mhi_vanes_parse(&kNames, "1"));
  TEST_ASSERT_EQUAL_INT(4, mhi_vanes_parse(&kNames, "4"));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_SWING, mhi_vanes_parse(&kNames, "5"));
}

static void test_parse_rejects_everything_else(void) {
  TEST_ASSERT_EQUAL_INT(MHI_VANES_UNKNOWN, mhi_vanes_parse(&kNames, "0"));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_UNKNOWN, mhi_vanes_parse(&kNames, "6"));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_UNKNOWN, mhi_vanes_parse(&kNames, "?"));   // a state, not a command
  TEST_ASSERT_EQUAL_INT(MHI_VANES_UNKNOWN, mhi_vanes_parse(&kNames, "up"));  // case-sensitive, like every payload
  TEST_ASSERT_EQUAL_INT(MHI_VANES_UNKNOWN, mhi_vanes_parse(&kNames, "Up "));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_UNKNOWN, mhi_vanes_parse(&kNames, "11"));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_UNKNOWN, mhi_vanes_parse(&kNames, ""));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_UNKNOWN, mhi_vanes_parse(&kNames, NULL));
}

static void test_a_configuration_that_keeps_the_numbers_as_names_still_works(void) {
  // A user who defines PAYLOAD_VANES_1 "1" keeps v2.8's texts on the topic too.
  const MhiVanesNames numeric = {{"1", "2", "3", "4"}, "Swing", "?"};
  TEST_ASSERT_EQUAL_STRING("3", mhi_vanes_text(&numeric, 3));
  TEST_ASSERT_EQUAL_INT(3, mhi_vanes_parse(&numeric, "3"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_text_names_the_four_positions_and_swing);
  RUN_TEST(test_text_is_unknown_for_anything_else);
  RUN_TEST(test_parse_accepts_the_names);
  RUN_TEST(test_parse_still_accepts_the_numbers_of_v2_8);
  RUN_TEST(test_parse_rejects_everything_else);
  RUN_TEST(test_a_configuration_that_keeps_the_numbers_as_names_still_works);
  return UNITY_END();
}
