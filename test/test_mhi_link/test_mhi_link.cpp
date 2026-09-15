// Host tests for link supervision (issue #10, A1 and A2).
//
// WIFI_LOST and MQTT_LOST were published on every MQTT connect but nothing
// ever incremented them, and the Wi-Fi scan state had no way out when the
// SDK refused to start a scan. The decisions behind both fixes are pure
// arithmetic, so they live in lib/mhi_pure and are pinned here.

#include <unity.h>

#include "mhi_link.h"

void setUp(void) {}
void tearDown(void) {}

// --- link-loss edge counting ----------------------------------------------

static void test_a_link_that_was_never_up_has_not_dropped(void) {
  bool was_up = false;
  TEST_ASSERT_FALSE(mhi_link_dropped(&was_up, false));
  TEST_ASSERT_FALSE(was_up);
}

static void test_a_link_that_stays_up_has_not_dropped(void) {
  bool was_up = false;
  TEST_ASSERT_FALSE(mhi_link_dropped(&was_up, true));
  TEST_ASSERT_FALSE(mhi_link_dropped(&was_up, true));
  TEST_ASSERT_TRUE(was_up);
}

static void test_an_up_to_down_edge_counts_once(void) {
  bool was_up = false;
  mhi_link_dropped(&was_up, true);
  TEST_ASSERT_TRUE(mhi_link_dropped(&was_up, false));
  TEST_ASSERT_FALSE(mhi_link_dropped(&was_up, false));  // still down, no new drop
  TEST_ASSERT_FALSE(was_up);
}

static void test_every_new_outage_counts_again(void) {
  bool was_up = false;
  mhi_link_dropped(&was_up, true);
  TEST_ASSERT_TRUE(mhi_link_dropped(&was_up, false));
  TEST_ASSERT_FALSE(mhi_link_dropped(&was_up, true));  // recovery is not a drop
  TEST_ASSERT_TRUE(mhi_link_dropped(&was_up, false));
}

// --- Wi-Fi scan supervision -------------------------------------------------

static void test_a_scan_the_sdk_refused_is_given_up_at_once(void) {
  TEST_ASSERT_TRUE(mhi_scan_gave_up(MHI_SCAN_FAILED, 0, 30000));
}

static void test_a_running_scan_is_left_alone_within_the_deadline(void) {
  TEST_ASSERT_FALSE(mhi_scan_gave_up(MHI_SCAN_RUNNING, 0, 30000));
  TEST_ASSERT_FALSE(mhi_scan_gave_up(MHI_SCAN_RUNNING, 30000, 30000));
}

static void test_a_running_scan_is_given_up_after_the_deadline(void) {
  TEST_ASSERT_TRUE(mhi_scan_gave_up(MHI_SCAN_RUNNING, 30001, 30000));
}

static void test_a_finished_scan_belongs_to_its_callback(void) {
  // A count of 0 or more means the scan completed; the completion callback
  // moves the state on, so the supervisor must not.
  TEST_ASSERT_FALSE(mhi_scan_gave_up(0, 60000, 30000));
  TEST_ASSERT_FALSE(mhi_scan_gave_up(3, 60000, 30000));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_link_that_was_never_up_has_not_dropped);
  RUN_TEST(test_a_link_that_stays_up_has_not_dropped);
  RUN_TEST(test_an_up_to_down_edge_counts_once);
  RUN_TEST(test_every_new_outage_counts_again);
  RUN_TEST(test_a_scan_the_sdk_refused_is_given_up_at_once);
  RUN_TEST(test_a_running_scan_is_left_alone_within_the_deadline);
  RUN_TEST(test_a_running_scan_is_given_up_after_the_deadline);
  RUN_TEST(test_a_finished_scan_belongs_to_its_callback);
  return UNITY_END();
}
