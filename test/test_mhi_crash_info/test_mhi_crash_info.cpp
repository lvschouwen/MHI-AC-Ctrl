// Host tests for the crash details (fork #25, N2 from #11).
//
// The core's crash hook stores the SDK's rst_info in RTC; the next normal boot
// publishes it retained on CrashInfo and clears it, so a boot without a crash
// reads {"exccause":-1}.

#include <unity.h>
#include <string.h>

#include "mhi_crash_info.h"

void setUp(void) {}
void tearDown(void) {}

static void test_a_stored_crash_reads_back_as_json(void) {
  uint32_t rec[MHI_CRASH_RECORD_WORDS];
  mhi_crash_record_make(rec, 2, 29, 0x4020abcdu, 0);
  TEST_ASSERT_TRUE(mhi_crash_record_valid(rec));
  char out[MHI_CRASH_INFO_JSON_MAX];
  TEST_ASSERT_TRUE(mhi_crash_info_json(rec, out, sizeof(out)) > 0);
  TEST_ASSERT_EQUAL_STRING("{\"exccause\":29,\"reason\":2,\"epc1\":\"0x4020abcd\",\"excvaddr\":\"0x00000000\"}", out);
}

static void test_no_record_reads_as_minus_one(void) {
  uint32_t rec[MHI_CRASH_RECORD_WORDS] = {0};
  TEST_ASSERT_FALSE(mhi_crash_record_valid(rec));
  char out[MHI_CRASH_INFO_JSON_MAX];
  TEST_ASSERT_TRUE(mhi_crash_info_json(rec, out, sizeof(out)) > 0);
  TEST_ASSERT_EQUAL_STRING("{\"exccause\":-1}", out);
  TEST_ASSERT_TRUE(mhi_crash_info_json(NULL, out, sizeof(out)) > 0);
  TEST_ASSERT_EQUAL_STRING("{\"exccause\":-1}", out);
}

// RTC survives a restart but holds garbage after power-on: one flipped bit
// makes the record invalid.
static void test_a_corrupted_record_is_invalid(void) {
  uint32_t rec[MHI_CRASH_RECORD_WORDS];
  for (size_t w = 0; w < MHI_CRASH_RECORD_WORDS; w++) {
    mhi_crash_record_make(rec, 2, 29, 0x4020abcdu, 0x10u);
    rec[w] ^= 0x100u;
    TEST_ASSERT_FALSE(mhi_crash_record_valid(rec));
  }
}

static void test_the_largest_values_fit(void) {
  uint32_t rec[MHI_CRASH_RECORD_WORDS];
  mhi_crash_record_make(rec, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu);
  char out[MHI_CRASH_INFO_JSON_MAX];
  TEST_ASSERT_TRUE(mhi_crash_info_json(rec, out, sizeof(out)) > 0);
  TEST_ASSERT_EQUAL_STRING("{\"exccause\":4294967295,\"reason\":4294967295,\"epc1\":\"0xffffffff\",\"excvaddr\":\"0xffffffff\"}", out);
}

static void test_a_short_buffer_gives_nothing(void) {
  uint32_t rec[MHI_CRASH_RECORD_WORDS];
  mhi_crash_record_make(rec, 2, 29, 1, 2);
  char out[20];
  TEST_ASSERT_EQUAL_size_t(0, mhi_crash_info_json(rec, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_stored_crash_reads_back_as_json);
  RUN_TEST(test_no_record_reads_as_minus_one);
  RUN_TEST(test_a_corrupted_record_is_invalid);
  RUN_TEST(test_the_largest_values_fit);
  RUN_TEST(test_a_short_buffer_gives_nothing);
  return UNITY_END();
}
