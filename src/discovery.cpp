#include "discovery.h"

#include <Arduino.h>

#include "MHI-AC-Ctrl-core.h"
#include "MHI-AC-Ctrl.h"
#include "mhi_diag.h"
#include "mhi_discovery.h"
#include "support.h"

#ifdef HA_DISCOVERY

// The ~/set/... topics assume the set prefix sits under the status prefix,
// as the defaults do (MQTT_SET_PREFIX MQTT_PREFIX "set/").
constexpr bool starts_with(const char* s, const char* prefix) {
  return *prefix == '\0' || (*s == *prefix && starts_with(s + 1, prefix + 1));
}
static_assert(starts_with(MQTT_SET_PREFIX, MQTT_PREFIX), "HA_DISCOVERY needs MQTT_SET_PREFIX to start with MQTT_PREFIX");
static_assert(sizeof(MQTT_PREFIX) > 1, "HA_DISCOVERY needs a non-empty MQTT_PREFIX");

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
            HA_NAME_RSSI, HA_NAME_RESET_REASON, HA_NAME_WIFI_PHY},
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
