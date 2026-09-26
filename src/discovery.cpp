#include "discovery.h"

#include <Arduino.h>

#include "MHI-AC-Ctrl-core.h"
#include "MHI-AC-Ctrl.h"
#include "mhi_diag.h"
#include "mhi_discovery.h"
#include "support.h"

#ifdef HA_DISCOVERY

// The climate's "off" mode is set/Mode <PAYLOAD_MODE_OFF>, which the firmware
// parses only with POWERON_WHEN_CHANGING_MODE; without it Home Assistant could
// not switch the unit off and Mode would show the last mode while it is off.
#ifndef POWERON_WHEN_CHANGING_MODE
#error "HA_DISCOVERY needs POWERON_WHEN_CHANGING_MODE: the climate's off mode is set/Mode off"
#endif

// The ~/set/... topics assume the set prefix sits under the status prefix,
// as the defaults do (MQTT_SET_PREFIX MQTT_PREFIX "set/").
constexpr bool starts_with(const char* s, const char* prefix) {
  return *prefix == '\0' || (*s == *prefix && starts_with(s + 1, prefix + 1));
}
static_assert(starts_with(MQTT_SET_PREFIX, MQTT_PREFIX), "HA_DISCOVERY needs MQTT_SET_PREFIX to start with MQTT_PREFIX");
// The outdoor rows read "~/<op_prefix><topic>" with the group root as "~"
// (fork #22), so the publisher's prefix must sit under the group root.
static_assert(starts_with(GROUP_OP_PREFIX, GROUP_ROOT), "HA_DISCOVERY needs GROUP_OP_PREFIX to start with GROUP_ROOT");
// The run-time row reads "~/<unit_op_prefix><topic>" with the unit's own
// prefix as "~" (fork #27).
static_assert(starts_with(MQTT_OP_PREFIX, MQTT_PREFIX), "HA_DISCOVERY needs MQTT_OP_PREFIX to start with MQTT_PREFIX");
static_assert(sizeof(MQTT_PREFIX) > 1, "HA_DISCOVERY needs a non-empty MQTT_PREFIX");
// Every topic in the configs is "~/<name>", so the prefix must end in "/".
static_assert(MQTT_PREFIX[sizeof(MQTT_PREFIX) - 2] == '/', "HA_DISCOVERY needs MQTT_PREFIX to end with /");

// MQTT_PREFIX without its trailing slash: the payload's "~".
static char base_topic[sizeof(MQTT_PREFIX)];
// GROUP_ROOT without its trailing slash: the outdoor rows' "~" (fork #22).
static char group_base_topic[sizeof(GROUP_ROOT)];

// Not const: discovery_setup() fills in outdoor_id, which is derived at boot.
static MhiDiscoveryCtx ctx = {
  .discovery_prefix = HA_DISCOVERY_PREFIX,
  .base = base_topic,
  .set_prefix = MQTT_SET_PREFIX + (sizeof(MQTT_PREFIX) - 1),
  .hostname = HOSTNAME,
  .device_name = HA_DEVICE_NAME,
  .version = VERSION,
  .climate_id = HA_CLIMATE_ID,
  .id_prefix = HA_ID_PREFIX,
#ifdef HA_ENTITY_PREFIX
  .entity_prefix = HA_ENTITY_PREFIX,
#else
  .entity_prefix = NULL,
#endif
  .names = {NULL, HA_NAME_VANES, HA_NAME_SILENT, HA_NAME_PROBLEM, HA_NAME_WIRING, HA_NAME_UPTIME, HA_NAME_FREE_HEAP,
            HA_NAME_RSSI, HA_NAME_RESET_REASON, HA_NAME_WIFI_PHY,
            HA_NAME_VANES_LR, HA_NAME_3DAUTO, HA_NAME_FRAME_ERRORS, HA_NAME_FRAME_TIMEOUTS, HA_NAME_ERROR_CODE,
            HA_NAME_OU_OUTDOOR, HA_NAME_OU_CT, HA_NAME_OU_KWH, HA_NAME_OU_COMP, HA_NAME_OU_DEFROST,
            HA_NAME_OU_COMP_RUN, HA_NAME_OU_PROTECTION, HA_NAME_GROUP_ROLE, HA_NAME_RESTART, HA_NAME_RUN_TIME,
            HA_NAME_CLEANING, HA_NAME_TROOM_EXTERNAL, HA_NAME_CRASH_INFO,
            HA_NAME_REMOTE, HA_NAME_IU_FAN_SPEED, HA_NAME_INTERNAL_SETPOINT,
            HA_NAME_EXPANSION_VALVE, HA_NAME_COIL_TEMP, HA_NAME_VERSION, HA_NAME_OU_DISCHARGE_TEMP, HA_NAME_OU_SUPERHEAT},
#ifdef HA_RESET_REASON_TPL
  .reset_reason_tpl = HA_RESET_REASON_TPL,
#else
  .reset_reason_tpl = NULL,
#endif
  .t_mode = TOPIC_MODE, .t_tsetpoint = TOPIC_TSETPOINT, .t_fan = TOPIC_FAN, .t_vanes = TOPIC_VANES, .t_troom = TOPIC_TROOM,
  .t_action = TOPIC_ACTION, .t_connected = TOPIC_CONNECTED, .t_silent = TOPIC_SILENT, .t_errorcode = TOPIC_ERRORCODE,
  .t_wiring = TOPIC_WIRING, .t_uptime = TOPIC_UPTIME, .t_free_heap = TOPIC_FREE_HEAP, .t_rssi = TOPIC_RSSI,
  .t_reset_reason = TOPIC_RESET_REASON, .t_wifi_phy = TOPIC_WIFI_PHY,
  .modes = {PAYLOAD_MODE_OFF, PAYLOAD_MODE_AUTO, PAYLOAD_MODE_DRY, PAYLOAD_MODE_COOL, PAYLOAD_MODE_FAN, PAYLOAD_MODE_HEAT},
  .fan_auto = PAYLOAD_FAN_AUTO,
  .vanes = {PAYLOAD_VANES_1, PAYLOAD_VANES_2, PAYLOAD_VANES_3, PAYLOAD_VANES_4, PAYLOAD_VANES_SWING, PAYLOAD_VANES_UNKNOWN},
  .connected_on = PAYLOAD_CONNECTED_TRUE, .connected_off = PAYLOAD_CONNECTED_FALSE,
  .silent_on = PAYLOAD_SILENT_ON, .silent_off = PAYLOAD_SILENT_OFF,
  .wiring_ok = MHI_WIRING_OK,
#ifdef USE_EXTENDED_FRAME_SIZE
  .has_lr = true,
#else
  .has_lr = false,
#endif
  .t_vaneslr = TOPIC_VANESLR, .t_3dauto = TOPIC_3DAUTO,
  .vanes_lr = {PAYLOAD_VANESLR_1, PAYLOAD_VANESLR_2, PAYLOAD_VANESLR_3, PAYLOAD_VANESLR_4, PAYLOAD_VANESLR_5,
               PAYLOAD_VANESLR_6, PAYLOAD_VANESLR_7, PAYLOAD_VANESLR_SWING},
  .threedauto_on = PAYLOAD_3DAUTO_ON, .threedauto_off = PAYLOAD_3DAUTO_OFF,
  .has_outdoor = true,  // every unit is a candidate publisher; the group starts the outdoor cursor (fork #22)
  .outdoor_id = NULL, .outdoor_name = HA_OUTDOOR_NAME,  // outdoor_id: discovery_setup()
#ifdef HA_OUTDOOR_ENTITY_PREFIX
  .outdoor_entity_prefix = HA_OUTDOOR_ENTITY_PREFIX,
#else
  .outdoor_entity_prefix = NULL,
#endif
  .op_prefix = GROUP_OP_PREFIX + (sizeof(GROUP_ROOT) - 1),
  .t_op_outdoor = TOPIC_OUTDOOR, .t_op_ct = TOPIC_CT, .t_op_kwh = TOPIC_KWH, .t_op_comp = TOPIC_COMP,
  .t_op_defrost = TOPIC_DEFROST, .t_op_total_comp_run = TOPIC_TOTAL_COMP_RUN, .t_op_protection_no = TOPIC_PROTECTION_NO,
  .defrost_on = PAYLOAD_OP_DEFROST_ON, .defrost_off = PAYLOAD_OP_DEFROST_OFF,
  .t_frame_errors = TOPIC_FRAME_ERRORS, .t_frame_timeouts = TOPIC_FRAME_TIMEOUTS,
  .fan = {PAYLOAD_FAN_1, PAYLOAD_FAN_2, PAYLOAD_FAN_3, PAYLOAD_FAN_4},
  .group_base = group_base_topic,
  .avty_prefix = {NULL, NULL, NULL}, .avty_count = 0, .via_device = NULL,  // discovery_start_outdoor() (fork #29)
  .t_group = TOPIC_GROUP,
  .t_request_reset = TOPIC_REQUEST_RESET, .request_reset = PAYLOAD_REQUEST_RESET,
  .unit_op_prefix = MQTT_OP_PREFIX + (sizeof(MQTT_PREFIX) - 1), .t_op_total_iu_run = TOPIC_TOTAL_IU_RUN,
  .t_cleaning = TOPIC_CLEANING, .cleaning_on = PAYLOAD_CLEANING_ON, .cleaning_off = PAYLOAD_CLEANING_OFF,
  .t_troom_external = TOPIC_TROOM_EXTERNAL,
  .troom_external_on = PAYLOAD_TROOM_EXTERNAL_ON, .troom_external_off = PAYLOAD_TROOM_EXTERNAL_OFF,
  .t_crash_info = TOPIC_CRASH_INFO,
  .t_remote = TOPIC_REMOTE, .remote_on = PAYLOAD_REMOTE_ON, .remote_off = PAYLOAD_REMOTE_OFF,
  .t_op_iu_fanspeed = TOPIC_IU_FANSPEED, .t_op_tsetpoint = TOPIC_TSETPOINT,
  .t_op_ou_eev1 = TOPIC_OU_EEV1, .t_op_thi_r1 = TOPIC_THI_R1, .t_version = TOPIC_VERSION,
  .t_op_td = TOPIC_TD, .t_op_tdsh = TOPIC_TDSH,
};

static bool modes_ok = false;
static uint8_t next_row = MHI_DISCOVERY_ROWS;          // the unit rows; nothing to publish until a connect
static uint8_t next_outdoor_row = MHI_DISCOVERY_ROWS;  // the outdoor rows; only the group starts it
static bool unit_row_skipped = false;     // a unit row did not fit since the connect (fork #24)
static bool outdoor_row_skipped = false;  // an outdoor row did not fit in this outdoor pass
static bool status_ok = false;            // the Discovery topic says ok; modes > skipped > ok over both passes

// A prefix without its trailing slash, for a payload's "~".
static void strip_slash(char* dst, size_t size, const char* prefix) {
  strncpy(dst, prefix, size);
  dst[size - 1] = '\0';
  const size_t n = strlen(dst);
  if (n > 0 && dst[n - 1] == '/') dst[n - 1] = '\0';
}

void discovery_setup() {
  strip_slash(base_topic, sizeof(base_topic), MQTT_PREFIX);
  strip_slash(group_base_topic, sizeof(group_base_topic), GROUP_ROOT);
  ctx.outdoor_id = outdoor_id();
  modes_ok = mhi_discovery_modes_valid(&ctx);
  if (!modes_ok)
    Serial.println(F("HA_DISCOVERY: the PAYLOAD_MODE_* texts are not Home Assistant's mode names, the climate config will not be published (Discovery: modes)"));
}

void discovery_restart() {
  next_row = 0;
  unit_row_skipped = false;
  status_ok = false;  // until the unit rows are through
}

// The list the outdoor rows carry, copied: the group's peer table may change
// while the rows go out one per pass (fork #29).
static MhiGroupAvty avty;

void discovery_start_outdoor(const MhiGroupAvty* list) {
  avty = *list;
  ctx.avty_count = avty.count;
  for (uint8_t i = 0; i < MHI_DISCOVERY_AVTY_MAX; i++) ctx.avty_prefix[i] = avty.prefix[i];
  ctx.via_device = avty.host[0];
  next_outdoor_row = MHI_DISCOVERY_OU_OUTDOOR;
  outdoor_row_skipped = false;
}

void discovery_cancel_outdoor() {
  next_outdoor_row = MHI_DISCOVERY_ROWS;
}

// One row, retained. A row that is not part of this build is skipped, one that
// does not fit is refused, so a discovery topic never gets an empty payload.
// False only for a row that did not fit: the Discovery topic says "skipped".
static bool publish_row(MhiDiscoveryRow row) {
  // Static, not on the stack: loop() runs on the ESP8266's 4 KB cont stack and
  // the publish path runs below this frame.
  static char payload[MHI_DISCOVERY_BUF];
  char topic[MHI_DISCOVERY_TOPIC_MAX];
  if (!mhi_discovery_row_enabled(row, &ctx)) return true;  // has_lr off, or the retired energy row
  if (mhi_discovery_topic(row, &ctx, topic, sizeof(topic)) == 0 ||
      mhi_discovery_build(row, &ctx, payload, sizeof(payload)) == 0) {
    Serial.printf_P(PSTR("HA_DISCOVERY: row %u does not fit, not published\n"), (unsigned)row);
    return false;
  }
  MQTTclient.publish(topic, payload, true);
  return true;
}

void discovery_loop() {
  if (!MQTTclient.connected()) return;
  if (next_row < MHI_DISCOVERY_ROWS) {
    const MhiDiscoveryRow row = (MhiDiscoveryRow)next_row++;
    if (row == MHI_DISCOVERY_CLIMATE && !modes_ok) {
      // skipped: said so at boot, and the Discovery topic says "modes"
    }
    else if (!mhi_discovery_is_outdoor_row(row) && !publish_row(row)) {  // the outdoor rows are the group's
      unit_row_skipped = true;
    }
    if (next_row == MHI_DISCOVERY_ROWS) {  // "modes" first, then "skipped" (fork #24 spec §2.1)
      if (!modes_ok)
        output_P((ACStatus)type_status, PSTR(TOPIC_DISCOVERY), PSTR(PAYLOAD_DISCOVERY_MODES));
      else if (unit_row_skipped)
        output_P((ACStatus)type_status, PSTR(TOPIC_DISCOVERY), PSTR(PAYLOAD_DISCOVERY_SKIPPED));
      else
        output_P((ACStatus)type_status, PSTR(TOPIC_DISCOVERY), PSTR(PAYLOAD_DISCOVERY_OK));
      status_ok = modes_ok && !unit_row_skipped;
    }
  }
  else if (next_outdoor_row < MHI_DISCOVERY_ROWS) {  // after the unit rows, one per pass
    const MhiDiscoveryRow row = mhi_discovery_next_outdoor_row(next_outdoor_row);  // both blocks (fork #41)
    if (row < MHI_DISCOVERY_ROWS) {
      next_outdoor_row = row + 1;
      if (!publish_row(row)) outdoor_row_skipped = true;
    }
    else {
      next_outdoor_row = MHI_DISCOVERY_ROWS;  // past the last outdoor row: done
      // Nothing when all fit (fork #24 spec §2.1), and never over modes or
      // an earlier skipped: modes > skipped > ok across both passes.
      if (outdoor_row_skipped && status_ok) {
        status_ok = false;
        output_P((ACStatus)type_status, PSTR(TOPIC_DISCOVERY), PSTR(PAYLOAD_DISCOVERY_SKIPPED));
      }
    }
  }
}

#else

void discovery_setup() {}
void discovery_restart() {}
void discovery_loop() {}
void discovery_start_outdoor(const MhiGroupAvty*) {}
void discovery_cancel_outdoor() {}

#endif
