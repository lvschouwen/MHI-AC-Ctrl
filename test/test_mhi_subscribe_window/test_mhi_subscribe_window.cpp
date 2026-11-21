// Host tests for the window after a subscribe (issues #50, #67).

#include <unity.h>

#include "mhi_subscribe_window.h"

void setUp(void) {}
void tearDown(void) {}

static const uint32_t kMin = 3000;
static const uint32_t kQuiet = 1000;
static const uint32_t kCap = 30000;

static bool hit(MhiSubscribeWindow* w, uint32_t now) {
  return mhi_subscribe_window_hit(w, now, kQuiet, kCap);
}

static void test_closed_before_any_subscribe(void) {
  MhiSubscribeWindow w = {false, 0, 0};
  TEST_ASSERT_FALSE(hit(&w, 0));
}

static void test_inside_the_minimum_a_message_is_in_the_window(void) {
  MhiSubscribeWindow w = {false, 0, 0};
  mhi_subscribe_window_mark(&w, 10000, kMin);
  TEST_ASSERT_TRUE(hit(&w, 10000));
  TEST_ASSERT_TRUE(hit(&w, 12999));
}

static void test_a_live_message_after_the_quiet_gap_is_accepted(void) {
  MhiSubscribeWindow w = {false, 0, 0};
  mhi_subscribe_window_mark(&w, 10000, kMin);
  TEST_ASSERT_FALSE(hit(&w, 13000));
  TEST_ASSERT_FALSE(hit(&w, 14000));
}

// Retained messages spread over longer than the minimum stay dropped as long
// as each follows the previous within the quiet gap.
static void test_a_slow_burst_stays_inside_the_window(void) {
  MhiSubscribeWindow w = {false, 0, 0};
  mhi_subscribe_window_mark(&w, 0, kMin);
  for (uint32_t t = 800; t <= 9600; t += 800) TEST_ASSERT_TRUE(hit(&w, t));
  TEST_ASSERT_TRUE(hit(&w, 10400));
  // A gap longer than the quiet time after the last one: a live press.
  TEST_ASSERT_FALSE(hit(&w, 10400 + kQuiet));
}

static void test_the_cap_ends_an_endless_burst(void) {
  MhiSubscribeWindow w = {false, 0, 0};
  mhi_subscribe_window_mark(&w, 0, kMin);
  bool in = true;
  for (uint32_t t = 500; t < kCap; t += 500) in = hit(&w, t);
  TEST_ASSERT_TRUE(in);
  TEST_ASSERT_FALSE(hit(&w, kCap));
}

static void test_it_latches_closed_until_the_next_subscribe(void) {
  MhiSubscribeWindow w = {false, 0, 0};
  mhi_subscribe_window_mark(&w, 0, kMin);
  TEST_ASSERT_FALSE(hit(&w, 5000));
  TEST_ASSERT_FALSE(hit(&w, 5100));  // would be inside a re-extended window; stays closed
  mhi_subscribe_window_mark(&w, 60000, kMin);
  TEST_ASSERT_TRUE(hit(&w, 60500));
}

static void test_a_tick_latches_it_without_any_message(void) {
  MhiSubscribeWindow w = {false, 0, 0};
  mhi_subscribe_window_mark(&w, 0, kMin);
  mhi_subscribe_window_tick(&w, kMin);
  TEST_ASSERT_FALSE(w.open);
  // 49.7 days on, the wrapped clock is back near the subscribe time; still closed.
  TEST_ASSERT_FALSE(hit(&w, 1000));
}

static void test_the_window_survives_the_millis_rollover(void) {
  MhiSubscribeWindow w = {false, 0, 0};
  mhi_subscribe_window_mark(&w, 0xFFFFFF00u, kMin);
  TEST_ASSERT_TRUE(hit(&w, 0xFFFFFF00u + 100));
  TEST_ASSERT_TRUE(hit(&w, 1000));   // wrapped, 1256 ms in (extends to 2256 + ...)
  TEST_ASSERT_FALSE(hit(&w, 3000));  // 3256 ms in, past min and the quiet gap
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_closed_before_any_subscribe);
  RUN_TEST(test_inside_the_minimum_a_message_is_in_the_window);
  RUN_TEST(test_a_live_message_after_the_quiet_gap_is_accepted);
  RUN_TEST(test_a_slow_burst_stays_inside_the_window);
  RUN_TEST(test_the_cap_ends_an_endless_burst);
  RUN_TEST(test_it_latches_closed_until_the_next_subscribe);
  RUN_TEST(test_a_tick_latches_it_without_any_message);
  RUN_TEST(test_the_window_survives_the_millis_rollover);
  return UNITY_END();
}
