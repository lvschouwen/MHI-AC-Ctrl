// Host tests for the uptime counter (issue #18).
//
// The Uptime topic must keep growing past the millis() wrap at 49.7 days. A
// plain millis()/1000 would read 0 again at that point and look like a
// reboot, which is exactly what the topic exists to make visible.

#include <unity.h>

#include "mhi_uptime.h"

void setUp(void) {}
void tearDown(void) {}

static void test_uptime_starts_at_zero(void) {
  MhiUptime u = {0, 0, 0};
  TEST_ASSERT_EQUAL_UINT32(0, mhi_uptime_advance(&u, 0));
}

static void test_uptime_counts_whole_seconds(void) {
  MhiUptime u = {0, 0, 0};
  TEST_ASSERT_EQUAL_UINT32(1, mhi_uptime_advance(&u, 1500));
  TEST_ASSERT_EQUAL_UINT32(3, mhi_uptime_advance(&u, 3000));
}

static void test_uptime_carries_the_sub_second_remainder(void) {
  // 999 ms, then 1 ms more: the remainders add up to a whole second.
  MhiUptime u = {0, 0, 0};
  TEST_ASSERT_EQUAL_UINT32(0, mhi_uptime_advance(&u, 999));
  TEST_ASSERT_EQUAL_UINT32(1, mhi_uptime_advance(&u, 1000));
  TEST_ASSERT_EQUAL_UINT32(1, mhi_uptime_advance(&u, 1999));
  TEST_ASSERT_EQUAL_UINT32(2, mhi_uptime_advance(&u, 2000));
}

static void test_uptime_survives_the_millis_wrap(void) {
  MhiUptime u = {0, 0, 0};
  TEST_ASSERT_EQUAL_UINT32(4294963, mhi_uptime_advance(&u, 0xFFFFF000u));  // .200 s left over
  TEST_ASSERT_EQUAL_UINT32(4294967, mhi_uptime_advance(&u, 0));            // 4096 ms later
  TEST_ASSERT_EQUAL_UINT32(4294968, mhi_uptime_advance(&u, 704));          // .296 + .704
}

static void test_advancing_without_elapsed_time_adds_nothing(void) {
  // The counter is advanced on every loop pass, most of them within the
  // same millisecond.
  MhiUptime u = {0, 0, 0};
  mhi_uptime_advance(&u, 5000);
  TEST_ASSERT_EQUAL_UINT32(5, mhi_uptime_advance(&u, 5000));
  TEST_ASSERT_EQUAL_UINT32(5, mhi_uptime_advance(&u, 5000));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_uptime_starts_at_zero);
  RUN_TEST(test_uptime_counts_whole_seconds);
  RUN_TEST(test_uptime_carries_the_sub_second_remainder);
  RUN_TEST(test_uptime_survives_the_millis_wrap);
  RUN_TEST(test_advancing_without_elapsed_time_adds_nothing);
  return UNITY_END();
}
