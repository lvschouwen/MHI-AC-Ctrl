// Host tests for the setpoint limits per mode (fork #25 F3).
//
// Night setback on the IR remote is a plain heat setpoint of 10 °C on the bus
// (capture 25 Sep 2026), so heat accepts 10-30. Every other mode keeps 18-30:
// Home Assistant's climate has one min_temp for all modes.

#include <unity.h>

#include "mhi_setpoint.h"

void setUp(void) {}
void tearDown(void) {}

// DB0 mode bits.
static const uint8_t kAuto = 0x00, kDry = 0x04, kCool = 0x08, kFan = 0x0c, kHeat = 0x10;

static void test_heat_accepts_10_to_30(void) {
  TEST_ASSERT_TRUE(mhi_setpoint_allowed(10.0f, kHeat));
  TEST_ASSERT_TRUE(mhi_setpoint_allowed(10.5f, kHeat));
  TEST_ASSERT_TRUE(mhi_setpoint_allowed(17.5f, kHeat));
  TEST_ASSERT_TRUE(mhi_setpoint_allowed(30.0f, kHeat));
}

static void test_heat_refuses_outside_10_to_30(void) {
  TEST_ASSERT_FALSE(mhi_setpoint_allowed(9.5f, kHeat));
  TEST_ASSERT_FALSE(mhi_setpoint_allowed(30.5f, kHeat));
}

static void test_other_modes_accept_18_to_30_only(void) {
  const uint8_t modes[] = {kAuto, kDry, kCool, kFan};
  for (uint8_t m : modes) {
    TEST_ASSERT_TRUE(mhi_setpoint_allowed(18.0f, m));
    TEST_ASSERT_TRUE(mhi_setpoint_allowed(30.0f, m));
    TEST_ASSERT_FALSE(mhi_setpoint_allowed(17.5f, m));
    TEST_ASSERT_FALSE(mhi_setpoint_allowed(10.0f, m));
    TEST_ASSERT_FALSE(mhi_setpoint_allowed(30.5f, m));
  }
}

static void test_unknown_mode_is_not_heat(void) {
  TEST_ASSERT_FALSE(mhi_setpoint_allowed(10.0f, MHI_SETPOINT_MODE_UNKNOWN));
  TEST_ASSERT_TRUE(mhi_setpoint_allowed(18.0f, MHI_SETPOINT_MODE_UNKNOWN));
}

static void test_nan_is_refused(void) {
  // atof() of a non-number is 0, but a NaN must not pass either.
  TEST_ASSERT_FALSE(mhi_setpoint_allowed(0.0f / 0.0f, kHeat));
}

// DB2 carries the setpoint in half degrees: 0x14 = 10 °C, 0x24 = 18 °C.
static void test_leaving_heat_below_18_writes_18(void) {
  TEST_ASSERT_EQUAL_UINT8(36, mhi_setpoint_on_mode_change(kCool, 0x14));
  TEST_ASSERT_EQUAL_UINT8(36, mhi_setpoint_on_mode_change(kAuto, 35));
  TEST_ASSERT_EQUAL_UINT8(36, mhi_setpoint_on_mode_change(kFan, 0x14));
}

static void test_leaving_heat_at_18_or_more_writes_nothing(void) {
  TEST_ASSERT_EQUAL_UINT8(0, mhi_setpoint_on_mode_change(kCool, 36));
  TEST_ASSERT_EQUAL_UINT8(0, mhi_setpoint_on_mode_change(kDry, 60));
}

static void test_into_heat_writes_nothing(void) {
  TEST_ASSERT_EQUAL_UINT8(0, mhi_setpoint_on_mode_change(kHeat, 0x14));
}

static void test_unknown_setpoint_writes_nothing(void) {
  TEST_ASSERT_EQUAL_UINT8(0, mhi_setpoint_on_mode_change(kCool, MHI_SETPOINT_UNKNOWN));
}

static void test_the_bit_7_of_db2_is_ignored(void) {
  // The MISO write sets bit 7; a DB2 with it set still means the same setpoint.
  TEST_ASSERT_EQUAL_UINT8(36, mhi_setpoint_on_mode_change(kCool, 0x80 | 0x14));
}

// --- the guard main.cpp keeps: requests and the bus both update it ----------

static MhiSetpointGuard heat_at_20(void) {
  MhiSetpointGuard g;
  mhi_setpoint_guard_init(&g);
  mhi_setpoint_guard_on_bus_mode(&g, kHeat);
  mhi_setpoint_guard_on_bus_db2(&g, 40);
  return g;
}

// Review finding (25 Sep): set/Tsetpoint 10 in heat and then set/Mode cool
// within one frame cycle. The bus has not echoed 10 yet; the mode change must
// still see it, or cool goes out with 10 in the same frame.
static void test_a_requested_low_setpoint_is_corrected_by_a_mode_change_before_the_bus_echoes_it(void) {
  MhiSetpointGuard g = heat_at_20();
  uint8_t db2 = 0;
  TEST_ASSERT_TRUE(mhi_setpoint_guard_request(&g, 10.0f, &db2));
  TEST_ASSERT_EQUAL_UINT8(20, db2);
  TEST_ASSERT_EQUAL_UINT8(36, mhi_setpoint_guard_mode(&g, kCool));
  // And a later request in cool is judged by cool.
  TEST_ASSERT_FALSE(mhi_setpoint_guard_request(&g, 17.0f, &db2));
}

static void test_a_requested_mode_decides_the_next_setpoint_at_once(void) {
  MhiSetpointGuard g = heat_at_20();
  TEST_ASSERT_EQUAL_UINT8(0, mhi_setpoint_guard_mode(&g, kCool));  // 20 is fine in cool
  uint8_t db2 = 0;
  TEST_ASSERT_FALSE(mhi_setpoint_guard_request(&g, 10.0f, &db2));
  TEST_ASSERT_EQUAL_UINT8(0, mhi_setpoint_guard_mode(&g, kHeat));
  TEST_ASSERT_TRUE(mhi_setpoint_guard_request(&g, 10.0f, &db2));
}

static void test_a_refused_request_changes_nothing(void) {
  MhiSetpointGuard g = heat_at_20();
  uint8_t db2 = 99;
  TEST_ASSERT_FALSE(mhi_setpoint_guard_request(&g, 9.0f, &db2));
  TEST_ASSERT_EQUAL_UINT8(99, db2);
  TEST_ASSERT_EQUAL_UINT8(0, mhi_setpoint_guard_mode(&g, kCool));  // still 20
}

static void test_the_correction_counts_as_the_new_setpoint(void) {
  MhiSetpointGuard g = heat_at_20();
  uint8_t db2 = 0;
  mhi_setpoint_guard_request(&g, 12.0f, &db2);
  TEST_ASSERT_EQUAL_UINT8(36, mhi_setpoint_guard_mode(&g, kDry));
  TEST_ASSERT_EQUAL_UINT8(0, mhi_setpoint_guard_mode(&g, kCool));  // 18 already
}

static void test_the_remote_updates_the_guard_through_the_bus(void) {
  MhiSetpointGuard g;
  mhi_setpoint_guard_init(&g);
  uint8_t db2 = 0;
  TEST_ASSERT_FALSE(mhi_setpoint_guard_request(&g, 10.0f, &db2));  // mode unknown
  mhi_setpoint_guard_on_bus_mode(&g, kHeat);
  mhi_setpoint_guard_on_bus_db2(&g, 0x14);  // night setback from the remote
  TEST_ASSERT_EQUAL_UINT8(36, mhi_setpoint_guard_mode(&g, kFan));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_heat_accepts_10_to_30);
  RUN_TEST(test_heat_refuses_outside_10_to_30);
  RUN_TEST(test_other_modes_accept_18_to_30_only);
  RUN_TEST(test_unknown_mode_is_not_heat);
  RUN_TEST(test_nan_is_refused);
  RUN_TEST(test_leaving_heat_below_18_writes_18);
  RUN_TEST(test_leaving_heat_at_18_or_more_writes_nothing);
  RUN_TEST(test_into_heat_writes_nothing);
  RUN_TEST(test_unknown_setpoint_writes_nothing);
  RUN_TEST(test_the_bit_7_of_db2_is_ignored);
  RUN_TEST(test_a_requested_low_setpoint_is_corrected_by_a_mode_change_before_the_bus_echoes_it);
  RUN_TEST(test_a_requested_mode_decides_the_next_setpoint_at_once);
  RUN_TEST(test_a_refused_request_changes_nothing);
  RUN_TEST(test_the_correction_counts_as_the_new_setpoint);
  RUN_TEST(test_the_remote_updates_the_guard_through_the_bus);
  return UNITY_END();
}
