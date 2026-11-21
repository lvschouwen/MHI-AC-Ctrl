// Host tests for CmdUnconfirmed (issue #60).

#include <string.h>
#include <unity.h>

#include "mhi_cmd_confirm.h"
#include "mhi_frame.h"

void setUp(void) {}
void tearDown(void) {}

static const uint16_t kN = 5;

static MhiCmdConfirm fresh() {
  MhiCmdConfirm c;
  mhi_cc_init(&c);
  return c;
}

// A MISO frame carrying one write; the others stay empty.
static void miso_set(uint8_t* f, uint8_t db0, uint8_t db1, uint8_t db2) {
  memset(f, 0, 33);
  f[DB0] = db0;
  f[DB1] = db1;
  f[DB2] = db2;
}

static void test_a_confirmed_setpoint_is_not_counted(void) {
  MhiCmdConfirm c = fresh();
  uint8_t miso[33], mosi[33] = {0};
  miso_set(miso, 0, 0, 0x80 | 44);  // 22 degrees in half degrees
  mhi_cc_sent(&c, miso, kN);
  mosi[DB2] = 36;  // not yet
  mhi_cc_observe(&c, mosi);
  mosi[DB2] = 44;  // here, without the echo flag
  mhi_cc_observe(&c, mosi);
  for (int i = 0; i < 20; i++) mhi_cc_observe(&c, mosi);
  TEST_ASSERT_EQUAL_UINT32(0, c.unconfirmed);
}

static void test_the_echo_flag_alone_is_not_the_value(void) {
  MhiCmdConfirm c = fresh();
  uint8_t miso[33], mosi[33] = {0};
  miso_set(miso, 0, 0, 0x80 | 44);
  mhi_cc_sent(&c, miso, kN);
  mosi[DB2] = 0x80 | 36;  // flag still set from an earlier write, value is the old one
  for (int i = 0; i < kN; i++) mhi_cc_observe(&c, mosi);
  TEST_ASSERT_EQUAL_UINT32(1, c.unconfirmed);
}

static void test_a_write_that_never_shows_is_counted_once(void) {
  MhiCmdConfirm c = fresh();
  uint8_t miso[33], mosi[33] = {0};
  miso_set(miso, 0x02 | 0x01, 0, 0);  // power on
  mhi_cc_sent(&c, miso, kN);
  mosi[DB0] = 0;  // the AC stays off
  for (int i = 0; i < kN - 1; i++) mhi_cc_observe(&c, mosi);
  TEST_ASSERT_EQUAL_UINT32(0, c.unconfirmed);  // one frame to go
  mhi_cc_observe(&c, mosi);
  TEST_ASSERT_EQUAL_UINT32(1, c.unconfirmed);
  for (int i = 0; i < 50; i++) mhi_cc_observe(&c, mosi);
  TEST_ASSERT_EQUAL_UINT32(1, c.unconfirmed);  // no longer watched
}

// A write whose target is already what the AC reports confirms at the next frame.
static void test_a_write_of_the_current_value_confirms_at_once(void) {
  MhiCmdConfirm c = fresh();
  uint8_t miso[33], mosi[33] = {0};
  mosi[DB1] = 0x02;                     // fan 2 already
  miso_set(miso, 0, 0x08 | 0x02, 0);    // set fan 2
  mhi_cc_sent(&c, miso, kN);
  mhi_cc_observe(&c, mosi);
  for (int i = 0; i < 20; i++) mhi_cc_observe(&c, mosi);
  TEST_ASSERT_EQUAL_UINT32(0, c.unconfirmed);
}

static void test_each_field_reads_its_own_bits(void) {
  uint8_t miso[33], mosi[33];
  // mode cool (0x08) with set flag 0x20, power untouched
  MhiCmdConfirm c = fresh();
  miso_set(miso, 0x20 | 0x08, 0, 0);
  mhi_cc_sent(&c, miso, kN);
  memset(mosi, 0, sizeof mosi);
  mosi[DB0] = 0x01 | 0x08 | 0x80;  // power on, cool, echo flag
  mhi_cc_observe(&c, mosi);
  TEST_ASSERT_EQUAL_UINT16(0, c.frames_left[MHI_CC_MODE]);
  TEST_ASSERT_EQUAL_UINT16(0, c.frames_left[MHI_CC_POWER]);  // never watched: no power write
  // vanes: position 3 (0x20) with its flag, swing off (flag 0x80, bit 0x40 clear)
  c = fresh();
  miso_set(miso, 0x80, 0x80 | 0x20, 0);
  mhi_cc_sent(&c, miso, kN);
  TEST_ASSERT_EQUAL_UINT16(kN, c.frames_left[MHI_CC_VANES_POS]);
  TEST_ASSERT_EQUAL_UINT16(kN, c.frames_left[MHI_CC_VANES_SWING]);
  mosi[DB0] = 0;
  mosi[DB1] = 0x20;
  mhi_cc_observe(&c, mosi);
  TEST_ASSERT_EQUAL_UINT16(0, c.frames_left[MHI_CC_VANES_POS]);
  TEST_ASSERT_EQUAL_UINT16(0, c.frames_left[MHI_CC_VANES_SWING]);
  TEST_ASSERT_EQUAL_UINT32(0, c.unconfirmed);
  // swing on: 0xc0 in DB0, position bits untouched (no DB1 flag)
  c = fresh();
  miso_set(miso, 0xc0, 0, 0);
  mhi_cc_sent(&c, miso, kN);
  TEST_ASSERT_EQUAL_UINT16(0, c.frames_left[MHI_CC_VANES_POS]);
  mosi[DB0] = 0x40;
  mhi_cc_observe(&c, mosi);
  TEST_ASSERT_EQUAL_UINT16(0, c.frames_left[MHI_CC_VANES_SWING]);
  TEST_ASSERT_EQUAL_UINT32(0, c.unconfirmed);
}

// The AC's frame of the exchange that carried the write predates it: observe
// first, then sent, so that frame does not age the new watch.
static void test_a_newer_write_replaces_the_watched_one(void) {
  MhiCmdConfirm c = fresh();
  uint8_t miso[33], mosi[33] = {0};
  miso_set(miso, 0, 0, 0x80 | 40);
  mhi_cc_sent(&c, miso, kN);
  mhi_cc_observe(&c, mosi);
  miso_set(miso, 0, 0, 0x80 | 50);
  mhi_cc_sent(&c, miso, kN);  // the older one is dropped, not counted
  TEST_ASSERT_EQUAL_UINT16(kN, c.frames_left[MHI_CC_TSETPOINT]);
  mosi[DB2] = 50;
  mhi_cc_observe(&c, mosi);
  TEST_ASSERT_EQUAL_UINT32(0, c.unconfirmed);
}

static void test_a_frame_with_no_write_starts_nothing(void) {
  MhiCmdConfirm c = fresh();
  uint8_t miso[33];
  miso_set(miso, 0x01 | 0x08, 0x02, 0x2c);  // values but no set flags: an idle frame
  mhi_cc_sent(&c, miso, kN);
  for (uint8_t i = 0; i < MHI_CC_FIELDS; i++) TEST_ASSERT_EQUAL_UINT16(0, c.frames_left[i]);
}

// An IR remote press clears every echo flag. Once the flag has shown and is
// gone again with the value still not the written one, the remote took the
// field over: resolved, not counted.
static void test_an_ir_press_inside_the_watch_resolves_without_a_count(void) {
  MhiCmdConfirm c = fresh();
  uint8_t miso[33], mosi[33] = {0};
  miso_set(miso, 0, 0, 0x80 | 44);
  mhi_cc_sent(&c, miso, kN);
  mosi[DB2] = 0x80 | 36;  // the flag shows (earlier write), value still the old one
  mhi_cc_observe(&c, mosi);
  mosi[DB2] = 40;         // an IR press: every flag cleared, another setpoint
  mhi_cc_observe(&c, mosi);
  TEST_ASSERT_EQUAL_UINT16(0, c.frames_left[MHI_CC_TSETPOINT]);
  for (int i = 0; i < 20; i++) mhi_cc_observe(&c, mosi);
  TEST_ASSERT_EQUAL_UINT32(0, c.unconfirmed);
}

// The pair's second frame carries the same bytes but is not armed again (the
// core calls mhi_cc_sent() on the double frame only), so an echo seen between
// the two is kept and a later IR press still resolves the check.
static void test_an_echo_seen_between_the_pair_is_kept(void) {
  MhiCmdConfirm c = fresh();
  uint8_t miso[33], mosi[33] = {0};
  miso_set(miso, 0, 0, 0x80 | 44);
  mhi_cc_sent(&c, miso, kN);  // the double frame D
  mosi[DB2] = 0x80 | 36;      // the echo flag shows, value not yet
  mhi_cc_observe(&c, mosi);
  TEST_ASSERT_TRUE(c.seen_echo[MHI_CC_TSETPOINT]);
  // the odd frame N: no mhi_cc_sent() call
  mhi_cc_observe(&c, mosi);
  TEST_ASSERT_TRUE(c.seen_echo[MHI_CC_TSETPOINT]);
  mosi[DB2] = 40;             // an IR press: flag gone, another value
  mhi_cc_observe(&c, mosi);
  TEST_ASSERT_EQUAL_UINT16(0, c.frames_left[MHI_CC_TSETPOINT]);
  TEST_ASSERT_EQUAL_UINT32(0, c.unconfirmed);
}

// Arming again forgets the echo seen so far: why the core must not do it.
static void test_arming_again_forgets_the_echo(void) {
  MhiCmdConfirm c = fresh();
  uint8_t miso[33], mosi[33] = {0};
  miso_set(miso, 0, 0, 0x80 | 44);
  mhi_cc_sent(&c, miso, kN);
  mosi[DB2] = 0x80 | 36;
  mhi_cc_observe(&c, mosi);
  mhi_cc_sent(&c, miso, kN);
  TEST_ASSERT_FALSE(c.seen_echo[MHI_CC_TSETPOINT]);
}

// A flag that never shows is no press: the AC never took the write.
static void test_no_flag_and_no_value_is_a_lost_write(void) {
  MhiCmdConfirm c = fresh();
  uint8_t miso[33], mosi[33] = {0};
  miso_set(miso, 0, 0, 0x80 | 44);
  mhi_cc_sent(&c, miso, kN);
  mosi[DB2] = 36;  // flag never set
  for (int i = 0; i < kN; i++) mhi_cc_observe(&c, mosi);
  TEST_ASSERT_EQUAL_UINT32(1, c.unconfirmed);
}

// A flag that stays set while the value stays old (the AC clamped or refused
// the write) is counted: the mismatch is reported, not guessed away.
static void test_a_clamped_value_with_the_flag_set_is_counted(void) {
  MhiCmdConfirm c = fresh();
  uint8_t miso[33], mosi[33] = {0};
  miso_set(miso, 0, 0, 0x80 | 44);
  mhi_cc_sent(&c, miso, kN);
  mosi[DB2] = 0x80 | 40;  // the AC settled on 20 degrees
  for (int i = 0; i < kN; i++) mhi_cc_observe(&c, mosi);
  TEST_ASSERT_EQUAL_UINT32(1, c.unconfirmed);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_confirmed_setpoint_is_not_counted);
  RUN_TEST(test_the_echo_flag_alone_is_not_the_value);
  RUN_TEST(test_a_write_that_never_shows_is_counted_once);
  RUN_TEST(test_a_write_of_the_current_value_confirms_at_once);
  RUN_TEST(test_each_field_reads_its_own_bits);
  RUN_TEST(test_a_newer_write_replaces_the_watched_one);
  RUN_TEST(test_a_frame_with_no_write_starts_nothing);
  RUN_TEST(test_an_ir_press_inside_the_watch_resolves_without_a_count);
  RUN_TEST(test_an_echo_seen_between_the_pair_is_kept);
  RUN_TEST(test_arming_again_forgets_the_echo);
  RUN_TEST(test_no_flag_and_no_value_is_a_lost_write);
  RUN_TEST(test_a_clamped_value_with_the_flag_set_is_counted);
  return UNITY_END();
}
