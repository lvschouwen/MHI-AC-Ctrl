// The discovery payloads the firmware publishes for one unit, rendered on the
// build machine from the same builder (lib/mhi_pure/mhi_discovery). One line
// per row: the topic, a tab, the JSON. Used for the cutover rehearsal, the
// post-flash check and hass-config's tests (fork #4 batch B, spec §4.4/4.5).
//
// Build:
//   g++ -std=gnu++17 -Wall -Wextra -Werror -I lib/mhi_pure lib/mhi_pure/mhi_discovery.cpp lib/mhi_pure/mhi_group.cpp tools/discovery_payloads.cpp -o .pio/discovery_payloads
// Usage:
//   .pio/discovery_payloads --base airco/uitkijk --hostname airco-uitkijk --device-name "AC Uitkijk"
//     --climate-id AC_Uitkijk --id-prefix ac_uitkijk --entity-prefix ac_uitkijk --version 2eab73c
//     --name-uptime "time since boot" ... --reset-reason-tpl "{{ ... }}"
// Batch C (fork #20/#19/#21) adds: --lr 0|1 (USE_EXTENDED_FRAME_SIZE) with
//   --vanes-lr (eight comma-separated names); --outdoor 0|1 (HA_OUTDOOR_DEVICE, gone since fork #22: see below)
//   with --outdoor-id, --outdoor-name and --outdoor-entity-prefix; --fan-1 ..
//   --fan-4; and one --name-* per new row: --name-vanes-lr, --name-3dauto,
//   --name-frame-errors, --name-frame-timeouts, --name-error-code,
//   --name-ou-outdoor, --name-ou-ct, --name-ou-kwh, --name-ou-comp,
//   --name-ou-defrost, --name-ou-comp-run, --name-ou-protection.
// The outdoor election (fork #22) adds: --group-base (GROUP_ROOT without its
//   trailing slash, default --base: a single split), which the six outdoor
//   rows read, and --name-group-role. --outdoor 1 adds the outdoor rows (eight since fork #41) as this
//   unit publishes them when it is the group's publisher. The retired energy
//   row is never rendered.
// Fork #29 adds --avty-member <hostname>=<MQTT_PREFIX>, once per unit of the
//   group (at most 3, any order; the tool sorts them by hostname as the unit
//   does): the outdoor rows' availability list and, from the first, their
//   via_device. None: this unit alone.
// The Restart button (fork #24) adds --name-restart.
// The run-time row (fork #27) adds --name-run-time; it reads the unit's own
//   OpData/TOTAL-IU-RUN.
// The cleaning and external-Troom rows (fork #25) add --name-cleaning and
//   --name-troom-external; the crash-info row adds --name-crash-info.
// Fork #41 adds --name-expansion-valve, --name-coil-temp, --name-version and,
//   for the outdoor rows, --name-ou-discharge-temp and --name-ou-superheat.
// Every option has the repo default; --modes and --vanes take exactly six
// comma-separated items and --vanes-lr exactly eight; --lr and --outdoor take
// exactly 0 or 1; --outdoor-id defaults to the slug of --group-base plus
// "_outdoor", as the firmware derives HA_OUTDOOR_ID from GROUP_ROOT; an option
// given twice takes its last value.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mhi_discovery.h"
#include "mhi_group.h"

static const char* kNameOption[MHI_DISCOVERY_ROWS] = {
  NULL, "--name-vanes", "--name-silent", "--name-problem", "--name-wiring", "--name-uptime",
  "--name-free-heap", "--name-rssi", "--name-reset-reason", "--name-wifi-phy",
  "--name-vanes-lr", "--name-3dauto", "--name-frame-errors", "--name-frame-timeouts", "--name-error-code",
  "--name-ou-outdoor", "--name-ou-ct", "--name-ou-kwh", "--name-ou-comp", "--name-ou-defrost",
  "--name-ou-comp-run", "--name-ou-protection", "--name-group-role", "--name-restart",
  "--name-run-time", "--name-cleaning", "--name-troom-external", "--name-crash-info",
  "--name-remote", "--name-iu-fan-speed", "--name-internal-setpoint",
  "--name-expansion-valve", "--name-coil-temp", "--name-version", "--name-ou-discharge-temp", "--name-ou-superheat"};

// A 0/1 option; anything else is malformed. External callers compare live
// payloads against this output, so "--lr true" has to be an error rather than a
// silent off that renders the wrong set of rows.
static bool flag(const char* val, bool* out) {
  if (strcmp(val, "0") == 0 || strcmp(val, "1") == 0) {
    *out = val[0] == '1';
    return true;
  }
  return false;
}

// Splits "a,b,c" in place into exactly n items; fewer or more is an error.
static bool split(char* list, const char** items, size_t n) {
  size_t i = 0;
  for (char* p = strtok(list, ","); p; p = strtok(NULL, ",")) {
    if (i == n) return false;
    items[i++] = p;
  }
  return i == n;
}

int main(int argc, char** argv) {
  MhiDiscoveryCtx c = {
    .discovery_prefix = "homeassistant",
    .base = "MHI-AC-Ctrl",
    .set_prefix = "set/",
    .hostname = "MHI-AC-Ctrl",
    .device_name = "MHI-AC-Ctrl",
    .version = "unknown",
    .climate_id = "MHI-AC-Ctrl",
    .id_prefix = "MHI-AC-Ctrl",
    .entity_prefix = NULL,
    .names = {NULL, "Vanes", "Silent", "Problem", "Wiring", "Uptime", "Free heap", "Wi-Fi signal", "Reset reason", "Wi-Fi PHY",
              "Vanes left/right", "3D auto", "Frame errors", "Frame timeouts", "Error code",
              "Temperature", "Current", "Energy", "Compressor frequency", "Defrost", "Compressor run time", "Protection state",
              "Group role", "Restart", "Run time", "Cleaning", "External Troom", "Crash info",
              "Remote", "Indoor fan speed", "Internal setpoint",
              "Expansion valve", "Coil temperature", "Version", "Discharge temperature", "Discharge superheat"},
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
    // outdoor_id, group_base and the availability list are derived after the option loop
    // (from --group-base and --base) unless given; these literals are the
    // same values for the defaults.
    .outdoor_id = "mhi_ac_ctrl_outdoor", .outdoor_name = "AC outdoor unit", .outdoor_entity_prefix = NULL,
    .op_prefix = "OpData/",
    .t_op_outdoor = "OUTDOOR", .t_op_ct = "CT", .t_op_kwh = "KWH", .t_op_comp = "COMP", .t_op_defrost = "DEFROST",
    .t_op_total_comp_run = "TOTAL-COMP-RUN", .t_op_protection_no = "PROTECTION-NO",
    .defrost_on = "On", .defrost_off = "Off",
    .t_frame_errors = "FrameErrors", .t_frame_timeouts = "FrameTimeouts",
    .fan = {"1", "2", "3", "4"},
    .group_base = "MHI-AC-Ctrl",
    .avty_prefix = {NULL, NULL, NULL}, .avty_count = 0, .via_device = NULL,
    .t_group = "Group",
    .t_request_reset = "reset", .request_reset = "reset",
    .unit_op_prefix = "OpData/", .t_op_total_iu_run = "TOTAL-IU-RUN",
    .t_cleaning = "Cleaning", .cleaning_on = "On", .cleaning_off = "Off",
    .t_troom_external = "TroomExternal", .troom_external_on = "On", .troom_external_off = "Off",
    .t_crash_info = "CrashInfo",
    .t_remote = "Remote", .remote_on = "On", .remote_off = "Off",
    .t_op_iu_fanspeed = "IU-FANSPEED", .t_op_tsetpoint = "Tsetpoint",
    .t_op_ou_eev1 = "OU-EEV1", .t_op_thi_r1 = "THI-R1", .t_version = "Version",
    .t_op_td = "TD", .t_op_tdsh = "TDSH",
  };
  bool outdoor_id_given = false, group_base_given = false;
  // --avty-member <host>=<MQTT_PREFIX>, once per unit of the group, sorted by
  // hostname as the unit sorts them (fork #29); none: this unit alone.
  char* member_host[MHI_DISCOVERY_AVTY_MAX];
  char* member_prefix[MHI_DISCOVERY_AVTY_MAX];
  uint8_t members = 0;
  for (int i = 1; i + 1 < argc; i += 2) {
    const char* opt = argv[i];
    char* val = argv[i + 1];
    bool known = true;
    if (strcmp(opt, "--discovery-prefix") == 0) c.discovery_prefix = val;
    else if (strcmp(opt, "--base") == 0) c.base = val;
    else if (strcmp(opt, "--group-base") == 0) { c.group_base = val; group_base_given = true; }
    else if (strcmp(opt, "--set-prefix") == 0) c.set_prefix = val;
    else if (strcmp(opt, "--hostname") == 0) c.hostname = val;
    else if (strcmp(opt, "--device-name") == 0) c.device_name = val;
    else if (strcmp(opt, "--version") == 0) c.version = val;
    else if (strcmp(opt, "--climate-id") == 0) c.climate_id = val;
    else if (strcmp(opt, "--id-prefix") == 0) c.id_prefix = val;
    else if (strcmp(opt, "--entity-prefix") == 0) c.entity_prefix = val;
    else if (strcmp(opt, "--reset-reason-tpl") == 0) c.reset_reason_tpl = val;
    else if (strcmp(opt, "--fan-auto") == 0) c.fan_auto = val;
    else if (strcmp(opt, "--silent-on") == 0) c.silent_on = val;
    else if (strcmp(opt, "--silent-off") == 0) c.silent_off = val;
    else if (strcmp(opt, "--modes") == 0) known = split(val, c.modes, 6);
    else if (strcmp(opt, "--vanes") == 0) known = split(val, c.vanes, 6);
    else if (strcmp(opt, "--lr") == 0) known = flag(val, &c.has_lr);
    else if (strcmp(opt, "--vanes-lr") == 0) known = split(val, c.vanes_lr, 8);
    else if (strcmp(opt, "--outdoor") == 0) known = flag(val, &c.has_outdoor);
    else if (strcmp(opt, "--outdoor-id") == 0) { c.outdoor_id = val; outdoor_id_given = true; }
    else if (strcmp(opt, "--outdoor-name") == 0) c.outdoor_name = val;
    else if (strcmp(opt, "--avty-member") == 0) {
      char* eq = strchr(val, '=');
      known = eq != NULL && eq != val && eq[1] != '\0' && members < MHI_DISCOVERY_AVTY_MAX;
      if (known) {
        *eq = '\0';
        member_host[members] = val;
        member_prefix[members++] = eq + 1;
      }
    }
    else if (strcmp(opt, "--outdoor-entity-prefix") == 0) c.outdoor_entity_prefix = val;
    else if (strcmp(opt, "--fan-1") == 0) c.fan[0] = val;
    else if (strcmp(opt, "--fan-2") == 0) c.fan[1] = val;
    else if (strcmp(opt, "--fan-3") == 0) c.fan[2] = val;
    else if (strcmp(opt, "--fan-4") == 0) c.fan[3] = val;
    else {
      known = false;
      for (int r = 1; r < MHI_DISCOVERY_ROWS; r++)
        if (strcmp(opt, kNameOption[r]) == 0) { c.names[r] = val; known = true; }
    }
    if (!known) {
      fprintf(stderr, "discovery_payloads: unknown or malformed option %s\n", opt);
      return 2;
    }
  }
  if ((argc - 1) % 2 != 0) {
    fprintf(stderr, "discovery_payloads: every option takes one value\n");
    return 2;
  }
  // What the firmware derives, once the option loop is done: GROUP_ROOT
  // defaults to MQTT_PREFIX, a lone unit's availability list is itself
  // (<MQTT_PREFIX> = --base plus "/"), and HA_OUTDOOR_ID defaults to the slug
  // of GROUP_ROOT plus "_outdoor" (src/support.cpp, mhi_group_default_outdoor_id).
  if (!group_base_given) c.group_base = c.base;
  char own_prefix[MHI_DISCOVERY_TOPIC_MAX];
  if (members == 0) {
    const int an = snprintf(own_prefix, sizeof(own_prefix), "%s/", c.base);
    if (an < 0 || (size_t)an >= sizeof(own_prefix)) {
      fprintf(stderr, "discovery_payloads: --base is too long for the availability topic\n");
      return 1;
    }
    member_host[0] = (char*)c.hostname;
    member_prefix[0] = own_prefix;
    members = 1;
  }
  // Sorted by hostname as mhi_group_availability() sorts them, whatever order
  // the caller gave; a hostname given twice is a mistake, not a list.
  for (uint8_t i = 1; i < members; i++)
    for (uint8_t j = i; j > 0 && strcmp(member_host[j], member_host[j - 1]) < 0; j--) {
      char* th = member_host[j];
      member_host[j] = member_host[j - 1];
      member_host[j - 1] = th;
      char* tp = member_prefix[j];
      member_prefix[j] = member_prefix[j - 1];
      member_prefix[j - 1] = tp;
    }
  for (uint8_t i = 1; i < members; i++)
    if (strcmp(member_host[i], member_host[i - 1]) == 0) {
      fprintf(stderr, "discovery_payloads: --avty-member %s given twice\n", member_host[i]);
      return 2;
    }
  for (uint8_t i = 0; i < members; i++) c.avty_prefix[i] = member_prefix[i];
  c.avty_count = members;
  c.via_device = member_host[0];
  char derived_outdoor_id[MHI_GROUP_ID_MAX + 1];
  if (!outdoor_id_given) {
    // The slug ignores the trailing slash, so the group base derives what GROUP_ROOT does.
    if (mhi_group_default_outdoor_id(c.group_base, derived_outdoor_id, sizeof(derived_outdoor_id)) == 0) {
      fprintf(stderr, "discovery_payloads: --group-base is too long for the derived outdoor id; give --outdoor-id\n");
      return 1;
    }
    c.outdoor_id = derived_outdoor_id;
  }
  if (!mhi_discovery_modes_valid(&c)) {
    fprintf(stderr, "discovery_payloads: the modes are not Home Assistant's; the firmware would skip the climate row\n");
    return 1;
  }
  for (int r = 0; r < MHI_DISCOVERY_ROWS; r++) {
    if (!mhi_discovery_row_enabled((MhiDiscoveryRow)r, &c)) continue;
    char topic[MHI_DISCOVERY_TOPIC_MAX], payload[MHI_DISCOVERY_BUF];
    if (mhi_discovery_topic((MhiDiscoveryRow)r, &c, topic, sizeof(topic)) == 0 ||
        mhi_discovery_build((MhiDiscoveryRow)r, &c, payload, sizeof(payload)) == 0) {
      fprintf(stderr, "discovery_payloads: row %d does not fit\n", r);
      return 1;
    }
    printf("%s\t%s\n", topic, payload);
  }
  return 0;
}
