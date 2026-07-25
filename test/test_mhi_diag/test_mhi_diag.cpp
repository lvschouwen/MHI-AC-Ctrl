// Host tests for the boot-time wiring check.
//
// The point of this logic is that a wiring fault must be *reported*, never
// fatal: MeasureFrequency() used to end in while(1) on a bad MISO reading,
// which meant an endless hardware-WDT reboot loop before setupOTA() ever ran.
// With no spare board that turns a loose wire into opening an air conditioner.

#include <unity.h>
#include <string.h>

#include "mhi_diag.h"

void setUp(void) {}
void tearDown(void) {}

// Frequencies from a healthy unit: SCK clocks fast, MOSI carries the AC's
// frames, MISO is driven by us and idles.
static const uint32_t kGoodSck = 4000;
static const uint32_t kGoodMosi = 400;
static const uint32_t kGoodMiso = 0;

static void test_healthy_wiring_reports_no_fault(void) {
  TEST_ASSERT_EQUAL_UINT8(0, mhi_wiring_faults(kGoodSck, kGoodMosi, kGoodMiso));
}

static void test_silent_sck_is_a_fault(void) {
  const uint8_t faults = mhi_wiring_faults(0, kGoodMosi, kGoodMiso);
  TEST_ASSERT_TRUE(faults & MHI_WIRING_FAULT_SCK);
}

static void test_sck_threshold(void) {
  TEST_ASSERT_TRUE(mhi_wiring_faults(3000, kGoodMosi, kGoodMiso) & MHI_WIRING_FAULT_SCK);
  TEST_ASSERT_FALSE(mhi_wiring_faults(3001, kGoodMosi, kGoodMiso) & MHI_WIRING_FAULT_SCK);
}

static void test_mosi_must_be_active_but_slower_than_sck(void) {
  TEST_ASSERT_TRUE(mhi_wiring_faults(kGoodSck, 30, kGoodMiso) & MHI_WIRING_FAULT_MOSI);
  TEST_ASSERT_FALSE(mhi_wiring_faults(kGoodSck, 31, kGoodMiso) & MHI_WIRING_FAULT_MOSI);
  // Faster than the clock it is sampled with means something is wrong.
  TEST_ASSERT_TRUE(mhi_wiring_faults(kGoodSck, kGoodSck, kGoodMiso) & MHI_WIRING_FAULT_MOSI);
}

static void test_miso_must_be_quiet(void) {
  TEST_ASSERT_FALSE(mhi_wiring_faults(kGoodSck, kGoodMosi, 10) & MHI_WIRING_FAULT_MISO);
  TEST_ASSERT_TRUE(mhi_wiring_faults(kGoodSck, kGoodMosi, 11) & MHI_WIRING_FAULT_MISO);
}

static void test_faults_are_reported_independently(void) {
  // The old code bailed out on MISO before it could tell you about the rest.
  const uint8_t faults = mhi_wiring_faults(0, 0, 5000);
  TEST_ASSERT_TRUE(faults & MHI_WIRING_FAULT_SCK);
  TEST_ASSERT_TRUE(faults & MHI_WIRING_FAULT_MOSI);
  TEST_ASSERT_TRUE(faults & MHI_WIRING_FAULT_MISO);
}

// --- the text published to MQTT -------------------------------------------

static void test_healthy_wiring_reads_as_ok(void) {
  char buf[32];
  mhi_wiring_fault_text(0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("o.k.", buf);
}

static void test_single_fault_names_the_pin(void) {
  char buf[32];
  mhi_wiring_fault_text(MHI_WIRING_FAULT_MISO, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("MISO", buf);
}

static void test_multiple_faults_are_listed(void) {
  char buf[32];
  mhi_wiring_fault_text(MHI_WIRING_FAULT_SCK | MHI_WIRING_FAULT_MISO, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("SCK,MISO", buf);
}

static void test_text_never_overruns_a_short_buffer(void) {
  char buf[4];
  memset(buf, 'x', sizeof(buf));
  mhi_wiring_fault_text(MHI_WIRING_FAULT_SCK | MHI_WIRING_FAULT_MOSI | MHI_WIRING_FAULT_MISO,
                        buf, sizeof(buf));
  TEST_ASSERT_EQUAL_CHAR('\0', buf[sizeof(buf) - 1]);
  TEST_ASSERT_TRUE(strlen(buf) < sizeof(buf));
}

static void test_text_tolerates_a_zero_length_buffer(void) {
  char buf[1] = {'x'};
  mhi_wiring_fault_text(MHI_WIRING_FAULT_SCK, buf, 0);
  TEST_ASSERT_EQUAL_CHAR('x', buf[0]);  // untouched, not written past
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_healthy_wiring_reports_no_fault);
  RUN_TEST(test_silent_sck_is_a_fault);
  RUN_TEST(test_sck_threshold);
  RUN_TEST(test_mosi_must_be_active_but_slower_than_sck);
  RUN_TEST(test_miso_must_be_quiet);
  RUN_TEST(test_faults_are_reported_independently);
  RUN_TEST(test_healthy_wiring_reads_as_ok);
  RUN_TEST(test_single_fault_names_the_pin);
  RUN_TEST(test_multiple_faults_are_listed);
  RUN_TEST(test_text_never_overruns_a_short_buffer);
  RUN_TEST(test_text_tolerates_a_zero_length_buffer);
  return UNITY_END();
}
