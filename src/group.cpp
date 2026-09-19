#include "group.h"

#include <Arduino.h>

#include "MHI-AC-Ctrl-core.h"
#include "discovery.h"
#include "mhi_group.h"
#include "mhi_mqtt.h"
#include "support.h"

extern MHI_AC_Ctrl_Core mhi_ac_ctrl_core;  // main.cpp

static MhiGroup group;
// Static, not on the stack: loop() runs on the ESP8266's 4 KB cont stack.
static MhiGroupActions actions;

static const char kMembers[] PROGMEM = GROUP_ROOT "members/";

void group_setup() {
  mhi_group_init(&group, HOSTNAME, outdoor_id(), MQTT_PREFIX, TELEMETRY_PERIOD);
}

void group_connected() {
  mhi_group_connect(&group, millis());
  discovery_cancel_outdoor();  // nothing of an earlier connection goes out in the grace period
}

static void publish_record() {
  char record[MHI_GROUP_RECORD_MAX + 1];
  if (mhi_group_own_record(&group, uptime_seconds(), record, sizeof(record)) == 0) {
    Serial.println(F("Group: this unit's record does not fit, not published"));
    return;
  }
  MQTTclient.publish(GROUP_ROOT "members/" HOSTNAME, record, true);
}

static void publish_state(uint8_t state) {
  char text[4];
  itoa(state, text, 10);
  output_P((ACStatus)type_status, PSTR(TOPIC_GROUP), text);
}

// A peer's <prefix><TOPIC_CONNECTED>, from loop(): subscribing inside the MQTT
// callback would overwrite the buffer the callback's topic and payload live in.
// Never unsubscribed: a peer's old topic goes at the next connect (spec §6.1).
static void subscribe_connected(const char* prefix) {
  char topic[MHI_GROUP_ROOT_MAX + sizeof(TOPIC_CONNECTED)];
  snprintf(topic, sizeof(topic), "%s%s", prefix, TOPIC_CONNECTED);
  if (!MQTTclient.subscribe(topic)) {
    // The pure layer has already marked this peer subscribed (it does not see
    // MQTT's return value), so without dropping the connection this topic is
    // never retried. Force a reconnect through support.cpp's helper, never
    // espClient directly: the next mhi_group_connect() clears the peer table,
    // so the peer is re-learned from its retained record and subscribed again.
    Serial.printf_P(PSTR("Group: subscribe %s failed, dropping the connection so it is retried\n"), topic);
    mqtt_drop_connection();
  }
}

void group_loop() {
  if (!MQTTclient.connected()) return;
  mhi_group_tick(&group, millis(), &actions);
  if (actions.subscribe[0] != '\0') subscribe_connected(actions.subscribe);
  if (actions.flags & MHI_GROUP_ACT_DEMOTE) discovery_cancel_outdoor();
  if (actions.flags & MHI_GROUP_ACT_RECORD) publish_record();
  if (actions.flags & MHI_GROUP_ACT_STATE) publish_state(actions.state);
  if (actions.flags & MHI_GROUP_ACT_START) mhi_ac_ctrl_core.reset_system_values();
  if (actions.flags & MHI_GROUP_ACT_CONFIGS) discovery_start_outdoor();
}

static bool payload_is(const uint8_t* payload, unsigned int length, const char* text) {
  return length == strlen(text) && memcmp(payload, text, length) == 0;
}

bool group_handle_message(const char* topic, const uint8_t* payload, unsigned int length) {
  if (strncmp_P(topic, kMembers, sizeof(kMembers) - 1) == 0) {
    const char* host = topic + sizeof(kMembers) - 1;
    // Its own copy: the command path's is 32 bytes, a record up to 140.
    char record[MHI_GROUP_RECORD_MAX + 1];
    mhi_copy_payload(record, sizeof(record), payload, length);
    switch (mhi_group_on_record(&group, host, record, length, millis())) {
      case MHI_GROUP_REC_OK:
        break;
      case MHI_GROUP_REC_INVALID:
        Serial.printf_P(PSTR("Group: the record of %s is not valid, ignored\n"), host);
        break;
      case MHI_GROUP_REC_FULL:
        Serial.printf_P(PSTR("Group: the table is full and no unit in it is gone, %s ignored\n"), host);
        break;
    }
    return true;
  }
  const char* host = mhi_group_host_of_connected_topic(&group, topic, TOPIC_CONNECTED);
  if (host != NULL) {
    if (payload_is(payload, length, PAYLOAD_CONNECTED_TRUE))
      mhi_group_on_connected(&group, host, true, millis());
    else if (payload_is(payload, length, PAYLOAD_CONNECTED_FALSE))
      mhi_group_on_connected(&group, host, false, millis());
    return true;
  }
  // Every subscription outside MQTT_SET_PREFIX is the group's.
  return strncmp_P(topic, PSTR(MQTT_SET_PREFIX), sizeof(MQTT_SET_PREFIX) - 1) != 0;
}

bool group_may_publish_system() {
  return mhi_group_may_publish_system(&group);
}
