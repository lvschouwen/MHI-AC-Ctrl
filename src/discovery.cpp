#include "discovery.h"

#include <Arduino.h>

#include "MHI-AC-Ctrl-core.h"
#include "MHI-AC-Ctrl.h"
#include "mhi_diag.h"
#include "mhi_discovery.h"
#include "support.h"

// Outside the #ifdef below: inside it this could never fire.
#if defined(HA_OUTDOOR_DEVICE) && !defined(HA_DISCOVERY)
#error "HA_OUTDOOR_DEVICE needs HA_DISCOVERY"
#endif

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
// The outdoor rows read "~/<op_prefix><topic>", so the operating-data prefix
// must sit under the status prefix too.
static_assert(starts_with(MQTT_OP_PREFIX, MQTT_PREFIX), "HA_DISCOVERY needs MQTT_OP_PREFIX to start with MQTT_PREFIX");
static_assert(sizeof(MQTT_PREFIX) > 1, "HA_DISCOVERY needs a non-empty MQTT_PREFIX");
// Every topic in the configs is "~/<name>", so the prefix must end in "/".
static_assert(MQTT_PREFIX[sizeof(MQTT_PREFIX) - 2] == '/', "HA_DISCOVERY needs MQTT_PREFIX to end with /");

// MQTT_PREFIX without its trailing slash: the payload's "~".
static char base_topic[sizeof(MQTT_PREFIX)];

static const MhiDiscoveryCtx ctx = {
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
            HA_NAME_OU_COMP_RUN, HA_NAME_OU_PROTECTION},
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
#ifdef HA_OUTDOOR_DEVICE
  .has_outdoor = true,
#else
  .has_outdoor = false,
#endif
  .outdoor_id = HA_OUTDOOR_ID, .outdoor_name = HA_OUTDOOR_NAME,
#ifdef HA_OUTDOOR_ENTITY_PREFIX
  .outdoor_entity_prefix = HA_OUTDOOR_ENTITY_PREFIX,
#else
  .outdoor_entity_prefix = NULL,
#endif
  .op_prefix = MQTT_OP_PREFIX + (sizeof(MQTT_PREFIX) - 1),
  .t_op_outdoor = TOPIC_OUTDOOR, .t_op_ct = TOPIC_CT, .t_op_kwh = TOPIC_KWH, .t_op_comp = TOPIC_COMP,
  .t_op_defrost = TOPIC_DEFROST, .t_op_total_comp_run = TOPIC_TOTAL_COMP_RUN, .t_op_protection_no = TOPIC_PROTECTION_NO,
  .defrost_on = PAYLOAD_OP_DEFROST_ON, .defrost_off = PAYLOAD_OP_DEFROST_OFF,
  .t_frame_errors = TOPIC_FRAME_ERRORS, .t_frame_timeouts = TOPIC_FRAME_TIMEOUTS,
  .fan = {PAYLOAD_FAN_1, PAYLOAD_FAN_2, PAYLOAD_FAN_3, PAYLOAD_FAN_4},
};

static bool modes_ok = false;
static uint8_t next_row = MHI_DISCOVERY_ROWS;  // nothing to publish until a connect

void discovery_setup() {
  strncpy(base_topic, MQTT_PREFIX, sizeof(base_topic));
  base_topic[sizeof(base_topic) - 1] = '\0';
  const size_t n = strlen(base_topic);
  if (n > 0 && base_topic[n - 1] == '/') base_topic[n - 1] = '\0';
  modes_ok = mhi_discovery_modes_valid(&ctx);
  if (!modes_ok)
    Serial.println(F("HA_DISCOVERY: the PAYLOAD_MODE_* texts are not Home Assistant's mode names, the climate config will not be published (Discovery: modes)"));
}

void discovery_restart() {
  next_row = 0;
}

void discovery_loop() {
  // Static, not on the stack: loop() runs on the ESP8266's 4 KB cont stack and
  // the publish path runs below this frame.
  static char payload[MHI_DISCOVERY_BUF];
  char topic[MHI_DISCOVERY_TOPIC_MAX];
  if (next_row >= MHI_DISCOVERY_ROWS || !MQTTclient.connected()) return;
  const MhiDiscoveryRow row = (MhiDiscoveryRow)next_row++;
  if (row == MHI_DISCOVERY_CLIMATE && !modes_ok) {
    // skipped: said so at boot, and the Discovery topic says "modes"
  }
  else if (!mhi_discovery_row_enabled(row, &ctx)) {
    // skipped: has_lr or has_outdoor is off in this build
  }
  else if (mhi_discovery_topic(row, &ctx, topic, sizeof(topic)) == 0 ||
           mhi_discovery_build(row, &ctx, payload, sizeof(payload)) == 0) {
    Serial.printf_P(PSTR("HA_DISCOVERY: row %u does not fit, not published\n"), (unsigned)row);
  }
  else {
    MQTTclient.publish(topic, payload, true);
  }
  if (next_row == MHI_DISCOVERY_ROWS) {
    if (modes_ok)
      output_P((ACStatus)type_status, PSTR(TOPIC_DISCOVERY), PSTR(PAYLOAD_DISCOVERY_OK));
    else
      output_P((ACStatus)type_status, PSTR(TOPIC_DISCOVERY), PSTR(PAYLOAD_DISCOVERY_MODES));
  }
}

#else

void discovery_setup() {}
void discovery_restart() {}
void discovery_loop() {}

#endif
