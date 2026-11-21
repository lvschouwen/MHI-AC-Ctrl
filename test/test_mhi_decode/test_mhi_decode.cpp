// Host tests for the frame decode (fork #64): what a valid frame from the AC
// says, reported only when it changed, in the order the old inline code used.
// The sink records every call; the frames are built byte by byte.

#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "mhi_decode.h"
#include "mhi_frame.h"
#include "mhi_vanes.h"

void setUp(void) {}
void tearDown(void) {}

enum Kind { K_STATUS, K_RAW, K_UNKNOWN };
struct Ev {
  Kind kind;
  int status;
  int value;      // status value; for K_UNKNOWN db10
  uint8_t b[4];   // K_RAW bytes; K_UNKNOWN db9 in b[0]
};

struct Fx {
  MhiDecodeState s;
  Ev ev[40];
  size_t n;
  uint32_t now;
  int now_calls;
  uint8_t mosi[33], miso[33];
  uint8_t cnt;
  uint32_t last_troom;
  uint8_t size;
  MhiDecodeSink sink;
};

static void on_status(void* c, ACStatus s, int v) {
  Fx* f = (Fx*)c;
  f->ev[f->n++] = {K_STATUS, (int)s, v, {0, 0, 0, 0}};
}
static void on_raw(void* c, ACStatus s, const uint8_t* b, size_t len) {
  Fx* f = (Fx*)c;
  TEST_ASSERT_EQUAL(4, (int)len);
  f->ev[f->n] = {K_RAW, (int)s, 0, {0, 0, 0, 0}};
  memcpy(f->ev[f->n].b, b, 4);
  f->n++;
}
static uint32_t on_now(void* c) {
  Fx* f = (Fx*)c;
  f->now_calls++;
  return f->now;
}
static void on_unknown(void* c, int db9, int db10) {
  Fx* f = (Fx*)c;
  f->ev[f->n++] = {K_UNKNOWN, 0, db10, {(uint8_t)db9, 0, 0, 0}};
}

static void fx_init(Fx* f, uint8_t size) {
  memset(f, 0, sizeof *f);
  mhi_decode_reset_old(&f->s);
  f->miso[DB3] = 0xff;  // the internal room sensor is in use
  f->mosi[DB9] = 0xff;  // no operating data unless a test sets some
  f->size = size;
  f->sink = {f, on_status, on_raw, on_now, on_unknown};
}

static size_t decode(Fx* f) {
  f->n = 0;
  f->now_calls = 0;
  const MhiDecodeFrame fr = {f->mosi, f->miso, f->size, &f->cnt, &f->last_troom};
  mhi_decode_frame(&f->s, &fr, &f->sink);
  return f->n;
}

static void expect_status(const Fx* f, size_t i, ACStatus s, int v) {
  TEST_ASSERT_TRUE(i < f->n);
  TEST_ASSERT_EQUAL(K_STATUS, f->ev[i].kind);
  TEST_ASSERT_EQUAL_HEX(s, f->ev[i].status);
  TEST_ASSERT_EQUAL_INT(v, f->ev[i].value);
}

// A status frame: power on, cool, fan 2, setpoint 22 degrees, room 0x80.
static void status_frame(Fx* f) {
  f->mosi[DB0] = 0x01 | 0x08;
  f->mosi[DB1] = 0x02;
  f->mosi[DB2] = 0x2c;
  f->mosi[DB3] = 0x80;
  f->mosi[DB4] = 0;
  f->mosi[DB13] = 0x06;  // compressor running, heating bit clear: cooling
}

static void test_the_status_goes_out_in_the_old_order(void) {
  Fx f;
  fx_init(&f, 20);
  status_frame(&f);
  f.now = 10000;  // the internal sensor's jitter limit (5 s) has long passed
  const size_t n = decode(&f);
  size_t i = 0;
  expect_status(&f, i++, status_mode, 0x08);   // mhi_status.h: power on goes after the mode
  expect_status(&f, i++, status_power, 1);
  expect_status(&f, i++, status_fan, 2);
  TEST_ASSERT_EQUAL_HEX(status_vanes, f.ev[i++].status);
  TEST_ASSERT_EQUAL_HEX(status_remote, f.ev[i++].status);
  expect_status(&f, i++, status_troom, 0x80);
  expect_status(&f, i++, status_tsetpoint, 0x2c);
  expect_status(&f, i++, status_errorcode, 0);
  TEST_ASSERT_EQUAL_HEX(status_action, f.ev[i++].status);
  TEST_ASSERT_EQUAL_size_t(i, n);
  TEST_ASSERT_EQUAL_UINT32(10000, f.last_troom);
}

static void test_nothing_changed_reports_nothing_and_a_reset_reports_it_again(void) {
  Fx f;
  fx_init(&f, 20);
  status_frame(&f);
  f.now = 10000;
  decode(&f);
  TEST_ASSERT_EQUAL_size_t(0, decode(&f));
  mhi_decode_reset_old(&f.s);
  f.now = 20000;
  TEST_ASSERT_TRUE(decode(&f) > 5);
}

// The AC's own sensor jitters: while it is the room sensor, a change goes out
// at most every 5 s. A room sensor from MQTT or DS18x20 (MISO DB3 not 0xff)
// goes out at once, and the clock is not even read.
static void test_the_internal_room_sensor_is_held_back_by_the_jitter_limit(void) {
  Fx f;
  fx_init(&f, 20);
  status_frame(&f);
  f.now = 10000;
  decode(&f);
  f.mosi[DB3] = 0x81;
  f.now = 12000;
  decode(&f);
  for (size_t i = 0; i < f.n; i++) TEST_ASSERT_NOT_EQUAL(status_troom, f.ev[i].status);
  f.now = 15001;
  decode(&f);
  TEST_ASSERT_EQUAL_size_t(1, f.n);
  expect_status(&f, 0, status_troom, 0x81);
  TEST_ASSERT_EQUAL_UINT32(15001, f.last_troom);
}

static void test_an_external_room_sensor_goes_out_at_once(void) {
  Fx f;
  fx_init(&f, 20);
  f.miso[DB3] = 90;  // set/Troom or a DS18x20
  f.last_troom = 123;
  f.mosi[DB3] = 0x81;
  f.mosi[DB9] = 0xff;
  decode(&f);
  size_t troom = 0;
  for (size_t i = 0; i < f.n; i++)
    if (f.ev[i].status == status_troom) troom++;
  TEST_ASSERT_EQUAL_size_t(1, troom);
  TEST_ASSERT_EQUAL_UINT32(0, f.last_troom);
  TEST_ASSERT_EQUAL_INT(0, f.now_calls);
}

struct OpRow {
  uint8_t db9, db6, db10, db11, db12;
  ACStatus status;
  int value;
  bool repeats;  // error operating data is reported every time, operating data only when it changes
};

static const OpRow kOp[] = {
    {0x94, 0x80, 0x10, 0x10, 0x01, opdata_kwh, 0x0110, false},
    {0x02, 0x80, 0x1a, 0x00, 0x00, opdata_mode, 0x28, false},  // (DB10 & 0x0f) << 2
    {0x02, 0x80, 0x2a, 0x00, 0x00, erropdata_mode, 0x28, true},
    {0x05, 0x80, 0x13, 0x2c, 0x00, opdata_tsetpoint, 0x2c, false},
    {0x05, 0x80, 0x33, 0x2c, 0x00, erropdata_tsetpoint, 0x2c, true},
    {0x81, 0x80, 0x20, 0x41, 0x00, opdata_thi_r1, 0x41, false},
    {0x81, 0x00, 0x10, 0x42, 0x00, opdata_thi_r2, 0x42, false},
    {0x87, 0x80, 0x10, 0x43, 0x00, opdata_thi_r3, 0x43, false},
    {0x80, 0x80, 0x20, 0x44, 0x00, opdata_return_air, 0x44, false},
    {0x80, 0x00, 0x10, 0x45, 0x00, opdata_outdoor, 0x45, false},
    {0x1f, 0x80, 0x13, 0x00, 0x00, opdata_iu_fanspeed, 0x03, false},
    {0x1f, 0x00, 0x12, 0x00, 0x00, opdata_ou_fanspeed, 0x02, false},
    {0x1e, 0x80, 0x10, 0x46, 0x00, opdata_total_iu_run, 0x46, false},
    {0x1e, 0x00, 0x11, 0x47, 0x00, opdata_total_comp_run, 0x47, false},
    {0x82, 0x00, 0x10, 0x48, 0x00, opdata_tho_r1, 0x48, false},
    {0x11, 0x00, 0x12, 0x34, 0x00, opdata_comp, 0x234, false},  // 0x1234 & 0x0fff
    {0x85, 0x00, 0x10, 0x49, 0x00, opdata_td, 0x49, false},
    {0x90, 0x00, 0x10, 0x4a, 0x00, opdata_ct, 0x4a, false},
    {0xb1, 0x00, 0x10, 0x4b, 0x00, opdata_tdsh, 0x4b, false},
    {0x7c, 0x00, 0x10, 0x4c, 0x00, opdata_protection_no, 0x4c, false},
    {0x0c, 0x00, 0x13, 0x00, 0x00, opdata_defrost, 0x01, false},  // DB10 & 1
    {0x13, 0x00, 0x10, 0x20, 0x03, opdata_ou_eev1, 0x0320, false},
    {0x13, 0x00, 0x20, 0x20, 0x03, erropdata_ou_eev1, 0x0320, true},
    {0x85, 0x00, 0x20, 0x4d, 0x00, erropdata_td, 0x4d, true},
    {0x45, 0x80, 0x11, 0x05, 0x00, erropdata_errorcode, 0x05, true},
};

static void test_every_operating_data_code(void) {
  for (size_t r = 0; r < sizeof kOp / sizeof kOp[0]; r++) {
    const OpRow& row = kOp[r];
    Fx f;
    fx_init(&f, 20);
    f.mosi[DB3] = 0xfe;  // equal to the first "old" Troom: no status goes out
    f.mosi[DB0] = 0xff & 0x00;
    mhi_decode_reset_old(&f.s);
    // report the status once so only the operating data speaks
    f.mosi[DB4] = 0;
    f.s.status_power_old = 0; f.s.status_mode_old = 0; f.s.status_fan_old = 0; f.s.status_vanes_old = mhi_vanes_decode(0, 0);
    f.s.status_remote_old = 1; f.s.status_tsetpoint_old = 0; f.s.status_errorcode_old = 0;
    f.s.status_action_old = 0xff;
    decode(&f);  // whatever that was, it is reported now
    f.mosi[DB9] = row.db9; f.mosi[DB6] = row.db6; f.mosi[DB10] = row.db10; f.mosi[DB11] = row.db11; f.mosi[DB12] = row.db12;
    char msg[40];
    snprintf(msg, sizeof msg, "code %02x db6 %02x db10 %02x", row.db9, row.db6, row.db10);
    decode(&f);
    TEST_ASSERT_EQUAL_MESSAGE(1, f.n, msg);
    TEST_ASSERT_EQUAL_HEX_MESSAGE(row.status, f.ev[0].status, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(row.value, f.ev[0].value, msg);
    // the same frame again: the AC repeats it; the frame is not "new" to the core, but the decoder alone
    // reports only a changed reading, except error operating data
    decode(&f);
    TEST_ASSERT_EQUAL_MESSAGE(row.repeats ? 1 : 0, f.n, msg);
  }
}

static void test_the_error_data_count_is_stored_for_the_frame_build(void) {
  Fx f;
  fx_init(&f, 20);
  f.mosi[DB9] = 0x45; f.mosi[DB6] = 0x80; f.mosi[DB10] = 0x12; f.mosi[DB11] = 3;
  decode(&f);
  TEST_ASSERT_EQUAL_UINT8(7, f.cnt);
  f.mosi[DB11] = 0xff;  // a byte: wraps, as the old assignment did
  decode(&f);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)(0xff + 4), f.cnt);
}

static void test_silent_is_reported_on_a_change_only(void) {
  Fx f;
  fx_init(&f, 20);
  f.mosi[DB9] = 0xdd; f.mosi[DB10] = 0x80; f.mosi[DB11] = 0x20;
  decode(&f);
  bool found = false;
  for (size_t i = 0; i < f.n; i++)
    if (f.ev[i].status == status_silent) { found = true; TEST_ASSERT_EQUAL_INT(1, f.ev[i].value); }
  TEST_ASSERT_TRUE(found);
  f.mosi[DB11] = 0x20;
  TEST_ASSERT_EQUAL_size_t(0, decode(&f));
  f.mosi[DB11] = 0x00;
  decode(&f);
  expect_status(&f, 0, status_silent, 0);
  f.mosi[DB10] = 0x00;  // not the answer's type byte: ignored
  f.mosi[DB11] = 0x20;
  TEST_ASSERT_EQUAL_size_t(0, decode(&f));
}

static void test_an_unknown_code_reports_raw_then_number_then_the_serial_line(void) {
  Fx f;
  fx_init(&f, 20);
  status_frame(&f);
  f.now = 10000;
  decode(&f);
  f.mosi[DB9] = 0x21; f.mosi[DB10] = 0x03; f.mosi[DB11] = 0x55; f.mosi[DB12] = 0x66;
  decode(&f);
  TEST_ASSERT_EQUAL_size_t(3, f.n);
  TEST_ASSERT_EQUAL(K_RAW, f.ev[0].kind);
  TEST_ASSERT_EQUAL_HEX(raw_opdata, f.ev[0].status);
  TEST_ASSERT_EQUAL_HEX8(0x21, f.ev[0].b[0]);
  TEST_ASSERT_EQUAL_HEX8(0x03, f.ev[0].b[1]);
  TEST_ASSERT_EQUAL_HEX8(0x55, f.ev[0].b[2]);
  TEST_ASSERT_EQUAL_HEX8(0x66, f.ev[0].b[3]);
  expect_status(&f, 1, opdata_unknown, 0x0321);
  TEST_ASSERT_EQUAL(K_UNKNOWN, f.ev[2].kind);
  TEST_ASSERT_EQUAL_HEX8(0x21, f.ev[2].b[0]);
  TEST_ASSERT_EQUAL_INT(0x03, f.ev[2].value);
  decode(&f);  // reported again each time: unknown data has no old value
  TEST_ASSERT_EQUAL_size_t(3, f.n);
}

static void test_the_dummy_and_default_codes_say_nothing(void) {
  Fx f;
  fx_init(&f, 20);
  f.mosi[DB9] = 0x00;
  decode(&f);
  const size_t first = f.n;  // the status of an empty frame, reported once
  (void)first;
  TEST_ASSERT_EQUAL_size_t(0, decode(&f));
  f.mosi[DB9] = 0xff;
  TEST_ASSERT_EQUAL_size_t(0, decode(&f));
}

static void test_the_33_byte_frame_adds_left_right_and_3d_auto_first(void) {
  Fx f;
  fx_init(&f, 33);
  f.mosi[DB16] = 0x12;
  f.mosi[DB17] = 0x04;
  decode(&f);
  TEST_ASSERT_TRUE(f.n >= 2);
  TEST_ASSERT_EQUAL_HEX(status_vanesLR, f.ev[0].status);
  expect_status(&f, 1, status_3Dauto, MHI_DECODE_DAUTO_ON);
  Fx g;
  fx_init(&g, 20);
  g.mosi[DB16] = 0x12;
  g.mosi[DB17] = 0x04;
  decode(&g);
  for (size_t i = 0; i < g.n; i++) {
    TEST_ASSERT_NOT_EQUAL(status_vanesLR, g.ev[i].status);
    TEST_ASSERT_NOT_EQUAL(status_3Dauto, g.ev[i].status);
  }
}

// reset_system_values(): the 11 system values are reported again, nothing else.
static void test_a_system_reset_only_repeats_system_values(void) {
  Fx f;
  fx_init(&f, 20);
  f.mosi[DB9] = 0x80; f.mosi[DB6] = 0x00; f.mosi[DB10] = 0x10; f.mosi[DB11] = 0x45;  // OUTDOOR, a system value
  decode(&f);
  TEST_ASSERT_EQUAL_size_t(0, decode(&f));
  mhi_decode_reset_system(&f.s);
  size_t outdoor = 0;
  decode(&f);
  for (size_t i = 0; i < f.n; i++)
    if (f.ev[i].status == opdata_outdoor) outdoor++;
  TEST_ASSERT_EQUAL_size_t(1, outdoor);
  TEST_ASSERT_EQUAL_size_t(1, f.n);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_status_goes_out_in_the_old_order);
  RUN_TEST(test_nothing_changed_reports_nothing_and_a_reset_reports_it_again);
  RUN_TEST(test_the_internal_room_sensor_is_held_back_by_the_jitter_limit);
  RUN_TEST(test_an_external_room_sensor_goes_out_at_once);
  RUN_TEST(test_every_operating_data_code);
  RUN_TEST(test_the_error_data_count_is_stored_for_the_frame_build);
  RUN_TEST(test_silent_is_reported_on_a_change_only);
  RUN_TEST(test_an_unknown_code_reports_raw_then_number_then_the_serial_line);
  RUN_TEST(test_the_dummy_and_default_codes_say_nothing);
  RUN_TEST(test_the_33_byte_frame_adds_left_right_and_3d_auto_first);
  RUN_TEST(test_a_system_reset_only_repeats_system_values);
  return UNITY_END();
}
