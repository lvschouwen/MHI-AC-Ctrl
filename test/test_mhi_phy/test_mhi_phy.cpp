// Host tests for the Wi-Fi PHY-mode fallback (issue #17).
//
// Upstream #224: an ESP8266 stopped joining a router that had 802.11ax on
// 2.4 GHz, reporting a wrong password with the right one, until the PHY mode
// was forced to 11g. A setting cannot help a unit that is already off the
// network, so the firmware falls back on its own: the mode the link last had
// for the first period without a link, then the other mode, alternating every
// period. A router that rejects 11g gets the default back. A successful join
// restarts the clock, and the mode is never changed while the link is up.

#include <unity.h>

#include "mhi_phy.h"

static const uint32_t kPeriod = 300000;  // five minutes

void setUp(void) {}
void tearDown(void) {}

// --- the first period keeps the mode the link last had ----------------------

static void test_boot_tries_the_default_mode_first(void) {
  MhiPhyFallback f = {0, MHI_PHY_11N};
  TEST_ASSERT_EQUAL_UINT8(MHI_PHY_11N, mhi_phy_mode_for_join(&f, 0, kPeriod));
  TEST_ASSERT_EQUAL_UINT8(MHI_PHY_11N, mhi_phy_mode_for_join(&f, kPeriod - 1, kPeriod));
}

static void test_a_full_period_without_a_link_falls_back_to_11g(void) {
  MhiPhyFallback f = {0, MHI_PHY_11N};
  TEST_ASSERT_EQUAL_UINT8(MHI_PHY_11G, mhi_phy_mode_for_join(&f, kPeriod, kPeriod));
  TEST_ASSERT_EQUAL_UINT8(MHI_PHY_11G, mhi_phy_mode_for_join(&f, 2 * kPeriod - 1, kPeriod));
}

// --- the alternation rule ---------------------------------------------------

static void test_a_period_of_failures_in_11g_returns_to_the_default(void) {
  // A router that rejects 11g must not strand the unit in it.
  MhiPhyFallback f = {0, MHI_PHY_11N};
  TEST_ASSERT_EQUAL_UINT8(MHI_PHY_11N, mhi_phy_mode_for_join(&f, 2 * kPeriod, kPeriod));
  TEST_ASSERT_EQUAL_UINT8(MHI_PHY_11G, mhi_phy_mode_for_join(&f, 3 * kPeriod, kPeriod));
  TEST_ASSERT_EQUAL_UINT8(MHI_PHY_11N, mhi_phy_mode_for_join(&f, 4 * kPeriod, kPeriod));
}

// --- a successful join restarts the clock -----------------------------------

static void test_a_join_restarts_the_clock(void) {
  // Measured from boot, 700000 + kPeriod - 1 is an odd number of periods and
  // would give 11g; measured from the join it is not one period yet.
  MhiPhyFallback f = {0, MHI_PHY_11N};
  mhi_phy_link_up(&f, MHI_PHY_11N, 700000);
  TEST_ASSERT_EQUAL_UINT8(MHI_PHY_11N, mhi_phy_mode_for_join(&f, 700000 + kPeriod - 1, kPeriod));
  TEST_ASSERT_EQUAL_UINT8(MHI_PHY_11G, mhi_phy_mode_for_join(&f, 700000 + kPeriod, kPeriod));
}

static void test_the_mode_does_not_change_while_the_link_is_up(void) {
  // setupWiFi() reports the link on every pass; hours of link time must not
  // count as time without one.
  MhiPhyFallback f = {0, MHI_PHY_11N};
  mhi_phy_link_up(&f, MHI_PHY_11N, 0);
  mhi_phy_link_up(&f, MHI_PHY_11N, 10000000);
  TEST_ASSERT_EQUAL_UINT8(MHI_PHY_11N, mhi_phy_mode_for_join(&f, 10000001, kPeriod));
}

static void test_a_unit_that_joined_in_11g_tries_11g_first_after_a_drop(void) {
  // The #224 router: the unit must not spend a period on 11n after every
  // router reboot. The alternation still brings the default back later.
  MhiPhyFallback f = {0, MHI_PHY_11N};
  mhi_phy_link_up(&f, MHI_PHY_11G, 5000);
  TEST_ASSERT_EQUAL_UINT8(MHI_PHY_11G, mhi_phy_mode_for_join(&f, 5001, kPeriod));
  TEST_ASSERT_EQUAL_UINT8(MHI_PHY_11N, mhi_phy_mode_for_join(&f, 5000 + kPeriod, kPeriod));
}

static void test_the_fallback_survives_the_millis_wrap(void) {
  MhiPhyFallback f = {0, MHI_PHY_11N};
  mhi_phy_link_up(&f, MHI_PHY_11N, 0xFFFFF000u);         // 4096 ms before the wrap
  TEST_ASSERT_EQUAL_UINT8(MHI_PHY_11N, mhi_phy_mode_for_join(&f, 0, kPeriod));
  TEST_ASSERT_EQUAL_UINT8(MHI_PHY_11G, mhi_phy_mode_for_join(&f, kPeriod - 4096, kPeriod));
}

// --- the status topic text --------------------------------------------------

static void test_mode_text_matches_the_sdk_names(void) {
  TEST_ASSERT_EQUAL_STRING("11b", mhi_phy_mode_text(MHI_PHY_11B));
  TEST_ASSERT_EQUAL_STRING("11g", mhi_phy_mode_text(MHI_PHY_11G));
  TEST_ASSERT_EQUAL_STRING("11n", mhi_phy_mode_text(MHI_PHY_11N));
}

static void test_an_unknown_mode_has_a_text_too(void) {
  TEST_ASSERT_EQUAL_STRING("?", mhi_phy_mode_text(0));
  TEST_ASSERT_EQUAL_STRING("?", mhi_phy_mode_text(4));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_boot_tries_the_default_mode_first);
  RUN_TEST(test_a_full_period_without_a_link_falls_back_to_11g);
  RUN_TEST(test_a_period_of_failures_in_11g_returns_to_the_default);
  RUN_TEST(test_a_join_restarts_the_clock);
  RUN_TEST(test_the_mode_does_not_change_while_the_link_is_up);
  RUN_TEST(test_a_unit_that_joined_in_11g_tries_11g_first_after_a_drop);
  RUN_TEST(test_the_fallback_survives_the_millis_wrap);
  RUN_TEST(test_mode_text_matches_the_sdk_names);
  RUN_TEST(test_an_unknown_mode_has_a_text_too);
  return UNITY_END();
}
