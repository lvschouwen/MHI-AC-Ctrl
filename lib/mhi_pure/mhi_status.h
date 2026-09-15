// Reporting power and mode from status byte DB0.
//
// Power and mode arrive together in DB0 but leave as separate MQTT messages.
// With POWERON_WHEN_CHANGING_MODE the Mode topic also stands in for power: it
// carries "off" while the unit is off, and a consumer such as Home Assistant's
// MQTT climate reads on/off from Mode alone. An active mode on that topic for a
// unit that is off is therefore not a harmless extra message. It shows up as a
// real off -> cool -> off, and v2.8 produced one after every MQTT reconnect.

#pragma once

#include <stddef.h>
#include <stdint.h>

// "Nothing reported yet", the value reset_old_values() puts back.
#define MHI_STATUS_UNKNOWN 0xff

enum MhiDb0Field : uint8_t { MHI_DB0_POWER, MHI_DB0_MODE };

struct MhiDb0Change {
  MhiDb0Field field;
  uint8_t value;  // power: 0 or 1; mode: the DB0 mode bits, 0x00 to 0x10
};

// Compare db0 with the last reported power and mode, store the new values, and
// fill `out` with what changed in the order to report it. A switch-off goes
// before the mode, so the unit is already off when its mode goes out; anything
// else puts the mode first, so a switch-on reports the mode it switched on
// into. Returns the number of changes, 0 to 2.
size_t mhi_db0_changes(uint8_t db0, uint8_t* power_old, uint8_t* mode_old, MhiDb0Change out[2]);

// What the status handler knows when Mode stands in for power. Starts as
// {MHI_STATUS_UNKNOWN, MHI_STATUS_UNKNOWN} and, unlike the parser's old values,
// survives an MQTT reconnect.
struct MhiModeTopic {
  uint8_t power;
  uint8_t mode;
};

enum MhiModeTopicAction : uint8_t {
  MHI_MODE_TOPIC_OFF,   // publish "off"
  MHI_MODE_TOPIC_MODE,  // publish the stored mode
};

// A mode was decoded. Returns true when Mode should carry it now, which is only
// while the unit is known to be on. Otherwise it is held for the switch-on.
bool mhi_mode_topic_on_mode(MhiModeTopic* topic, uint8_t mode);

// Power was decoded. Returns what Mode should carry now.
MhiModeTopicAction mhi_mode_topic_on_power(MhiModeTopic* topic, uint8_t power);
