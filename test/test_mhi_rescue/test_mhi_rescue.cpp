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
  return mhi_rescue_tick(r, link_up, client, now, kDown, kAp);
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

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_unit_that_never_joins_opens_the_ap_after_15_min);
  RUN_TEST(test_a_link_restarts_the_15_min);
  RUN_TEST(test_the_ap_closes_after_10_min_and_the_cycle_repeats);
  RUN_TEST(test_a_connected_station_keeps_the_ap_up);
  RUN_TEST(test_a_link_while_the_ap_is_up_closes_it);
  RUN_TEST(test_the_millis_wrap);
  return UNITY_END();
}
