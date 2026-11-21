// Host tests for the rescue access point (fork #28).
//
// After 15 min without a Wi-Fi link the unit opens its own WPA2 access point,
// OTA only, for 10 min, then tries the normal join again; it repeats while
// there is no link. A station connected to the access point keeps it up.

#include <unity.h>

#include "mhi_rescue.h"

void setUp(void) {}
void tearDown(void) {}

static const uint32_t kDown = 15u * 60u * 1000u;
static const uint32_t kAp = 10u * 60u * 1000u;

static MhiRescueAction tick(MhiRescue* r, bool link_up, bool client, uint32_t now) {
  return mhi_rescue_tick(r, link_up, client, now, kDown, kAp, kDown);  // max == down: no back off
}

static const uint32_t kCap = 8u * kDown;

static MhiRescueAction tick_bo(MhiRescue* r, bool link_up, bool client, uint32_t now) {
  return mhi_rescue_tick(r, link_up, client, now, kDown, kAp, kCap);
}

static void test_a_unit_that_never_joins_opens_the_ap_after_15_min(void) {
  MhiRescue r;
  mhi_rescue_init(&r, 0);
  TEST_ASSERT_EQUAL(MHI_RESCUE_NONE, tick(&r, false, false, kDown - 1));
  TEST_ASSERT_FALSE(r.ap_up);
  TEST_ASSERT_EQUAL(MHI_RESCUE_START_AP, tick(&r, false, false, kDown));
  TEST_ASSERT_TRUE(r.ap_up);
  TEST_ASSERT_EQUAL(MHI_RESCUE_NONE, tick(&r, false, false, kDown + 1));
}

static void test_a_link_restarts_the_15_min(void) {
  MhiRescue r;
  mhi_rescue_init(&r, 0);
  tick(&r, false, false, kDown - 1000);
  tick(&r, true, false, kDown - 500);  // joined just in time
  TEST_ASSERT_EQUAL(MHI_RESCUE_NONE, tick(&r, false, false, kDown));
  TEST_ASSERT_EQUAL(MHI_RESCUE_NONE, tick(&r, false, false, 2 * kDown - 501));
  TEST_ASSERT_EQUAL(MHI_RESCUE_START_AP, tick(&r, false, false, 2 * kDown - 500));
}

static void test_the_ap_closes_after_10_min_and_the_cycle_repeats(void) {
  MhiRescue r;
  mhi_rescue_init(&r, 0);
  tick(&r, false, false, kDown);
  TEST_ASSERT_EQUAL(MHI_RESCUE_NONE, tick(&r, false, false, kDown + kAp - 1));
  TEST_ASSERT_EQUAL(MHI_RESCUE_STOP_AP, tick(&r, false, false, kDown + kAp));
  TEST_ASSERT_FALSE(r.ap_up);
  // Another 15 min of trying to join, then the access point again.
  TEST_ASSERT_EQUAL(MHI_RESCUE_NONE, tick(&r, false, false, 2 * kDown + kAp - 1));
  TEST_ASSERT_EQUAL(MHI_RESCUE_START_AP, tick(&r, false, false, 2 * kDown + kAp));
}

static void test_a_connected_station_keeps_the_ap_up(void) {
  MhiRescue r;
  mhi_rescue_init(&r, 0);
  tick(&r, false, false, kDown);
  TEST_ASSERT_EQUAL(MHI_RESCUE_NONE, tick(&r, false, true, kDown + kAp));
  TEST_ASSERT_EQUAL(MHI_RESCUE_NONE, tick(&r, false, true, kDown + 3 * kAp));
  // The station leaves: the 10 min count from the last pass that saw it.
  TEST_ASSERT_EQUAL(MHI_RESCUE_NONE, tick(&r, false, false, kDown + 3 * kAp + 1));
  TEST_ASSERT_EQUAL(MHI_RESCUE_NONE, tick(&r, false, false, kDown + 4 * kAp - 1));
  TEST_ASSERT_EQUAL(MHI_RESCUE_STOP_AP, tick(&r, false, false, kDown + 4 * kAp));
}

// The station is off while the access point is up, so link_up cannot be true
// then; if it is anyway, the access point closes and the unit is back.
static void test_a_link_while_the_ap_is_up_closes_it(void) {
  MhiRescue r;
  mhi_rescue_init(&r, 0);
  tick(&r, false, false, kDown);
  TEST_ASSERT_EQUAL(MHI_RESCUE_STOP_AP, tick(&r, true, false, kDown + 1000));
  TEST_ASSERT_FALSE(r.ap_up);
  TEST_ASSERT_EQUAL(MHI_RESCUE_NONE, tick(&r, false, false, 2 * kDown + 999));
}

static void test_the_millis_wrap(void) {
  MhiRescue r;
  const uint32_t start = UINT32_MAX - 1000;
  mhi_rescue_init(&r, start);
  TEST_ASSERT_EQUAL(MHI_RESCUE_NONE, tick(&r, false, false, start + kDown - 1));
  TEST_ASSERT_EQUAL(MHI_RESCUE_START_AP, tick(&r, false, false, start + kDown));
  TEST_ASSERT_EQUAL(MHI_RESCUE_STOP_AP, tick(&r, false, false, start + kDown + kAp));
}

// Fork #58. Each window that ends with no link doubles the wait, up to the cap.
static void test_the_wait_doubles_per_window_up_to_the_cap(void) {
  MhiRescue r;
  mhi_rescue_init(&r, 0);
  uint32_t now = 0;
  uint32_t wait = kDown;
  for (int w = 0; w < 7; w++) {
    TEST_ASSERT_EQUAL(MHI_RESCUE_NONE, tick_bo(&r, false, false, now + wait - 1));
    TEST_ASSERT_EQUAL(MHI_RESCUE_START_AP, tick_bo(&r, false, false, now + wait));
    now += wait + kAp;
    TEST_ASSERT_EQUAL(MHI_RESCUE_STOP_AP, tick_bo(&r, false, false, now));
    if (wait < kCap) wait *= 2;
  }
  TEST_ASSERT_EQUAL_UINT32(kCap, wait);  // 15, 30, 60, 120 min, then held at the 8x cap
}

static void test_the_first_window_is_never_later_than_without_back_off(void) {
  MhiRescue r;
  mhi_rescue_init(&r, 0);
  TEST_ASSERT_EQUAL(MHI_RESCUE_NONE, tick_bo(&r, false, false, kDown - 1));
  TEST_ASSERT_EQUAL(MHI_RESCUE_START_AP, tick_bo(&r, false, false, kDown));
}

static void test_a_link_resets_the_back_off(void) {
  MhiRescue r;
  mhi_rescue_init(&r, 0);
  tick_bo(&r, false, false, kDown);              // window 1
  tick_bo(&r, false, false, kDown + kAp);        // closes
  tick_bo(&r, false, false, 3 * kDown + kAp);    // window 2 after 2x
  uint32_t now = 3 * kDown + 2 * kAp;
  tick_bo(&r, false, false, now);                // closes
  tick_bo(&r, true, false, now + 1000);          // joins
  // Next outage: first window again after the plain down time.
  TEST_ASSERT_EQUAL(MHI_RESCUE_NONE, tick_bo(&r, false, false, now + 1000 + kDown - 1));
  TEST_ASSERT_EQUAL(MHI_RESCUE_START_AP, tick_bo(&r, false, false, now + 1000 + kDown));
}

static void test_a_cap_below_down_means_no_back_off(void) {
  MhiRescue r;
  mhi_rescue_init(&r, 0);
  tick(&r, false, false, kDown);
  tick(&r, false, false, kDown + kAp);
  TEST_ASSERT_EQUAL(MHI_RESCUE_START_AP, tick(&r, false, false, 2 * kDown + kAp));
}

static void test_the_back_off_survives_the_millis_wrap(void) {
  MhiRescue r;
  const uint32_t start = UINT32_MAX - 1000;
  mhi_rescue_init(&r, start);
  tick_bo(&r, false, false, start + kDown);
  tick_bo(&r, false, false, start + kDown + kAp);
  const uint32_t closed = start + kDown + kAp;
  TEST_ASSERT_EQUAL(MHI_RESCUE_NONE, tick_bo(&r, false, false, closed + 2 * kDown - 1));
  TEST_ASSERT_EQUAL(MHI_RESCUE_START_AP, tick_bo(&r, false, false, closed + 2 * kDown));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_unit_that_never_joins_opens_the_ap_after_15_min);
  RUN_TEST(test_a_link_restarts_the_15_min);
  RUN_TEST(test_the_ap_closes_after_10_min_and_the_cycle_repeats);
  RUN_TEST(test_a_connected_station_keeps_the_ap_up);
  RUN_TEST(test_a_link_while_the_ap_is_up_closes_it);
  RUN_TEST(test_the_millis_wrap);
  RUN_TEST(test_the_wait_doubles_per_window_up_to_the_cap);
  RUN_TEST(test_the_first_window_is_never_later_than_without_back_off);
  RUN_TEST(test_a_link_resets_the_back_off);
  RUN_TEST(test_a_cap_below_down_means_no_back_off);
  RUN_TEST(test_the_back_off_survives_the_millis_wrap);
  return UNITY_END();
}
