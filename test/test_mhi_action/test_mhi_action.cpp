// Host tests for the Action topic (issue #16).
//
// Home Assistant's climate shows what the AC is doing, not only what it is set
// to: a unit set to cool with a room already below the setpoint is idle, not
// cooling. DB13 carries the outdoor unit's state. The mapping follows
// hberntsen/mhi-ac-ctrl-esp32 (mhi_ac_ctrl.h), which reads the same byte.

#include <unity.h>

#include "mhi_action.h"

void setUp(void) {}
void tearDown(void) {}

// DB0 as the AC sends it: bit 0 is power, bits 2-4 the mode, bits 6-7 vanes.
static const uint8_t kOn = 0x01;
static const uint8_t kAuto = 0x00;
static const uint8_t kDry = 0x04;
static const uint8_t kCool = 0x08;
static const uint8_t kFan = 0x0c;
static const uint8_t kHeat = 0x10;

// DB13: bit 1 heating (as opposed to cooling), bit 2 compressor running.
static const uint8_t kCompressorOff = 0x00;
static const uint8_t kCompressorCooling = 0x04;
static const uint8_t kCompressorHeating = 0x06;

static void test_a_unit_that_is_off_is_off_whatever_the_outdoor_unit_says(void) {
  TEST_ASSERT_EQUAL_UINT8(MHI_ACTION_OFF, mhi_hvac_action(kCool, kCompressorOff));
  TEST_ASSERT_EQUAL_UINT8(MHI_ACTION_OFF, mhi_hvac_action(kHeat, kCompressorHeating));  // run-down after switch-off
}

static void test_cool_follows_the_compressor(void) {
  TEST_ASSERT_EQUAL_UINT8(MHI_ACTION_COOLING, mhi_hvac_action(kCool | kOn, kCompressorCooling));
  TEST_ASSERT_EQUAL_UINT8(MHI_ACTION_IDLE, mhi_hvac_action(kCool | kOn, kCompressorOff));
}

static void test_heat_follows_the_compressor(void) {
  TEST_ASSERT_EQUAL_UINT8(MHI_ACTION_HEATING, mhi_hvac_action(kHeat | kOn, kCompressorHeating));
  TEST_ASSERT_EQUAL_UINT8(MHI_ACTION_IDLE, mhi_hvac_action(kHeat | kOn, kCompressorOff));
}

static void test_dry_follows_the_compressor(void) {
  TEST_ASSERT_EQUAL_UINT8(MHI_ACTION_DRYING, mhi_hvac_action(kDry | kOn, kCompressorCooling));
  TEST_ASSERT_EQUAL_UINT8(MHI_ACTION_IDLE, mhi_hvac_action(kDry | kOn, kCompressorOff));
}

static void test_fan_mode_is_fan_even_if_the_compressor_bit_is_set(void) {
  TEST_ASSERT_EQUAL_UINT8(MHI_ACTION_FAN, mhi_hvac_action(kFan | kOn, kCompressorOff));
  TEST_ASSERT_EQUAL_UINT8(MHI_ACTION_FAN, mhi_hvac_action(kFan | kOn, kCompressorCooling));
}

static void test_auto_takes_heating_or_cooling_from_the_outdoor_unit(void) {
  TEST_ASSERT_EQUAL_UINT8(MHI_ACTION_HEATING, mhi_hvac_action(kAuto | kOn, kCompressorHeating));
  TEST_ASSERT_EQUAL_UINT8(MHI_ACTION_COOLING, mhi_hvac_action(kAuto | kOn, kCompressorCooling));
  TEST_ASSERT_EQUAL_UINT8(MHI_ACTION_IDLE, mhi_hvac_action(kAuto | kOn, kCompressorOff));
}

static void test_cool_stays_cooling_when_the_heat_bit_is_set(void) {
  // The mode decides in every mode but auto; only auto needs the heat bit.
  TEST_ASSERT_EQUAL_UINT8(MHI_ACTION_COOLING, mhi_hvac_action(kCool | kOn, kCompressorHeating));
}

static void test_an_undocumented_mode_is_judged_like_auto(void) {
  TEST_ASSERT_EQUAL_UINT8(MHI_ACTION_HEATING, mhi_hvac_action(0x14 | kOn, kCompressorHeating));
  TEST_ASSERT_EQUAL_UINT8(MHI_ACTION_IDLE, mhi_hvac_action(0x1c | kOn, kCompressorOff));
}

static void test_vane_bits_and_other_db13_bits_do_not_matter(void) {
  TEST_ASSERT_EQUAL_UINT8(MHI_ACTION_COOLING, mhi_hvac_action(0xc0 | kCool | kOn, 0xf9 | kCompressorCooling));
  TEST_ASSERT_EQUAL_UINT8(MHI_ACTION_IDLE, mhi_hvac_action(0x80 | kCool | kOn, 0xf9 & ~kCompressorCooling));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_unit_that_is_off_is_off_whatever_the_outdoor_unit_says);
  RUN_TEST(test_cool_follows_the_compressor);
  RUN_TEST(test_heat_follows_the_compressor);
  RUN_TEST(test_dry_follows_the_compressor);
  RUN_TEST(test_fan_mode_is_fan_even_if_the_compressor_bit_is_set);
  RUN_TEST(test_auto_takes_heating_or_cooling_from_the_outdoor_unit);
  RUN_TEST(test_cool_stays_cooling_when_the_heat_bit_is_set);
  RUN_TEST(test_an_undocumented_mode_is_judged_like_auto);
  RUN_TEST(test_vane_bits_and_other_db13_bits_do_not_matter);
  return UNITY_END();
}
