// Host tests for the set/ command parser (issue #55).
//
// The table was written against the callback as it stood in main.cpp before
// the move, and pins what it accepted and refused: same payloads, same
// errors, same topics. The default texts are spelled out here on purpose, not
// read from MHI-AC-Ctrl.h: they are the MQTT contract hass-config consumes.
// Every build variant is a config, so all run in one native test.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "mhi_cmd.h"

void setUp(void) {}
void tearDown(void) {}

static const MhiFanNames kFan = {{"1", "2", "3", "4"}, "Auto"};
static const MhiVanesNames kVanes = {{"Up", "UpCenter", "CenterDown", "Down"}, "Swing", "?"};
static const MhiVanesLrNames kVanesLr = {{"Left", "LeftCenter", "Center", "CenterRight", "Right", "Wide", "Spot"}, "Swing"};

static MhiCmdConfig defaults() {
  MhiCmdConfig c = {};
  c.set_prefix = "unit/set/";
  c.t_power = "Power";
  c.t_mode = "Mode";
  c.t_tsetpoint = "Tsetpoint";
  c.t_fan = "Fan";
  c.t_vanes = "Vanes";
  c.t_vaneslr = "VanesLR";
  c.t_3dauto = "3Dauto";
  c.t_troom = "Troom";
  c.t_erropdata = "ErrOpData";
  c.t_reset = "reset";
  c.t_passive = "PassiveMode";
  c.t_diag = "Diag";
  c.t_opdata = "OpDataRequest";
  c.t_silent = "Silent";
  c.power_on = "On";
  c.power_off = "Off";
  c.mode_off = "Off";
  c.mode_auto = "Auto";
  c.mode_dry = "Dry";
  c.mode_cool = "Cool";
  c.mode_fan = "Fan";
  c.mode_heat = "Heat";
  c.dauto_on = "On";
  c.dauto_off = "Off";
  c.reset = "reset";
  c.reset_crash = "crash";
  c.passive_on = "On";
  c.passive_off = "Off";
  c.diag_on = "On";
  c.diag_off = "Off";
  c.silent_on = "On";
  c.silent_off = "Off";
  c.fan_names = &kFan;
  c.vanes_names = &kVanes;
  c.vanes_lr_names = NULL;  // 20-byte frame
  c.mode_carries_power = false;
  c.allow_crash = false;
  return c;
}

static MhiCmdConfig extended() {
  MhiCmdConfig c = defaults();
  c.vanes_lr_names = &kVanesLr;
  return c;
}

struct Row {
  const char* topic;    // below the prefix
  const char* payload;
  MhiCmdKind kind;
  bool error;
  int value;
};

static MhiCmd parse(const MhiCmdConfig* c, const char* suffix, const char* payload) {
  char topic[96];
  snprintf(topic, sizeof topic, "%s%s", c->set_prefix, suffix);
  MhiCmd cmd;
  mhi_cmd_parse(c, topic, payload, false, false, &cmd);
  return cmd;
}

static void run_rows(const MhiCmdConfig* c, const Row* rows, size_t n) {
  for (size_t i = 0; i < n; i++) {
    char msg[96];
    snprintf(msg, sizeof msg, "%s \"%s\"", rows[i].topic, rows[i].payload);
    const MhiCmd cmd = parse(c, rows[i].topic, rows[i].payload);
    TEST_ASSERT_EQUAL_MESSAGE(rows[i].kind, cmd.kind, msg);
    TEST_ASSERT_EQUAL_MESSAGE(rows[i].error, cmd.error, msg);
    if (!rows[i].error) TEST_ASSERT_EQUAL_INT_MESSAGE(rows[i].value, cmd.value, msg);
  }
}
#define RUN(cfg, rows) run_rows(&(cfg), rows, sizeof(rows) / sizeof(rows[0]))

static const Row kCommon[] = {
    // Power (not in a POWERON_WHEN_CHANGING_MODE build)
    {"Power", "On", MHI_CMD_POWER, false, 1},
    {"Power", "Off", MHI_CMD_POWER, false, 0},
    {"Power", "on", MHI_CMD_POWER, true, 0},
    {"Power", "ON", MHI_CMD_POWER, true, 0},
    {"Power", "", MHI_CMD_POWER, true, 0},
    {"Power", "1", MHI_CMD_POWER, true, 0},
    // Mode: five modes, exact case; "Off" is not a mode here
    {"Mode", "Auto", MHI_CMD_MODE, false, MHI_CMD_MODE_AUTO},
    {"Mode", "Dry", MHI_CMD_MODE, false, MHI_CMD_MODE_DRY},
    {"Mode", "Cool", MHI_CMD_MODE, false, MHI_CMD_MODE_COOL},
    {"Mode", "Fan", MHI_CMD_MODE, false, MHI_CMD_MODE_FAN},
    {"Mode", "Heat", MHI_CMD_MODE, false, MHI_CMD_MODE_HEAT},
    {"Mode", "Off", MHI_CMD_MODE, true, 0},
    {"Mode", "cool", MHI_CMD_MODE, true, 0},
    {"Mode", "Stop", MHI_CMD_MODE, true, 0},
    {"Mode", "", MHI_CMD_MODE, true, 0},
    {"Mode", "Cool ", MHI_CMD_MODE, true, 0},
    // Tsetpoint: only the number is parsed here; the guard decides the range
    {"Tsetpoint", "20", MHI_CMD_TSETPOINT, false, 0},
    {"Tsetpoint", "20.5", MHI_CMD_TSETPOINT, false, 0},
    {"Tsetpoint", " 22 ", MHI_CMD_TSETPOINT, false, 0},
    {"Tsetpoint", "5", MHI_CMD_TSETPOINT, false, 0},
    {"Tsetpoint", "abc", MHI_CMD_TSETPOINT, true, 0},
    {"Tsetpoint", "21.5abc", MHI_CMD_TSETPOINT, true, 0},
    {"Tsetpoint", "", MHI_CMD_TSETPOINT, true, 0},
    {"Tsetpoint", "nan", MHI_CMD_TSETPOINT, true, 0},
    {"Tsetpoint", "inf", MHI_CMD_TSETPOINT, true, 0},
    // Fan: names and "1".."4", Auto
    {"Fan", "1", MHI_CMD_FAN, false, 1},
    {"Fan", "2", MHI_CMD_FAN, false, 2},
    {"Fan", "3", MHI_CMD_FAN, false, 3},
    {"Fan", "4", MHI_CMD_FAN, false, 4},
    {"Fan", "Auto", MHI_CMD_FAN, false, MHI_FAN_AUTO},
    {"Fan", "auto", MHI_CMD_FAN, true, 0},
    {"Fan", "0", MHI_CMD_FAN, true, 0},
    {"Fan", "5", MHI_CMD_FAN, true, 0},
    {"Fan", "", MHI_CMD_FAN, true, 0},
    // Vanes: names and "1".."5"
    {"Vanes", "Up", MHI_CMD_VANES, false, 1},
    {"Vanes", "UpCenter", MHI_CMD_VANES, false, 2},
    {"Vanes", "CenterDown", MHI_CMD_VANES, false, 3},
    {"Vanes", "Down", MHI_CMD_VANES, false, 4},
    {"Vanes", "Swing", MHI_CMD_VANES, false, 5},
    {"Vanes", "1", MHI_CMD_VANES, false, 1},
    {"Vanes", "5", MHI_CMD_VANES, false, 5},
    {"Vanes", "6", MHI_CMD_VANES, true, 0},
    {"Vanes", "0", MHI_CMD_VANES, true, 0},
    {"Vanes", "?", MHI_CMD_VANES, true, 0},
    {"Vanes", "up", MHI_CMD_VANES, true, 0},
    {"Vanes", "", MHI_CMD_VANES, true, 0},
    // Troom: only the number is parsed here; the caller tests plausibility
    {"Troom", "21.5", MHI_CMD_TROOM, false, 0},
    {"Troom", "-5", MHI_CMD_TROOM, false, 0},
    {"Troom", "100", MHI_CMD_TROOM, false, 0},
    {"Troom", "unavailable", MHI_CMD_TROOM, true, 0},
    {"Troom", "", MHI_CMD_TROOM, true, 0},
    // No payload to check
    {"ErrOpData", "", MHI_CMD_ERROPDATA, false, 0},
    {"ErrOpData", "anything", MHI_CMD_ERROPDATA, false, 0},
    // reset: the restart only; "crash" is a test build's
    {"reset", "reset", MHI_CMD_RESET, false, MHI_CMD_RESET_RESTART},
    {"reset", "Reset", MHI_CMD_RESET, true, 0},
    {"reset", "crash", MHI_CMD_RESET, true, 0},
    {"reset", "", MHI_CMD_RESET, true, 0},
    // On/off topics
    {"PassiveMode", "On", MHI_CMD_PASSIVE, false, 1},
    {"PassiveMode", "Off", MHI_CMD_PASSIVE, false, 0},
    {"PassiveMode", "on", MHI_CMD_PASSIVE, true, 0},
    {"Diag", "On", MHI_CMD_DIAG, false, 1},
    {"Diag", "Off", MHI_CMD_DIAG, false, 0},
    {"Diag", "1", MHI_CMD_DIAG, true, 0},
    {"Silent", "On", MHI_CMD_SILENT, false, 1},
    {"Silent", "Off", MHI_CMD_SILENT, false, 0},
    {"Silent", "true", MHI_CMD_SILENT, true, 0},
    // Unknown topics
    {"Bogus", "On", MHI_CMD_UNKNOWN, false, 0},
    {"", "On", MHI_CMD_UNKNOWN, false, 0},
    {"mode", "Cool", MHI_CMD_UNKNOWN, false, 0},
    {"Mode/", "Cool", MHI_CMD_UNKNOWN, false, 0},
    {"Modes", "Cool", MHI_CMD_UNKNOWN, false, 0},
    {"Mod", "Cool", MHI_CMD_UNKNOWN, false, 0},
    // Topics that exist only in the extended frame are unknown here
    {"VanesLR", "Left", MHI_CMD_UNKNOWN, false, 0},
    {"3Dauto", "On", MHI_CMD_UNKNOWN, false, 0},
};

static void test_every_topic_and_payload_of_a_default_build(void) {
  const MhiCmdConfig c = defaults();
  RUN(c, kCommon);
}

static void test_numbers_are_carried(void) {
  const MhiCmdConfig c = defaults();
  MhiCmd cmd = parse(&c, "Tsetpoint", "20.5");
  TEST_ASSERT_EQUAL_FLOAT(20.5f, cmd.fvalue);
  cmd = parse(&c, "Troom", " 22 ");
  TEST_ASSERT_EQUAL_FLOAT(22.0f, cmd.fvalue);
  cmd = parse(&c, "Troom", "junk");  // refused, not 0 degrees C (sweep #33 F1)
  TEST_ASSERT_TRUE(cmd.error);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, cmd.fvalue);
}

static void test_opdata_request(void) {
  const MhiCmdConfig c = defaults();
  MhiCmd cmd = parse(&c, "OpDataRequest", "c021");
  TEST_ASSERT_EQUAL(MHI_CMD_OPDATA, cmd.kind);
  TEST_ASSERT_FALSE(cmd.error);
  TEST_ASSERT_EQUAL_UINT8(0xc0, cmd.a);
  TEST_ASSERT_EQUAL_UINT8(0x21, cmd.b);
  cmd = parse(&c, "OpDataRequest", "40FF");
  TEST_ASSERT_FALSE(cmd.error);
  TEST_ASSERT_EQUAL_UINT8(0x40, cmd.a);
  TEST_ASSERT_EQUAL_UINT8(0xff, cmd.b);
  static const char* bad[] = {"", "c02", "c0210", "8021", "zz21", "c0 21"};
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    cmd = parse(&c, "OpDataRequest", bad[i]);
    TEST_ASSERT_EQUAL_MESSAGE(MHI_CMD_OPDATA, cmd.kind, bad[i]);
    TEST_ASSERT_TRUE_MESSAGE(cmd.error, bad[i]);
  }
}

static void test_a_topic_outside_the_prefix_is_unknown(void) {
  const MhiCmdConfig c = defaults();
  MhiCmd cmd;
  mhi_cmd_parse(&c, "unit/Mode", "Cool", false, false, &cmd);
  TEST_ASSERT_EQUAL(MHI_CMD_UNKNOWN, cmd.kind);
  mhi_cmd_parse(&c, "other/set/Mode", "Cool", false, false, &cmd);
  TEST_ASSERT_EQUAL(MHI_CMD_UNKNOWN, cmd.kind);
  mhi_cmd_parse(&c, "unit/set", "Cool", false, false, &cmd);
  TEST_ASSERT_EQUAL(MHI_CMD_UNKNOWN, cmd.kind);
  mhi_cmd_parse(&c, "", "Cool", false, false, &cmd);
  TEST_ASSERT_EQUAL(MHI_CMD_UNKNOWN, cmd.kind);
}

static const Row kExtended[] = {
    {"VanesLR", "Left", MHI_CMD_VANESLR, false, 1},
    {"VanesLR", "LeftCenter", MHI_CMD_VANESLR, false, 2},
    {"VanesLR", "Center", MHI_CMD_VANESLR, false, 3},
    {"VanesLR", "CenterRight", MHI_CMD_VANESLR, false, 4},
    {"VanesLR", "Right", MHI_CMD_VANESLR, false, 5},
    {"VanesLR", "Wide", MHI_CMD_VANESLR, false, 6},
    {"VanesLR", "Spot", MHI_CMD_VANESLR, false, 7},
    {"VanesLR", "Swing", MHI_CMD_VANESLR, false, 8},
    {"VanesLR", "8", MHI_CMD_VANESLR, false, 8},
    {"VanesLR", "1", MHI_CMD_VANESLR, false, 1},
    {"VanesLR", "9", MHI_CMD_VANESLR, true, 0},
    {"VanesLR", "left", MHI_CMD_VANESLR, true, 0},
    {"VanesLR", "", MHI_CMD_VANESLR, true, 0},
    {"3Dauto", "On", MHI_CMD_3DAUTO, false, 1},
    {"3Dauto", "Off", MHI_CMD_3DAUTO, false, 0},
    {"3Dauto", "on", MHI_CMD_3DAUTO, true, 0},
    // everything of the default build still works
    {"Mode", "Heat", MHI_CMD_MODE, false, MHI_CMD_MODE_HEAT},
    {"Vanes", "Swing", MHI_CMD_VANES, false, 5},
};

static void test_extended_frame_adds_vaneslr_and_3dauto(void) {
  const MhiCmdConfig c = extended();
  RUN(c, kExtended);
}

// POWERON_WHEN_CHANGING_MODE: no Power topic (unknown, so "unknown command"),
// and Mode accepts the off text, checked first.
static const Row kPoweron[] = {
    {"Power", "On", MHI_CMD_UNKNOWN, false, 0},
    {"Power", "Off", MHI_CMD_UNKNOWN, false, 0},
    {"Mode", "Off", MHI_CMD_MODE, false, MHI_CMD_MODE_OFF},
    {"Mode", "Cool", MHI_CMD_MODE, false, MHI_CMD_MODE_COOL},
    {"Mode", "off", MHI_CMD_MODE, true, 0},
    {"Mode", "Stop", MHI_CMD_MODE, true, 0},
};

static void test_poweron_when_changing_mode(void) {
  MhiCmdConfig c = defaults();
  c.mode_carries_power = true;
  RUN(c, kPoweron);
}

static const Row kCrash[] = {
    {"reset", "reset", MHI_CMD_RESET, false, MHI_CMD_RESET_RESTART},
    {"reset", "crash", MHI_CMD_RESET, false, MHI_CMD_RESET_CRASH},
    {"reset", "Crash", MHI_CMD_RESET, true, 0},
};

static void test_reset_crash_build(void) {
  MhiCmdConfig c = defaults();
  c.allow_crash = true;
  RUN(c, kCrash);
}

// A build with Home Assistant's lower-case payloads (the [ha_payloads] set).
static void test_custom_payload_texts(void) {
  static const MhiFanNames fan = {{"Low", "Medium", "High", "Top"}, "Auto"};
  MhiCmdConfig c = defaults();
  c.mode_carries_power = true;
  c.mode_off = "off";
  c.mode_auto = "auto";
  c.mode_dry = "dry";
  c.mode_cool = "cool";
  c.mode_fan = "fan_only";
  c.mode_heat = "heat";
  c.fan_names = &fan;
  static const Row rows[] = {
      {"Mode", "off", MHI_CMD_MODE, false, MHI_CMD_MODE_OFF},
      {"Mode", "cool", MHI_CMD_MODE, false, MHI_CMD_MODE_COOL},
      {"Mode", "fan_only", MHI_CMD_MODE, false, MHI_CMD_MODE_FAN},
      {"Mode", "Cool", MHI_CMD_MODE, true, 0},
      {"Mode", "Fan", MHI_CMD_MODE, true, 0},
      {"Fan", "Low", MHI_CMD_FAN, false, 1},
      {"Fan", "Top", MHI_CMD_FAN, false, 4},
      {"Fan", "4", MHI_CMD_FAN, false, 4},
      {"Fan", "Auto", MHI_CMD_FAN, false, MHI_FAN_AUTO},
  };
  RUN(c, rows);
}

// Renamed topics are matched, the old names are not.
static void test_custom_topic_names(void) {
  MhiCmdConfig c = defaults();
  c.t_mode = "ModeX";
  const MhiCmd cmd = parse(&c, "ModeX", "Cool");
  TEST_ASSERT_EQUAL(MHI_CMD_MODE, cmd.kind);
  TEST_ASSERT_EQUAL(MHI_CMD_UNKNOWN, parse(&c, "Mode", "Cool").kind);
}

// Fork #67: a retained set/* message is dropped whatever its topic or payload,
// with nothing to reply; a live one is parsed as usual.
static void test_a_retained_message_is_dropped_for_every_topic_kind(void) {
  const MhiCmdConfig c = extended();
  static const Row all[] = {
      {"Mode", "Cool", MHI_CMD_MODE, false, MHI_CMD_MODE_COOL},
      {"Power", "On", MHI_CMD_POWER, false, 1},
      {"Tsetpoint", "21", MHI_CMD_TSETPOINT, false, 0},
      {"Fan", "Auto", MHI_CMD_FAN, false, MHI_FAN_AUTO},
      {"Vanes", "Swing", MHI_CMD_VANES, false, 5},
      {"VanesLR", "Left", MHI_CMD_VANESLR, false, 1},
      {"3Dauto", "On", MHI_CMD_3DAUTO, false, 1},
      {"Troom", "21.5", MHI_CMD_TROOM, false, 0},
      {"ErrOpData", "", MHI_CMD_ERROPDATA, false, 0},
      {"reset", "reset", MHI_CMD_RESET, false, MHI_CMD_RESET_RESTART},
      {"PassiveMode", "On", MHI_CMD_PASSIVE, false, 1},
      {"Diag", "On", MHI_CMD_DIAG, false, 1},
      {"OpDataRequest", "c021", MHI_CMD_OPDATA, false, 0},
      {"Silent", "On", MHI_CMD_SILENT, false, 1},
      // invalid payloads and unknown topics get no reply either
      {"Mode", "Bogus", MHI_CMD_MODE, true, 0},
      {"reset", "nonsense", MHI_CMD_RESET, true, 0},
      {"Bogus", "x", MHI_CMD_UNKNOWN, false, 0},
  };
  for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
    char topic[96], msg[96];
    snprintf(topic, sizeof topic, "%s%s", c.set_prefix, all[i].topic);
    snprintf(msg, sizeof msg, "%s \"%s\"", all[i].topic, all[i].payload);
    MhiCmd cmd;
    mhi_cmd_parse(&c, topic, all[i].payload, true, false, &cmd);
    TEST_ASSERT_EQUAL_MESSAGE(MHI_CMD_DROPPED, cmd.kind, msg);
    mhi_cmd_parse(&c, topic, all[i].payload, false, false, &cmd);
    TEST_ASSERT_EQUAL_MESSAGE(all[i].kind, cmd.kind, msg);
    TEST_ASSERT_EQUAL_MESSAGE(all[i].error, cmd.error, msg);
  }
}

// Fork #50: the boot-loop guard for set/reset does not rest on the retain peek
// alone. Inside the subscribe window a reset is dropped, whatever the payload
// and whether or not the flag says retained; nothing else is affected.
static void test_a_reset_inside_the_subscribe_window_is_dropped(void) {
  MhiCmdConfig c = defaults();
  c.allow_crash = true;
  MhiCmd cmd;
  static const char* payloads[] = {"reset", "crash", "nonsense", ""};
  for (size_t i = 0; i < sizeof payloads / sizeof payloads[0]; i++) {
    mhi_cmd_parse(&c, "unit/set/reset", payloads[i], false, true, &cmd);
    TEST_ASSERT_EQUAL_MESSAGE(MHI_CMD_DROPPED, cmd.kind, payloads[i]);
  }
  mhi_cmd_parse(&c, "unit/set/reset", "crash", false, false, &cmd);
  TEST_ASSERT_EQUAL(MHI_CMD_RESET, cmd.kind);
  TEST_ASSERT_EQUAL(MHI_CMD_RESET_CRASH, cmd.value);
  // A live command inside the window (HA's setpoint restore, Troom) goes through.
  mhi_cmd_parse(&c, "unit/set/Tsetpoint", "15", false, true, &cmd);
  TEST_ASSERT_EQUAL(MHI_CMD_TSETPOINT, cmd.kind);
  TEST_ASSERT_FALSE(cmd.error);
  mhi_cmd_parse(&c, "unit/set/Troom", "21.5", false, true, &cmd);
  TEST_ASSERT_EQUAL(MHI_CMD_TROOM, cmd.kind);
  mhi_cmd_parse(&c, "unit/set/Mode", "Cool", false, true, &cmd);
  TEST_ASSERT_EQUAL(MHI_CMD_MODE, cmd.kind);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_every_topic_and_payload_of_a_default_build);
  RUN_TEST(test_numbers_are_carried);
  RUN_TEST(test_opdata_request);
  RUN_TEST(test_a_topic_outside_the_prefix_is_unknown);
  RUN_TEST(test_extended_frame_adds_vaneslr_and_3dauto);
  RUN_TEST(test_poweron_when_changing_mode);
  RUN_TEST(test_reset_crash_build);
  RUN_TEST(test_custom_payload_texts);
  RUN_TEST(test_custom_topic_names);
  RUN_TEST(test_a_retained_message_is_dropped_for_every_topic_kind);
  RUN_TEST(test_a_reset_inside_the_subscribe_window_is_dropped);
  return UNITY_END();
}
