// Host tests for the frame-error/timeout counters (fork #21 F1; spec §4.1).
// err_msg is MHI-AC-Ctrl-core.h's ErrMsg as loop() returns it: 0 valid,
// -1 invalid signature, -2 invalid checksum, -3/-4 SCK timeouts. Passed as a
// plain int so this module need not include MHI-AC-Ctrl-core.h (Arduino.h).

#include <stdint.h>
#include <unity.h>

#include "mhi_frame_stats.h"

void setUp(void) {}
void tearDown(void) {}

static void test_a_valid_frame_counts_as_neither(void) {
  MhiFrameStats s = {0, 0};
  mhi_frame_stats_count(&s, 0);
  TEST_ASSERT_EQUAL_UINT32(0, s.errors);
  TEST_ASSERT_EQUAL_UINT32(0, s.timeouts);
}

static void test_a_bad_signature_or_checksum_counts_as_an_error(void) {
  MhiFrameStats s = {0, 0};
  mhi_frame_stats_count(&s, -1);
  mhi_frame_stats_count(&s, -2);
  TEST_ASSERT_EQUAL_UINT32(2, s.errors);
  TEST_ASSERT_EQUAL_UINT32(0, s.timeouts);
}

static void test_either_sck_timeout_counts_as_a_timeout(void) {
  MhiFrameStats s = {0, 0};
  mhi_frame_stats_count(&s, -3);
  mhi_frame_stats_count(&s, -4);
  TEST_ASSERT_EQUAL_UINT32(0, s.errors);
  TEST_ASSERT_EQUAL_UINT32(2, s.timeouts);
}

static void test_an_unrecognised_value_counts_as_neither(void) {
  MhiFrameStats s = {0, 0};
  mhi_frame_stats_count(&s, 42);
  TEST_ASSERT_EQUAL_UINT32(0, s.errors);
  TEST_ASSERT_EQUAL_UINT32(0, s.timeouts);
}

static void test_counts_saturate(void) {
  MhiFrameStats s = {UINT32_MAX, UINT32_MAX};
  mhi_frame_stats_count(&s, -1);
  mhi_frame_stats_count(&s, -3);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, s.errors);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, s.timeouts);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_valid_frame_counts_as_neither);
  RUN_TEST(test_a_bad_signature_or_checksum_counts_as_an_error);
  RUN_TEST(test_either_sck_timeout_counts_as_a_timeout);
  RUN_TEST(test_an_unrecognised_value_counts_as_neither);
  RUN_TEST(test_counts_saturate);
  return UNITY_END();
}
