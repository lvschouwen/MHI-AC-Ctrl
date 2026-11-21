// Host tests for the window after a subscribe (issue #50).

#include <unity.h>

#include "mhi_subscribe_window.h"

void setUp(void) {}
void tearDown(void) {}

static const uint32_t W = 3000;

static void test_not_in_window_before_any_subscribe(void) {
  MhiSubscribeWindow g = {false, 0};
  TEST_ASSERT_FALSE(mhi_subscribe_window_in(&g, 0, W));
}

static void test_in_window_right_after_the_subscribe(void) {
  MhiSubscribeWindow g = {false, 0};
  mhi_subscribe_window_mark(&g, 10000);
  TEST_ASSERT_TRUE(mhi_subscribe_window_in(&g, 10000, W));
  TEST_ASSERT_TRUE(mhi_subscribe_window_in(&g, 12999, W));
}

static void test_out_of_window_afterwards(void) {
  MhiSubscribeWindow g = {false, 0};
  mhi_subscribe_window_mark(&g, 10000);
  TEST_ASSERT_FALSE(mhi_subscribe_window_in(&g, 13000, W));
  TEST_ASSERT_FALSE(mhi_subscribe_window_in(&g, 600000, W));
}

static void test_a_new_subscribe_reopens_the_window(void) {
  MhiSubscribeWindow g = {false, 0};
  mhi_subscribe_window_mark(&g, 10000);
  mhi_subscribe_window_mark(&g, 50000);
  TEST_ASSERT_TRUE(mhi_subscribe_window_in(&g, 51000, W));
  TEST_ASSERT_FALSE(mhi_subscribe_window_in(&g, 53000, W));
}

static void test_window_survives_the_millis_rollover(void) {
  MhiSubscribeWindow g = {false, 0};
  mhi_subscribe_window_mark(&g, 0xFFFFFF00u);
  TEST_ASSERT_TRUE(mhi_subscribe_window_in(&g, 0xFFFFFF00u + 100, W));
  TEST_ASSERT_TRUE(mhi_subscribe_window_in(&g, 1000, W));  // wrapped, 1256 ms in
  TEST_ASSERT_FALSE(mhi_subscribe_window_in(&g, 3000, W));   // 3256 ms in
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_not_in_window_before_any_subscribe);
  RUN_TEST(test_in_window_right_after_the_subscribe);
  RUN_TEST(test_out_of_window_afterwards);
  RUN_TEST(test_a_new_subscribe_reopens_the_window);
  RUN_TEST(test_window_survives_the_millis_rollover);
  return UNITY_END();
}
