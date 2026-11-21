// The set/ command parser (fork #55; was inline in main.cpp's MQTT callback).
//
// Maps a full topic and an already copied, NUL-terminated payload to a command:
// which topic it is, and the value it carries. It decides nothing that needs
// live state: the Tsetpoint guard, the Troom plausibility test (which adds the
// core's offset first) and the subscribe window stay with the caller, in the
// order the old callback applied them. Everything build-dependent arrives in
// MhiCmdConfig, built from the macros in main.cpp, so the native tests can
// run every variant (no #ifdef in here).
//
// A retained set/* message is never acted on, whatever it is (fork #67): the
// broker replays stored messages after every subscribe, so a stale retained
// Mode/Power/Tsetpoint would override the user's last choice on every
// reconnect. mhi_mqtt_retain.h reads the flag; here it is an input, and a
// retained message is kind MHI_CMD_DROPPED, with no reply to publish. Live (and
// unrecognisable) messages are handled as always. A reset is also dropped
// inside the subscribe window (mhi_subscribe_window.h, fork #50), so the
// boot-loop guard never rests on library internals alone.
//
// Matching is exact and case-sensitive, in the order of the old if-chain.
// Strings in the config are plain RAM literals, never PSTR(): the parsers use
// strcmp. Pure logic, no Arduino.

#pragma once

#include <stdint.h>

#include "mhi_fan.h"
#include "mhi_vanes.h"
#include "mhi_vanes_lr.h"

enum MhiCmdKind : uint8_t {
  MHI_CMD_DROPPED,  // retained (or a reset inside the subscribe window): log it, do nothing, publish nothing
  MHI_CMD_UNKNOWN,  // no such topic in this build, or outside the set/ prefix: reply "unknown command"
  MHI_CMD_POWER,    // value 1 on, 0 off
  MHI_CMD_MODE,     // value MHI_CMD_MODE_*
  MHI_CMD_TSETPOINT,  // fvalue, parsed only; the guard decides
  MHI_CMD_FAN,        // value MHI_FAN_AUTO or 1..4
  MHI_CMD_VANES,      // value 1..5
  MHI_CMD_VANESLR,    // value 1..8
  MHI_CMD_3DAUTO,     // value 1 on, 0 off
  MHI_CMD_TROOM,      // fvalue, parsed only; the caller adds the offset and tests plausibility
  MHI_CMD_ERROPDATA,
  MHI_CMD_RESET,      // value MHI_CMD_RESET_*
  MHI_CMD_PASSIVE,    // value 1 on, 0 off
  MHI_CMD_DIAG,       // value 1 on, 0 off
  MHI_CMD_OPDATA,     // a = request prefix, b = code
  MHI_CMD_SILENT,     // value 1 on, 0 off
};

// MHI_CMD_MODE values; main.cpp maps them to the core's ACMode.
#define MHI_CMD_MODE_OFF (-1)  // only with MhiCmdConfig::mode_carries_power
#define MHI_CMD_MODE_AUTO 0
#define MHI_CMD_MODE_DRY 1
#define MHI_CMD_MODE_COOL 2
#define MHI_CMD_MODE_FAN 3
#define MHI_CMD_MODE_HEAT 4

#define MHI_CMD_RESET_RESTART 0
#define MHI_CMD_RESET_CRASH 1  // only with MhiCmdConfig::allow_crash

struct MhiCmd {
  MhiCmdKind kind;
  bool error;     // the topic is known but the payload is not acceptable: reply "invalid parameter"
  int value;
  float fvalue;
  uint8_t a, b;
};

struct MhiCmdConfig {
  const char* set_prefix;  // MQTT_SET_PREFIX
  // Topic names below the prefix; NULL where the topic does not exist.
  const char *t_power, *t_mode, *t_tsetpoint, *t_fan, *t_vanes, *t_vaneslr, *t_3dauto, *t_troom, *t_erropdata, *t_reset,
      *t_passive, *t_diag, *t_opdata, *t_silent;
  // Payload texts.
  const char *power_on, *power_off;
  const char *mode_off, *mode_auto, *mode_dry, *mode_cool, *mode_fan, *mode_heat;
  const char *dauto_on, *dauto_off;
  const char *reset, *reset_crash;
  const char *passive_on, *passive_off;
  const char *diag_on, *diag_off;
  const char *silent_on, *silent_off;
  const MhiFanNames* fan_names;
  const MhiVanesNames* vanes_names;
  const MhiVanesLrNames* vanes_lr_names;  // NULL: no VanesLR/3Dauto (20-byte frame)
  bool mode_carries_power;  // POWERON_WHEN_CHANGING_MODE: no Power topic, Mode accepts mode_off
  bool allow_crash;         // RESET_CRASH_COMMAND: the reset topic accepts reset_crash
};

// Fills *cmd. Never fails: unmatched input is kind MHI_CMD_UNKNOWN, any input
// with retained set is MHI_CMD_DROPPED, and so is a reset (whatever its
// payload) with in_window set.
void mhi_cmd_parse(const MhiCmdConfig* cfg, const char* topic, const char* payload, bool retained, bool in_window,
                   MhiCmd* cmd);
