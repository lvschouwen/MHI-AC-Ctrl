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

// Fork #25 C3: a room sensor's value sent over set/Troom rounds to the nearest
// quarter degree; 23.93 truncated to 23.75.
static void test_rounded_encoding_goes_to_the_nearest_quarter(void) {
  TEST_ASSERT_EQUAL_UINT8(157, mhi_troom_round_from_celsius(23.93f));  // 24.0
  TEST_ASSERT_EQUAL_UINT8(156, mhi_troom_round_from_celsius(23.8f));   // 23.75
  TEST_ASSERT_EQUAL_UINT8(146, mhi_troom_round_from_celsius(21.125f)); // half a step rounds up
  TEST_ASSERT_EQUAL_UINT8(145, mhi_troom_round_from_celsius(21.1f));
  TEST_ASSERT_EQUAL_UINT8(21, mhi_troom_round_from_celsius(-10.0f));
  TEST_ASSERT_EQUAL_UINT8(40, mhi_troom_round_from_celsius(-5.3f));    // -5.25
  TEST_ASSERT_EQUAL_UINT8(0, mhi_troom_round_from_celsius(-100.0f));
  TEST_ASSERT_EQUAL_UINT8(255, mhi_troom_round_from_celsius(100.0f));
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

static void test_sub_zero_ds18x20_readings_encode_normally(void) {
  // Previously anything below 0 degC returned 0, which the caller's plausible
  // check then dropped: the MQTT path accepted -10 degC but a sensor in an
  // unheated room could not report frost. Now both paths agree.
  TEST_ASSERT_EQUAL_UINT8(41, mhi_troom_from_ds18x20_raw(-5 * 128));
  TEST_ASSERT_EQUAL_UINT8(21, mhi_troom_from_ds18x20_raw(-10 * 128));
}

static void test_sub_zero_ds18x20_agrees_with_the_celsius_encoding(void) {
  for (int step = -39; step < 0; step++) {  // -9.75 degC .. -0.25 degC
    const float celsius = step * 0.25f;
    TEST_ASSERT_EQUAL_UINT8(mhi_troom_from_celsius(celsius),
                            mhi_troom_from_ds18x20_raw((int16_t)(step * 32)));
  }
}

static void test_ds18x20_encoding_truncates_the_same_way_below_zero(void) {
  // -9.9921875 degC. Truncating the raw value first would give 22; the float
  // path gives 21, and the two must not disagree.
  TEST_ASSERT_EQUAL_UINT8(21, mhi_troom_from_ds18x20_raw(-1279));
}

// --- the third copy of the plausible window, in byte form ------------------

static void test_troom_byte_window_matches_the_celsius_window(void) {
  TEST_ASSERT_FALSE(mhi_troom_byte_plausible(21));   // exactly -10 degC
  TEST_ASSERT_TRUE(mhi_troom_byte_plausible(22));
  TEST_ASSERT_TRUE(mhi_troom_byte_plausible(252));
  TEST_ASSERT_FALSE(mhi_troom_byte_plausible(253));  // exactly 48 degC

  for (int troom = 0; troom <= 255; troom++) {
    const float celsius = mhi_celsius_from_troom(troom);
    TEST_ASSERT_EQUAL_INT(mhi_troom_celsius_plausible(celsius),
                          mhi_troom_byte_plausible((uint8_t)troom));
  }
}

// set/Troom and set/Tsetpoint (sweep #33 F1): atof() read junk as 0 degC,
// which passed the plausibility window and was sent to the unit as the room.
static void test_parses_a_plain_number(void) {
  float c = -99.0f;
  TEST_ASSERT_TRUE(mhi_parse_celsius("21.5", &c));
  TEST_ASSERT_EQUAL_FLOAT(21.5f, c);
  TEST_ASSERT_TRUE(mhi_parse_celsius("-3", &c));
  TEST_ASSERT_EQUAL_FLOAT(-3.0f, c);
  TEST_ASSERT_TRUE(mhi_parse_celsius("0", &c));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, c);
  TEST_ASSERT_TRUE(mhi_parse_celsius("20.50", &c));
  TEST_ASSERT_EQUAL_FLOAT(20.5f, c);
}

static void test_surrounding_whitespace_is_allowed(void) {
  float c = -99.0f;
  TEST_ASSERT_TRUE(mhi_parse_celsius(" 21.5\n", &c));
  TEST_ASSERT_EQUAL_FLOAT(21.5f, c);
}

static void test_anything_but_a_number_is_refused(void) {
  const char* junk[] = {"", " ", "abc", "unavailable", "unknown", "21.5abc", "21,5",
                        "nan", "inf", "-inf", "1e40", "."};
  for (const char* s : junk) {
    float c = -99.0f;
    TEST_ASSERT_FALSE_MESSAGE(mhi_parse_celsius(s, &c), s);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(-99.0f, c, s);  // untouched
  }
  float c = -99.0f;
  TEST_ASSERT_FALSE(mhi_parse_celsius(NULL, &c));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_encodes_celsius_in_quarter_degree_steps);
  RUN_TEST(test_decodes_the_troom_byte_back_to_celsius);
  RUN_TEST(test_round_trips_every_quarter_degree_in_range);
  RUN_TEST(test_encoding_truncates_rather_than_rounds);
  RUN_TEST(test_rounded_encoding_goes_to_the_nearest_quarter);
  RUN_TEST(test_celsius_plausibility_window_is_exclusive);
  RUN_TEST(test_ds18x20_plausibility_window_is_inclusive);
  RUN_TEST(test_ds18x20_power_on_reset_value_is_rejected);
  RUN_TEST(test_converts_ds18x20_raw_to_the_troom_byte);
  RUN_TEST(test_ds18x20_agrees_with_the_celsius_encoding);
  RUN_TEST(test_sub_zero_ds18x20_readings_encode_normally);
  RUN_TEST(test_sub_zero_ds18x20_agrees_with_the_celsius_encoding);
  RUN_TEST(test_ds18x20_encoding_truncates_the_same_way_below_zero);
  RUN_TEST(test_troom_byte_window_matches_the_celsius_window);
  RUN_TEST(test_parses_a_plain_number);
  RUN_TEST(test_surrounding_whitespace_is_allowed);
  RUN_TEST(test_anything_but_a_number_is_refused);
  return UNITY_END();
}
