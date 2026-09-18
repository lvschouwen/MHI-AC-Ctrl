// Host tests for the left/right louvers and 3D auto (fork #20; spec
// docs/superpowers/specs/2026-09-18-batch-c-louvers-outdoor-counters-design.md §2.1).
// The upstream commands are coupled (see MHI-AC-Ctrl-core.cpp before Task 2);
// decode must mask the AC's echo of both set flags (DB16 & 0x10, DB17 & 0x0a).

#include <string.h>
#include <unity.h>

#include "mhi_vanes_lr.h"

void setUp(void) {}
void tearDown(void) {}

static const MhiVanesLrNames kNames = {
  {"Left", "LeftCenter", "Center", "CenterRight", "Right", "Wide", "Spot"}, "Swing"};

static void test_command_sets_the_position_and_the_swing_set_flag_only(void) {
  uint8_t db16, db17;
  TEST_ASSERT_TRUE(mhi_vanes_lr_command(1, &db16, &db17));
  TEST_ASSERT_EQUAL_HEX8(0x10, db16);  // set flag (0x10) + position 0
  TEST_ASSERT_EQUAL_HEX8(0x02, db17);  // swing set flag, swing off; no 0x08
  TEST_ASSERT_TRUE(mhi_vanes_lr_command(7, &db16, &db17));
  TEST_ASSERT_EQUAL_HEX8(0x16, db16);  // set flag + position 6
  TEST_ASSERT_EQUAL_HEX8(0x02, db17);
}

static void test_command_swing_sets_no_position(void) {
  uint8_t db16, db17;
  TEST_ASSERT_TRUE(mhi_vanes_lr_command(MHI_VANES_LR_SWING, &db16, &db17));
  TEST_ASSERT_EQUAL_HEX8(0x00, db16);
  TEST_ASSERT_EQUAL_HEX8(0x03, db17);  // swing set flag + swing on; no 0x08
}

static void test_command_refuses_a_value_it_does_not_know(void) {
  // Reject, never clamp: a clamped value would move the louver somewhere
  // nobody asked for. Both bytes are left at 0, so the frame carries no set
  // flag at all and the caller can tell the command apart from a real one.
  static const int kBad[] = {0, -1, 9, 255};
  for (unsigned i = 0; i < sizeof(kBad) / sizeof(kBad[0]); i++) {
    uint8_t db16 = 0xff, db17 = 0xff;
    TEST_ASSERT_FALSE(mhi_vanes_lr_command(kBad[i], &db16, &db17));
    TEST_ASSERT_EQUAL_HEX8(0x00, db16);
    TEST_ASSERT_EQUAL_HEX8(0x00, db17);
  }
}

static void test_3dauto_command_sets_no_swing_flag(void) {
  TEST_ASSERT_EQUAL_HEX8(0x0c, mhi_3dauto_command(true));   // 0x08 | 0x04; no 0x02
  TEST_ASSERT_EQUAL_HEX8(0x08, mhi_3dauto_command(false));  // 0x08 | 0; no 0x02
}

static void test_decode_reads_position_and_swing(void) {
  TEST_ASSERT_EQUAL_INT(1, mhi_vanes_lr_decode(0x00, 0x00));
  TEST_ASSERT_EQUAL_INT(7, mhi_vanes_lr_decode(0x06, 0x00));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_LR_SWING, mhi_vanes_lr_decode(0x00, 0x01));
}

static void test_decode_masks_the_acs_echo_of_the_set_flags(void) {
  // Uitkijk, 18 Sep 2026: after a write the AC echoes DB16 & 0x10, DB17 & 0x0a
  // until the remote is used next.
  TEST_ASSERT_EQUAL_INT(3, mhi_vanes_lr_decode(0x10 | 0x02, 0x0a));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_LR_SWING, mhi_vanes_lr_decode(0x10, 0x0a | 0x01));
}

static void test_3dauto_decode_reads_bit_2_only(void) {
  TEST_ASSERT_FALSE(mhi_3dauto_decode(0x0a));        // set-flag echo, 3D auto off
  TEST_ASSERT_TRUE(mhi_3dauto_decode(0x0a | 0x04));  // set-flag echo, 3D auto on
}

static void test_text_and_parse_round_trip(void) {
  TEST_ASSERT_EQUAL_STRING("Left", mhi_vanes_lr_text(&kNames, 1));
  TEST_ASSERT_EQUAL_STRING("Spot", mhi_vanes_lr_text(&kNames, 7));
  TEST_ASSERT_EQUAL_STRING("Swing", mhi_vanes_lr_text(&kNames, MHI_VANES_LR_SWING));
  TEST_ASSERT_NULL(mhi_vanes_lr_text(&kNames, 0));
  TEST_ASSERT_NULL(mhi_vanes_lr_text(&kNames, 9));
  TEST_ASSERT_EQUAL_INT(1, mhi_vanes_lr_parse(&kNames, "Left"));
  TEST_ASSERT_EQUAL_INT(7, mhi_vanes_lr_parse(&kNames, "7"));  // v2.8-style numbers still work
  TEST_ASSERT_EQUAL_INT(MHI_VANES_LR_SWING, mhi_vanes_lr_parse(&kNames, "8"));
}

static void test_parse_rejects_everything_else(void) {
  TEST_ASSERT_EQUAL_INT(MHI_VANES_LR_UNKNOWN, mhi_vanes_lr_parse(&kNames, "0"));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_LR_UNKNOWN, mhi_vanes_lr_parse(&kNames, "9"));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_LR_UNKNOWN, mhi_vanes_lr_parse(&kNames, "left"));  // case-sensitive
  TEST_ASSERT_EQUAL_INT(MHI_VANES_LR_UNKNOWN, mhi_vanes_lr_parse(&kNames, ""));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_LR_UNKNOWN, mhi_vanes_lr_parse(&kNames, NULL));
}

static void test_a_configuration_that_keeps_the_numbers_as_names_still_works(void) {
  const MhiVanesLrNames numeric = {{"1", "2", "3", "4", "5", "6", "7"}, "8"};
  TEST_ASSERT_EQUAL_STRING("3", mhi_vanes_lr_text(&numeric, 3));
  TEST_ASSERT_EQUAL_INT(3, mhi_vanes_lr_parse(&numeric, "3"));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_LR_SWING, mhi_vanes_lr_parse(&numeric, "8"));  // "8" is the swing NAME here
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_command_sets_the_position_and_the_swing_set_flag_only);
  RUN_TEST(test_command_swing_sets_no_position);
  RUN_TEST(test_command_refuses_a_value_it_does_not_know);
  RUN_TEST(test_3dauto_command_sets_no_swing_flag);
  RUN_TEST(test_decode_reads_position_and_swing);
  RUN_TEST(test_decode_masks_the_acs_echo_of_the_set_flags);
  RUN_TEST(test_3dauto_decode_reads_bit_2_only);
  RUN_TEST(test_text_and_parse_round_trip);
  RUN_TEST(test_parse_rejects_everything_else);
  RUN_TEST(test_a_configuration_that_keeps_the_numbers_as_names_still_works);
  return UNITY_END();
}
