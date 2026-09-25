// Host tests for heating below 18 °C by a shifted room temperature (fork #30).

#include <math.h>
#include <unity.h>

#include "mhi_heat_shift.h"

void setUp(void) {}
void tearDown(void) {}

static MhiHeatShift s;

// A unit heating at 21 °C (DB2 42) whose DB2 the bus has reported.
static void heating_at_21(void) {
  mhi_heat_shift_init(&s);
  mhi_heat_shift_on_bus_db2(&s, 0x80 | 42);
}

static void test_a_target_below_18_in_heat_writes_18_and_shifts(void) {
  heating_at_21();
  uint8_t db2 = 32;
  TEST_ASSERT_FALSE(mhi_heat_shift_request(&s, 16.0f, true, &db2));
  TEST_ASSERT_EQUAL_UINT8(36, db2);
  TEST_ASSERT_TRUE(mhi_heat_shift_active(&s));
  // room 15: 15 + (18 - 16) + 2 = 19, what the unit regulates to 20 on.
  TEST_ASSERT_EQUAL_FLOAT(19.0f, mhi_heat_shift_troom(&s, 15.0f, 2.0f));
  TEST_ASSERT_EQUAL_FLOAT(20.0f, mhi_heat_shift_troom(&s, 16.0f, 2.0f));  // at target: the unit's own 18 + 2
  TEST_ASSERT_EQUAL_FLOAT(16.0f, mhi_heat_shift_setpoint(&s, 0x80 | 36));  // published: T
  TEST_ASSERT_EQUAL_FLOAT(21.0f, mhi_heat_shift_setpoint(&s, 0x80 | 42));  // until the echo: what the bus says
}

static void test_half_degrees_and_the_night_setback_floor(void) {
  heating_at_21();
  uint8_t db2 = 33;
  mhi_heat_shift_request(&s, 16.5f, true, &db2);
  TEST_ASSERT_EQUAL_UINT8(36, db2);
  TEST_ASSERT_EQUAL_FLOAT(20.5f, mhi_heat_shift_troom(&s, 17.0f, 2.0f));
  db2 = 20;
  mhi_heat_shift_request(&s, 10.0f, true, &db2);
  TEST_ASSERT_EQUAL_UINT8(36, db2);
  TEST_ASSERT_EQUAL_FLOAT(10.0f, mhi_heat_shift_setpoint(&s, 36));
}

static void test_18_or_more_writes_itself_and_ends_the_shift(void) {
  heating_at_21();
  uint8_t db2 = 32;
  mhi_heat_shift_request(&s, 16.0f, true, &db2);
  db2 = 36;
  TEST_ASSERT_TRUE(mhi_heat_shift_request(&s, 18.0f, true, &db2));  // ended: drop the external value
  TEST_ASSERT_EQUAL_UINT8(36, db2);
  TEST_ASSERT_FALSE(mhi_heat_shift_active(&s));
  TEST_ASSERT_EQUAL_FLOAT(15.0f, mhi_heat_shift_troom(&s, 15.0f, 2.0f));  // unshifted
  TEST_ASSERT_EQUAL_FLOAT(18.0f, mhi_heat_shift_setpoint(&s, 36));
  db2 = 44;
  TEST_ASSERT_FALSE(mhi_heat_shift_request(&s, 22.0f, true, &db2));  // nothing was active
  TEST_ASSERT_EQUAL_UINT8(44, db2);
}

static void test_outside_heat_nothing_shifts(void) {
  mhi_heat_shift_init(&s);
  uint8_t db2 = 40;
  TEST_ASSERT_FALSE(mhi_heat_shift_request(&s, 20.0f, false, &db2));
  TEST_ASSERT_EQUAL_UINT8(40, db2);
  TEST_ASSERT_FALSE(mhi_heat_shift_active(&s));
}

static void test_leaving_heat_ends_the_shift(void) {
  heating_at_21();
  uint8_t db2 = 32;
  mhi_heat_shift_request(&s, 16.0f, true, &db2);
  TEST_ASSERT_FALSE(mhi_heat_shift_on_mode(&s, true));  // still heat
  TEST_ASSERT_TRUE(mhi_heat_shift_active(&s));
  TEST_ASSERT_TRUE(mhi_heat_shift_on_mode(&s, false));
  TEST_ASSERT_FALSE(mhi_heat_shift_active(&s));
  TEST_ASSERT_FALSE(mhi_heat_shift_on_mode(&s, false));  // once
}

// The IR remote: DB2 moves away from 18 after the bus showed the 18 we wrote.
static void test_the_remote_ends_the_shift_only_after_the_echo(void) {
  heating_at_21();
  uint8_t db2 = 32;
  mhi_heat_shift_request(&s, 16.0f, true, &db2);
  // The old 21 again before the echo (a reconnect's re-send): not the remote.
  TEST_ASSERT_FALSE(mhi_heat_shift_on_bus_db2(&s, 0x80 | 42));
  TEST_ASSERT_TRUE(mhi_heat_shift_active(&s));
  TEST_ASSERT_FALSE(mhi_heat_shift_on_bus_db2(&s, 0x80 | 36));  // the echo
  TEST_ASSERT_FALSE(mhi_heat_shift_on_bus_db2(&s, 0x80 | 36));  // again: nothing
  TEST_ASSERT_TRUE(mhi_heat_shift_on_bus_db2(&s, 0x80 | 40));   // the remote: 20
  TEST_ASSERT_FALSE(mhi_heat_shift_active(&s));
  TEST_ASSERT_EQUAL_FLOAT(20.0f, mhi_heat_shift_setpoint(&s, 0x80 | 40));
}

// DB2 already at 18 when the request comes: no echo will arrive (the bus
// reports changes only), so the request counts as confirmed at once.
static void test_db2_already_18_confirms_at_once(void) {
  mhi_heat_shift_init(&s);
  mhi_heat_shift_on_bus_db2(&s, 0x80 | 36);
  uint8_t db2 = 34;
  mhi_heat_shift_request(&s, 17.0f, true, &db2);
  TEST_ASSERT_TRUE(mhi_heat_shift_on_bus_db2(&s, 0x80 | 20));  // the remote's night setback
  TEST_ASSERT_FALSE(mhi_heat_shift_active(&s));
}

// A new target while shifting moves it and stays confirmed: DB2 stays at 18.
static void test_a_new_target_below_18_moves_the_shift(void) {
  heating_at_21();
  uint8_t db2 = 32;
  mhi_heat_shift_request(&s, 16.0f, true, &db2);
  mhi_heat_shift_on_bus_db2(&s, 36);
  db2 = 30;
  TEST_ASSERT_FALSE(mhi_heat_shift_request(&s, 15.0f, true, &db2));
  TEST_ASSERT_EQUAL_FLOAT(15.0f, mhi_heat_shift_setpoint(&s, 36));
  TEST_ASSERT_TRUE(mhi_heat_shift_on_bus_db2(&s, 42));  // still confirmed: the remote is still seen
}

static void test_nothing_active_after_init(void) {
  mhi_heat_shift_init(&s);
  TEST_ASSERT_FALSE(mhi_heat_shift_active(&s));
  TEST_ASSERT_TRUE(isnan(s.target));
  TEST_ASSERT_FALSE(mhi_heat_shift_on_bus_db2(&s, 36));
  TEST_ASSERT_FALSE(mhi_heat_shift_on_mode(&s, false));
  TEST_ASSERT_EQUAL_FLOAT(18.0f, mhi_heat_shift_setpoint(&s, 0x80 | 36));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_target_below_18_in_heat_writes_18_and_shifts);
  RUN_TEST(test_half_degrees_and_the_night_setback_floor);
  RUN_TEST(test_18_or_more_writes_itself_and_ends_the_shift);
  RUN_TEST(test_outside_heat_nothing_shifts);
  RUN_TEST(test_leaving_heat_ends_the_shift);
  RUN_TEST(test_the_remote_ends_the_shift_only_after_the_echo);
  RUN_TEST(test_db2_already_18_confirms_at_once);
  RUN_TEST(test_a_new_target_below_18_moves_the_shift);
  RUN_TEST(test_nothing_active_after_init);
  return UNITY_END();
}
