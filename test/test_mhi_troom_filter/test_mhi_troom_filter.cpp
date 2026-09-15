// Host tests for the Troom publish filter (issue #10, A4).
//
// main.cpp kept the last published Troom byte in a function-static that
// nothing could reset, so after an MQTT reconnect every status was re-sent
// except Troom. The filter now lives here with an explicit reset. The
// threshold semantics are the existing ones: TROOM_FILTER_LIMIT 0.25 means a
// change has to exceed one 0.25 degC step, so a single step is held back.

#include <unity.h>

#include "mhi_troom_filter.h"

void setUp(void) {}
void tearDown(void) {}

static MhiTroomFilter fresh(void) {
  MhiTroomFilter f;
  mhi_troom_filter_reset(&f);
  return f;
}

static void test_the_first_value_after_a_reset_is_published(void) {
  MhiTroomFilter f = fresh();
  TEST_ASSERT_TRUE(mhi_troom_filter_pass(&f, 145, 0.25f));  // 21.00 degC
}

static void test_the_same_value_again_is_not_published(void) {
  MhiTroomFilter f = fresh();
  mhi_troom_filter_pass(&f, 145, 0.25f);
  TEST_ASSERT_FALSE(mhi_troom_filter_pass(&f, 145, 0.25f));
}

static void test_a_single_step_is_held_back_at_the_default_limit(void) {
  MhiTroomFilter f = fresh();
  mhi_troom_filter_pass(&f, 145, 0.25f);
  TEST_ASSERT_FALSE(mhi_troom_filter_pass(&f, 146, 0.25f));  // +0.25 degC
  TEST_ASSERT_FALSE(mhi_troom_filter_pass(&f, 144, 0.25f));  // -0.25 degC
}

static void test_two_steps_pass_at_the_default_limit(void) {
  MhiTroomFilter f = fresh();
  mhi_troom_filter_pass(&f, 145, 0.25f);
  TEST_ASSERT_TRUE(mhi_troom_filter_pass(&f, 147, 0.25f));  // +0.50 degC
}

static void test_a_zero_limit_publishes_every_step(void) {
  MhiTroomFilter f = fresh();
  mhi_troom_filter_pass(&f, 145, 0.0f);
  TEST_ASSERT_TRUE(mhi_troom_filter_pass(&f, 146, 0.0f));
}

static void test_a_held_back_step_does_not_move_the_reference(void) {
  // 145 published, 146 held, then 147: that is two steps from what was last
  // published, so it goes out. Comparing against 146 would swallow it.
  MhiTroomFilter f = fresh();
  mhi_troom_filter_pass(&f, 145, 0.25f);
  mhi_troom_filter_pass(&f, 146, 0.25f);
  TEST_ASSERT_TRUE(mhi_troom_filter_pass(&f, 147, 0.25f));
}

static void test_a_large_drop_passes(void) {
  MhiTroomFilter f = fresh();
  mhi_troom_filter_pass(&f, 200, 0.25f);
  TEST_ASSERT_TRUE(mhi_troom_filter_pass(&f, 100, 0.25f));
}

static void test_a_reset_lets_the_current_value_be_published_again(void) {
  // The MQTT reconnect case: the AC reports the same Troom, and it has to
  // reach the broker again.
  MhiTroomFilter f = fresh();
  mhi_troom_filter_pass(&f, 145, 0.25f);
  mhi_troom_filter_reset(&f);
  TEST_ASSERT_TRUE(mhi_troom_filter_pass(&f, 145, 0.25f));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_first_value_after_a_reset_is_published);
  RUN_TEST(test_the_same_value_again_is_not_published);
  RUN_TEST(test_a_single_step_is_held_back_at_the_default_limit);
  RUN_TEST(test_two_steps_pass_at_the_default_limit);
  RUN_TEST(test_a_zero_limit_publishes_every_step);
  RUN_TEST(test_a_held_back_step_does_not_move_the_reference);
  RUN_TEST(test_a_large_drop_passes);
  RUN_TEST(test_a_reset_lets_the_current_value_be_published_again);
  return UNITY_END();
}
