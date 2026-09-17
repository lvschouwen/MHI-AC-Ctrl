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

// --- unknown operating data with its value bytes ---------------------------

static void test_opdata_text_shows_the_four_bytes_in_hex(void) {
  const uint8_t db9[4] = {0xdd, 0x80, 0x01, 0x00};  // Silent, as seen on 16 Sep
  char out[16];
  TEST_ASSERT_EQUAL_size_t(11, mhi_diag_opdata_text(db9, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("dd 80 01 00", out);
}

static void test_opdata_text_refuses_a_buffer_that_cannot_hold_it(void) {
  const uint8_t db9[4] = {0x21, 0x10, 0x00, 0x00};
  char out[11];  // one short of the NUL
  TEST_ASSERT_EQUAL_size_t(0, mhi_diag_opdata_text(db9, out, sizeof(out)));
}

// --- the compare mask ------------------------------------------------------

static void test_default_mask_ignores_what_changes_on_its_own(void) {
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 20);
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[SB0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[SB1]);
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[SB2]);
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[DB3]);   // raw Troom dithers at a temperature boundary (16 Sep 2026)
  TEST_ASSERT_EQUAL_HEX8(0x3f, mask[DB6]);   // request-prefix bits 0xc0 cycle
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[DB9]);
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[DB10]);
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[DB11]);
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[DB12]);
  TEST_ASSERT_EQUAL_HEX8(0xfb, mask[DB14]);  // bit 2 toggles every frame
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[CBH]);
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[CBL]);
}

static void test_default_mask_compares_the_status_bytes_in_full(void) {
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 33);
  TEST_ASSERT_EQUAL_HEX8(0xff, mask[DB0]);
  TEST_ASSERT_EQUAL_HEX8(0xff, mask[DB5]);   // undocumented per SPI.md, the point of the tool
  TEST_ASSERT_EQUAL_HEX8(0xff, mask[DB13]);
  TEST_ASSERT_EQUAL_HEX8(0xff, mask[DB15]);
  TEST_ASSERT_EQUAL_HEX8(0xff, mask[DB26]);
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[CBL2]);
}

// --- the frame diff ----------------------------------------------------------

// A plausible 20-byte status frame: header 6c 80 04, then DB0..DB14, checksum.
static const uint8_t kFrame[20] = {0x6c, 0x80, 0x04, 0x08, 0x3b, 0x2e, 0x4c, 0x22, 0x00, 0x00, 0x00, 0x00,
                                   0x02, 0x10, 0x3b, 0x00, 0x05, 0x00, 0x02, 0x1d};

static void test_a_room_temperature_dither_alone_publishes_nothing(void) {
  // Uitkijk, 16 Sep 2026: "DB3 89>8a" up to 13 times a minute while the room
  // sat on a boundary. Troom already carries the filtered value.
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 20);
  char out[MHI_DIAG_TEXT_MAX];
  TEST_ASSERT_TRUE(mhi_diag_frame_changes(&d, kFrame, 20, mask, out, sizeof(out)) > 0);  // the first frame
  uint8_t dither[20];
  memcpy(dither, kFrame, 20);
  dither[DB3] = kFrame[DB3] + 1;
  TEST_ASSERT_EQUAL_size_t(0, mhi_diag_frame_changes(&d, dither, 20, mask, out, sizeof(out)));
}

static void test_the_first_frame_is_published_whole(void) {
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 20);
  char out[MHI_DIAG_TEXT_MAX];
  const size_t n = mhi_diag_frame_changes(&d, kFrame, 20, mask, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("first | 6c 80 04 08 3b 2e 4c 22 00 00 00 00 02 10 3b 00 05 00 02 1d", out);
  TEST_ASSERT_EQUAL_size_t(strlen(out), n);
  TEST_ASSERT_TRUE(d.have_last);
}

static void test_an_unchanged_frame_publishes_nothing(void) {
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 20);
  char out[MHI_DIAG_TEXT_MAX] = "untouched";
  mhi_diag_frame_changes(&d, kFrame, 20, mask, out, sizeof(out));
  TEST_ASSERT_EQUAL_size_t(0, mhi_diag_frame_changes(&d, kFrame, 20, mask, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("first | 6c 80 04 08 3b 2e 4c 22 00 00 00 00 02 10 3b 00 05 00 02 1d", out);  // left alone
}

static void test_a_changed_status_byte_is_named_with_old_and_new(void) {
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 20);
  char out[MHI_DIAG_TEXT_MAX];
  mhi_diag_frame_changes(&d, kFrame, 20, mask, out, sizeof(out));
  uint8_t next[20];
  memcpy(next, kFrame, 20);
  next[DB5] = 0x10;  // a HI/ECO press flipping an undocumented bit
  TEST_ASSERT_GREATER_THAN_size_t(0, mhi_diag_frame_changes(&d, next, 20, mask, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("DB5 00>10 | 6c 80 04 08 3b 2e 4c 22 10 00 00 00 02 10 3b 00 05 00 02 1d", out);
}

static void test_the_compare_is_against_the_last_published_frame(void) {
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 20);
  char out[MHI_DIAG_TEXT_MAX];
  mhi_diag_frame_changes(&d, kFrame, 20, mask, out, sizeof(out));
  uint8_t next[20];
  memcpy(next, kFrame, 20);
  next[DB5] = 0x10;
  mhi_diag_frame_changes(&d, next, 20, mask, out, sizeof(out));
  next[DB5] = 0x11;
  mhi_diag_frame_changes(&d, next, 20, mask, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING_LEN("DB5 10>11 |", out, 11);
}

static void test_masked_bytes_never_trigger_a_publish(void) {
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 20);
  char out[MHI_DIAG_TEXT_MAX];
  mhi_diag_frame_changes(&d, kFrame, 20, mask, out, sizeof(out));
  uint8_t next[20];
  memcpy(next, kFrame, 20);
  next[DB9] = 0x80;   // the operating-data cycle
  next[DB10] = 0x10;
  next[DB11] = 0x33;
  next[DB12] = 0x01;
  next[DB6] ^= 0xc0;  // the echoed request prefix
  next[DB14] ^= 0x04; // the frame toggle
  next[CBH] = 0xaa;   // checksum follows the rest
  next[CBL] = 0xbb;
  TEST_ASSERT_EQUAL_size_t(0, mhi_diag_frame_changes(&d, next, 20, mask, out, sizeof(out)));
}

static void test_the_low_bits_of_db6_and_the_rest_of_db14_still_count(void) {
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 20);
  char out[MHI_DIAG_TEXT_MAX];
  mhi_diag_frame_changes(&d, kFrame, 20, mask, out, sizeof(out));
  uint8_t next[20];
  memcpy(next, kFrame, 20);
  next[DB6] |= 0x01;
  next[DB14] |= 0x08;
  TEST_ASSERT_GREATER_THAN_size_t(0, mhi_diag_frame_changes(&d, next, 20, mask, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING_LEN("DB6 00>01 DB14 00>08 |", out, 22);
}

static void test_more_than_six_changes_are_summarised_with_a_plus(void) {
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 20);
  char out[MHI_DIAG_TEXT_MAX];
  mhi_diag_frame_changes(&d, kFrame, 20, mask, out, sizeof(out));
  uint8_t next[20];
  memcpy(next, kFrame, 20);
  for (size_t i = DB0; i <= DB5; i++) next[i] ^= 0x01;  // five real changes (DB3 is masked)
  next[DB7] ^= 0x01;
  next[DB8] ^= 0x01;                                    // DB3 is masked; DB8 keeps it at seven
  TEST_ASSERT_GREATER_THAN_size_t(0, mhi_diag_frame_changes(&d, next, 20, mask, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING_LEN("DB0 08>09 DB1 3b>3a DB2 2e>2f DB4 22>23 DB5 00>01 DB7 00>01 + |", out, 62);
  TEST_ASSERT_LESS_THAN_size_t(MHI_DIAG_TEXT_MAX, strlen(out));
}

static void test_an_extended_frame_names_its_extra_bytes(void) {
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 33);
  char out[MHI_DIAG_TEXT_MAX];
  uint8_t frame[33] = {0};
  memcpy(frame, kFrame, 20);
  mhi_diag_frame_changes(&d, frame, 33, mask, out, sizeof(out));
  frame[DB15] = 0x04;  // 3D auto
  TEST_ASSERT_GREATER_THAN_size_t(0, mhi_diag_frame_changes(&d, frame, 33, mask, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING_LEN("DB15 00>04 |", out, 12);
  TEST_ASSERT_LESS_THAN_size_t(MHI_DIAG_TEXT_MAX, strlen(out));
}

static void test_a_frame_longer_than_the_maximum_is_refused(void) {
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 33);
  uint8_t frame[40] = {0};
  char out[MHI_DIAG_TEXT_MAX];
  TEST_ASSERT_EQUAL_size_t(0, mhi_diag_frame_changes(&d, frame, 40, mask, out, sizeof(out)));
  TEST_ASSERT_FALSE(d.have_last);
}

static void test_the_worst_case_extended_frame_fits_the_text_buffer(void) {
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 33);
  char out[MHI_DIAG_TEXT_MAX];
  uint8_t frame[33] = {0};
  memcpy(frame, kFrame, 20);
  frame[CBL2] = 0xab;  // the extended frame's second checksum byte; arbitrary but distinctive
  mhi_diag_frame_changes(&d, frame, 33, mask, out, sizeof(out));  // first, whole frame
  for (size_t i = DB0; i <= DB5; i++) frame[i] ^= 0x01;  // five real changes (DB3 is masked)
  frame[DB7] ^= 0x01;
  frame[DB8] ^= 0x01;                                    // DB3 is masked; DB8 keeps it at seven
  const size_t n = mhi_diag_frame_changes(&d, frame, 33, mask, out, sizeof(out));
  TEST_ASSERT_GREATER_THAN_size_t(0, n);
  TEST_ASSERT_TRUE(strlen(out) < MHI_DIAG_TEXT_MAX);
  TEST_ASSERT_EQUAL_STRING_LEN("DB0 08>09 DB1 3b>3a DB2 2e>2f DB4 22>23 DB5 00>01 DB7 00>01 + |", out, 62);
  TEST_ASSERT_EQUAL_STRING(" ab", out + strlen(out) - 3);  // ends with the frame's last byte
}

static void test_a_text_buffer_smaller_than_the_maximum_is_refused(void) {
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 20);
  char out[MHI_DIAG_TEXT_MAX] = "untouched";
  TEST_ASSERT_EQUAL_size_t(0, mhi_diag_frame_changes(&d, kFrame, 20, mask, out, MHI_DIAG_TEXT_MAX - 1));
  TEST_ASSERT_FALSE(d.have_last);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_request_parses_an_indoor_code);
  RUN_TEST(test_request_parses_an_outdoor_code_in_upper_case);
  RUN_TEST(test_request_rejects_a_prefix_the_ac_never_sees);
  RUN_TEST(test_request_rejects_anything_but_four_hex_digits);
  RUN_TEST(test_opdata_text_shows_the_four_bytes_in_hex);
  RUN_TEST(test_opdata_text_refuses_a_buffer_that_cannot_hold_it);
  RUN_TEST(test_default_mask_ignores_what_changes_on_its_own);
  RUN_TEST(test_default_mask_compares_the_status_bytes_in_full);
  RUN_TEST(test_a_room_temperature_dither_alone_publishes_nothing);
  RUN_TEST(test_the_first_frame_is_published_whole);
  RUN_TEST(test_an_unchanged_frame_publishes_nothing);
  RUN_TEST(test_a_changed_status_byte_is_named_with_old_and_new);
  RUN_TEST(test_the_compare_is_against_the_last_published_frame);
  RUN_TEST(test_masked_bytes_never_trigger_a_publish);
  RUN_TEST(test_the_low_bits_of_db6_and_the_rest_of_db14_still_count);
  RUN_TEST(test_more_than_six_changes_are_summarised_with_a_plus);
  RUN_TEST(test_an_extended_frame_names_its_extra_bytes);
  RUN_TEST(test_a_frame_longer_than_the_maximum_is_refused);
  RUN_TEST(test_the_worst_case_extended_frame_fits_the_text_buffer);
  RUN_TEST(test_a_text_buffer_smaller_than_the_maximum_is_refused);
  return UNITY_END();
}
