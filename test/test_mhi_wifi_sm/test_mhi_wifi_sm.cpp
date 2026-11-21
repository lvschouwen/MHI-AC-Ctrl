// Host tests for the Wi-Fi join decisions and the reconnect counter (fork #64).

#include <unity.h>

#include "mhi_wifi_sm.h"

void setUp(void) {}
void tearDown(void) {}

static const uint32_t kDeadline = 30000;

static MhiWifiStep decide(int status, bool link, int scan = 0, uint32_t scan_waited = 0, uint32_t waited = 0) {
  return mhi_wifi_decide(status, link, scan, scan_waited, kDeadline, waited);
}

static void test_a_joined_or_timed_out_unit_starts_a_scan(void) {
  TEST_ASSERT_EQUAL(MHI_WIFI_STEP_START_SCAN, decide(MHI_WIFI_OK, true));
  TEST_ASSERT_EQUAL(MHI_WIFI_STEP_START_SCAN, decide(MHI_WIFI_OK, false));
  TEST_ASSERT_EQUAL(MHI_WIFI_STEP_START_SCAN, decide(MHI_WIFI_TIMEOUT, false));
}

static void test_a_scan_that_is_running_is_left_alone(void) {
  TEST_ASSERT_EQUAL(MHI_WIFI_STEP_NONE, decide(MHI_WIFI_SCANNING, false, /*WIFI_SCAN_RUNNING*/ -1, 5000));
  TEST_ASSERT_EQUAL(MHI_WIFI_STEP_NONE, decide(MHI_WIFI_SCANNING, true, -1, kDeadline));  // at the deadline, not past it
}

// scanNetworksAsync() can refuse to start, and then the callback never comes:
// a failed scan, or one still running well past the deadline, connects without it.
static void test_a_scan_that_never_comes_connects_without_it(void) {
  TEST_ASSERT_EQUAL(MHI_WIFI_STEP_SCAN_GAVE_UP, decide(MHI_WIFI_SCANNING, false, /*WIFI_SCAN_FAILED*/ -2, 100));
  TEST_ASSERT_EQUAL(MHI_WIFI_STEP_SCAN_GAVE_UP, decide(MHI_WIFI_SCANNING, true, -1, kDeadline + 1));
}

static void test_a_finished_scan_for_a_stronger_ap_leaves_the_link_alone(void) {
  TEST_ASSERT_EQUAL(MHI_WIFI_STEP_SCAN_DONE, decide(MHI_WIFI_SCANNING_DONE, true));
}

static void test_a_join_attempt_succeeds_or_times_out_after_more_than_ten_seconds(void) {
  TEST_ASSERT_EQUAL(MHI_WIFI_STEP_CONNECTED, decide(MHI_WIFI_ONGOING, true, 0, 0, 0));
  TEST_ASSERT_EQUAL(MHI_WIFI_STEP_CONNECTED, decide(MHI_WIFI_ONGOING, true, 0, 0, 60000));  // up beats the timeout
  TEST_ASSERT_EQUAL(MHI_WIFI_STEP_NONE, decide(MHI_WIFI_ONGOING, false, 0, 0, 10000));      // exactly 10 s: still waiting
  TEST_ASSERT_EQUAL(MHI_WIFI_STEP_TIMED_OUT, decide(MHI_WIFI_ONGOING, false, 0, 0, 10001));
  TEST_ASSERT_EQUAL(MHI_WIFI_STEP_NONE, decide(MHI_WIFI_ONGOING, false, 0, 0, 0));
}

// The state numbers are support.h's; an unknown one does nothing.
static void test_an_unknown_state_does_nothing(void) {
  TEST_ASSERT_EQUAL(MHI_WIFI_STEP_NONE, decide(9, true));
}

// reconnect_trials++ > 9: the eleventh failed attempt in a row resets Wi-Fi.
static void test_the_eleventh_attempt_exceeds_the_trials(void) {
  int trials = 0;
  for (int i = 1; i <= 10; i++) TEST_ASSERT_FALSE_MESSAGE(mhi_reconnect_trials_exceeded(&trials), "attempts 1..10");
  TEST_ASSERT_EQUAL_INT(10, trials);
  TEST_ASSERT_TRUE(mhi_reconnect_trials_exceeded(&trials));
  TEST_ASSERT_EQUAL_INT(11, trials);  // the caller prints it and resets it
  trials = 0;
  TEST_ASSERT_FALSE(mhi_reconnect_trials_exceeded(&trials));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_joined_or_timed_out_unit_starts_a_scan);
  RUN_TEST(test_a_scan_that_is_running_is_left_alone);
  RUN_TEST(test_a_scan_that_never_comes_connects_without_it);
  RUN_TEST(test_a_finished_scan_for_a_stronger_ap_leaves_the_link_alone);
  RUN_TEST(test_a_join_attempt_succeeds_or_times_out_after_more_than_ten_seconds);
  RUN_TEST(test_an_unknown_state_does_nothing);
  RUN_TEST(test_the_eleventh_attempt_exceeds_the_trials);
  return UNITY_END();
}
