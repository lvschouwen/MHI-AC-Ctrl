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

// Fork #61: what the AC was doing, numbers appended after the existing keys.
static void test_a_snapshot_is_appended_after_the_existing_keys(void) {
  uint32_t rec[MHI_CRASH_RECORD_WORDS], snap[MHI_CRASH_SNAPSHOT_WORDS];
  mhi_crash_record_make(rec, 2, 29, 0x4020abcdu, 0);
  mhi_crash_snapshot_make(snap, 1, 8, 0xac, 0x7f);
  TEST_ASSERT_TRUE(mhi_crash_snapshot_valid(snap));
  char out[MHI_CRASH_INFO_JSON_MAX];
  TEST_ASSERT_TRUE(mhi_crash_info_json_ex(rec, snap, out, sizeof(out)) > 0);
  TEST_ASSERT_EQUAL_STRING("{\"exccause\":29,\"reason\":2,\"epc1\":\"0x4020abcd\",\"excvaddr\":\"0x00000000\","
                           "\"power\":1,\"mode\":8,\"tset\":172,\"troom\":127}", out);
}

// The existing output is unchanged without a snapshot, and a crash record
// written by older firmware (no snapshot record) still reads.
static void test_without_a_snapshot_the_output_is_the_old_one(void) {
  uint32_t rec[MHI_CRASH_RECORD_WORDS], none[MHI_CRASH_SNAPSHOT_WORDS] = {0};
  mhi_crash_record_make(rec, 2, 29, 0x4020abcdu, 0);
  char a[MHI_CRASH_INFO_JSON_MAX], b[MHI_CRASH_INFO_JSON_MAX], c[MHI_CRASH_INFO_JSON_MAX];
  TEST_ASSERT_TRUE(mhi_crash_info_json(rec, a, sizeof(a)) > 0);
  TEST_ASSERT_TRUE(mhi_crash_info_json_ex(rec, NULL, b, sizeof(b)) > 0);
  TEST_ASSERT_TRUE(mhi_crash_info_json_ex(rec, none, c, sizeof(c)) > 0);  // invalid snapshot words, e.g. RTC after power-on
  TEST_ASSERT_EQUAL_STRING(a, b);
  TEST_ASSERT_EQUAL_STRING(a, c);
  TEST_ASSERT_EQUAL_STRING("{\"exccause\":29,\"reason\":2,\"epc1\":\"0x4020abcd\",\"excvaddr\":\"0x00000000\"}", a);
}

static void test_a_snapshot_without_a_crash_record_says_no_crash(void) {
  uint32_t rec[MHI_CRASH_RECORD_WORDS] = {0}, snap[MHI_CRASH_SNAPSHOT_WORDS];
  mhi_crash_snapshot_make(snap, 1, 8, 0xac, 0x7f);
  char out[MHI_CRASH_INFO_JSON_MAX];
  TEST_ASSERT_TRUE(mhi_crash_info_json_ex(rec, snap, out, sizeof(out)) > 0);
  TEST_ASSERT_EQUAL_STRING("{\"exccause\":-1}", out);
}

static void test_a_corrupted_snapshot_is_invalid(void) {
  uint32_t snap[MHI_CRASH_SNAPSHOT_WORDS];
  for (size_t w = 0; w < MHI_CRASH_SNAPSHOT_WORDS; w++) {
    mhi_crash_snapshot_make(snap, 1, 8, 0xac, 0x7f);
    snap[w] ^= 0x100u;
    TEST_ASSERT_FALSE(mhi_crash_snapshot_valid(snap));
  }
}

// The largest payload still fits the buffer with its terminator.
static void test_the_largest_payload_with_a_snapshot_fits(void) {
  uint32_t rec[MHI_CRASH_RECORD_WORDS], snap[MHI_CRASH_SNAPSHOT_WORDS];
  mhi_crash_record_make(rec, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu);
  mhi_crash_snapshot_make(snap, 0xff, 0xff, 0xff, 0xff);
  char out[MHI_CRASH_INFO_JSON_MAX];
  const size_t n = mhi_crash_info_json_ex(rec, snap, out, sizeof(out));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_TRUE(n < MHI_CRASH_INFO_JSON_MAX);
  TEST_ASSERT_EQUAL_STRING("{\"exccause\":4294967295,\"reason\":4294967295,\"epc1\":\"0xffffffff\",\"excvaddr\":\"0xffffffff\","
                           "\"power\":255,\"mode\":255,\"tset\":255,\"troom\":255}", out);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_stored_crash_reads_back_as_json);
  RUN_TEST(test_no_record_reads_as_minus_one);
  RUN_TEST(test_a_corrupted_record_is_invalid);
  RUN_TEST(test_the_largest_values_fit);
  RUN_TEST(test_a_short_buffer_gives_nothing);
  RUN_TEST(test_a_snapshot_is_appended_after_the_existing_keys);
  RUN_TEST(test_without_a_snapshot_the_output_is_the_old_one);
  RUN_TEST(test_a_snapshot_without_a_crash_record_says_no_crash);
  RUN_TEST(test_a_corrupted_snapshot_is_invalid);
  RUN_TEST(test_the_largest_payload_with_a_snapshot_fits);
  return UNITY_END();
}
