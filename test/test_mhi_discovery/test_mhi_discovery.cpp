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
  .names = {NULL, "Vanes", "Silent", "Problem", "Wiring", "Uptime", "Free heap", "Wi-Fi signal", "Reset reason", "Wi-Fi PHY"},
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
};

// Lucas's Uitkijk: the names hass-config uses, a template, an entity prefix,
// and a quote in the device name to exercise the escaping.
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
  .names = {NULL, "lamellen", "stil", "storing", "bedrading", "tijd sinds opstart", "vrij geheugen", "wifi-signaal", "herstartreden", "wifi-standaard"},
  .reset_reason_tpl = "{{ {'Power On': 'stroom ingeschakeld', 'Software/System restart': 'software-herstart (update of reset)', 'Hardware Watchdog': 'hardware-watchdog', 'Software Watchdog': 'software-watchdog', 'Exception': 'crash', 'Deep-Sleep Wake': 'wakker uit diepe slaap', 'External System': 'externe reset'}.get(value, value) }}",
  .t_mode = "Mode", .t_tsetpoint = "Tsetpoint", .t_fan = "Fan", .t_vanes = "Vanes", .t_troom = "Troom", .t_action = "Action",
  .t_connected = "connected", .t_silent = "Silent", .t_errorcode = "Errorcode", .t_wiring = "Wiring",
  .t_uptime = "Uptime", .t_free_heap = "FreeHeap", .t_rssi = "RSSI", .t_reset_reason = "ResetReason", .t_wifi_phy = "WIFI_PHY",
  .modes = {"off", "auto", "dry", "cool", "fan_only", "heat"},
  .fan_auto = "Auto",
  .vanes = {"Up", "UpCenter", "CenterDown", "Down", "Swing", "?"},
  .connected_on = "1", .connected_off = "0",
  .silent_on = "On", .silent_off = "Off",
  .wiring_ok = "o.k.",
};

static const char* const kFixtureName[MHI_DISCOVERY_ROWS] = {
  "climate", "vanes", "silent", "problem", "wiring", "uptime", "free_heap", "rssi", "reset_reason", "wifi_phy"};

// Balanced braces and brackets outside strings, every string closed, no
// printf conversion left over and no NULL argument printed.
static bool json_shape_ok(const char* s) {
  int depth = 0;
  bool in_str = false;
  if (strstr(s, "(null)") != NULL) return false;
  for (; *s; s++) {
    if (in_str) {
      if (*s == '\\' && s[1]) s++;
      else if (*s == '"') in_str = false;
      continue;
    }
    if (*s == '"') in_str = true;
    else if (*s == '{' || *s == '[') depth++;
    else if (*s == '}' || *s == ']') { if (--depth < 0) return false; }
    else if (*s == '%') return false;
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

static void every_row_fits(const MhiDiscoveryCtx* ctx, const char* label) {
  size_t longest = 0;
  int longest_row = -1;
  for (int r = 0; r < MHI_DISCOVERY_ROWS; r++) {
    char out[MHI_DISCOVERY_BUF];
    const size_t n = mhi_discovery_build((MhiDiscoveryRow)r, ctx, out, sizeof(out));
    char msg[64];
    snprintf(msg, sizeof(msg), "%s row %d", label, r);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, msg);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(strlen(out), n, msg);
    TEST_ASSERT_TRUE_MESSAGE(json_shape_ok(out), msg);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"uniq_id\":\""), msg);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"avty_t\":\"~/connected\",\"pl_avail\":\"1\",\"pl_not_avail\":\"0\""), msg);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"dev\":{\"ids\":[\""), msg);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"mf\":\"Mitsubishi Heavy Industries\",\"mdl\":\"MHI-AC-Ctrl\",\"sw\":\""), msg);
    if (n > longest) { longest = n; longest_row = r; }
  }
  printf("  %s: longest row %d, %u of %u bytes\n", label, longest_row, (unsigned)longest, (unsigned)MHI_DISCOVERY_BUF);
}

static void test_every_default_row_fits(void) { every_row_fits(&kDefault, "default"); }
static void test_every_uitkijk_row_fits(void) { every_row_fits(&kUitkijk, "uitkijk"); }

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
  TEST_ASSERT_NOT_NULL(strstr(out, "\"name\":\"lamellen\",\"uniq_id\":\"ac_uitkijk_vanes\",\"default_entity_id\":\"select.ac_uitkijk_lamellen\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/Vanes\",\"cmd_t\":\"~/set/Vanes\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ops\":[\"Up\",\"UpCenter\",\"CenterDown\",\"Down\",\"Swing\",\"?\"],"));
  TEST_ASSERT_NULL(strstr(out, "ent_cat"));
}

static void test_the_silent_switch_uses_the_payload_texts(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_SILENT, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"uniq_id\":\"ac_uitkijk_silent\",\"default_entity_id\":\"switch.ac_uitkijk_stil\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/Silent\",\"cmd_t\":\"~/set/Silent\",\"pl_on\":\"On\",\"pl_off\":\"Off\",\"ic\":\"mdi:volume-low\","));
}

static void test_the_problem_sensors_are_diagnostic_and_template_the_topics(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_PROBLEM, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"default_entity_id\":\"binary_sensor.ac_uitkijk_storing\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/Errorcode\",\"val_tpl\":\"{{ 'ON' if value|int(0) != 0 else 'OFF' }}\",\"dev_cla\":\"problem\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_WIRING, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/Wiring\",\"val_tpl\":\"{{ 'OFF' if value == 'o.k.' else 'ON' }}\",\"dev_cla\":\"problem\",\"ent_cat\":\"diagnostic\","));
}

static void test_the_five_sensors_match_hass_config_304(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_UPTIME, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"name\":\"tijd sinds opstart\",\"uniq_id\":\"ac_uitkijk_uptime\",\"default_entity_id\":\"sensor.ac_uitkijk_tijd_sinds_opstart\","));
  // Plain seconds, no state class: hass-config's reboot counter compares the raw state.
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/Uptime\",\"dev_cla\":\"duration\",\"unit_of_meas\":\"s\",\"sug_dsp_prc\":0,\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_NULL(strstr(out, "stat_cla"));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_FREE_HEAP, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/FreeHeap\",\"dev_cla\":\"data_size\",\"unit_of_meas\":\"B\",\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_RSSI, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/RSSI\",\"dev_cla\":\"signal_strength\",\"unit_of_meas\":\"dBm\",\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_RESET_REASON, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/ResetReason\",\"val_tpl\":\"{{ {'Power On': 'stroom ingeschakeld', "));
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
  // What HA makes of "AC Slaapkamer" + "tijd sinds opstart" without an area:
  // lower case, every run of non-alphanumerics one underscore, none at the ends.
  char out[48];
  TEST_ASSERT_EQUAL_size_t(18, mhi_discovery_slug("tijd sinds opstart", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("tijd_sinds_opstart", out);
  TEST_ASSERT_TRUE(mhi_discovery_slug("wifi-signaal", out, sizeof(out)) > 0);
  TEST_ASSERT_EQUAL_STRING("wifi_signaal", out);
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
  TEST_ASSERT_EQUAL_size_t(0, mhi_discovery_slug("tijd sinds opstart", out, sizeof(out)));
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

// --- the committed reference payloads ---------------------------------------

static void test_reference_fixtures_are_written(void) {
  for (int r = 0; r < MHI_DISCOVERY_ROWS; r++) {
    char path[80], topic[MHI_DISCOVERY_TOPIC_MAX], payload[MHI_DISCOVERY_BUF];
    snprintf(path, sizeof(path), "test/fixtures/discovery/%s.txt", kFixtureName[r]);
    TEST_ASSERT_TRUE(mhi_discovery_topic((MhiDiscoveryRow)r, &kDefault, topic, sizeof(topic)) > 0);
    TEST_ASSERT_TRUE(mhi_discovery_build((MhiDiscoveryRow)r, &kDefault, payload, sizeof(payload)) > 0);
    FILE* f = fopen(path, "w");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "cannot write test/fixtures/discovery/: run pio test from the project root");
    fprintf(f, "%s\n%s\n", topic, payload);
    fclose(f);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ha_mode_names_are_valid);
  RUN_TEST(test_the_repo_default_mode_names_are_not);
  RUN_TEST(test_topics_carry_the_component_and_the_uniq_id);
  RUN_TEST(test_a_topic_that_does_not_fit_is_refused);
  RUN_TEST(test_every_default_row_fits);
  RUN_TEST(test_every_uitkijk_row_fits);
  RUN_TEST(test_a_payload_that_does_not_fit_is_refused);
  RUN_TEST(test_the_climate_is_the_device_and_lists_come_from_the_payload_texts);
  RUN_TEST(test_the_vanes_select_offers_the_names_and_the_unknown_state);
  RUN_TEST(test_the_silent_switch_uses_the_payload_texts);
  RUN_TEST(test_the_problem_sensors_are_diagnostic_and_template_the_topics);
  RUN_TEST(test_the_five_sensors_match_hass_config_304);
  RUN_TEST(test_without_a_template_or_entity_prefix_those_keys_are_absent);
  RUN_TEST(test_quotes_in_a_name_are_escaped);
  RUN_TEST(test_slug_is_home_assistants_own_derivation_of_a_name);
  RUN_TEST(test_slug_refuses_an_empty_name_or_a_small_buffer);
  RUN_TEST(test_a_name_that_slugs_to_nothing_fails_the_row);
  RUN_TEST(test_reference_fixtures_are_written);
  return UNITY_END();
}
