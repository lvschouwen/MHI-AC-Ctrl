// Host tests for LoopMaxMs / ConnectBurstMs (issue #51).

#include <unity.h>

#include "mhi_loop_gap.h"

void setUp(void) {}
void tearDown(void) {}

static MhiLoopGap fresh() {
  MhiLoopGap g;
  mhi_loop_gap_init(&g);
  return g;
}

static void test_the_first_call_has_no_gap(void) {
  MhiLoopGap g = fresh();
  mhi_loop_gap_begin(&g, 5000);
  TEST_ASSERT_EQUAL_UINT32(0, mhi_loop_gap_take_max(&g));
}

// Start to start: a pass is one frame period, so that is the healthy floor.
static void test_the_gap_is_start_to_start(void) {
  MhiLoopGap g = fresh();
  mhi_loop_gap_begin(&g, 1000);
  mhi_loop_gap_begin(&g, 1052);
  mhi_loop_gap_begin(&g, 1101);
  TEST_ASSERT_EQUAL_UINT32(52, mhi_loop_gap_take_max(&g));
}

// A stall inside the call (the status re-send after a reconnect publishes from
// the status callbacks, which run inside loop()) shows as a long start-to-start gap.
static void test_a_stall_inside_the_call_is_seen(void) {
  MhiLoopGap g = fresh();
  mhi_loop_gap_begin(&g, 0);
  mhi_loop_gap_begin(&g, 50);
  mhi_loop_gap_begin(&g, 950);  // the call that started at 50 took 900 ms
  mhi_loop_gap_begin(&g, 1000);
  TEST_ASSERT_EQUAL_UINT32(900, mhi_loop_gap_take_max(&g));
}

static void test_take_starts_a_new_period(void) {
  MhiLoopGap g = fresh();
  mhi_loop_gap_begin(&g, 0);
  mhi_loop_gap_begin(&g, 400);
  TEST_ASSERT_EQUAL_UINT32(400, mhi_loop_gap_take_max(&g));
  TEST_ASSERT_EQUAL_UINT32(0, mhi_loop_gap_take_max(&g));
  mhi_loop_gap_begin(&g, 430);  // measured from the call that started the new period
  TEST_ASSERT_EQUAL_UINT32(30, mhi_loop_gap_take_max(&g));
}

static void test_a_gap_survives_the_millis_wrap(void) {
  MhiLoopGap g = fresh();
  mhi_loop_gap_begin(&g, 0xFFFFFFF0u);
  mhi_loop_gap_begin(&g, 0x00000100u);  // 272 ms later, across the rollover
  TEST_ASSERT_EQUAL_UINT32(272, mhi_loop_gap_take_max(&g));
}

static void test_elapsed_ms_is_wrap_safe(void) {
  TEST_ASSERT_EQUAL_UINT32(10, mhi_elapsed_ms(100, 110));
  TEST_ASSERT_EQUAL_UINT32(0, mhi_elapsed_ms(100, 100));
  TEST_ASSERT_EQUAL_UINT32(20, mhi_elapsed_ms(0xFFFFFFF8u, 12));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_first_call_has_no_gap);
  RUN_TEST(test_the_gap_is_start_to_start);
  RUN_TEST(test_a_stall_inside_the_call_is_seen);
  RUN_TEST(test_take_starts_a_new_period);
  RUN_TEST(test_a_gap_survives_the_millis_wrap);
  RUN_TEST(test_elapsed_ms_is_wrap_safe);
  return UNITY_END();
}
