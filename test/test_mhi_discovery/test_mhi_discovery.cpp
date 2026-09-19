// Host tests for the Home Assistant discovery payloads (fork issue #4, batch B;
// spec docs/superpowers/specs/2026-09-16-phase-4-batches-design.md §4.4).
//
// Beyond correctness, this suite is the size check the design relies on: every
// row of the reference build is measured against MHI_DISCOVERY_BUF and the
// longest is printed. It also writes the reference payloads to
// test/fixtures/discovery/ so a reviewer sees the JSON and CI fails when the
// committed copies no longer match the code.

#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "mhi_discovery.h"

void setUp(void) {}
void tearDown(void) {}

// The repo defaults with Home Assistant's mode names (ci-custom-payloads).
static const MhiDiscoveryCtx kDefault = {
  .discovery_prefix = "homeassistant",
  .base = "MHI-AC-Ctrl",
  .set_prefix = "set/",
  .hostname = "MHI-AC-Ctrl",
  .device_name = "MHI-AC-Ctrl",
  .version = "fixture",
  .climate_id = "MHI-AC-Ctrl",
  .id_prefix = "MHI-AC-Ctrl",
  .entity_prefix = NULL,
  .names = {NULL, "Vanes", "Silent", "Problem", "Wiring", "Uptime", "Free heap", "Wi-Fi signal", "Reset reason", "Wi-Fi PHY",
            "Vanes left/right", "3D auto", "Frame errors", "Frame timeouts", "Error code",
            "Temperature", "Current", "Energy", "Compressor frequency", "Defrost", "Compressor run time", "Protection state",
            "Group role", "Restart"},
  .reset_reason_tpl = NULL,
  .t_mode = "Mode", .t_tsetpoint = "Tsetpoint", .t_fan = "Fan", .t_vanes = "Vanes", .t_troom = "Troom", .t_action = "Action",
  .t_connected = "connected", .t_silent = "Silent", .t_errorcode = "Errorcode", .t_wiring = "Wiring",
  .t_uptime = "Uptime", .t_free_heap = "FreeHeap", .t_rssi = "RSSI", .t_reset_reason = "ResetReason", .t_wifi_phy = "WIFI_PHY",
  .modes = {"off", "auto", "dry", "cool", "fan_only", "heat"},
  .fan_auto = "Auto",
  .vanes = {"Up", "UpCenter", "CenterDown", "Down", "Swing", "?"},
  .connected_on = "1", .connected_off = "0",
  .silent_on = "On", .silent_off = "Off",
  .wiring_ok = "o.k.",
  .has_lr = false,
  .t_vaneslr = "VanesLR", .t_3dauto = "3Dauto",
  .vanes_lr = {"Left", "LeftCenter", "Center", "CenterRight", "Right", "Wide", "Spot", "Swing"},
  .threedauto_on = "On", .threedauto_off = "Off",
  .has_outdoor = false,
  .outdoor_id = "mhi_ac_ctrl_outdoor", .outdoor_name = "AC outdoor unit", .outdoor_entity_prefix = NULL,  // derived from GROUP_ROOT "MHI-AC-Ctrl/" (fork #22)
  .op_prefix = "OpData/",
  .t_op_outdoor = "OUTDOOR", .t_op_ct = "CT", .t_op_kwh = "KWH", .t_op_comp = "COMP", .t_op_defrost = "DEFROST",
  .t_op_total_comp_run = "TOTAL-COMP-RUN", .t_op_protection_no = "PROTECTION-NO",
  .defrost_on = "On", .defrost_off = "Off",
  .t_frame_errors = "FrameErrors", .t_frame_timeouts = "FrameTimeouts",
  .fan = {"1", "2", "3", "4"},
  .group_base = "MHI-AC-Ctrl", .avty_topic = "MHI-AC-Ctrl/connected", .t_group = "Group",  // a single split: GROUP_ROOT = MQTT_PREFIX
  .t_request_reset = "reset", .request_reset = "reset",
};

// Lucas's Uitkijk: custom names throughout, a template, an entity prefix, and a
// quote in the device name to exercise the escaping. Every name here is
// deliberately unlike both the repo default and its own uniq_id suffix, so the
// assertions below can tell a default_entity_id built from the slug of the name
// apart from one built from the uniq_id (the bug fixed in 7bc7879).
static const MhiDiscoveryCtx kUitkijk = {
  .discovery_prefix = "homeassistant",
  .base = "airco/uitkijk",
  .set_prefix = "set/",
  .hostname = "airco-uitkijk",
  .device_name = "AC \"Uitkijk\"",
  .version = "2eab73c",
  .climate_id = "AC_Uitkijk",
  .id_prefix = "ac_uitkijk",
  .entity_prefix = "ac_uitkijk",
  .names = {NULL, "louvers", "quiet mode", "fault", "wiring fault", "time since boot", "heap free", "wifi-signal", "restart reason", "wifi-standard",
            "Vanes left/right", "3D auto", "Frame errors", "Frame timeouts", "Error code",
            "Temperature", "Current", "Energy", "Compressor frequency", "Defrost", "Compressor run time", "Protection state",
            "Group role", "Restart"},
  .reset_reason_tpl = "{{ {'Power On': 'power applied', 'Software/System restart': 'software restart (update or reset)', 'Hardware Watchdog': 'hardware watchdog', 'Software Watchdog': 'software watchdog', 'Exception': 'crash', 'Deep-Sleep Wake': 'woke from deep sleep', 'External System': 'external reset'}.get(value, value) }}",
  .t_mode = "Mode", .t_tsetpoint = "Tsetpoint", .t_fan = "Fan", .t_vanes = "Vanes", .t_troom = "Troom", .t_action = "Action",
  .t_connected = "connected", .t_silent = "Silent", .t_errorcode = "Errorcode", .t_wiring = "Wiring",
  .t_uptime = "Uptime", .t_free_heap = "FreeHeap", .t_rssi = "RSSI", .t_reset_reason = "ResetReason", .t_wifi_phy = "WIFI_PHY",
  .modes = {"off", "auto", "dry", "cool", "fan_only", "heat"},
  .fan_auto = "Auto",
  .vanes = {"Up", "UpCenter", "CenterDown", "Down", "Swing", "?"},
  .connected_on = "1", .connected_off = "0",
  .silent_on = "On", .silent_off = "Off",
  .wiring_ok = "o.k.",
  .has_lr = true,
  .t_vaneslr = "VanesLR", .t_3dauto = "3Dauto",
  .vanes_lr = {"Left", "LeftCenter", "Center", "CenterRight", "Right", "Wide", "Spot", "Swing"},
  .threedauto_on = "On", .threedauto_off = "Off",
  .has_outdoor = false,
  .outdoor_id = "ac_uitkijk_outdoor", .outdoor_name = "AC outdoor unit", .outdoor_entity_prefix = NULL,
  .op_prefix = "OpData/",
  .t_op_outdoor = "OUTDOOR", .t_op_ct = "CT", .t_op_kwh = "KWH", .t_op_comp = "COMP", .t_op_defrost = "DEFROST",
  .t_op_total_comp_run = "TOTAL-COMP-RUN", .t_op_protection_no = "PROTECTION-NO",
  .defrost_on = "On", .defrost_off = "Off",
  .t_frame_errors = "FrameErrors", .t_frame_timeouts = "FrameTimeouts",
  .fan = {"1", "2", "3", "4"},
  .group_base = "airco/uitkijk", .avty_topic = "airco/uitkijk/connected", .t_group = "Group",
  .t_request_reset = "reset", .request_reset = "reset",
};

// Slaapkamer: has_lr and the outdoor device both on (batch C spec §3), as the
// group's publisher (fork #22).
static MhiDiscoveryCtx make_slaapkamer() {
  MhiDiscoveryCtx c = kUitkijk;
  c.base = "airco/slaapkamer";
  c.hostname = "airco-slaapkamer";
  c.device_name = "AC Slaapkamer";
  c.version = "batchc-fixture";
  c.climate_id = "AC_Slaapkamer";
  c.id_prefix = "ac_slaapkamer";
  c.entity_prefix = "ac_slaapkamer";
  c.has_outdoor = true;
  c.outdoor_id = "ac_slaapkamer_outdoor";
  c.outdoor_entity_prefix = "ac_outdoor";
  // Lucas's group (fork #22): the outdoor rows read airco/outdoor/ and are
  // available while Slaapkamer, their publisher, is.
  c.group_base = "airco/outdoor";
  c.avty_topic = "airco/slaapkamer/connected";
  return c;
}
static const MhiDiscoveryCtx kSlaapkamer = make_slaapkamer();

static const char* const kFixtureName[MHI_DISCOVERY_ROWS] = {
  "climate", "vanes", "silent", "problem", "wiring", "uptime", "free_heap", "rssi", "reset_reason", "wifi_phy",
  "vanes_lr", "3dauto", "frame_errors", "frame_timeouts", "error_code",
  "ou_outdoor", "ou_ct", "ou_kwh", "ou_comp", "ou_defrost", "ou_comp_run", "ou_protection",
  "group_role", "restart"};

// Balanced braces and brackets outside strings, every string closed, no
// printf conversion left over and no NULL argument printed.
static bool json_shape_ok(const char* s) {
  int depth = 0;
  bool in_str = false;
  if (strstr(s, "(null)") != NULL) return false;
  for (; *s; s++) {
    if (*s == '%') return false;  // a conversion left over, inside or outside a string
    if (in_str) {
      if (*s == '\\' && s[1]) s++;
      else if (*s == '"') in_str = false;
      continue;
    }
    if (*s == '"') in_str = true;
    else if (*s == '{' || *s == '[') depth++;
    else if (*s == '}' || *s == ']') { if (--depth < 0) return false; }
  }
  return depth == 0 && !in_str;
}

// --- mode names -------------------------------------------------------------

static void test_ha_mode_names_are_valid(void) {
  TEST_ASSERT_TRUE(mhi_discovery_modes_valid(&kDefault));
}

static void test_the_repo_default_mode_names_are_not(void) {
  // MHI-AC-Ctrl.h's defaults: HA rejects a climate whose modes say "Fan" or "Off".
  MhiDiscoveryCtx c = kDefault;
  c.modes[0] = "Off";
  TEST_ASSERT_FALSE(mhi_discovery_modes_valid(&c));
  c = kDefault;
  c.modes[4] = "Fan";
  TEST_ASSERT_FALSE(mhi_discovery_modes_valid(&c));
  c = kDefault;
  c.modes[5] = NULL;
  TEST_ASSERT_FALSE(mhi_discovery_modes_valid(&c));
}

// --- topics -----------------------------------------------------------------

static void test_topics_carry_the_component_and_the_uniq_id(void) {
  char t[MHI_DISCOVERY_TOPIC_MAX];
  TEST_ASSERT_TRUE(mhi_discovery_topic(MHI_DISCOVERY_CLIMATE, &kUitkijk, t, sizeof(t)) > 0);
  TEST_ASSERT_EQUAL_STRING("homeassistant/climate/AC_Uitkijk/config", t);
  TEST_ASSERT_TRUE(mhi_discovery_topic(MHI_DISCOVERY_VANES, &kUitkijk, t, sizeof(t)) > 0);
  TEST_ASSERT_EQUAL_STRING("homeassistant/select/ac_uitkijk_vanes/config", t);
  TEST_ASSERT_TRUE(mhi_discovery_topic(MHI_DISCOVERY_SILENT, &kUitkijk, t, sizeof(t)) > 0);
  TEST_ASSERT_EQUAL_STRING("homeassistant/switch/ac_uitkijk_silent/config", t);
  TEST_ASSERT_TRUE(mhi_discovery_topic(MHI_DISCOVERY_WIRING, &kUitkijk, t, sizeof(t)) > 0);
  TEST_ASSERT_EQUAL_STRING("homeassistant/binary_sensor/ac_uitkijk_wiring/config", t);
  TEST_ASSERT_TRUE(mhi_discovery_topic(MHI_DISCOVERY_RESET_REASON, &kUitkijk, t, sizeof(t)) > 0);
  TEST_ASSERT_EQUAL_STRING("homeassistant/sensor/ac_uitkijk_reset_reason/config", t);
}

static void test_a_topic_that_does_not_fit_is_refused(void) {
  char t[30];
  TEST_ASSERT_EQUAL_size_t(0, mhi_discovery_topic(MHI_DISCOVERY_CLIMATE, &kUitkijk, t, sizeof(t)));
  TEST_ASSERT_EQUAL_STRING("", t);
}

// --- every row builds, fits and is well-formed ------------------------------

// Returns the longest enabled row, so a caller can gate the headroom too.
static size_t every_row_fits(const MhiDiscoveryCtx* ctx, const char* label) {
  size_t longest = 0;
  int longest_row = -1;
  for (int r = 0; r < MHI_DISCOVERY_ROWS; r++) {
    if (!mhi_discovery_row_enabled((MhiDiscoveryRow)r, ctx)) continue;
    char out[MHI_DISCOVERY_BUF];
    const size_t n = mhi_discovery_build((MhiDiscoveryRow)r, ctx, out, sizeof(out));
    char msg[64];
    snprintf(msg, sizeof(msg), "%s row %d", label, r);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, msg);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(strlen(out), n, msg);
    TEST_ASSERT_TRUE_MESSAGE(json_shape_ok(out), msg);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"uniq_id\":\""), msg);
    char avty[128];
    if (mhi_discovery_is_outdoor_row((MhiDiscoveryRow)r))  // the publisher's connected topic in full (fork #22)
      snprintf(avty, sizeof(avty), "\"avty_t\":\"%s\",\"pl_avail\":\"1\",\"pl_not_avail\":\"0\"", ctx->avty_topic);
    else
      snprintf(avty, sizeof(avty), "\"avty_t\":\"~/connected\",\"pl_avail\":\"1\",\"pl_not_avail\":\"0\"");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, avty), msg);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"dev\":{\"ids\":[\""), msg);
    // The outdoor rows carry their own device: each kind of row has its own
    // full model check.
    if (mhi_discovery_is_outdoor_row((MhiDiscoveryRow)r))
      TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"mf\":\"Mitsubishi Heavy Industries\",\"mdl\":\"outdoor unit\",\"via_device\":\""), msg);
    else
      TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"mf\":\"Mitsubishi Heavy Industries\",\"mdl\":\"MHI-AC-Ctrl\",\"sw\":\""), msg);
    if (n > longest) { longest = n; longest_row = r; }
  }
  printf("  %s: longest row %d, %u of %u bytes\n", label, longest_row, (unsigned)longest, (unsigned)MHI_DISCOVERY_BUF);
  return longest;
}

static void test_every_default_row_fits(void) { every_row_fits(&kDefault, "default"); }
static void test_every_uitkijk_row_fits(void) { every_row_fits(&kUitkijk, "uitkijk"); }
static void test_every_slaapkamer_row_fits(void) {
  // Everything on, custom names and the reset-reason template: the worst case
  // the firmware builds. It measures about 921-928 B today, and 980 is the
  // headroom gate the plan checked by hand -- a name or a template that eats
  // the remaining 44 B of MHI_DISCOVERY_BUF fails here, not on the unit.
  TEST_ASSERT_LESS_OR_EQUAL_size_t(980, every_row_fits(&kSlaapkamer, "slaapkamer"));
}

static void test_a_payload_that_does_not_fit_is_refused(void) {
  char out[200];
  TEST_ASSERT_EQUAL_size_t(0, mhi_discovery_build(MHI_DISCOVERY_CLIMATE, &kDefault, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
}

// --- the climate ------------------------------------------------------------

static void test_the_climate_is_the_device_and_lists_come_from_the_payload_texts(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_CLIMATE, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "{\"~\":\"airco/uitkijk\",\"name\":null,\"uniq_id\":\"AC_Uitkijk\","));
  // Pinned to the ID the automations read; HA 2026 would otherwise prepend the area after a registry loss.
  TEST_ASSERT_NOT_NULL(strstr(out, "\"name\":null,\"uniq_id\":\"AC_Uitkijk\",\"default_entity_id\":\"climate.ac_uitkijk\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"mode_cmd_t\":\"~/set/Mode\",\"mode_stat_t\":\"~/Mode\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"temp_cmd_t\":\"~/set/Tsetpoint\",\"temp_stat_t\":\"~/Tsetpoint\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"fan_mode_cmd_t\":\"~/set/Fan\",\"fan_mode_stat_t\":\"~/Fan\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"swing_mode_cmd_t\":\"~/set/Vanes\",\"swing_mode_stat_t\":\"~/Vanes\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"curr_temp_t\":\"~/Troom\",\"act_t\":\"~/Action\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"modes\":[\"off\",\"auto\",\"dry\",\"cool\",\"fan_only\",\"heat\"],"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"fan_modes\":[\"1\",\"2\",\"3\",\"4\",\"Auto\"],"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"swing_modes\":[\"Up\",\"UpCenter\",\"CenterDown\",\"Down\",\"Swing\",\"?\"],"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"min_temp\":18,\"max_temp\":30,\"temp_step\":0.5,"));
  TEST_ASSERT_NULL(strstr(out, "ent_cat"));  // the climate card, not the diagnostics list
}

// --- the other rows ---------------------------------------------------------

static void test_the_vanes_select_offers_the_names_and_the_unknown_state(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_VANES, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"name\":\"louvers\",\"uniq_id\":\"ac_uitkijk_vanes\",\"default_entity_id\":\"select.ac_uitkijk_louvers\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/Vanes\",\"cmd_t\":\"~/set/Vanes\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ops\":[\"Up\",\"UpCenter\",\"CenterDown\",\"Down\",\"Swing\",\"?\"],"));
  TEST_ASSERT_NULL(strstr(out, "ent_cat"));
}

static void test_the_silent_switch_uses_the_payload_texts(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_SILENT, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"uniq_id\":\"ac_uitkijk_silent\",\"default_entity_id\":\"switch.ac_uitkijk_quiet_mode\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/Silent\",\"cmd_t\":\"~/set/Silent\",\"pl_on\":\"On\",\"pl_off\":\"Off\",\"ic\":\"mdi:volume-low\","));
}

static void test_the_problem_sensors_are_diagnostic_and_template_the_topics(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_PROBLEM, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"default_entity_id\":\"binary_sensor.ac_uitkijk_fault\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/Errorcode\",\"val_tpl\":\"{{ 'ON' if value|int(0) != 0 else 'OFF' }}\",\"dev_cla\":\"problem\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_WIRING, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/Wiring\",\"val_tpl\":\"{{ 'OFF' if value == 'o.k.' else 'ON' }}\",\"dev_cla\":\"problem\",\"ent_cat\":\"diagnostic\","));
}

static void test_the_five_diagnostic_sensors_carry_their_custom_names_classes_and_units(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_UPTIME, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"name\":\"time since boot\",\"uniq_id\":\"ac_uitkijk_uptime\",\"default_entity_id\":\"sensor.ac_uitkijk_time_since_boot\","));
  // Plain seconds, no state class: hass-config's reboot counter compares the raw state.
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/Uptime\",\"dev_cla\":\"duration\",\"unit_of_meas\":\"s\",\"sug_dsp_prc\":0,\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_NULL(strstr(out, "stat_cla"));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_FREE_HEAP, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/FreeHeap\",\"dev_cla\":\"data_size\",\"unit_of_meas\":\"B\",\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_RSSI, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/RSSI\",\"dev_cla\":\"signal_strength\",\"unit_of_meas\":\"dBm\",\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_RESET_REASON, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/ResetReason\",\"val_tpl\":\"{{ {'Power On': 'power applied', "));
  TEST_ASSERT_NOT_NULL(strstr(out, "}.get(value, value) }}\",\"ic\":\"mdi:restart-alert\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_WIFI_PHY, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/WIFI_PHY\",\"ic\":\"mdi:wifi-cog\",\"ent_cat\":\"diagnostic\","));
}

static void test_without_a_template_or_entity_prefix_those_keys_are_absent(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_RESET_REASON, &kDefault, out, sizeof(out)) > 0);
  TEST_ASSERT_NULL(strstr(out, "val_tpl"));
  TEST_ASSERT_NULL(strstr(out, "default_entity_id"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"name\":\"Reset reason\",\"uniq_id\":\"MHI-AC-Ctrl_reset_reason\",\"stat_t\":\"~/ResetReason\",\"ic\":\"mdi:restart-alert\","));
}

static void test_quotes_in_a_name_are_escaped(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_SILENT, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"dev\":{\"ids\":[\"airco-uitkijk\"],\"name\":\"AC \\\"Uitkijk\\\"\",\"mf\":"));
  TEST_ASSERT_TRUE(json_shape_ok(out));
}

// --- entity-ID slugs ---------------------------------------------------------

static void test_slug_is_home_assistants_own_derivation_of_a_name(void) {
  // What HA makes of "AC Slaapkamer" + "time since boot" without an area:
  // lower case, every run of non-alphanumerics one underscore, none at the ends.
  char out[48];
  TEST_ASSERT_EQUAL_size_t(15, mhi_discovery_slug("time since boot", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("time_since_boot", out);
  TEST_ASSERT_TRUE(mhi_discovery_slug("wifi-signal", out, sizeof(out)) > 0);
  TEST_ASSERT_EQUAL_STRING("wifi_signal", out);
  TEST_ASSERT_TRUE(mhi_discovery_slug("Wi-Fi PHY", out, sizeof(out)) > 0);
  TEST_ASSERT_EQUAL_STRING("wi_fi_phy", out);
  TEST_ASSERT_TRUE(mhi_discovery_slug("  Free  heap. ", out, sizeof(out)) > 0);
  TEST_ASSERT_EQUAL_STRING("free_heap", out);
}

static void test_slug_refuses_an_empty_name_or_a_small_buffer(void) {
  char out[8] = "x";
  TEST_ASSERT_EQUAL_size_t(0, mhi_discovery_slug("", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
  TEST_ASSERT_EQUAL_size_t(0, mhi_discovery_slug("---", out, sizeof(out)));
  TEST_ASSERT_EQUAL_size_t(0, mhi_discovery_slug("time since boot", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
  TEST_ASSERT_EQUAL_size_t(0, mhi_discovery_slug(NULL, out, sizeof(out)));
}

static void test_a_name_that_slugs_to_nothing_fails_the_row(void) {
  MhiDiscoveryCtx c = kUitkijk;
  c.names[MHI_DISCOVERY_VANES] = "???";
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_EQUAL_size_t(0, mhi_discovery_build(MHI_DISCOVERY_VANES, &c, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
}

// --- batch C: the louvers, the counters, the error code, the outdoor device ---

static void test_row_enabled_gates_vaneslr_3dauto_and_outdoor(void) {
  TEST_ASSERT_FALSE(mhi_discovery_row_enabled(MHI_DISCOVERY_VANES_LR, &kDefault));
  TEST_ASSERT_FALSE(mhi_discovery_row_enabled(MHI_DISCOVERY_3DAUTO, &kDefault));
  TEST_ASSERT_FALSE(mhi_discovery_row_enabled(MHI_DISCOVERY_OU_PROTECTION, &kUitkijk));  // has_lr, not has_outdoor
  TEST_ASSERT_TRUE(mhi_discovery_row_enabled(MHI_DISCOVERY_VANES_LR, &kUitkijk));
  TEST_ASSERT_TRUE(mhi_discovery_row_enabled(MHI_DISCOVERY_OU_PROTECTION, &kSlaapkamer));
  // A row past the end of the table belongs to no build, even in the context
  // that enables everything: is_outdoor_row() would otherwise call it outdoor.
  TEST_ASSERT_FALSE(mhi_discovery_row_enabled(MHI_DISCOVERY_ROWS, &kSlaapkamer));
  for (int r = 0; r < MHI_DISCOVERY_ROWS; r++) {
    if (r == MHI_DISCOVERY_VANES_LR || r == MHI_DISCOVERY_3DAUTO || mhi_discovery_is_outdoor_row((MhiDiscoveryRow)r)) continue;
    TEST_ASSERT_TRUE(mhi_discovery_row_enabled((MhiDiscoveryRow)r, &kDefault));
  }
}

static void test_the_climate_gains_swing_horizontal_with_has_lr(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_CLIMATE, &kDefault, out, sizeof(out)) > 0);
  TEST_ASSERT_NULL(strstr(out, "swing_h"));  // kDefault: has_lr false
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_CLIMATE, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"swing_h_mode_cmd_t\":\"~/set/VanesLR\",\"swing_h_mode_stat_t\":\"~/VanesLR\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"swing_h_modes\":[\"Left\",\"LeftCenter\",\"Center\",\"CenterRight\",\"Right\",\"Wide\",\"Spot\",\"Swing\"],"));
}

static void test_the_vaneslr_select_and_3dauto_switch(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_VANES_LR, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"uniq_id\":\"ac_uitkijk_vanes_lr\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/VanesLR\",\"cmd_t\":\"~/set/VanesLR\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ops\":[\"Left\",\"LeftCenter\",\"Center\",\"CenterRight\",\"Right\",\"Wide\",\"Spot\",\"Swing\"],"));
  TEST_ASSERT_NULL(strstr(out, "ent_cat"));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_3DAUTO, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"uniq_id\":\"ac_uitkijk_3d_auto\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/3Dauto\",\"cmd_t\":\"~/set/3Dauto\",\"pl_on\":\"On\",\"pl_off\":\"Off\","));
  TEST_ASSERT_NULL(strstr(out, "ent_cat"));
}

static void test_frame_counters_and_error_code_are_diagnostic(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_FRAME_ERRORS, &kDefault, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/FrameErrors\",\"stat_cla\":\"total_increasing\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_FRAME_TIMEOUTS, &kDefault, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/FrameTimeouts\",\"stat_cla\":\"total_increasing\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_ERROR_CODE, &kDefault, out, sizeof(out)) > 0);
  // The AC's own number, on the topic the Problem binary sensor also reads.
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/Errorcode\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_NULL(strstr(out, "stat_cla"));  // a code, not a measurement
}

static void test_outdoor_rows_carry_their_own_device(void) {
  char out[MHI_DISCOVERY_BUF], topic[MHI_DISCOVERY_TOPIC_MAX];
  // The topic carries the uniq_id (mhi_discovery.h): keyed by the outdoor
  // device, not by the unit that publishes it, so moving the publisher to the
  // other unit reuses this topic instead of orphaning it with a duplicate
  // uniq_id.
  TEST_ASSERT_TRUE(mhi_discovery_topic(MHI_DISCOVERY_OU_OUTDOOR, &kSlaapkamer, topic, sizeof(topic)) > 0);
  TEST_ASSERT_EQUAL_STRING("homeassistant/sensor/ac_slaapkamer_outdoor_outdoor_temp/config", topic);
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_OUTDOOR, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"uniq_id\":\"ac_slaapkamer_outdoor_outdoor_temp\","));
  TEST_ASSERT_NOT_NULL(strstr(
      out, "\"dev\":{\"ids\":[\"ac_slaapkamer_outdoor\"],\"name\":\"AC outdoor unit\",\"mf\":\"Mitsubishi Heavy Industries\",\"mdl\":\"outdoor unit\",\"via_device\":\"airco-slaapkamer\"}}"));
  TEST_ASSERT_NULL(strstr(out, "\"sw\":"));  // the outdoor device has no firmware version of its own
  // The device is already called "AC outdoor unit", so the entity name is just "Temperature".
  TEST_ASSERT_NOT_NULL(strstr(out, "\"default_entity_id\":\"sensor.ac_outdoor_temperature\","));
}

static void test_outdoor_rows_read_the_group_roots_opdata_topics(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_OUTDOOR, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/OpData/OUTDOOR\",\"dev_cla\":\"temperature\",\"unit_of_meas\":\"\xc2\xb0" "C\",\"stat_cla\":\"measurement\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_CT, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/OpData/CT\",\"dev_cla\":\"current\",\"unit_of_meas\":\"A\",\"stat_cla\":\"measurement\","));
  TEST_ASSERT_NULL(strstr(out, "ent_cat"));  // OU_CT is not diagnostic
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_COMP, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/OpData/COMP\",\"dev_cla\":\"frequency\",\"unit_of_meas\":\"Hz\",\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_DEFROST, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/OpData/DEFROST\",\"pl_on\":\"On\",\"pl_off\":\"Off\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_COMP_RUN, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/OpData/TOTAL-COMP-RUN\",\"dev_cla\":\"duration\",\"unit_of_meas\":\"h\",\"stat_cla\":\"total_increasing\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_PROTECTION, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/OpData/PROTECTION-NO\",\"ent_cat\":\"diagnostic\","));
}

// --- fork #22: the group role, the group root, the retired energy row ----------

static void test_the_group_role_row_is_a_unit_row(void) {
  char out[MHI_DISCOVERY_BUF], topic[MHI_DISCOVERY_TOPIC_MAX];
  TEST_ASSERT_FALSE(mhi_discovery_is_outdoor_row(MHI_DISCOVERY_GROUP_ROLE));  // a closed range, not ">= OU_OUTDOOR"
  TEST_ASSERT_TRUE(mhi_discovery_row_enabled(MHI_DISCOVERY_GROUP_ROLE, &kDefault));
  TEST_ASSERT_TRUE(mhi_discovery_topic(MHI_DISCOVERY_GROUP_ROLE, &kSlaapkamer, topic, sizeof(topic)) > 0);
  TEST_ASSERT_EQUAL_STRING("homeassistant/sensor/ac_slaapkamer_group_role/config", topic);
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_GROUP_ROLE, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "{\"~\":\"airco/slaapkamer\",\"name\":\"Group role\",\"uniq_id\":\"ac_slaapkamer_group_role\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"default_entity_id\":\"sensor.ac_slaapkamer_group_role\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/Group\",\"ent_cat\":\"diagnostic\",\"avty_t\":\"~/connected\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"dev\":{\"ids\":[\"airco-slaapkamer\"],\"name\":\"AC Slaapkamer\","));
  TEST_ASSERT_NULL(strstr(out, "dev_cla"));
  TEST_ASSERT_NULL(strstr(out, "unit_of_meas"));
  TEST_ASSERT_NULL(strstr(out, "stat_cla"));
  // Without an entity prefix, no default_entity_id.
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_GROUP_ROLE, &kDefault, out, sizeof(out)) > 0);
  TEST_ASSERT_NULL(strstr(out, "default_entity_id"));
}

static void test_is_outdoor_row_is_the_closed_outdoor_block(void) {
  for (int r = 0; r < MHI_DISCOVERY_ROWS; r++)
    TEST_ASSERT_EQUAL(r >= MHI_DISCOVERY_OU_OUTDOOR && r <= MHI_DISCOVERY_OU_PROTECTION,
                      mhi_discovery_is_outdoor_row((MhiDiscoveryRow)r));
  TEST_ASSERT_FALSE(mhi_discovery_is_outdoor_row(MHI_DISCOVERY_ROWS));
}

static void test_outdoor_rows_use_the_group_base_and_absolute_availability(void) {
  char out[MHI_DISCOVERY_BUF], topic[MHI_DISCOVERY_TOPIC_MAX];
  for (int r = MHI_DISCOVERY_OU_OUTDOOR; r <= MHI_DISCOVERY_OU_PROTECTION; r++) {
    if (!mhi_discovery_row_enabled((MhiDiscoveryRow)r, &kSlaapkamer)) continue;
    TEST_ASSERT_TRUE(mhi_discovery_build((MhiDiscoveryRow)r, &kSlaapkamer, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING_LEN("{\"~\":\"airco/outdoor\",", out, strlen("{\"~\":\"airco/outdoor\","));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"avty_t\":\"airco/slaapkamer/connected\",\"pl_avail\":\"1\",\"pl_not_avail\":\"0\","));
    TEST_ASSERT_NULL(strstr(out, "\"avty_t\":\"~/"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"via_device\":\"airco-slaapkamer\"}}"));
  }
  // The topic and the uniq_id stay keyed by the outdoor ID: the same for every publisher.
  TEST_ASSERT_TRUE(mhi_discovery_topic(MHI_DISCOVERY_OU_CT, &kSlaapkamer, topic, sizeof(topic)) > 0);
  TEST_ASSERT_EQUAL_STRING("homeassistant/sensor/ac_slaapkamer_outdoor_current/config", topic);
  // Another publisher changes only avty_t and via_device (spec §8.2).
  MhiDiscoveryCtx uitkijk = kSlaapkamer;
  uitkijk.base = "airco/uitkijk";
  uitkijk.hostname = "airco-uitkijk";
  uitkijk.avty_topic = "airco/uitkijk/connected";
  char other[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_CT, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_CT, &uitkijk, other, sizeof(other)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(other, "\"avty_t\":\"airco/uitkijk/connected\","));
  TEST_ASSERT_NOT_NULL(strstr(other, "\"via_device\":\"airco-uitkijk\"}}"));
  const char* cut_out = strstr(out, "\"avty_t\"");
  const char* cut_other = strstr(other, "\"avty_t\"");
  TEST_ASSERT_EQUAL_size_t((size_t)(cut_out - out), (size_t)(cut_other - other));
  TEST_ASSERT_EQUAL_STRING_LEN(out, other, (size_t)(cut_out - out));  // everything before avty_t is identical
}

static void test_the_retired_energy_row_is_never_built(void) {
  const MhiDiscoveryCtx* all[] = {&kDefault, &kUitkijk, &kSlaapkamer};
  char out[MHI_DISCOVERY_BUF];
  for (size_t i = 0; i < 3; i++) {
    TEST_ASSERT_FALSE(mhi_discovery_row_enabled(MHI_DISCOVERY_OU_KWH, all[i]));
    strcpy(out, "stale");
    TEST_ASSERT_EQUAL_size_t(0, mhi_discovery_build(MHI_DISCOVERY_OU_KWH, all[i], out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
  }
}

// HA expands "~" only where a value starts with it; "~" never ends in "/", so
// no expanded topic holds "//" (spec §8.2).
static void test_no_expanded_topic_contains_a_double_slash(void) {
  const MhiDiscoveryCtx* all[] = {&kDefault, &kUitkijk, &kSlaapkamer};
  for (size_t i = 0; i < 3; i++) {
    for (int r = 0; r < MHI_DISCOVERY_ROWS; r++) {
      if (!mhi_discovery_row_enabled((MhiDiscoveryRow)r, all[i])) continue;
      char out[MHI_DISCOVERY_BUF], expanded[2 * MHI_DISCOVERY_BUF], tilde[80], msg[48];
      snprintf(msg, sizeof(msg), "context %u row %d", (unsigned)i, r);
      TEST_ASSERT_TRUE_MESSAGE(mhi_discovery_build((MhiDiscoveryRow)r, all[i], out, sizeof(out)) > 0, msg);
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, strncmp(out, "{\"~\":\"", 6), msg);
      const char* end = strchr(out + 6, '"');
      TEST_ASSERT_NOT_NULL_MESSAGE(end, msg);
      snprintf(tilde, sizeof(tilde), "%.*s", (int)(end - (out + 6)), out + 6);
      size_t n = 0;
      for (const char* p = out; *p; p++) {
        if (p[0] == '"' && p[1] == '~' && p[2] == '/') {
          n += (size_t)snprintf(expanded + n, sizeof(expanded) - n, "\"%s", tilde);
          p++;  // the "~"; the "/" is copied next
          continue;
        }
        expanded[n++] = *p;
      }
      expanded[n] = '\0';
      TEST_ASSERT_NULL_MESSAGE(strstr(expanded, "//"), msg);
    }
  }
}

static void test_no_row_builds_an_empty_payload(void) {
  const MhiDiscoveryCtx* all[] = {&kDefault, &kUitkijk, &kSlaapkamer};
  for (size_t i = 0; i < 3; i++) {
    for (int r = 0; r < MHI_DISCOVERY_ROWS; r++) {
      char out[MHI_DISCOVERY_BUF], msg[48];
      snprintf(msg, sizeof(msg), "context %u row %d", (unsigned)i, r);
      const size_t n = mhi_discovery_build((MhiDiscoveryRow)r, all[i], out, sizeof(out));
      TEST_ASSERT_EQUAL_size_t_MESSAGE(strlen(out), n, msg);
      if (mhi_discovery_row_enabled((MhiDiscoveryRow)r, all[i])) {
        TEST_ASSERT_TRUE_MESSAGE(n > 2, msg);
        TEST_ASSERT_EQUAL_CHAR_MESSAGE('{', out[0], msg);
        TEST_ASSERT_EQUAL_CHAR_MESSAGE('}', out[n - 1], msg);
      }
    }
  }
}

// --- fork #24: the Restart button ------------------------------------------------

static void test_the_restart_button_presses_set_reset(void) {
  char out[MHI_DISCOVERY_BUF], topic[MHI_DISCOVERY_TOPIC_MAX];
  TEST_ASSERT_FALSE(mhi_discovery_is_outdoor_row(MHI_DISCOVERY_RESTART));
  TEST_ASSERT_TRUE(mhi_discovery_row_enabled(MHI_DISCOVERY_RESTART, &kDefault));
  TEST_ASSERT_TRUE(mhi_discovery_topic(MHI_DISCOVERY_RESTART, &kSlaapkamer, topic, sizeof(topic)) > 0);
  TEST_ASSERT_EQUAL_STRING("homeassistant/button/ac_slaapkamer_restart/config", topic);
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_RESTART, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "{\"~\":\"airco/slaapkamer\",\"name\":\"Restart\",\"uniq_id\":\"ac_slaapkamer_restart\",\"default_entity_id\":\"button.ac_slaapkamer_restart\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"cmd_t\":\"~/set/reset\",\"pl_prs\":\"reset\",\"dev_cla\":\"restart\",\"ent_cat\":\"config\",\"avty_t\":\"~/connected\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"dev\":{\"ids\":[\"airco-slaapkamer\"],"));
  TEST_ASSERT_NULL(strstr(out, "diagnostic"));
  TEST_ASSERT_NULL(strstr(out, "stat_t"));  // a button has no state
}

static void test_a_unit_has_17_entities_with_the_33_byte_frame(void) {
  int with_lr = 0, without_lr = 0;
  for (int r = 0; r < MHI_DISCOVERY_ROWS; r++) {
    if (mhi_discovery_is_outdoor_row((MhiDiscoveryRow)r)) continue;
    if (mhi_discovery_row_enabled((MhiDiscoveryRow)r, &kUitkijk)) with_lr++;
    if (mhi_discovery_row_enabled((MhiDiscoveryRow)r, &kDefault)) without_lr++;
  }
  TEST_ASSERT_EQUAL_INT(17, with_lr);
  TEST_ASSERT_EQUAL_INT(15, without_lr);
}

// --- the committed reference payloads ---------------------------------------

// Writes every enabled row of ctx to <dir>/<fixture name>.txt as
// "<topic>\n<payload>\n", then reads each file back and compares: the file
// holds exactly the row the builder made, well-formed. CI then compares the
// files with the committed ones.
static void write_fixture_set(const MhiDiscoveryCtx* ctx, const char* dir) {
  for (int r = 0; r < MHI_DISCOVERY_ROWS; r++) {
    if (!mhi_discovery_row_enabled((MhiDiscoveryRow)r, ctx)) continue;
    char path[96], topic[MHI_DISCOVERY_TOPIC_MAX], payload[MHI_DISCOVERY_BUF];
    char expected[MHI_DISCOVERY_TOPIC_MAX + MHI_DISCOVERY_BUF + 2], back[sizeof(expected) + 1];
    snprintf(path, sizeof(path), "%s/%s.txt", dir, kFixtureName[r]);
    const size_t topic_len = mhi_discovery_topic((MhiDiscoveryRow)r, ctx, topic, sizeof(topic));
    const size_t payload_len = mhi_discovery_build((MhiDiscoveryRow)r, ctx, payload, sizeof(payload));
    TEST_ASSERT_TRUE_MESSAGE(topic_len > 0 && payload_len > 0, path);
    TEST_ASSERT_TRUE_MESSAGE(json_shape_ok(payload), path);
    snprintf(expected, sizeof(expected), "%s\n%s\n", topic, payload);
    FILE* f = fopen(path, "w");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "cannot write the fixture directory: run pio test from the project root");
    fputs(expected, f);
    fclose(f);
    f = fopen(path, "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    const size_t got = fread(back, 1, sizeof(back) - 1, f);
    fclose(f);
    back[got] = '\0';
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, back, path);
  }
}

static void test_second_fixture_set_is_written(void) { write_fixture_set(&kSlaapkamer, "test/fixtures/discovery_all"); }

static void test_reference_fixtures_are_written(void) { write_fixture_set(&kDefault, "test/fixtures/discovery"); }

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ha_mode_names_are_valid);
  RUN_TEST(test_the_repo_default_mode_names_are_not);
  RUN_TEST(test_topics_carry_the_component_and_the_uniq_id);
  RUN_TEST(test_a_topic_that_does_not_fit_is_refused);
  RUN_TEST(test_every_default_row_fits);
  RUN_TEST(test_every_uitkijk_row_fits);
  RUN_TEST(test_every_slaapkamer_row_fits);
  RUN_TEST(test_a_payload_that_does_not_fit_is_refused);
  RUN_TEST(test_the_climate_is_the_device_and_lists_come_from_the_payload_texts);
  RUN_TEST(test_the_vanes_select_offers_the_names_and_the_unknown_state);
  RUN_TEST(test_the_silent_switch_uses_the_payload_texts);
  RUN_TEST(test_the_problem_sensors_are_diagnostic_and_template_the_topics);
  RUN_TEST(test_the_five_diagnostic_sensors_carry_their_custom_names_classes_and_units);
  RUN_TEST(test_without_a_template_or_entity_prefix_those_keys_are_absent);
  RUN_TEST(test_quotes_in_a_name_are_escaped);
  RUN_TEST(test_slug_is_home_assistants_own_derivation_of_a_name);
  RUN_TEST(test_slug_refuses_an_empty_name_or_a_small_buffer);
  RUN_TEST(test_a_name_that_slugs_to_nothing_fails_the_row);
  RUN_TEST(test_row_enabled_gates_vaneslr_3dauto_and_outdoor);
  RUN_TEST(test_the_climate_gains_swing_horizontal_with_has_lr);
  RUN_TEST(test_the_vaneslr_select_and_3dauto_switch);
  RUN_TEST(test_frame_counters_and_error_code_are_diagnostic);
  RUN_TEST(test_outdoor_rows_carry_their_own_device);
  RUN_TEST(test_outdoor_rows_read_the_group_roots_opdata_topics);
  RUN_TEST(test_the_group_role_row_is_a_unit_row);
  RUN_TEST(test_is_outdoor_row_is_the_closed_outdoor_block);
  RUN_TEST(test_outdoor_rows_use_the_group_base_and_absolute_availability);
  RUN_TEST(test_the_retired_energy_row_is_never_built);
  RUN_TEST(test_no_expanded_topic_contains_a_double_slash);
  RUN_TEST(test_no_row_builds_an_empty_payload);
  RUN_TEST(test_the_restart_button_presses_set_reset);
  RUN_TEST(test_a_unit_has_17_entities_with_the_33_byte_frame);
  RUN_TEST(test_reference_fixtures_are_written);
  RUN_TEST(test_second_fixture_set_is_written);
  return UNITY_END();
}
