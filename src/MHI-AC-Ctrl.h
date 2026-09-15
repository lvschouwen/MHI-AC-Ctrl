#pragma once

#include <cstdint>

// Every topic and payload text below can be replaced from the gitignored
// src/config_defaults.h, like the options in support.h. A build for Home
// Assistant uses that for the lower-case mode names its MQTT climate expects.
#include "mhi_config.h"

// MQTT topic names
#ifndef TOPIC_CONNECTED
#define TOPIC_CONNECTED "connected"
#endif
#ifndef TOPIC_VERSION
#define TOPIC_VERSION "Version"
#endif
#ifndef TOPIC_RSSI
#define TOPIC_RSSI "RSSI"
#endif
#ifndef TOPIC_WIFI_LOST
#define TOPIC_WIFI_LOST "WIFI_LOST"
#endif
#ifndef TOPIC_MQTT_LOST
#define TOPIC_MQTT_LOST "MQTT_LOST"
#endif
#ifndef TOPIC_WIFI_BSSID
#define TOPIC_WIFI_BSSID "WIFI_BSSID"
#endif
#ifndef TOPIC_CMD_RECEIVED
#define TOPIC_CMD_RECEIVED "cmd_received"
#endif
#ifndef TOPIC_TDS1820
#define TOPIC_TDS1820 "Tds1820"
#endif
#ifndef TOPIC_FSCK
#define TOPIC_FSCK "fSCK"
#endif
#ifndef TOPIC_FMOSI
#define TOPIC_FMOSI "fMOSI"
#endif
#ifndef TOPIC_FMISO
#define TOPIC_FMISO "fMISO"
#endif
#ifndef TOPIC_WIRING
#define TOPIC_WIRING "Wiring"
#endif

#ifndef TOPIC_POWER
#define TOPIC_POWER "Power"
#endif
#ifndef TOPIC_MODE
#define TOPIC_MODE "Mode"
#endif
#ifndef TOPIC_FAN
#define TOPIC_FAN "Fan"
#endif
#ifndef TOPIC_VANES
#define TOPIC_VANES "Vanes"
#endif
#ifndef TOPIC_VANESLR
#define TOPIC_VANESLR "VanesLR"
#endif
#ifndef TOPIC_3DAUTO
#define TOPIC_3DAUTO "3Dauto"
#endif
#ifndef TOPIC_TROOM
#define TOPIC_TROOM "Troom"
#endif
#ifndef TOPIC_TSETPOINT
#define TOPIC_TSETPOINT "Tsetpoint"
#endif
#ifndef TOPIC_ERRORCODE
#define TOPIC_ERRORCODE "Errorcode"
#endif

#ifndef TOPIC_UNKNOWN
#define TOPIC_UNKNOWN "unknown"
#endif
#ifndef TOPIC_KWH
#define TOPIC_KWH "KWH"
#endif
#ifndef TOPIC_RETURNAIR
#define TOPIC_RETURNAIR "RETURN-AIR"
#endif
#ifndef TOPIC_THI_R1
#define TOPIC_THI_R1 "THI-R1"
#endif
#ifndef TOPIC_THI_R2
#define TOPIC_THI_R2 "THI-R2"
#endif
#ifndef TOPIC_THI_R3
#define TOPIC_THI_R3 "THI-R3"
#endif
#ifndef TOPIC_IU_FANSPEED
#define TOPIC_IU_FANSPEED "IU-FANSPEED"
#endif
#ifndef TOPIC_TOTAL_IU_RUN
#define TOPIC_TOTAL_IU_RUN "TOTAL-IU-RUN"
#endif
#ifndef TOPIC_OUTDOOR
#define TOPIC_OUTDOOR "OUTDOOR"
#endif
#ifndef TOPIC_COMP
#define TOPIC_COMP "COMP"
#endif
#ifndef TOPIC_TD
#define TOPIC_TD "TD"
#endif
#ifndef TOPIC_THO_R1
#define TOPIC_THO_R1 "THO-R1"
#endif
#ifndef TOPIC_CT
#define TOPIC_CT "CT"
#endif
#ifndef TOPIC_TDSH
#define TOPIC_TDSH "TDSH"
#endif
#ifndef TOPIC_PROTECTION_NO
#define TOPIC_PROTECTION_NO "PROTECTION-NO"
#endif
#ifndef TOPIC_OU_FANSPEED
#define TOPIC_OU_FANSPEED "OU-FANSPEED"
#endif
#ifndef TOPIC_DEFROST
#define TOPIC_DEFROST "DEFROST"
#endif
#ifndef TOPIC_TOTAL_COMP_RUN
#define TOPIC_TOTAL_COMP_RUN "TOTAL-COMP-RUN"
#endif
#ifndef TOPIC_OU_EEV1
#define TOPIC_OU_EEV1 "OU-EEV1"
#endif

#ifndef TOPIC_REQUEST_ERROPDATA
#define TOPIC_REQUEST_ERROPDATA "ErrOpData"
#endif
#ifndef TOPIC_REQUEST_RESET
#define TOPIC_REQUEST_RESET "reset"
#endif
#ifndef TOPIC_REQUEST_PASSIVEMODE
#define TOPIC_REQUEST_PASSIVEMODE "PassiveMode"
#endif

// MQTT payload text
#ifndef PAYLOAD_CONNECTED_TRUE
#define PAYLOAD_CONNECTED_TRUE "1"
#endif
#ifndef PAYLOAD_CONNECTED_FALSE
#define PAYLOAD_CONNECTED_FALSE "0"
#endif
#ifndef PAYLOAD_CMD_OK
#define PAYLOAD_CMD_OK "o.k."
#endif
#ifndef PAYLOAD_CMD_UNKNOWN
#define PAYLOAD_CMD_UNKNOWN "unknown command"
#endif
#ifndef PAYLOAD_CMD_INVALID_PARAMETER
#define PAYLOAD_CMD_INVALID_PARAMETER "invalid parameter"
#endif
#ifndef PAYLOAD_POWER_ON
#define PAYLOAD_POWER_ON "On"
#endif
#ifndef PAYLOAD_POWER_OFF
#define PAYLOAD_POWER_OFF "Off"
#endif
#ifndef PAYLOAD_MODE_OFF
#define PAYLOAD_MODE_OFF PAYLOAD_POWER_OFF
#endif
#ifndef PAYLOAD_MODE_AUTO
#define PAYLOAD_MODE_AUTO "Auto"
#endif
#ifndef PAYLOAD_MODE_STOP
#define PAYLOAD_MODE_STOP "Stop"
#endif
#ifndef PAYLOAD_MODE_DRY
#define PAYLOAD_MODE_DRY "Dry"
#endif
#ifndef PAYLOAD_MODE_COOL
#define PAYLOAD_MODE_COOL "Cool"
#endif
#ifndef PAYLOAD_MODE_FAN
#define PAYLOAD_MODE_FAN "Fan"
#endif
#ifndef PAYLOAD_MODE_HEAT
#define PAYLOAD_MODE_HEAT "Heat"
#endif
#ifndef PAYLOAD_FAN_AUTO
#define PAYLOAD_FAN_AUTO "Auto"
#endif
#ifndef PAYLOAD_VANES_UNKNOWN
#define PAYLOAD_VANES_UNKNOWN "?"
#endif
#ifndef PAYLOAD_VANES_SWING
#define PAYLOAD_VANES_SWING "Swing"
#endif
#ifndef PAYLOAD_OP_DEFROST_ON
#define PAYLOAD_OP_DEFROST_ON "On"
#endif
#ifndef PAYLOAD_OP_DEFROST_OFF
#define PAYLOAD_OP_DEFROST_OFF "Off"
#endif
#ifndef PAYLOAD_REQUEST_RESET
#define PAYLOAD_REQUEST_RESET "reset"
#endif
#ifndef PAYLOAD_VANESLR_SWING
#define PAYLOAD_VANESLR_SWING "Swing"
#endif
#ifndef PAYLOAD_3DAUTO_ON
#define PAYLOAD_3DAUTO_ON "On"
#endif
#ifndef PAYLOAD_3DAUTO_OFF
#define PAYLOAD_3DAUTO_OFF "Off"
#endif
#ifndef PAYLOAD_REQUEST_PASSIVEMODE_ON
#define PAYLOAD_REQUEST_PASSIVEMODE_ON "On"
#endif
#ifndef PAYLOAD_REQUEST_PASSIVEMODE_OFF
#define PAYLOAD_REQUEST_PASSIVEMODE_OFF "Off"
#endif

enum POWER_STATUS {
    unknown,
    off,
    on
};
