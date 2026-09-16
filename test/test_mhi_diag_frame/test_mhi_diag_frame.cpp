// Host tests for the protocol discovery tooling (fork issue #4, batch A;
// spec docs/superpowers/specs/2026-09-16-phase-4-batches-design.md §3).
//
// main.cpp publishes what these functions produce: the bytes of the AC's
// status frame that changed, the value bytes of unknown operating data, and
// a parsed set/OpDataRequest. The compare mask is what keeps diag/frame quiet
// while the operating-data bytes cycle.

#include <string.h>
#include <unity.h>

#include "mhi_diag_frame.h"
#include "mhi_frame.h"

void setUp(void) {}
void tearDown(void) {}

// --- set/OpDataRequest parsing ---------------------------------------------

static void test_request_parses_an_indoor_code(void) {
  uint8_t prefix = 0, code = 0;
  TEST_ASSERT_TRUE(mhi_opdata_request_parse("c021", &prefix, &code));
  TEST_ASSERT_EQUAL_HEX8(0xc0, prefix);
  TEST_ASSERT_EQUAL_HEX8(0x21, code);
}

static void test_request_parses_an_outdoor_code_in_upper_case(void) {
  uint8_t prefix = 0, code = 0;
  TEST_ASSERT_TRUE(mhi_opdata_request_parse("40DD", &prefix, &code));
  TEST_ASSERT_EQUAL_HEX8(0x40, prefix);
  TEST_ASSERT_EQUAL_HEX8(0xdd, code);
}

static void test_request_rejects_a_prefix_the_ac_never_sees(void) {
  // Only the two prefixes of the built-in request table are sent.
  uint8_t prefix = 1, code = 2;
  TEST_ASSERT_FALSE(mhi_opdata_request_parse("8021", &prefix, &code));
  TEST_ASSERT_FALSE(mhi_opdata_request_parse("0021", &prefix, &code));
  TEST_ASSERT_EQUAL_HEX8(1, prefix);  // untouched
  TEST_ASSERT_EQUAL_HEX8(2, code);
}

static void test_request_rejects_anything_but_four_hex_digits(void) {
  uint8_t prefix = 1, code = 2;
  TEST_ASSERT_FALSE(mhi_opdata_request_parse("c02", &prefix, &code));
  TEST_ASSERT_FALSE(mhi_opdata_request_parse("c0211", &prefix, &code));
  TEST_ASSERT_FALSE(mhi_opdata_request_parse("c0zz", &prefix, &code));
  TEST_ASSERT_FALSE(mhi_opdata_request_parse("", &prefix, &code));
  TEST_ASSERT_FALSE(mhi_opdata_request_parse(NULL, &prefix, &code));
  TEST_ASSERT_EQUAL_HEX8(1, prefix);
  TEST_ASSERT_EQUAL_HEX8(2, code);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_request_parses_an_indoor_code);
  RUN_TEST(test_request_parses_an_outdoor_code_in_upper_case);
  RUN_TEST(test_request_rejects_a_prefix_the_ac_never_sees);
  RUN_TEST(test_request_rejects_anything_but_four_hex_digits);
  return UNITY_END();
}
