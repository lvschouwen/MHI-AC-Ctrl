// Host tests for the Allergen Clear state (fork #25).
//
// Capture 25 Sep 2026 on Uitkijk: Allergen Clear turns DB0 from 0x51 (on,
// heat) into 0x4c (off, fan) for 1.5 h; cancelling gives 0x50 (off, heat). A
// unit switched off from fan mode also reads off + fan, so the state is only
// On when the mode turned to fan while the unit was off, and the last mode it
// ran in was not fan.

#include <unity.h>

#include "mhi_cleaning.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t kCool = 0x08, kFan = 0x0c, kHeat = 0x10;

static MhiCleaning fresh(void) {
  MhiCleaning c;
  mhi_cleaning_init(&c);
  return c;
}

// The capture: on in heat, then Allergen Clear, then cancelled.
static void test_allergen_clear_from_heat_is_cleaning(void) {
  MhiCleaning c = fresh();
  mhi_cleaning_on_mode(&c, kHeat);
  mhi_cleaning_on_power(&c, 1);
  TEST_ASSERT_FALSE(mhi_cleaning_on_power(&c, 0));  // off: still not cleaning
  TEST_ASSERT_EQUAL_UINT8(0, c.state);
  TEST_ASSERT_TRUE(mhi_cleaning_on_mode(&c, kFan));
  TEST_ASSERT_EQUAL_UINT8(1, c.state);
  TEST_ASSERT_TRUE(mhi_cleaning_on_mode(&c, kHeat));  // cancelled
  TEST_ASSERT_EQUAL_UINT8(0, c.state);
}

static void test_switched_off_from_fan_mode_is_not_cleaning(void) {
  MhiCleaning c = fresh();
  mhi_cleaning_on_mode(&c, kFan);
  mhi_cleaning_on_power(&c, 1);
  mhi_cleaning_on_power(&c, 0);
  TEST_ASSERT_EQUAL_UINT8(0, c.state);
}

static void test_switching_on_ends_cleaning(void) {
  MhiCleaning c = fresh();
  mhi_cleaning_on_mode(&c, kCool);
  mhi_cleaning_on_power(&c, 1);
  mhi_cleaning_on_power(&c, 0);
  mhi_cleaning_on_mode(&c, kFan);
  TEST_ASSERT_EQUAL_UINT8(1, c.state);
  // The remote switches on into heat: the parser reports the mode first.
  TEST_ASSERT_TRUE(mhi_cleaning_on_mode(&c, kHeat));
  TEST_ASSERT_EQUAL_UINT8(0, c.state);
  TEST_ASSERT_FALSE(mhi_cleaning_on_power(&c, 1));
  TEST_ASSERT_EQUAL_UINT8(0, c.state);
}

static void test_fan_while_on_is_not_cleaning(void) {
  MhiCleaning c = fresh();
  mhi_cleaning_on_mode(&c, kHeat);
  mhi_cleaning_on_power(&c, 1);
  mhi_cleaning_on_mode(&c, kFan);
  TEST_ASSERT_EQUAL_UINT8(0, c.state);
}

// After a boot the last mode the unit ran in is unknown: report not cleaning
// rather than guess.
static void test_boot_while_off_in_fan_is_not_cleaning(void) {
  MhiCleaning c = fresh();
  mhi_cleaning_on_power(&c, 0);
  mhi_cleaning_on_mode(&c, kFan);
  TEST_ASSERT_EQUAL_UINT8(0, c.state);
}

static void test_repeated_input_reports_no_change(void) {
  MhiCleaning c = fresh();
  mhi_cleaning_on_mode(&c, kHeat);
  mhi_cleaning_on_power(&c, 1);
  TEST_ASSERT_FALSE(mhi_cleaning_on_power(&c, 1));
  TEST_ASSERT_FALSE(mhi_cleaning_on_mode(&c, kHeat));
}

// The first known state is a change, so it goes out once after a boot.
static void test_first_state_is_a_change(void) {
  MhiCleaning c = fresh();
  TEST_ASSERT_FALSE(mhi_cleaning_on_mode(&c, kHeat));  // power still unknown: nothing to say
  TEST_ASSERT_TRUE(mhi_cleaning_on_power(&c, 1));
  TEST_ASSERT_EQUAL_UINT8(0, c.state);
}

// An MQTT reconnect re-sends the state without forgetting the last mode.
static void test_republish_keeps_the_last_mode(void) {
  MhiCleaning c = fresh();
  mhi_cleaning_on_mode(&c, kHeat);
  mhi_cleaning_on_power(&c, 1);
  mhi_cleaning_on_power(&c, 0);
  mhi_cleaning_on_mode(&c, kFan);
  mhi_cleaning_republish(&c);
  // After a reconnect the parser reports power and mode again.
  TEST_ASSERT_TRUE(mhi_cleaning_on_power(&c, 0));
  TEST_ASSERT_EQUAL_UINT8(1, c.state);
  TEST_ASSERT_FALSE(mhi_cleaning_on_mode(&c, kFan));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_allergen_clear_from_heat_is_cleaning);
  RUN_TEST(test_switched_off_from_fan_mode_is_not_cleaning);
  RUN_TEST(test_switching_on_ends_cleaning);
  RUN_TEST(test_fan_while_on_is_not_cleaning);
  RUN_TEST(test_boot_while_off_in_fan_is_not_cleaning);
  RUN_TEST(test_repeated_input_reports_no_change);
  RUN_TEST(test_first_state_is_a_change);
  RUN_TEST(test_republish_keeps_the_last_mode);
  return UNITY_END();
}
