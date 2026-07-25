// Host tests for the pure room-temperature conversions.
//
// The AC encodes Troom in 0.25 degC steps offset by 61, and the same two lines
// of arithmetic were repeated in main.cpp and support.cpp. These tests pin the
// existing behaviour exactly, including the two plausibility windows, which
// differ from each other and are deliberately left that way in this phase.

#include <unity.h>

#include "mhi_temp.h"

void setUp(void) {}
void tearDown(void) {}

// --- degC <-> MHI Troom byte ----------------------------------------------

static void test_encodes_celsius_in_quarter_degree_steps(void) {
  TEST_ASSERT_EQUAL_UINT8(61, mhi_troom_from_celsius(0.0f));
  TEST_ASSERT_EQUAL_UINT8(62, mhi_troom_from_celsius(0.25f));
  TEST_ASSERT_EQUAL_UINT8(145, mhi_troom_from_celsius(21.0f));
  TEST_ASSERT_EQUAL_UINT8(41, mhi_troom_from_celsius(-5.0f));
}

static void test_decodes_the_troom_byte_back_to_celsius(void) {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, mhi_celsius_from_troom(61));
  TEST_ASSERT_EQUAL_FLOAT(0.25f, mhi_celsius_from_troom(62));
  TEST_ASSERT_EQUAL_FLOAT(21.0f, mhi_celsius_from_troom(145));
  TEST_ASSERT_EQUAL_FLOAT(-5.0f, mhi_celsius_from_troom(41));
}

static void test_round_trips_every_quarter_degree_in_range(void) {
  for (int step = -39; step <= 191; step++) {  // -9.75 degC .. 47.75 degC
    const float celsius = step * 0.25f;
    TEST_ASSERT_EQUAL_FLOAT(
        celsius, mhi_celsius_from_troom(mhi_troom_from_celsius(celsius)));
  }
}

static void test_encoding_truncates_rather_than_rounds(void) {
  // 21.1 degC -> 145.4 -> 145. Preserved from the original (byte)(f*4+61).
  TEST_ASSERT_EQUAL_UINT8(145, mhi_troom_from_celsius(21.1f));
  TEST_ASSERT_EQUAL_UINT8(145, mhi_troom_from_celsius(21.24f));
  TEST_ASSERT_EQUAL_UINT8(146, mhi_troom_from_celsius(21.25f));
}

// --- plausibility windows --------------------------------------------------

static void test_celsius_plausibility_window_is_exclusive(void) {
  // From the MQTT Troom handler: (f > -10) & (f < 48)
  TEST_ASSERT_TRUE(mhi_troom_celsius_plausible(21.0f));
  TEST_ASSERT_TRUE(mhi_troom_celsius_plausible(-9.9f));
  TEST_ASSERT_TRUE(mhi_troom_celsius_plausible(47.9f));
  TEST_ASSERT_FALSE(mhi_troom_celsius_plausible(-10.0f));
  TEST_ASSERT_FALSE(mhi_troom_celsius_plausible(48.0f));
  TEST_ASSERT_FALSE(mhi_troom_celsius_plausible(85.0f));  // DS18x20 reset value
}

static void test_ds18x20_plausibility_window_is_inclusive(void) {
  // From the DS18x20 handler: reject if raw > 48*128 or raw < -10*128.
  // The bound differs from the degC window above; that is existing behaviour.
  TEST_ASSERT_TRUE(mhi_ds18x20_raw_plausible(48 * 128));
  TEST_ASSERT_TRUE(mhi_ds18x20_raw_plausible(-10 * 128));
  TEST_ASSERT_FALSE(mhi_ds18x20_raw_plausible(48 * 128 + 1));
  TEST_ASSERT_FALSE(mhi_ds18x20_raw_plausible(-10 * 128 - 1));
}

static void test_ds18x20_power_on_reset_value_is_rejected(void) {
  // A DS18x20 that has not completed a conversion reports 85 degC.
  TEST_ASSERT_FALSE(mhi_ds18x20_raw_plausible(85 * 128));
}

// --- DS18x20 raw -> MHI Troom byte -----------------------------------------

static void test_converts_ds18x20_raw_to_the_troom_byte(void) {
  TEST_ASSERT_EQUAL_UINT8(145, mhi_troom_from_ds18x20_raw(21 * 128));
  TEST_ASSERT_EQUAL_UINT8(61, mhi_troom_from_ds18x20_raw(0));
  TEST_ASSERT_EQUAL_UINT8(62, mhi_troom_from_ds18x20_raw(32));  // 0.25 degC
}

static void test_ds18x20_agrees_with_the_celsius_encoding(void) {
  for (int step = 0; step <= 191; step++) {  // 0 degC .. 47.75 degC
    const float celsius = step * 0.25f;
    TEST_ASSERT_EQUAL_UINT8(mhi_troom_from_celsius(celsius),
                            mhi_troom_from_ds18x20_raw((int16_t)(step * 32)));
  }
}

static void test_negative_ds18x20_readings_report_zero(void) {
  // Preserved from the original: below freezing the sensor path gives up
  // rather than encoding a negative Troom.
  TEST_ASSERT_EQUAL_UINT8(0, mhi_troom_from_ds18x20_raw(-1));
  TEST_ASSERT_EQUAL_UINT8(0, mhi_troom_from_ds18x20_raw(-5 * 128));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_encodes_celsius_in_quarter_degree_steps);
  RUN_TEST(test_decodes_the_troom_byte_back_to_celsius);
  RUN_TEST(test_round_trips_every_quarter_degree_in_range);
  RUN_TEST(test_encoding_truncates_rather_than_rounds);
  RUN_TEST(test_celsius_plausibility_window_is_exclusive);
  RUN_TEST(test_ds18x20_plausibility_window_is_inclusive);
  RUN_TEST(test_ds18x20_power_on_reset_value_is_rejected);
  RUN_TEST(test_converts_ds18x20_raw_to_the_troom_byte);
  RUN_TEST(test_ds18x20_agrees_with_the_celsius_encoding);
  RUN_TEST(test_negative_ds18x20_readings_report_zero);
  return UNITY_END();
}
