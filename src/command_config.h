// The texts of the set/ commands and the status topics of this build, shared by
// main.cpp's publishing and by mhi_cmd_parse() (fork #55). The tables are inline constants.
#pragma once

#include "MHI-AC-Ctrl-core.h"
#include "MHI-AC-Ctrl.h"
#include "mhi_cmd.h"
#include "mhi_fan.h"
#include "mhi_vanes.h"
#include "mhi_vanes_lr.h"
#include "support.h"

// The texts on the Vanes topic; set/Vanes accepts these and 1..5 (fork #4 batch B).
inline const MhiVanesNames vanes_names = {{PAYLOAD_VANES_1, PAYLOAD_VANES_2, PAYLOAD_VANES_3, PAYLOAD_VANES_4},
                                          PAYLOAD_VANES_SWING, PAYLOAD_VANES_UNKNOWN};
static_assert(MHI_VANES_SWING == vanes_swing && MHI_VANES_UNKNOWN == vanes_unknown, "mhi_vanes numbers the positions as the core's ACVanes does");

#ifdef USE_EXTENDED_FRAME_SIZE
// The texts on the VanesLR topic; set/VanesLR accepts these and 1..8 (fork #20).
// Only the 33-byte frame reports or accepts them, so a 20-byte build does not
// carry the table at all.
inline const MhiVanesLrNames vanes_lr_names = {
  {PAYLOAD_VANESLR_1, PAYLOAD_VANESLR_2, PAYLOAD_VANESLR_3, PAYLOAD_VANESLR_4, PAYLOAD_VANESLR_5, PAYLOAD_VANESLR_6,
   PAYLOAD_VANESLR_7},
  PAYLOAD_VANESLR_SWING};
static_assert(MHI_VANES_LR_SWING == vanesLR_swing, "mhi_vanes_lr numbers swing as the core's ACVanesLR does");
#endif

// The texts on the Fan topic; set/Fan accepts these and 1..4 (fork #21 F6).
inline const MhiFanNames fan_names = {{PAYLOAD_FAN_1, PAYLOAD_FAN_2, PAYLOAD_FAN_3, PAYLOAD_FAN_4}, PAYLOAD_FAN_AUTO};

// The set/ command texts of this build for mhi_cmd_parse(). Plain literals, not
// PSTR(): the pure parser uses strcmp.
#ifdef POWERON_WHEN_CHANGING_MODE
#define MHI_CMD_MODE_CARRIES_POWER true
#else
#define MHI_CMD_MODE_CARRIES_POWER false
#endif
#ifdef RESET_CRASH_COMMAND
#define MHI_CMD_RESET_CRASH_ON true
#else
#define MHI_CMD_RESET_CRASH_ON false
#endif
inline const MhiCmdConfig cmd_config = {
    MQTT_SET_PREFIX,
    TOPIC_POWER, TOPIC_MODE, TOPIC_TSETPOINT, TOPIC_FAN, TOPIC_VANES, TOPIC_VANESLR, TOPIC_3DAUTO, TOPIC_TROOM,
    TOPIC_REQUEST_ERROPDATA, TOPIC_REQUEST_RESET, TOPIC_REQUEST_PASSIVEMODE, TOPIC_REQUEST_DIAG, TOPIC_REQUEST_OPDATA,
    TOPIC_SILENT,
    PAYLOAD_POWER_ON, PAYLOAD_POWER_OFF,
    PAYLOAD_MODE_OFF, PAYLOAD_MODE_AUTO, PAYLOAD_MODE_DRY, PAYLOAD_MODE_COOL, PAYLOAD_MODE_FAN, PAYLOAD_MODE_HEAT,
    PAYLOAD_3DAUTO_ON, PAYLOAD_3DAUTO_OFF,
    PAYLOAD_REQUEST_RESET, PAYLOAD_REQUEST_RESET_CRASH,
    PAYLOAD_REQUEST_PASSIVEMODE_ON, PAYLOAD_REQUEST_PASSIVEMODE_OFF,
    PAYLOAD_DIAG_ON, PAYLOAD_DIAG_OFF,
    PAYLOAD_SILENT_ON, PAYLOAD_SILENT_OFF,
    &fan_names, &vanes_names,
#ifdef USE_EXTENDED_FRAME_SIZE
    &vanes_lr_names,
#else
    NULL,
#endif
    MHI_CMD_MODE_CARRIES_POWER, MHI_CMD_RESET_CRASH_ON};

// MHI_CMD_MODE_* to the core's modes.
static const ACMode kCmdModes[] = {mode_auto, mode_dry, mode_cool, mode_fan, mode_heat};
