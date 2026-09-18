// The discovery payloads the firmware publishes for one unit, rendered on the
// build machine from the same builder (lib/mhi_pure/mhi_discovery). One line
// per row: the topic, a tab, the JSON. Used for the cutover rehearsal, the
// post-flash check and hass-config's tests (fork #4 batch B, spec §4.4/4.5).
//
// Build:
//   g++ -std=gnu++17 -Wall -Wextra -Werror -I lib/mhi_pure lib/mhi_pure/mhi_discovery.cpp tools/discovery_payloads.cpp -o .pio/discovery_payloads
// Usage:
//   .pio/discovery_payloads --base airco/uitkijk --hostname airco-uitkijk --device-name "AC Uitkijk"
//     --climate-id AC_Uitkijk --id-prefix ac_uitkijk --entity-prefix ac_uitkijk --version 2eab73c
//     --name-uptime "tijd sinds opstart" ... --reset-reason-tpl "{{ ... }}"
// Every option has the repo default; --modes and --vanes take exactly six
// comma-separated items; an option given twice takes its last value.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mhi_discovery.h"

static const char* kNameOption[MHI_DISCOVERY_ROWS] = {
  NULL, "--name-vanes", "--name-silent", "--name-problem", "--name-wiring", "--name-uptime",
  "--name-free-heap", "--name-rssi", "--name-reset-reason", "--name-wifi-phy",
  "--name-vanes-lr", "--name-3dauto", "--name-frame-errors", "--name-frame-timeouts", "--name-error-code",
  "--name-ou-outdoor", "--name-ou-ct", "--name-ou-kwh", "--name-ou-comp", "--name-ou-defrost",
  "--name-ou-comp-run", "--name-ou-protection"};

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
              "Temperature", "Current", "Energy", "Compressor frequency", "Defrost", "Compressor run time", "Protection state"},
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
    .outdoor_id = "MHI-AC-Ctrl_outdoor", .outdoor_name = "AC outdoor unit", .outdoor_entity_prefix = NULL,
    .op_prefix = "OpData/",
    .t_op_outdoor = "OUTDOOR", .t_op_ct = "CT", .t_op_kwh = "KWH", .t_op_comp = "COMP", .t_op_defrost = "DEFROST",
    .t_op_total_comp_run = "TOTAL-COMP-RUN", .t_op_protection_no = "PROTECTION-NO",
    .defrost_on = "On", .defrost_off = "Off",
    .t_frame_errors = "FrameErrors", .t_frame_timeouts = "FrameTimeouts",
    .fan = {"1", "2", "3", "4"},
  };
  for (int i = 1; i + 1 < argc; i += 2) {
    const char* opt = argv[i];
    char* val = argv[i + 1];
    bool known = true;
    if (strcmp(opt, "--discovery-prefix") == 0) c.discovery_prefix = val;
    else if (strcmp(opt, "--base") == 0) c.base = val;
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
