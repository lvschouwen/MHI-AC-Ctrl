// The set/ command handler: see commands.h.

#include "commands.h"

#include "MHI-AC-Ctrl.h"
#include "command_config.h"
#include "group.h"
#include "mhi_mqtt.h"
#include "mhi_mqtt_retain.h"
#include "mhi_subscribe_window.h"
#include "mhi_temp.h"
#include "safe_mode.h"
#include "support.h"

// The retain peek (mhi_mqtt_retain.h) assumes a remaining length of at most two bytes.
static_assert(MQTT_MAX_PACKET_SIZE <= 16383, "mhi_retain_from_topic reads a one or two byte remaining length");

// Longest payload we ever parse is a temperature; anything longer is not a
// command we understand.
#define MQTT_PAYLOAD_MAX 32

void MQTT_subscribe_callback(const char* topic, byte* payload, unsigned int length) {
  if (group_handle_message(topic, payload, length))
    return;  // a record or a peer's connected topic: not a command (fork #22)
  // Copy out rather than terminating in place. `payload` points into
  // pubsubclient3's receive buffer, so payload[length] is _buffer[length] -
  // one byte past the end when a message fills the buffer.
  char payload_str[MQTT_PAYLOAD_MAX];
  mhi_copy_payload(payload_str, sizeof(payload_str), payload, length);
  Serial.printf_P(PSTR("MQTT_subscribe_callback, topic=%s payload=%s payload_length=%i\n"), topic, payload_str, length);
  // Every set/* message extends the window while it is open (fork #50).
  const bool in_subscribe_window = mhi_subscribe_window_hit(&subscribe_window, millis(), kSubscribeQuietMs, kSubscribeCapMs);

  // The broker's RETAIN flag, read from the packet header PubSubClient3 leaves before the topic.
  const bool retained = mhi_retain_from_topic(topic) == MHI_RETAIN_RETAINED;
  MhiCmd cmd;
  mhi_cmd_parse(&cmd_config, topic, payload_str, retained, in_subscribe_window, &cmd);
  if (cmd.kind == MHI_CMD_DROPPED) {
    // Fork #67: a retained set/* message is a replay of an old command (the
    // broker sends it after every subscribe), not what the user wants now;
    // a retained set/reset would reboot the unit in a loop. Serial log only, no
    // reply. A reset is also dropped inside the subscribe window (fork #50).
    Serial.printf_P(PSTR("set/* ignored (%s): %s\n"), retained ? "retained" : "reset right after subscribe", topic);
    return;
  }
  if (cmd.kind == MHI_CMD_UNKNOWN) {
    publish_cmd_unknown();
    return;
  }
  if (cmd.error) {
    publish_cmd_invalidparameter();
    return;
  }

  switch (cmd.kind) {
    case MHI_CMD_DROPPED:  // handled above
    case MHI_CMD_UNKNOWN:
      break;
    case MHI_CMD_POWER:
      mhi_ac_ctrl_core.set_power(cmd.value ? power_on : power_off);
      publish_cmd_ok();
      break;
    case MHI_CMD_MODE:
      if (cmd.value == MHI_CMD_MODE_OFF) {  // only in POWERON_WHEN_CHANGING_MODE builds
        mhi_ac_ctrl_core.set_power(power_off);
      }
      else {
        set_mode_checked(kCmdModes[cmd.value]);
#ifdef POWERON_WHEN_CHANGING_MODE
        mhi_ac_ctrl_core.set_power(power_on);
#endif
      }
      publish_cmd_ok();
      break;
    case MHI_CMD_TSETPOINT: {
      const float f = cmd.fvalue;
      uint8_t db2;
      if (!mhi_setpoint_guard_request(&setpoint_guard, f, &db2)) {  // 10-30 in heat, 18-30 otherwise (fork #25 F3)
        publish_cmd_invalidparameter();
        break;
      }
      // Below 18 in heat: DB2 18 and a shifted room temperature (fork #30).
      if (mhi_heat_shift_request(&heat_shift, f, setpoint_guard.mode == mode_heat, &db2))
        heat_shift_ended(db2 == heat_shift.bus_db2);
      else if (mhi_heat_shift_active(&heat_shift)) {
        if (troom_was_set_by_MQTT) apply_external_troom();
        ds18x20_reapply();
        if (heat_shift.bus_db2 == MHI_HEAT_SHIFT_DB2)  // no echo will come: DB2 is already 18
          publish_tsetpoint(heat_shift.target);
      }
      mhi_ac_ctrl_core.set_tsetpoint(db2);
      publish_cmd_ok();
      break;
    }
    case MHI_CMD_FAN: {
      static const byte kCoreFan[4] = {0, 1, 2, 6};  // the core's set_fan() values for levels 1..4
      mhi_ac_ctrl_core.set_fan(cmd.value == MHI_FAN_AUTO ? 7 : kCoreFan[cmd.value - 1]);
      publish_cmd_ok();
      break;
    }
    case MHI_CMD_VANES:
      mhi_ac_ctrl_core.set_vanes(cmd.value);
      publish_cmd_ok();
      break;
    case MHI_CMD_VANESLR:
      mhi_ac_ctrl_core.set_vanesLR(cmd.value);
      publish_cmd_ok();
      break;
    case MHI_CMD_3DAUTO:
      mhi_ac_ctrl_core.set_3Dauto(cmd.value ? Dauto_on : Dauto_off);
      publish_cmd_ok();
      break;
    case MHI_CMD_TROOM: {
      float f = cmd.fvalue;
#ifdef ENHANCED_RESOLUTION
      f = f + mhi_ac_ctrl_core.get_troom_offset() ;  // increase Troom with current offset to compensate higher setpoint
#endif
      if (!mhi_troom_celsius_plausible(f)) {
        publish_cmd_invalidparameter();
        break;
      }
      room_temp_set_timeout_Millis = millis();  // reset timeout
      troom_was_set_by_MQTT=true;
      troom_external_celsius = f;
      apply_external_troom();  // shifted while a heat target below 18 is active (fork #30)
      publish_cmd_ok();
      break;
    }
    case MHI_CMD_ERROPDATA:
      mhi_ac_ctrl_core.request_ErrOpData();
      publish_cmd_ok();
      break;
    case MHI_CMD_RESET:
      publish_cmd_ok();
      delay(500);
      if (cmd.value == MHI_CMD_RESET_RESTART)
        ESP.restart();
#ifdef RESET_CRASH_COMMAND
      // Test builds only (fork #25): an MQTT-reachable crash, and a retained one
      // would cycle the unit through safe mode. The safe-mode proof (fork #23
      // spec §1.5): three of these, each within 120 s of the boot before, start
      // safe mode. Every crash is one we send.
      else
        safe_mode_test_crash();
#endif
      break;
    case MHI_CMD_PASSIVE:
      mhi_ac_ctrl_core.set_passive_mode(cmd.value != 0);
      publish_cmd_ok();
      break;
    case MHI_CMD_DIAG:
      diag_on = cmd.value != 0;
      if (diag_on) {
        diag_frame.have_last = false;   // the next diag/frame is a whole frame, as after a connect
        mhi_retry_reset(&diag_pacer);
      }
      publish_diag_state();
      publish_cmd_ok();
      break;
    case MHI_CMD_OPDATA:
      mhi_ac_ctrl_core.request_OpData(cmd.a, cmd.b);
      publish_cmd_ok();
      break;
    case MHI_CMD_SILENT:
      mhi_ac_ctrl_core.set_silent(cmd.value != 0);
      publish_cmd_ok();
      break;
  }
}
