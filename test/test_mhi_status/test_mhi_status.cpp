// Host tests for reporting power and mode (issue #5).
//
// With POWERON_WHEN_CHANGING_MODE, Home Assistant reads on/off from the Mode
// topic alone. v2.8 published the remembered mode before "off" after every
// MQTT reconnect, so a unit that was off briefly read as cooling, and a
// "window open while the AC is on" automation fired on it.

#include <unity.h>
#include <string.h>

#include "mhi_status.h"

void setUp(void) {}
void tearDown(void) {}

// DB0 as the AC sends it: bit 0 is power, bits 2-4 the mode.
static const uint8_t kCoolOff = 0x08;
static const uint8_t kCoolOn = 0x09;
static const uint8_t kHeatOff = 0x10;
static const uint8_t kHeatOn = 0x11;
static const uint8_t kCool = 0x08;  // mode bits alone

// A controller reduced to the two steps under test: the SPI parser turns DB0
// into changes, the status handler decides what Mode carries. The glue matches
// MHI-AC-Ctrl-core.cpp and main.cpp. Records every Mode payload, comma-separated.
struct Unit {
  uint8_t power_old;
  uint8_t mode_old;
  MhiModeTopic topic;
  char mode_topic[64];
};

static const char* mode_payload(uint8_t mode) {
  switch (mode) {
    case 0x00: return "auto";
    case 0x04: return "dry";
    case 0x08: return "cool";
    case 0x0c: return "fan";
    case 0x10: return "heat";
  }
  return "?";
}

static void publish_mode(Unit* unit, const char* payload) {
  if (unit->mode_topic[0] != '\0')
    strcat(unit->mode_topic, ",");
  strcat(unit->mode_topic, payload);
}

static void report_mode(Unit* unit, uint8_t mode) {
  if (mhi_mode_topic_on_mode(&unit->topic, mode))
    publish_mode(unit, mode_payload(mode));
}

static void receive_frame(Unit* unit, uint8_t db0) {
  MhiDb0Change changes[2];
  const size_t count = mhi_db0_changes(db0, &unit->power_old, &unit->mode_old, changes);
  for (size_t i = 0; i < count; i++) {
    if (changes[i].field == MHI_DB0_MODE) {
      report_mode(unit, changes[i].value);
      continue;
    }
    if (mhi_mode_topic_on_power(&unit->topic, changes[i].value) == MHI_MODE_TOPIC_OFF)
      publish_mode(unit, "off");
    else
      report_mode(unit, unit->topic.mode);  // main.cpp re-enters status_mode
  }
}

// Fresh after power-up: nothing reported, nothing known.
static Unit booted(void) {
  Unit unit = {MHI_STATUS_UNKNOWN, MHI_STATUS_UNKNOWN, {MHI_STATUS_UNKNOWN, MHI_STATUS_UNKNOWN}, ""};
  return unit;
}

// Has been running in this state; everything reported.
static Unit running(uint8_t power, uint8_t mode) {
  Unit unit = {power, mode, {power, mode}, ""};
  return unit;
}

// Was running, then reconnected to MQTT: reset_old_values() forgot what the
// parser reported, but the status handler still knows.
static Unit reconnected(uint8_t power, uint8_t mode) {
  Unit unit = {MHI_STATUS_UNKNOWN, MHI_STATUS_UNKNOWN, {power, mode}, ""};
  return unit;
}

static const char* last_payload(const Unit* unit) {
  const char* comma = strrchr(unit->mode_topic, ',');
  return comma ? comma + 1 : unit->mode_topic;
}

// --- The Mode topic, as Home Assistant sees it ------------------------------

static void test_reconnect_with_ac_off_reports_only_off(void) {
  Unit unit = reconnected(0, kCool);
  receive_frame(&unit, kCoolOff);
  TEST_ASSERT_EQUAL_STRING("off", unit.mode_topic);
}

static void test_boot_with_ac_off_reports_only_off(void) {
  Unit unit = booted();
  receive_frame(&unit, kCoolOff);
  TEST_ASSERT_EQUAL_STRING("off", unit.mode_topic);
}

static void test_reconnect_with_ac_on_reports_its_mode(void) {
  Unit unit = reconnected(1, kCool);
  receive_frame(&unit, kCoolOn);
  TEST_ASSERT_NULL(strstr(unit.mode_topic, "off"));
  TEST_ASSERT_EQUAL_STRING("cool", last_payload(&unit));
}

static void test_boot_with_ac_on_reports_its_mode(void) {
  Unit unit = booted();
  receive_frame(&unit, kCoolOn);
  TEST_ASSERT_NULL(strstr(unit.mode_topic, "off"));
  TEST_ASSERT_EQUAL_STRING("cool", last_payload(&unit));
}

static void test_switch_on_into_another_mode_never_reports_the_old_one(void) {
  Unit unit = running(0, kCool);
  receive_frame(&unit, kHeatOn);
  TEST_ASSERT_NULL(strstr(unit.mode_topic, "cool"));
  TEST_ASSERT_EQUAL_STRING("heat", last_payload(&unit));
}

static void test_switch_off_reports_only_off(void) {
  Unit unit = running(1, kCool);
  receive_frame(&unit, kCoolOff);
  TEST_ASSERT_EQUAL_STRING("off", unit.mode_topic);
}

static void test_switch_off_and_mode_change_in_one_frame_reports_only_off(void) {
  Unit unit = running(1, kCool);
  receive_frame(&unit, kHeatOff);
  TEST_ASSERT_EQUAL_STRING("off", unit.mode_topic);
}

static void test_mode_change_while_off_waits_for_the_switch_on(void) {
  Unit unit = running(0, kCool);
  receive_frame(&unit, kHeatOff);
  TEST_ASSERT_EQUAL_STRING("", unit.mode_topic);
  receive_frame(&unit, kHeatOn);
  TEST_ASSERT_EQUAL_STRING("heat", unit.mode_topic);
}

static void test_mode_change_while_on_is_reported(void) {
  Unit unit = running(1, kCool);
  receive_frame(&unit, kHeatOn);
  TEST_ASSERT_EQUAL_STRING("heat", unit.mode_topic);
}

static void test_unchanged_frame_reports_nothing(void) {
  Unit unit = running(1, kCool);
  receive_frame(&unit, kCoolOn);
  TEST_ASSERT_EQUAL_STRING("", unit.mode_topic);
}

static void test_bits_outside_power_and_mode_are_ignored(void) {
  Unit unit = running(1, kCool);
  receive_frame(&unit, 0xeb);  // cool, on, plus bits 1 and 5-7
  TEST_ASSERT_EQUAL_STRING("", unit.mode_topic);
}

// --- The parser's changes, which every build publishes ----------------------

static void test_changes_put_a_switch_off_before_the_mode(void) {
  uint8_t power_old = MHI_STATUS_UNKNOWN;
  uint8_t mode_old = MHI_STATUS_UNKNOWN;
  MhiDb0Change changes[2];
  TEST_ASSERT_EQUAL_UINT(2, mhi_db0_changes(kCoolOff, &power_old, &mode_old, changes));
  TEST_ASSERT_EQUAL(MHI_DB0_POWER, changes[0].field);
  TEST_ASSERT_EQUAL_UINT8(0, changes[0].value);
  TEST_ASSERT_EQUAL(MHI_DB0_MODE, changes[1].field);
  TEST_ASSERT_EQUAL_UINT8(0x08, changes[1].value);
  TEST_ASSERT_EQUAL_UINT8(0, power_old);
  TEST_ASSERT_EQUAL_UINT8(0x08, mode_old);
}

static void test_changes_put_the_mode_before_a_switch_on(void) {
  uint8_t power_old = MHI_STATUS_UNKNOWN;
  uint8_t mode_old = MHI_STATUS_UNKNOWN;
  MhiDb0Change changes[2];
  TEST_ASSERT_EQUAL_UINT(2, mhi_db0_changes(kHeatOn, &power_old, &mode_old, changes));
  TEST_ASSERT_EQUAL(MHI_DB0_MODE, changes[0].field);
  TEST_ASSERT_EQUAL_UINT8(0x10, changes[0].value);
  TEST_ASSERT_EQUAL(MHI_DB0_POWER, changes[1].field);
  TEST_ASSERT_EQUAL_UINT8(1, changes[1].value);
  TEST_ASSERT_EQUAL_UINT8(1, power_old);
  TEST_ASSERT_EQUAL_UINT8(0x10, mode_old);
}

// Builds without POWERON_WHEN_CHANGING_MODE have a separate Power topic and
// publish the mode whatever the power state.
static void test_changes_report_a_mode_change_while_off(void) {
  uint8_t power_old = 0;
  uint8_t mode_old = kCool;
  MhiDb0Change changes[2];
  TEST_ASSERT_EQUAL_UINT(1, mhi_db0_changes(kHeatOff, &power_old, &mode_old, changes));
  TEST_ASSERT_EQUAL(MHI_DB0_MODE, changes[0].field);
  TEST_ASSERT_EQUAL_UINT8(0x10, changes[0].value);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_reconnect_with_ac_off_reports_only_off);
  RUN_TEST(test_boot_with_ac_off_reports_only_off);
  RUN_TEST(test_reconnect_with_ac_on_reports_its_mode);
  RUN_TEST(test_boot_with_ac_on_reports_its_mode);
  RUN_TEST(test_switch_on_into_another_mode_never_reports_the_old_one);
  RUN_TEST(test_switch_off_reports_only_off);
  RUN_TEST(test_switch_off_and_mode_change_in_one_frame_reports_only_off);
  RUN_TEST(test_mode_change_while_off_waits_for_the_switch_on);
  RUN_TEST(test_mode_change_while_on_is_reported);
  RUN_TEST(test_unchanged_frame_reports_nothing);
  RUN_TEST(test_bits_outside_power_and_mode_are_ignored);
  RUN_TEST(test_changes_put_a_switch_off_before_the_mode);
  RUN_TEST(test_changes_put_the_mode_before_a_switch_on);
  RUN_TEST(test_changes_report_a_mode_change_while_off);
  return UNITY_END();
}
