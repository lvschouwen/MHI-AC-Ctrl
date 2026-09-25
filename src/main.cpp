// MHI-AC-Ctrl by absalom-muc
// read + write data via SPI controlled by MQTT
// VERSION (the build's git commit hash) comes from the generated build_version.h
//
// Was MHI-AC-Ctrl.ino. As a .cpp there is no Arduino preprocessor generating
// includes and forward declarations, so they are spelled out.

#include <Arduino.h>

#include "MHI-AC-Ctrl-core.h"
#include "MHI-AC-Ctrl.h"
#include "discovery.h"
#include "group.h"
#include "mhi_action.h"
#include "mhi_diag.h"
#include "mhi_diag_frame.h"
#include "mhi_fan.h"
#include "mhi_link.h"
#include "mhi_mqtt.h"
#include "mhi_safe_mode.h"
#include "mhi_status.h"
#include "mhi_temp.h"
#include "mhi_troom_filter.h"
#include "mhi_heat_shift.h"
#include "mhi_cleaning.h"
#include "mhi_setpoint.h"
#include "mhi_vanes.h"
#include "mhi_vanes_lr.h"
#include "safe_mode.h"
#include "support.h"

MHI_AC_Ctrl_Core mhi_ac_ctrl_core;

POWER_STATUS power_status = unknown;

// Fork #23: decided first thing in setup(). In safe mode loop() runs only
// Wi-Fi and OTA, and restarts after 10 minutes.
static bool safe_mode = false;

unsigned long room_temp_set_timeout_Millis = millis();
bool troom_was_set_by_MQTT = false;
bool troom_was_set_by_DS18X20 = false;

// Reset on every MQTT (re)connect, so Troom is re-sent like every other status.
MhiTroomFilter troom_filter = {0, false};

// Fork #25 F3: the mode and setpoint the limits go by, see mhi_setpoint.h.
static MhiSetpointGuard setpoint_guard = {MHI_SETPOINT_MODE_UNKNOWN, MHI_SETPOINT_UNKNOWN};
// Fork #30: a heat target below 18 °C, reached by a shifted room temperature
// (mhi_heat_shift.h), and the room sensor's last value as set/Troom sent it.
static MhiHeatShift heat_shift = {NAN, false, MHI_HEAT_SHIFT_DB2_UNKNOWN};
static float troom_external_celsius = NAN;
static uint8_t troom_external_byte_published = 0;  // the room value last published on Troom while shifting; 0: none
#ifdef ROOM_TEMP_DS18X20
static byte ds18x20_value_old = 0;  // file scope: a heat shift's start and end re-apply the DS18x20 (fork #30)
#endif

// Fork #25: Allergen Clear (see mhi_cleaning.h), and what TroomExternal last
// carried; 0xff re-sends it, as after an MQTT (re)connect.
static MhiCleaning cleaning = {MHI_CLEANING_UNKNOWN, MHI_CLEANING_UNKNOWN, MHI_CLEANING_UNKNOWN, MHI_CLEANING_UNKNOWN};
static uint8_t troom_external_published = 0xff;

// Protocol discovery tooling (#4 batch A). diag/frame compares each valid
// frame with the last *published* one, at most once a second, while Diag is
// on; a reconnect starts with a whole frame ("first | ...").
static MhiDiagFrame diag_frame = {{0}, false};
static uint8_t diag_mask[MHI_DIAG_FRAME_MAX];
static MhiRetryPacer diag_pacer = {0, false};
static bool diag_on = DIAG_DEFAULT;

// The texts on the Vanes topic; set/Vanes accepts these and 1..5 (fork #4 batch B).
static const MhiVanesNames vanes_names = {{PAYLOAD_VANES_1, PAYLOAD_VANES_2, PAYLOAD_VANES_3, PAYLOAD_VANES_4},
                                          PAYLOAD_VANES_SWING, PAYLOAD_VANES_UNKNOWN};
static_assert(MHI_VANES_SWING == vanes_swing && MHI_VANES_UNKNOWN == vanes_unknown, "mhi_vanes numbers the positions as the core's ACVanes does");

#ifdef USE_EXTENDED_FRAME_SIZE
// The texts on the VanesLR topic; set/VanesLR accepts these and 1..8 (fork #20).
// Only the 33-byte frame reports or accepts them, so a 20-byte build does not
// carry the table at all.
static const MhiVanesLrNames vanes_lr_names = {
  {PAYLOAD_VANESLR_1, PAYLOAD_VANESLR_2, PAYLOAD_VANESLR_3, PAYLOAD_VANESLR_4, PAYLOAD_VANESLR_5, PAYLOAD_VANESLR_6,
   PAYLOAD_VANESLR_7},
  PAYLOAD_VANESLR_SWING};
static_assert(MHI_VANES_LR_SWING == vanesLR_swing, "mhi_vanes_lr numbers swing as the core's ACVanesLR does");
#endif

// The texts on the Fan topic; set/Fan accepts these and 1..4 (fork #21 F6).
static const MhiFanNames fan_names = {{PAYLOAD_FAN_1, PAYLOAD_FAN_2, PAYLOAD_FAN_3, PAYLOAD_FAN_4}, PAYLOAD_FAN_AUTO};

static void publish_cleaning() {
  output_P((ACStatus)type_status, PSTR(TOPIC_CLEANING),
           cleaning.state == 1 ? PSTR(PAYLOAD_CLEANING_ON) : PSTR(PAYLOAD_CLEANING_OFF));
}

static void publish_troom_external() {
  const uint8_t state = troom_was_set_by_MQTT ? 1 : 0;
  if (state == troom_external_published) return;
  troom_external_published = state;
  output_P((ACStatus)type_status, PSTR(TOPIC_TROOM_EXTERNAL),
           state ? PSTR(PAYLOAD_TROOM_EXTERNAL_ON) : PSTR(PAYLOAD_TROOM_EXTERNAL_OFF));
}

static void publish_tsetpoint(float celsius) {
  char text[8];
  dtostrf(celsius, 0, 1, text);
  output_P((ACStatus)type_status, PSTR(TOPIC_TSETPOINT), text);
}

// Sends the unit a room sensor's value (set/Troom or the DS18x20): shifted
// while a heat target below 18 is active (fork #30), and then held inside the
// plausible window, so a hot room with a low target still reads hot. While
// shifting, Troom carries the room itself, since the unit echoes the shifted value.
static void send_room_temperature(float room_celsius) {
  float sent = mhi_heat_shift_troom(&heat_shift, room_celsius, HEAT_SHIFT_OFFSET);
  if (sent > 47.75f) sent = 47.75f;  // mhi_troom_celsius_plausible(): below 48
  mhi_ac_ctrl_core.set_troom(mhi_troom_round_from_celsius(sent));
  if (mhi_heat_shift_active(&heat_shift)) {
    const uint8_t room = mhi_troom_round_from_celsius(room_celsius);
    if (room != troom_external_byte_published) {
      troom_external_byte_published = room;
      char text[10];
      dtostrf(mhi_celsius_from_troom(room), 0, 2, text);
      output_P((ACStatus)type_status, PSTR(TOPIC_TROOM), text);
    }
  }
}

static void apply_external_troom() {
  send_room_temperature(troom_external_celsius);
}

// The DS18x20's value is sent again at its next reading, shifted or not.
static void ds18x20_reapply() {
#ifdef ROOM_TEMP_DS18X20
  ds18x20_value_old = 0;
#endif
}

// A shift ended (fork #30): the room sensor's value goes too, so no unshifted
// external value lingers; the unit is back on its own sensor at once and Home
// Assistant sends it again only when its gate opens. With publish_setpoint the
// setpoint goes out now, since DB2 stays 18 and the bus reports no change; a
// set/Tsetpoint that writes another DB2 leaves that to the bus's echo.
static void heat_shift_ended(bool publish_setpoint = true) {
  if (troom_was_set_by_MQTT) {
    mhi_ac_ctrl_core.set_troom(0xff);
    troom_was_set_by_MQTT = false;
  }
  troom_external_byte_published = 0;
  ds18x20_reapply();  // a DS18x20 stays the room sensor, unshifted from its next reading
  mhi_troom_filter_reset(&troom_filter);  // the unit's own Troom goes out again at its next report
  if (publish_setpoint && heat_shift.bus_db2 != MHI_HEAT_SHIFT_DB2_UNKNOWN)
    publish_tsetpoint(mhi_heat_shift_setpoint(&heat_shift, heat_shift.bus_db2));
}

// set/Mode: the limits of set/Tsetpoint follow the commanded mode at once, and
// leaving heat with a setpoint below 18 writes 18 in the same frame (fork #25 F3).
static void set_mode_checked(ACMode mode) {
  mhi_ac_ctrl_core.set_mode(mode);
  const uint8_t setpoint = mhi_setpoint_guard_mode(&setpoint_guard, mode);
  if (setpoint != 0)
    mhi_ac_ctrl_core.set_tsetpoint(setpoint);
  if (mhi_heat_shift_on_mode(&heat_shift, mode == mode_heat))
    heat_shift_ended();
}

static void publish_diag_state() {
  if (diag_on)
    output_P((ACStatus)type_status, PSTR(TOPIC_DIAG), PSTR(PAYLOAD_DIAG_ON));
  else
    output_P((ACStatus)type_status, PSTR(TOPIC_DIAG), PSTR(PAYLOAD_DIAG_OFF));
}

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
#ifndef POWERON_WHEN_CHANGING_MODE
  if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_POWER)) == 0) {
    if (strcmp_P(payload_str, PSTR(PAYLOAD_POWER_ON)) == 0) {
      mhi_ac_ctrl_core.set_power(power_on);
      publish_cmd_ok();
    }
    else if (strcmp_P(payload_str, PSTR(PAYLOAD_POWER_OFF)) == 0) {
      mhi_ac_ctrl_core.set_power(power_off);
      publish_cmd_ok();
    }
    else
      publish_cmd_invalidparameter();
  }
  else 
#endif
  if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_MODE)) == 0) {
#ifdef POWERON_WHEN_CHANGING_MODE
    // The same text the Mode topic publishes for an AC that is off.
    if (strcmp_P(payload_str, PSTR(PAYLOAD_MODE_OFF)) == 0) {
      mhi_ac_ctrl_core.set_power(power_off);
      publish_cmd_ok();
    } else
#endif
      if (strcmp_P(payload_str, PSTR(PAYLOAD_MODE_AUTO)) == 0) {
        set_mode_checked(mode_auto);
#ifdef POWERON_WHEN_CHANGING_MODE
        mhi_ac_ctrl_core.set_power(power_on);
#endif
        publish_cmd_ok();
      }
      else if (strcmp_P(payload_str, PSTR(PAYLOAD_MODE_DRY)) == 0) {
        set_mode_checked(mode_dry);
#ifdef POWERON_WHEN_CHANGING_MODE
        mhi_ac_ctrl_core.set_power(power_on);
#endif
        publish_cmd_ok();
      }
      else if (strcmp_P(payload_str, PSTR(PAYLOAD_MODE_COOL)) == 0) {
        set_mode_checked(mode_cool);
#ifdef POWERON_WHEN_CHANGING_MODE
        mhi_ac_ctrl_core.set_power(power_on);
#endif
        publish_cmd_ok();
      }
      else if (strcmp_P(payload_str, PSTR(PAYLOAD_MODE_FAN)) == 0) {
        set_mode_checked(mode_fan);
#ifdef POWERON_WHEN_CHANGING_MODE
        mhi_ac_ctrl_core.set_power(power_on);
#endif
        publish_cmd_ok();
      }
      else if (strcmp_P(payload_str, PSTR(PAYLOAD_MODE_HEAT)) == 0) {
        set_mode_checked(mode_heat);
#ifdef POWERON_WHEN_CHANGING_MODE
        mhi_ac_ctrl_core.set_power(power_on);
#endif
        publish_cmd_ok();
      }
      else
        publish_cmd_invalidparameter();
  }
  else if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_TSETPOINT)) == 0) {
    float f;
    uint8_t db2;
    if (mhi_parse_celsius(payload_str, &f) && mhi_setpoint_guard_request(&setpoint_guard, f, &db2)) {  // 10-30 in heat, 18-30 otherwise (fork #25 F3)
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
    }
    else
      publish_cmd_invalidparameter();
  }
  else if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_FAN)) == 0) {
    static const byte kCoreFan[4] = {0, 1, 2, 6};  // the core's set_fan() values for levels 1..4
    const int fanlevel = mhi_fan_parse(&fan_names, payload_str);
    if (fanlevel == MHI_FAN_AUTO) {
      mhi_ac_ctrl_core.set_fan(7);
      publish_cmd_ok();
    }
    else if (fanlevel >= 1 && fanlevel <= 4) {
      mhi_ac_ctrl_core.set_fan(kCoreFan[fanlevel - 1]);
      publish_cmd_ok();
    }
    else
      publish_cmd_invalidparameter();
  }
  else if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_VANES)) == 0) {
    const int vanes = mhi_vanes_parse(&vanes_names, payload_str);
    if (vanes != MHI_VANES_UNKNOWN) {
      mhi_ac_ctrl_core.set_vanes(vanes);
      publish_cmd_ok();
    }
    else
      publish_cmd_invalidparameter();
  }
#ifdef USE_EXTENDED_FRAME_SIZE  
  else if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_VANESLR)) == 0) {
    const int vaneslr = mhi_vanes_lr_parse(&vanes_lr_names, payload_str);
    if (vaneslr != MHI_VANES_LR_UNKNOWN) {
      mhi_ac_ctrl_core.set_vanesLR(vaneslr);
      publish_cmd_ok();
    }
    else
      publish_cmd_invalidparameter();
  }
  else if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_3DAUTO)) == 0) {
    if (strcmp_P(payload_str, PSTR(PAYLOAD_3DAUTO_ON)) == 0) {
      mhi_ac_ctrl_core.set_3Dauto(Dauto_on);
      publish_cmd_ok();
    }
    else if (strcmp_P(payload_str, PSTR(PAYLOAD_3DAUTO_OFF)) == 0) {
      mhi_ac_ctrl_core.set_3Dauto(Dauto_off);
      publish_cmd_ok();
    }
    else
      publish_cmd_invalidparameter();
  }
#endif
  else if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_TROOM)) == 0) {
    float f = 0.0f;
    const bool parsed = mhi_parse_celsius(payload_str, &f);  // junk is refused, not read as 0 °C (sweep #33 F1)
#ifdef ENHANCED_RESOLUTION
    f = f + mhi_ac_ctrl_core.get_troom_offset() ;  // increase Troom with current offset to compensate higher setpoint
#endif
    if (parsed && mhi_troom_celsius_plausible(f)) {
      room_temp_set_timeout_Millis = millis();  // reset timeout
      troom_was_set_by_MQTT=true;
      troom_external_celsius = f;
      apply_external_troom();  // shifted while a heat target below 18 is active (fork #30)
      publish_cmd_ok();
    }
    else
      publish_cmd_invalidparameter();
  }
  else if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_REQUEST_ERROPDATA)) == 0) {
    mhi_ac_ctrl_core.request_ErrOpData();
    publish_cmd_ok();
  }
  else if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_REQUEST_RESET)) == 0) {
    if (strcmp_P(payload_str, PSTR(PAYLOAD_REQUEST_RESET)) == 0) {
      publish_cmd_ok();
      delay(500);
      ESP.restart();
    }
#ifdef RESET_CRASH_COMMAND
    // Test builds only (fork #25): an MQTT-reachable crash, and a retained one
    // would cycle the unit through safe mode.
    else if (strcmp_P(payload_str, PSTR(PAYLOAD_REQUEST_RESET_CRASH)) == 0) {
      // The safe-mode proof (fork #23 spec §1.5): three of these, each within
      // 120 s of the boot before, start safe mode. Every crash is one we send.
      publish_cmd_ok();
      delay(500);
      safe_mode_test_crash();
    }
#endif
    else
      publish_cmd_invalidparameter();
  }
  else if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_REQUEST_PASSIVEMODE)) == 0) {
    if (strcmp_P(payload_str, PSTR(PAYLOAD_REQUEST_PASSIVEMODE_ON)) == 0) {
      mhi_ac_ctrl_core.set_passive_mode(true);
      publish_cmd_ok();
    }
    else if (strcmp_P(payload_str, PSTR(PAYLOAD_REQUEST_PASSIVEMODE_OFF)) == 0) {
      mhi_ac_ctrl_core.set_passive_mode(false);
      publish_cmd_ok();
    }
    else
      publish_cmd_invalidparameter();
  }
  else if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_REQUEST_DIAG)) == 0) {
    if (strcmp_P(payload_str, PSTR(PAYLOAD_DIAG_ON)) == 0) {
      diag_on = true;
      diag_frame.have_last = false;   // the next diag/frame is a whole frame, as after a connect
      mhi_retry_reset(&diag_pacer);
      publish_diag_state();
      publish_cmd_ok();
    }
    else if (strcmp_P(payload_str, PSTR(PAYLOAD_DIAG_OFF)) == 0) {
      diag_on = false;
      publish_diag_state();
      publish_cmd_ok();
    }
    else
      publish_cmd_invalidparameter();
  }
  else if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_REQUEST_OPDATA)) == 0) {
    uint8_t prefix, code;
    if (mhi_opdata_request_parse(payload_str, &prefix, &code)) {
      mhi_ac_ctrl_core.request_OpData(prefix, code);
      publish_cmd_ok();
    }
    else
      publish_cmd_invalidparameter();
  }
  else if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_SILENT)) == 0) {
    if (strcmp_P(payload_str, PSTR(PAYLOAD_SILENT_ON)) == 0) {
      mhi_ac_ctrl_core.set_silent(true);
      publish_cmd_ok();
    }
    else if (strcmp_P(payload_str, PSTR(PAYLOAD_SILENT_OFF)) == 0) {
      mhi_ac_ctrl_core.set_silent(false);
      publish_cmd_ok();
    }
    else
      publish_cmd_invalidparameter();
  }
  else
    publish_cmd_unknown();
}

class StatusHandler : public CallbackInterface_Status {
  public:
    void cbiStatusFunction(ACStatus status, int value) {
      char strtmp[10];
#ifdef POWERON_WHEN_CHANGING_MODE
      static MhiModeTopic mode_topic = {MHI_STATUS_UNKNOWN, MHI_STATUS_UNKNOWN};
#endif
#ifdef ENHANCED_RESOLUTION      
      float offset = mhi_ac_ctrl_core.get_troom_offset();
      float tmp_value;
#endif
      //Serial.printf_P(PSTR("status=%i value=%i\n"), status, value);
      switch (status) {
        case raw_frame:      // never reaches cbiStatusFunction; delivered via cbiRawFunction (fork #4)
        case raw_opdata:
          break;
        case status_power:
          // After powerdown AC (230V), fan status is only showing 1, 2 or 3. 4 and Auto is not shown when changing with RC.
          // Only when setting fan to Auto one time after powerdown AC, it will show 4 and Auto.
          // Below will take care of this.
          if (power_status == unknown) {  // First time after startup esp
            Serial.printf_P(PSTR("power_status: unknown; received status_power: %i\n"), value);
            if (value == power_off) {  // Only when status is power off, set fan to Auto. 
              Serial.println(F("Set fan to Auto to fix fan status after powerdown (230V) AC"));
              mhi_ac_ctrl_core. set_fan(7);
            }
          } else if (power_status == off) 
            Serial.printf_P(PSTR("power_status: off; received status_power: %i\n"), value);
          else if (power_status == on) 
            Serial.printf_P(PSTR("power_status: on; received status_power: %i\n"), value);

          if (mhi_cleaning_on_power(&cleaning, value == power_on ? 1 : 0))
            publish_cleaning();
          if (value == power_on){
            output_P(status, (TOPIC_POWER), PSTR(PAYLOAD_POWER_ON));
            power_status = on;
          }
          else {
            output_P(status, (TOPIC_POWER), (PAYLOAD_POWER_OFF));
            power_status = off;
          }
#ifdef POWERON_WHEN_CHANGING_MODE
          // Mode stands in for power in this build, see mhi_status.h.
          if (mhi_mode_topic_on_power(&mode_topic, value) == MHI_MODE_TOPIC_OFF)
            output_P(status, PSTR(TOPIC_MODE), PSTR(PAYLOAD_MODE_OFF));
          else
            cbiStatusFunction(status_mode, mode_topic.mode);
#endif
          break;
        case status_mode:
          mhi_setpoint_guard_on_bus_mode(&setpoint_guard, value);
          if (mhi_heat_shift_on_mode(&heat_shift, value == mode_heat))  // fork #30
            heat_shift_ended();
          if (mhi_cleaning_on_mode(&cleaning, value))
            publish_cleaning();
#ifdef POWERON_WHEN_CHANGING_MODE
          if (!mhi_mode_topic_on_mode(&mode_topic, value))
            break;  // held until the unit is on
#endif
        case opdata_mode:
        case erropdata_mode:
          switch (value) {
            case mode_auto:
              if (status != erropdata_mode)
                output_P(status, PSTR(TOPIC_MODE), PSTR(PAYLOAD_MODE_AUTO));
              else
                output_P(status, PSTR(TOPIC_MODE), PSTR(PAYLOAD_MODE_STOP));
              break;
            case mode_dry:
              output_P(status, PSTR(TOPIC_MODE), PSTR(PAYLOAD_MODE_DRY));
              break;
            case mode_cool:
              output_P(status, PSTR(TOPIC_MODE), PSTR(PAYLOAD_MODE_COOL));
              break;
            case mode_fan:
              output_P(status, PSTR(TOPIC_MODE), PSTR(PAYLOAD_MODE_FAN));
              break;
            case mode_heat:
              output_P(status, PSTR(TOPIC_MODE), PSTR(PAYLOAD_MODE_HEAT));
              break;
          }
          break;
        case opdata_kwh:
          dtostrf(highByte(value)*64.0f + lowByte(value)*0.25f, 0, 2, strtmp);
          output_P(status, PSTR(TOPIC_KWH), strtmp);
          break;
        case opdata_unknown:
          itoa(value, strtmp, 10);
          output_P(status, PSTR(TOPIC_UNKNOWN), strtmp);
          break;
        case status_fan: {
          int fanlevel = MHI_FAN_NONE;
          switch (value) {
            case 0: fanlevel = 1; break;
            case 1: fanlevel = 2; break;
            case 2: fanlevel = 3; break;
            case 6: fanlevel = 4; break;
            case 7: fanlevel = MHI_FAN_AUTO; break;
          }
          if (fanlevel != MHI_FAN_NONE)
            output_P(status, TOPIC_FAN, mhi_fan_text(&fan_names, fanlevel));
          else { // invalid values
            itoa(value, strtmp, 10);
            strcat(strtmp, "?");
            output_P(status, TOPIC_FAN, strtmp);
          }
          break;
        }
        case status_vanes:
          output_P(status, PSTR(TOPIC_VANES), mhi_vanes_text(&vanes_names, value));
          break;
        // The case labels stay outside the #ifdef so the switch remains
        // exhaustive over ACStatus and -Wswitch keeps catching real omissions.
        // With the 20-byte frame the AC never reports these.
        case status_vanesLR:
#ifdef USE_EXTENDED_FRAME_SIZE
          {
            // NULL outside 1..8, which the core's decode cannot produce today;
            // output_P would hand publish_P a NULL payload, so check anyway.
            const char* vaneslr_text = mhi_vanes_lr_text(&vanes_lr_names, value);
            if (vaneslr_text != NULL)
              output_P(status, PSTR(TOPIC_VANESLR), vaneslr_text);
          }
#endif
          break;
        case status_3Dauto:
#ifdef USE_EXTENDED_FRAME_SIZE
          switch (value) {
            case Dauto_on:
              output_P(status, PSTR(TOPIC_3DAUTO), PSTR(PAYLOAD_3DAUTO_ON));
              break;
            case Dauto_off:
              output_P(status, PSTR(TOPIC_3DAUTO), PSTR(PAYLOAD_3DAUTO_OFF));
              break;
          }
#endif
          break;
        case status_troom:
          // A room sensor's value goes out on every change: the filter is for
          // the AC's own jittery sensor (fork #25 C3, upstream #221).
          // While shifting, the unit echoes the shifted value: apply_external_troom()
          // publishes the room itself (fork #30).
          if ((troom_was_set_by_MQTT || troom_was_set_by_DS18X20) && mhi_heat_shift_active(&heat_shift))
            break;
          if (mhi_troom_filter_pass(&troom_filter, (uint8_t)value,
                                    troom_was_set_by_MQTT || troom_was_set_by_DS18X20 ? 0.0f : TROOM_FILTER_LIMIT)) {
            dtostrf(mhi_celsius_from_troom(value), 0, 2, strtmp);
            output_P(status, PSTR(TOPIC_TROOM), strtmp);
          }
          break;
        case status_tsetpoint:
          mhi_setpoint_guard_on_bus_db2(&setpoint_guard, value);
#ifdef ENHANCED_RESOLUTION
          tmp_value = (value & 0x7f)/ 2.0;
          offset = round(tmp_value) - tmp_value;  // Calculate offset when setpoint is changed
          Serial.printf_P(PSTR("status_tsetpoint: Set Troom offset: %f\n"), offset);
          mhi_ac_ctrl_core.set_troom_offset(offset);
#endif
          // The IR remote moved DB2 off 18: the shift ends (fork #30). While
          // shifting with DB2 at 18, the published setpoint is the target.
          if (mhi_heat_shift_on_bus_db2(&heat_shift, value))
            heat_shift_ended();  // publishes the new setpoint
          else
            publish_tsetpoint(mhi_heat_shift_setpoint(&heat_shift, value));
          break;
        case opdata_tsetpoint:
        case erropdata_tsetpoint:
          dtostrf((value & 0x7f)/ 2.0, 0, 1, strtmp);
          output_P(status, PSTR(TOPIC_TSETPOINT), strtmp);
          break;
        case status_errorcode:
        case erropdata_errorcode:
          itoa(value, strtmp, 10);
          output_P(status, PSTR(TOPIC_ERRORCODE), strtmp);
          break;
        case status_action:
          switch (value) {
            case MHI_ACTION_OFF:
              output_P(status, PSTR(TOPIC_ACTION), PSTR(PAYLOAD_ACTION_OFF));
              break;
            case MHI_ACTION_IDLE:
              output_P(status, PSTR(TOPIC_ACTION), PSTR(PAYLOAD_ACTION_IDLE));
              break;
            case MHI_ACTION_COOLING:
              output_P(status, PSTR(TOPIC_ACTION), PSTR(PAYLOAD_ACTION_COOLING));
              break;
            case MHI_ACTION_HEATING:
              output_P(status, PSTR(TOPIC_ACTION), PSTR(PAYLOAD_ACTION_HEATING));
              break;
            case MHI_ACTION_DRYING:
              output_P(status, PSTR(TOPIC_ACTION), PSTR(PAYLOAD_ACTION_DRYING));
              break;
            case MHI_ACTION_FAN:
              output_P(status, PSTR(TOPIC_ACTION), PSTR(PAYLOAD_ACTION_FAN));
              break;
          }
          break;
        case status_silent:
          if (value)
            output_P(status, PSTR(TOPIC_SILENT), PSTR(PAYLOAD_SILENT_ON));
          else
            output_P(status, PSTR(TOPIC_SILENT), PSTR(PAYLOAD_SILENT_OFF));
          break;
        case opdata_return_air:
        case erropdata_return_air:
          dtostrf(mhi_celsius_from_troom(value), 0, 2, strtmp);
          output_P(status, PSTR(TOPIC_RETURNAIR), strtmp);
          break;
        case opdata_thi_r1:
        case erropdata_thi_r1:
          itoa(0.327f * value - 11.4f, strtmp, 10); // only rough approximation
          output_P(status, PSTR(TOPIC_THI_R1), strtmp);
          break;
        case opdata_thi_r2:
        case erropdata_thi_r2:
          itoa(0.327f * value - 11.4f, strtmp, 10); // formula for calculation not known
          output_P(status, PSTR(TOPIC_THI_R2), strtmp);
          break;
        case opdata_thi_r3:
        case erropdata_thi_r3:
          itoa(0.327f * value - 11.4f, strtmp, 10); // only rough approximation
          output_P(status, PSTR(TOPIC_THI_R3), strtmp);
          break;
        case opdata_iu_fanspeed:
        case erropdata_iu_fanspeed:
          itoa(value, strtmp, 10);
          output_P(status, PSTR(TOPIC_IU_FANSPEED), strtmp);
          break;
        case opdata_total_iu_run:
        case erropdata_total_iu_run:
          itoa(value * 100, strtmp, 10);
          output_P(status, PSTR(TOPIC_TOTAL_IU_RUN), strtmp);
          break;
        case erropdata_outdoor:
        case opdata_outdoor:
          dtostrf((value - 94) * 0.25f, 0, 2, strtmp);
          output_P(status, PSTR(TOPIC_OUTDOOR), strtmp);
          break;
        case opdata_tho_r1:
        case erropdata_tho_r1:
          itoa(0.327f * value - 11.4f, strtmp, 10); // formula for calculation not known
          output_P(status, PSTR(TOPIC_THO_R1), strtmp);
          break;
        case opdata_comp:
        case erropdata_comp:
          dtostrf(highByte(value) * 25.6f + 0.1f * lowByte(value), 0, 2, strtmp);  // to be confirmed
          output_P(status, PSTR(TOPIC_COMP), strtmp);
          break;
        case erropdata_td:
        case opdata_td:
          if (value < 0x12)
            strcpy(strtmp, "<=30");
          else
            itoa(value / 2 + 32, strtmp, 10);
          output_P(status, PSTR(TOPIC_TD), strtmp);
          break;
        case opdata_ct:
        case erropdata_ct:
          dtostrf(value * 14 / 51.0f, 0, 2, strtmp);
          output_P(status, PSTR(TOPIC_CT), strtmp);
          break;
        case opdata_tdsh:
          itoa(value, strtmp, 10); // formula for calculation not known
          output_P(status, PSTR(TOPIC_TDSH), strtmp);
          break;
        case opdata_protection_no:
          itoa(value, strtmp, 10);
          output_P(status, PSTR(TOPIC_PROTECTION_NO), strtmp);
          break;
        case opdata_ou_fanspeed:
        case erropdata_ou_fanspeed:
          itoa(value, strtmp, 10);
          output_P(status, PSTR(TOPIC_OU_FANSPEED), strtmp);
          break;
        case opdata_defrost:
          if (value)
            output_P(status, PSTR(TOPIC_DEFROST), PSTR(PAYLOAD_OP_DEFROST_ON));
          else
            output_P(status, PSTR(TOPIC_DEFROST), PSTR(PAYLOAD_OP_DEFROST_OFF));
          break;
        case opdata_total_comp_run:
        case erropdata_total_comp_run:
          itoa(value * 100, strtmp, 10);
          output_P(status, PSTR(TOPIC_TOTAL_COMP_RUN), strtmp);
          break;
        case opdata_ou_eev1:
        case erropdata_ou_eev1:
          itoa(value, strtmp, 10);
          output_P(status, PSTR(TOPIC_OU_EEV1), strtmp);
          break;
      }
    }

    void cbiRawFunction(ACStatus status, const uint8_t* bytes, size_t len) override {
      char text[MHI_DIAG_TEXT_MAX];
      if (status == raw_opdata) {
        // An event, not state: not retained, published every time it is seen.
        if (mhi_diag_opdata_text(bytes, text, sizeof(text)) > 0)
          MQTTclient.publish(MQTT_PREFIX TOPIC_DIAG_OPDATA, text, false);
      }
      else if (status == raw_frame && diag_on && mhi_retry_due(&diag_pacer, millis(), 1000)) {
        if (mhi_diag_frame_changes(&diag_frame, bytes, len, diag_mask, text, sizeof(text)) > 0)
          MQTTclient.publish(MQTT_PREFIX TOPIC_DIAG_FRAME, text, false);
      }
    }
};
StatusHandler mhiStatusHandler;

void setup() {
  Serial.begin(115200);
  // Fork #23: before anything that crashed on the last boots can run again.
  // RTC reads, a pure function and one RTC write: nothing that blocks or
  // needs the network.
  safe_mode = safe_mode_boot();
  delay(100);
  Serial.println();
  Serial.println(F("Starting MHI-AC-Ctrl build " VERSION));
  if (safe_mode) {
    // Wi-Fi and OTA only: no pin measurement, no DS18x20, no MQTT, no AC core
    // (MISO is never driven), no discovery, no group.
    Serial.printf_P(PSTR("SAFE MODE: %u crashes in a row; Wi-Fi and OTA only, a normal boot in %u min\n"),
                    (unsigned)MHI_SAFE_THRESHOLD, (unsigned)(MHI_SAFE_RESTART_MS / 60000u));
    initWiFi();
    setupOTA();
    return;
  }
  crash_info_boot();  // fork #25: after the safe-mode decision, which keeps the record for the next normal boot
  Serial.printf_P(PSTR("CPU frequency[Hz]=%lu\n"), F_CPU);
  Serial.printf_P(PSTR("ESP.getCoreVersion()=%s\n"), ESP.getCoreVersion().c_str());
  Serial.printf_P(PSTR("ESP.getSdkVersion()=%s\n"), ESP.getSdkVersion());
  Serial.printf_P(PSTR("ESP.checkFlashCRC()=%i\n"), ESP.checkFlashCRC());

#if TEMP_MEASURE_PERIOD > 0
  setup_ds18x20();
#endif
  initWiFi();
  MeasureFrequency();
  setupOTA();
  MQTTclient.setServer(MQTT_SERVER, MQTT_PORT);
  MQTTclient.setCallback(MQTT_subscribe_callback);
  mhi_ac_ctrl_core.MHIAcCtrlStatus(&mhiStatusHandler);
  discovery_setup();
  group_setup();
  mhi_heat_shift_init(&heat_shift);
  const bool drive_miso = mhi_miso_may_be_driven(wiring_faults);
  if (!drive_miso)
    Serial.println(F("Signal on MISO: leaving it an input, so commands will not reach the AC"));
  mhi_diag_mask_default(diag_mask, sizeof(diag_mask));
  mhi_ac_ctrl_core.init(drive_miso);
#ifdef USE_EXTENDED_FRAME_SIZE    
  mhi_ac_ctrl_core.set_frame_size(33); // switch to framesize 33 (like WF-RAC). Only 20 or 33 possible
#endif  
  // mhi_ac_ctrl_core.set_fan(7); // set fan AUTO, see https://github.com/absalom-muc/MHI-AC-Ctrl/issues/99
}


void loop() {
  static int WiFiStatus = WIFI_CONNECT_TIMEOUT;   // start connecting to WiFi
  static int MQTTStatus = MQTT_NOT_CONNECTED;
  static unsigned long previousMillis = millis();

  if (safe_mode) {  // fork #23: Wi-Fi and OTA only, then ESP.restart(), reason 4: a normal boot
    if (WiFi.status() != WL_CONNECTED || WiFiStatus != WIFI_CONNECT_OK)
      setupWiFi(WiFiStatus);
    else
      ArduinoOTA.handle();
    if (millis() >= MHI_SAFE_RESTART_MS) {
      Serial.println(F("SAFE MODE: 10 min are up, restarting"));
      ESP.restart();
    }
    return;
  }

  if (rescue_loop()) {  // fork #28: the rescue access point is up, OTA only; with CONTINUE_WITHOUT_MQTT the AC keeps being served
    ArduinoOTA.handle();
  }
  else if (((WiFi.status() != WL_CONNECTED)  || 
       (WiFiStatus != WIFI_CONNECT_OK)) || 
       (WiFI_SEARCHStrongestAP && (millis() - previousMillis >= WiFI_SEARCH_FOR_STRONGER_AP_INTERVALL*60*1000))) {
    //Serial.printf("loop: call setupWiFi(WiFiStatus)\n");
    setupWiFi(WiFiStatus);
    previousMillis = millis();
    //Serial.println(WiFiStatus);
  }
  else {
    //Serial.printf("loop: WiFi.status()=%i\n", WiFi.status()); // see https://realglitch.com/2018/07/arduino-wifi-status-codes/
    MQTTStatus=MQTTreconnect();
    if (MQTTStatus == MQTT_RECONNECTED) {
      mhi_ac_ctrl_core.reset_old_values();  // after a reconnect
      mhi_troom_filter_reset(&troom_filter);
      mhi_cleaning_republish(&cleaning);  // goes out again once the parser re-reports power
      troom_external_published = 0xff;
      troom_external_byte_published = 0;  // the room goes out again with the next set/Troom while shifting (fork #30)
      publish_diag_state();
      diag_frame.have_last = false;   // the next diag/frame is a whole frame
      mhi_retry_reset(&diag_pacer);
      discovery_restart();
      group_connected();
    }
    ArduinoOTA.handle();
    publish_troom_external();
    group_loop();
    discovery_loop();
  }
  publishTelemetry();  // every pass, connected or not, so the uptime counter never misses a millis() wrap
  safe_mode_clear_after_boot(millis());  // fork #23: up 120 s, so the crashes before do not count towards a loop

#if TEMP_MEASURE_PERIOD > 0
  // A reading has come back since setup or since the last fault. Decides
  // whether a fault re-enumerates the bus, independent of whether the sensor
  // drives Troom: a build that only publishes Tds1820 needs that too.
  static bool ds18x20_online = false;
  if (!troom_was_set_by_MQTT) {  // Only use ds18x20 if MQTT is NOT used for setting Troom
    byte ds18x20_value = getDs18x20Temperature(25);
    if (ds18x20_value == DS18X20_NOT_CONNECTED) {
      if (ds18x20_online) {  // earlier DS18X20 was working
        ds18x20_online = false;
        if (troom_was_set_by_DS18X20) {
          // fallback to AC internal Troom temperature sensor
          mhi_ac_ctrl_core.set_troom(0xff);  // use IU temperature sensor
          Serial.println(F("DS18X20 disconnected, use IU temperature sensor value!"));
          troom_was_set_by_DS18X20 = false;
#ifdef ROOM_TEMP_DS18X20
          ds18x20_value_old = 0;  // re-publish once the sensor comes back
#endif
        }
        Serial.println(F("Try setup DS18X20 again"));
        setup_ds18x20();  // try setup again
      }
    }
    else {
      ds18x20_online = true;
#ifdef ENHANCED_RESOLUTION
      // Only on a real reading: applied to the DS18X20_NOT_CONNECTED sentinel
      // this made a byte that only the plausibility window kept out.
      // offset is -0.5..+0.5, so offset*4 is -2..+2. Converting a negative
      // float straight to byte is undefined behaviour; it only produced the
      // right answer here by way of modular arithmetic on xtensa-gcc.
      float offset = mhi_ac_ctrl_core.get_troom_offset();
      int adjusted = (int)ds18x20_value + (int)(offset * 4.0f);
      if (adjusted < 0) adjusted = 0;
      if (adjusted > 255) adjusted = 255;
      ds18x20_value = (byte)adjusted;
#endif

#ifdef ROOM_TEMP_DS18X20
      if(ds18x20_value != ds18x20_value_old) {
        if (mhi_troom_byte_plausible(ds18x20_value)) {  // use only values -10°C < T < 48°C
          send_room_temperature(mhi_celsius_from_troom(ds18x20_value));  // shifted below 18 in heat (fork #30)
          troom_was_set_by_DS18X20 = true;
          ds18x20_value_old = ds18x20_value;
          Serial.printf_P(PSTR("update Troom based on DS18x20 value %i\n"), ds18x20_value);
        }
      }
#endif
    }
  }
#endif

  // fallback to AC internal Troom temperature sensor
  if(troom_was_set_by_MQTT & (millis() - room_temp_set_timeout_Millis >= ROOM_TEMP_MQTT_SET_TIMEOUT*1000)) {
    mhi_ac_ctrl_core.set_troom(0xff);  // use IU temperature sensor
    Serial.println(F("ROOM_TEMP_MQTT_SET_TIMEOUT exceeded, use IU temperature sensor value!"));
    troom_was_set_by_MQTT=false;
    troom_external_byte_published = 0;  // a shift keeps its target and resumes with the next value (fork #30)
#ifdef ROOM_TEMP_DS18X20
    // Otherwise the sensor's value, unchanged since MQTT took over, would not
    // be applied again until it moved a step; the AC stayed on its own sensor.
    ds18x20_value_old = 0;
#endif
  }

#ifndef CONTINUE_WITHOUT_MQTT 
  if((MQTTStatus==MQTT_RECONNECTED) || (MQTTStatus==MQTT_CONNECT_OK)){
#endif    
    //Serial.println("MQTT connected in main loop");
    int ret = mhi_ac_ctrl_core.loop(80);
    note_frame_result(ret);
    if (ret < 0)
      Serial.printf_P(PSTR("mhi_ac_ctrl_core.loop error: %i\n"), ret);
#ifndef CONTINUE_WITHOUT_MQTT 
  }
#endif

}
