// Host tests for the pure frame helpers extracted from MHI-AC-Ctrl-core.cpp.
//
// These run on the build machine (pio test -e native). Nothing here touches
// the SPI bus, so it is safe to change and re-run without an air conditioner.

#include <unity.h>

#include "mhi_frame.h"

// The MISO frame the core starts from, copied from MHI_AC_Ctrl_Core::loop().
// Byte 32 (CBL2) is the frame-33 checksum slot and is not summed.
static const uint8_t kMisoTemplate[33] = {
    0xA9, 0x00, 0x07, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xff, 0xff, 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x22};

void setUp(void) {}
void tearDown(void) {}

static void test_frame_layout_matches_the_protocol(void) {
  // A 20-byte frame is signature + data + a 16-bit checksum; the extended
  // WF-RAC frame adds 13 more bytes and one trailing checksum byte.
  TEST_ASSERT_EQUAL_INT(18, CBH);
  TEST_ASSERT_EQUAL_INT(19, CBL);
  TEST_ASSERT_EQUAL_INT(32, CBL2);
  // The outdoor unit state behind the Action topic; hberntsen reads index 16 too.
  TEST_ASSERT_EQUAL_INT(16, DB13);
}

static void test_checksum_of_an_empty_frame_is_zero(void) {
  uint8_t frame[33] = {0};
  TEST_ASSERT_EQUAL_UINT16(0, mhi_calc_checksum(frame));
  TEST_ASSERT_EQUAL_UINT16(0, mhi_calc_checksum_frame33(frame));
}

static void test_checksum_sums_every_byte_before_the_checksum_field(void) {
  // 0xA9 + 0x07 + 0xff + (4 * 0xff) + 0x0f = 1466
  TEST_ASSERT_EQUAL_UINT16(1466, mhi_calc_checksum(kMisoTemplate));
}

static void test_checksum_ignores_the_checksum_field_itself(void) {
  uint8_t frame[33] = {0};
  frame[CBH] = 0xff;
  frame[CBL] = 0xff;
  TEST_ASSERT_EQUAL_UINT16(0, mhi_calc_checksum(frame));
}

static void test_frame33_checksum_covers_the_extended_bytes(void) {
  // 1466 from the first 18 bytes + 4 * 0xff from bytes 28..31.
  TEST_ASSERT_EQUAL_UINT16(2486, mhi_calc_checksum_frame33(kMisoTemplate));
}

static void test_only_the_frame33_checksum_sees_extended_data(void) {
  uint8_t frame[33] = {0};
  frame[DB20] = 0x10;  // lives past CBL, so the 20-byte checksum must not move

  TEST_ASSERT_EQUAL_UINT16(0, mhi_calc_checksum(frame));
  TEST_ASSERT_EQUAL_UINT16(0x10, mhi_calc_checksum_frame33(frame));
}

static void test_frame33_checksum_ignores_its_own_field(void) {
  uint8_t frame[33] = {0};
  frame[CBL2] = 0xff;
  TEST_ASSERT_EQUAL_UINT16(0, mhi_calc_checksum_frame33(frame));
}

static void test_checksum_does_not_truncate_to_a_byte(void) {
  // Every summed byte at 0xff: 18 * 255 = 4590, which must not wrap.
  uint8_t frame[33];
  for (int i = 0; i < 33; i++) frame[i] = 0xff;

  TEST_ASSERT_EQUAL_UINT16(4590, mhi_calc_checksum(frame));
  TEST_ASSERT_EQUAL_UINT16(8160, mhi_calc_checksum_frame33(frame));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_frame_layout_matches_the_protocol);
  RUN_TEST(test_checksum_of_an_empty_frame_is_zero);
  RUN_TEST(test_checksum_sums_every_byte_before_the_checksum_field);
  RUN_TEST(test_checksum_ignores_the_checksum_field_itself);
  RUN_TEST(test_frame33_checksum_covers_the_extended_bytes);
  RUN_TEST(test_only_the_frame33_checksum_sees_extended_data);
  RUN_TEST(test_frame33_checksum_ignores_its_own_field);
  RUN_TEST(test_checksum_does_not_truncate_to_a_byte);
  return UNITY_END();
}
