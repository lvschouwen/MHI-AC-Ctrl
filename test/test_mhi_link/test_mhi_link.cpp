// Host tests for link supervision (issue #10, A1 and A2; issue #15).
//
// WIFI_LOST and MQTT_LOST were published on every MQTT connect but nothing
// ever incremented them, and the Wi-Fi scan state had no way out when the
// SDK refused to start a scan. The decisions behind both fixes are pure
// arithmetic, so they live in lib/mhi_pure and are pinned here. MQTT uses the
// edge detector; Wi-Fi asks the state machine, which already knows whether it
// believed the link was up, so a deliberate roam is not counted.

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

// --- Wi-Fi link loss, judged from the state machine -------------------------

static void test_wifi_is_lost_when_it_was_believed_up_and_is_not_connected(void) {
  TEST_ASSERT_TRUE(mhi_wifi_link_lost(true, false));
}

static void test_wifi_is_not_lost_while_still_connected(void) {
  TEST_ASSERT_FALSE(mhi_wifi_link_lost(true, true));  // the periodic rescan
}

static void test_wifi_is_not_lost_while_connecting_or_roaming(void) {
  // Not believed up: a boot-time connect attempt, or a roam to a stronger AP.
  TEST_ASSERT_FALSE(mhi_wifi_link_lost(false, false));
  TEST_ASSERT_FALSE(mhi_wifi_link_lost(false, true));
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

// --- MQTT reconnect pacing (issue #15) --------------------------------------

static void test_the_first_attempt_after_a_reset_is_immediate(void) {
  MhiRetryPacer pacer;
  mhi_retry_reset(&pacer);
  TEST_ASSERT_TRUE(mhi_retry_due(&pacer, 123456, 5000));
}

static void test_attempts_inside_the_interval_are_held_back(void) {
  MhiRetryPacer pacer;
  mhi_retry_reset(&pacer);
  mhi_retry_due(&pacer, 1000, 5000);
  TEST_ASSERT_FALSE(mhi_retry_due(&pacer, 1001, 5000));
  TEST_ASSERT_FALSE(mhi_retry_due(&pacer, 5999, 5000));
}

static void test_the_next_attempt_is_due_once_the_interval_has_passed(void) {
  MhiRetryPacer pacer;
  mhi_retry_reset(&pacer);
  mhi_retry_due(&pacer, 1000, 5000);
  TEST_ASSERT_TRUE(mhi_retry_due(&pacer, 6000, 5000));
  TEST_ASSERT_FALSE(mhi_retry_due(&pacer, 6001, 5000));  // measured from the last attempt
  TEST_ASSERT_TRUE(mhi_retry_due(&pacer, 11000, 5000));
}

static void test_pacing_survives_the_millis_wrap(void) {
  MhiRetryPacer pacer;
  mhi_retry_reset(&pacer);
  mhi_retry_due(&pacer, 0xFFFFF000u, 5000);             // 4096 ms before the wrap
  TEST_ASSERT_FALSE(mhi_retry_due(&pacer, 0x00000000u, 5000));
  TEST_ASSERT_TRUE(mhi_retry_due(&pacer, 904, 5000));  // 4096 + 904 = 5000
}

static void test_a_reset_while_connected_makes_the_next_drop_try_at_once(void) {
  MhiRetryPacer pacer;
  mhi_retry_reset(&pacer);
  mhi_retry_due(&pacer, 1000, 5000);
  mhi_retry_reset(&pacer);  // the broker came back
  TEST_ASSERT_TRUE(mhi_retry_due(&pacer, 1500, 5000));
}

static void test_a_broker_outage_under_50_s_does_not_reach_the_wifi_reset(void) {
  // MQTTreconnect() resets Wi-Fi on the 11th failed attempt in a row. Tried on
  // every loop() pass, a broker that refuses at once got there within
  // milliseconds. Paced at 5 s, the 11th attempt comes 50 s into the outage.
  MhiRetryPacer pacer;
  mhi_retry_reset(&pacer);
  int attempts = 0;
  for (uint32_t now = 0; now < 50000; now += 10)
    if (mhi_retry_due(&pacer, now, 5000)) attempts++;
  TEST_ASSERT_EQUAL_INT(10, attempts);
  TEST_ASSERT_TRUE(mhi_retry_due(&pacer, 50000, 5000));  // the 11th
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_link_that_was_never_up_has_not_dropped);
  RUN_TEST(test_a_link_that_stays_up_has_not_dropped);
  RUN_TEST(test_an_up_to_down_edge_counts_once);
  RUN_TEST(test_every_new_outage_counts_again);
  RUN_TEST(test_wifi_is_lost_when_it_was_believed_up_and_is_not_connected);
  RUN_TEST(test_wifi_is_not_lost_while_still_connected);
  RUN_TEST(test_wifi_is_not_lost_while_connecting_or_roaming);
  RUN_TEST(test_a_scan_the_sdk_refused_is_given_up_at_once);
  RUN_TEST(test_a_running_scan_is_left_alone_within_the_deadline);
  RUN_TEST(test_a_running_scan_is_given_up_after_the_deadline);
  RUN_TEST(test_a_finished_scan_belongs_to_its_callback);
  RUN_TEST(test_the_first_attempt_after_a_reset_is_immediate);
  RUN_TEST(test_attempts_inside_the_interval_are_held_back);
  RUN_TEST(test_the_next_attempt_is_due_once_the_interval_has_passed);
  RUN_TEST(test_pacing_survives_the_millis_wrap);
  RUN_TEST(test_a_reset_while_connected_makes_the_next_drop_try_at_once);
  RUN_TEST(test_a_broker_outage_under_50_s_does_not_reach_the_wifi_reset);
  return UNITY_END();
}
