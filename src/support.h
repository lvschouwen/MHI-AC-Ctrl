#pragma once

#include "MHI-AC-Ctrl-core.h"
#include "MHI-AC-Ctrl.h"

// VERSION is the short git commit hash of this build, with "-dirty" appended
// when tracked files were modified. scripts/build_version.py generates the
// header into the build directory before every build (fork issue #13).
#include "build_version.h"

// *** Configuration ***
//
// Every setting below is a default. There are two ways to override one without
// editing this file, which matters because this file is tracked in git and
// credentials must not be:
//
//   1. Create src/config_defaults.h with your own #defines. It is included
//      first and it is in .gitignore, so it never lands in a commit. This is
//      the recommended way.
//   2. Pass -D FOO=bar in build_flags for a PlatformIO environment. This is
//      how the ci-* environments exercise the #ifdef matrix.
//
// The boolean feature switches further down stay commented out on purpose:
// they are tested with #ifdef, so defining them at all turns them on.

#include "mhi_config.h"

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#ifndef HOSTNAME
#define HOSTNAME "MHI-AC-Ctrl"
#endif

#ifndef WiFI_SEARCHStrongestAP
#define WiFI_SEARCHStrongestAP true                 // when false then the first WiFi access point with matching SSID found is used.
                                                    // when true then the strongest WiFi access point with matching SSID found is used, it doesn't work with hidden SSID
#endif

#ifndef WiFI_SEARCH_FOR_STRONGER_AP_INTERVALL
#define WiFI_SEARCH_FOR_STRONGER_AP_INTERVALL 12    // WiFi network re-scan interval in minutes with alternate to +5dB stronger signal if detected
#endif

#ifndef DIAG_DEFAULT
#define DIAG_DEFAULT true                           // whether diag/frame (the status-frame change topic) is on after boot; set/Diag switches it at runtime
#endif

// Home Assistant MQTT discovery (fork #4 batch B, SW-Configuration.md "Home
// Assistant discovery"). Off by default: HA's climate accepts only its own mode
// names, so a build for it also sets the PAYLOAD_MODE_* texts (see the docs).
//#define HA_DISCOVERY true                           // uncomment to publish the discovery configs after every MQTT connect
#ifndef HA_DISCOVERY_PREFIX
#define HA_DISCOVERY_PREFIX "homeassistant"         // HA's discovery prefix
#endif
#ifndef HA_DEVICE_NAME
#define HA_DEVICE_NAME HOSTNAME                     // the device the entities belong to; HA prefixes every entity name with it
#endif
#ifndef HA_CLIMATE_ID
#define HA_CLIMATE_ID HOSTNAME                      // unique_id of the climate entity
#endif
#ifndef HA_ID_PREFIX
#define HA_ID_PREFIX HOSTNAME                       // unique_id prefix of the other entities: <prefix>_vanes, _silent, _problem, _wiring, _uptime, _free_heap, _rssi, _reset_reason, _wifi_phy, _vanes_lr, _3d_auto, _frame_errors, _frame_timeouts, _error_code
#endif
//#define HA_ENTITY_PREFIX "ac_slaapkamer"          // when defined, every entity gets default_entity_id: climate.<prefix>, and <domain>.<prefix>_<slug of its name> for the rest (what HA derives itself); lower case a-z 0-9 _
#ifndef HA_NAME_VANES
#define HA_NAME_VANES "Vanes"                       // entity names, shown after the device name
#endif
#ifndef HA_NAME_SILENT
#define HA_NAME_SILENT "Silent"
#endif
#ifndef HA_NAME_PROBLEM
#define HA_NAME_PROBLEM "Problem"
#endif
#ifndef HA_NAME_WIRING
#define HA_NAME_WIRING "Wiring"
#endif
#ifndef HA_NAME_UPTIME
#define HA_NAME_UPTIME "Uptime"
#endif
#ifndef HA_NAME_FREE_HEAP
#define HA_NAME_FREE_HEAP "Free heap"
#endif
#ifndef HA_NAME_RSSI
#define HA_NAME_RSSI "Wi-Fi signal"
#endif
#ifndef HA_NAME_RESET_REASON
#define HA_NAME_RESET_REASON "Reset reason"
#endif
#ifndef HA_NAME_WIFI_PHY
#define HA_NAME_WIFI_PHY "Wi-Fi PHY"
#endif
#ifndef HA_NAME_VANES_LR
#define HA_NAME_VANES_LR "Vanes left/right"
#endif
#ifndef HA_NAME_3DAUTO
#define HA_NAME_3DAUTO "3D auto"
#endif
#ifndef HA_NAME_FRAME_ERRORS
#define HA_NAME_FRAME_ERRORS "Frame errors"
#endif
#ifndef HA_NAME_FRAME_TIMEOUTS
#define HA_NAME_FRAME_TIMEOUTS "Frame timeouts"
#endif
#ifndef HA_NAME_ERROR_CODE
#define HA_NAME_ERROR_CODE "Error code"
#endif
// The outdoor device (fork #19). Every unit is a candidate publisher since
// fork #22: the group's publisher sends it (GROUP_ROOT below).
//#define HA_OUTDOOR_ID "ac_outdoor"                // default: derived at boot from GROUP_ROOT, <slug of GROUP_ROOT>_outdoor, so every member of the group derives the same
#ifndef HA_OUTDOOR_NAME
#define HA_OUTDOOR_NAME "AC outdoor unit"
#endif
//#define HA_OUTDOOR_ENTITY_PREFIX "ac_outdoor"       // same idea as HA_ENTITY_PREFIX, for the outdoor entities
// The outdoor entities are named without "outdoor": the device already is
// "AC outdoor unit", and Home Assistant shows "<device> <entity>".
#ifndef HA_NAME_OU_OUTDOOR
#define HA_NAME_OU_OUTDOOR "Temperature"
#endif
#ifndef HA_NAME_OU_CT
#define HA_NAME_OU_CT "Current"
#endif
#ifndef HA_NAME_OU_KWH
#define HA_NAME_OU_KWH "Energy"
#endif
#ifndef HA_NAME_OU_COMP
#define HA_NAME_OU_COMP "Compressor frequency"
#endif
#ifndef HA_NAME_OU_DEFROST
#define HA_NAME_OU_DEFROST "Defrost"
#endif
#ifndef HA_NAME_OU_COMP_RUN
#define HA_NAME_OU_COMP_RUN "Compressor run time"
#endif
#ifndef HA_NAME_OU_PROTECTION
#define HA_NAME_OU_PROTECTION "Protection state"
#endif
#ifndef HA_NAME_GROUP_ROLE
#define HA_NAME_GROUP_ROLE "Group role"             // the diagnostic sensor on the Group topic (fork #22)
#endif
//#define HA_RESET_REASON_TPL "{{ value }}"         // when defined, the reset-reason sensor's value_template (a Jinja template, e.g. a translation table)

#ifndef TELEMETRY_PERIOD
#define TELEMETRY_PERIOD 300                        // seconds between publishes of RSSI, Uptime, FreeHeap and the group record while MQTT is connected, 1..86400
#endif
#if TELEMETRY_PERIOD < 1 || TELEMETRY_PERIOD > 86400
#error "TELEMETRY_PERIOD must be 1..86400 seconds: the group record is re-sent every period and a unit counts as gone after 3 of them, in 32-bit milliseconds (fork #22)"
#endif

#ifndef MQTT_SERVER
#define MQTT_SERVER "192.168.178.111"               // broker name or IP address of the broker
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 1883                              // port number used by the broker
#endif
#ifndef MQTT_USER
#define MQTT_USER ""                                // if authentication is not used, leave it empty
#endif
#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD ""                            // if authentication is not used, leave it empty
#endif
#ifndef MQTT_PREFIX
#define MQTT_PREFIX HOSTNAME "/"                    // basic prefix used for publishing AC data (e.g. for status),
                                                    // replace "/" by e.g. "/Living-Room/" when you have multiple ACs
#endif
#ifndef MQTT_SET_PREFIX
#define MQTT_SET_PREFIX MQTT_PREFIX "set/"          // prefix for subscribing set commands, must end with a "/"
#endif
#ifndef MQTT_OP_PREFIX
#define MQTT_OP_PREFIX MQTT_PREFIX "OpData/"        // prefix for publishing operating data, must end with a "/"
#endif
#ifndef MQTT_ERR_OP_PREFIX
#define MQTT_ERR_OP_PREFIX MQTT_PREFIX "ErrOpData/" // prefix for publishing operating data from last error, must end with a "/"
#endif

// The outdoor election (fork #22, SW-Configuration.md "Several indoor units
// on one outdoor unit"): the indoor units of one outdoor unit share GROUP_ROOT
// and elect the one that writes the outdoor unit's values under
// GROUP_OP_PREFIX. A single split needs nothing: its root is its own
// MQTT_PREFIX, so its topics stay where they are.
#ifndef GROUP_ROOT
#define GROUP_ROOT MQTT_PREFIX                      // topic root shared by the units of one outdoor unit, ends in "/", at most 64 characters
#ifndef GROUP_OP_PREFIX
#define GROUP_OP_PREFIX MQTT_OP_PREFIX              // without a GROUP_ROOT a custom MQTT_OP_PREFIX keeps its topics
#endif
#endif
#ifndef GROUP_OP_PREFIX
#define GROUP_OP_PREFIX GROUP_ROOT "OpData/"        // where the publisher writes the outdoor unit's values
#endif

#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME HOSTNAME                       // default for the OTA_HOSTNAME is the HOSTNAME
#endif
#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""                             // Enter an OTA password if required
#endif

#ifndef TEMP_MEASURE_PERIOD
#define TEMP_MEASURE_PERIOD 0                       // period in seconds for temperature measurement with the external DS18x20 temperature sensor
                                                    // enter 0 if you don't use the DS18x20
#endif
#ifndef ONE_WIRE_BUS
#define ONE_WIRE_BUS 4                              // D2, PIN for connecting temperature sensor DS18x20 DQ pin
#endif
#ifndef ROOM_TEMP_DS18X20_OFFSET
#define ROOM_TEMP_DS18X20_OFFSET 0.0                // Temperature offset for DS18x20 sensor, can be positive or negative (examples: 0.0, -1.0, 1.5)
#endif

//#define ROOM_TEMP_DS18X20                           // use room temperature from DS18x20

#ifndef ROOM_TEMP_MQTT_SET_TIMEOUT
#define ROOM_TEMP_MQTT_SET_TIMEOUT  40              // time in seconds, after this time w/o receiving a valid room temperature
                                                    // via MQTT fallback to IU temperature sensor value
#endif

//#define POWERON_WHEN_CHANGING_MODE true           // uncomment it to switch on the AC when the mode (heat, cool, dry etc.) is changed
                                                    // used e.g. for home assistant support

#ifndef TROOM_FILTER_LIMIT
#define TROOM_FILTER_LIMIT 0.25                     // Defines from which Troom delta value a new Troom value is pubslised. Resolution 0.25°C.
                                                    // With a smaller resolution, Troom could toggle more. So deactivate the filter use 0.
#endif
//#define ENHANCED_RESOLUTION true                    // when using Tsetpoint with x.5 degrees, airco will use (x+1).0 setpoint
                                                    // uncomment this to compensatie (offset) Troom for this.
                                                    // this will simulate .x degrees resolution
//#define CONTINUE_WITHOUT_MQTT true                  // uncomment if communication with AC has to continue when MQTT or WiFi connection is disconnected.
                                                    // When Troom is supplied from external, it will fallback to AC internal Troom temperature sensor
                                                    // When ROOM_TEMP_DS18X20 is used, it will use room temperature from DS18x20
//#define USE_EXTENDED_FRAME_SIZE true                // uncomment if you want to use de extended frame size (33) which is used by the WF-RAC module
                                                    // Then it will be possible to get and set the 3D auto and vanes left/right



// *** The configuration ends here ***

#include <ESP8266WiFi.h>        // https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WiFi
#include <PubSubClient.h>       // https://github.com/hmueller01/pubsubclient3
#include <ArduinoOTA.h>         // https://github.com/esp8266/Arduino/tree/master/libraries/ArduinoOTA

#if TEMP_MEASURE_PERIOD > 0
#include <OneWire.h>            // https://www.pjrc.com/teensy/td_libs_OneWire.html
#include <DallasTemperature.h>  // https://github.com/milesburton/Arduino-Temperature-Control-Library
#endif

#ifdef ROOM_TEMP_DS18X20
#if (TEMP_MEASURE_PERIOD == 0)
#error "You have to use a value>0 for TEMP_MEASURE_PERIOD when you want to use DS18x20 as an external temperature sensor"
#endif
#endif

#ifdef HA_OUTDOOR_DEVICE
#error "HA_OUTDOOR_DEVICE was removed (fork #22): every unit is a candidate publisher now; give the units of one outdoor unit the same GROUP_ROOT"
#endif

// The group's configuration (fork #22 spec §2), checked with the rules the
// units apply to each other's records.
#include "mhi_group.h"
static_assert(mhi_group_root_valid(GROUP_ROOT), "GROUP_ROOT must be 1..64 characters, end in / and contain no + # ;");
// PubSubClient3 drops a received packet larger than its buffer whole, and the
// firmware never enlarges it: a longer root would make every unit drop the
// others' records, and two publishers would never see each other.
static_assert(mhi_group_record_packet_max(sizeof(GROUP_ROOT) - 1) <= MQTT_MAX_PACKET_SIZE,
              "the largest group record packet does not fit PubSubClient's receive buffer: shorten GROUP_ROOT");
static_assert(!mhi_group_starts_with(GROUP_ROOT "members/", MQTT_SET_PREFIX),
              "<GROUP_ROOT>members/ must not start with MQTT_SET_PREFIX: the records would be read as commands");
static_assert(mhi_group_host_valid(HOSTNAME), "HOSTNAME must be 1..32 characters without / + # ; \" \\");
// Every record carries MQTT_PREFIX and the outdoor ID, and a peer rejects a
// record whose fields break the record's rules (spec §5.1): check them here.
static_assert(mhi_group_prefix_valid(MQTT_PREFIX),
              "MQTT_PREFIX must be 1..64 characters, end in / and contain no ; + # \" \\, space or control character");
#ifdef HA_OUTDOOR_ID
static_assert(mhi_group_id_valid(HA_OUTDOOR_ID),
              "HA_OUTDOOR_ID must be 1..40 characters without ; / + # \" \\, space or control character");
#else
static_assert(mhi_group_default_outdoor_id_len(GROUP_ROOT) <= MHI_GROUP_ID_MAX,
              "the outdoor ID derived from GROUP_ROOT is longer than 40 characters: define HA_OUTDOOR_ID");
#endif

extern PubSubClient MQTTclient;

// Result of the boot-time wiring check: 0 when all three pins looked right,
// otherwise a bitmask of MhiWiringFault. A fault does not stop the program -
// it is published to the diagnostics topic and the unit stays reachable over
// OTA, which is the only way to fix a board that is already inside an AC.
extern uint8_t wiring_faults;

void MeasureFrequency();                                      // measures the frequency of the SPI pins
void initWiFi();                                              // basic WiFi initialization
void setupWiFi(int& WiFiStatus);                              // setup WIFi connection to AP
int MQTTreconnect();                                          // (re)connect to MQTT broker
void publishTelemetry();                                      // call every loop() pass: advances the uptime counter; publishes RSSI, Uptime, FreeHeap every TELEMETRY_PERIOD s while connected
void publish_cmd_ok();                                        // last MQTT cmd was o.k.
void publish_cmd_unknown();                                   // last MQTT cmd was unknown
void publish_cmd_invalidparameter();                          // a paramter of the last MQTT was wrong
void output_P(ACStatus status, PGM_P topic, PGM_P payload);   // publish via MQTT
void note_frame_result(int ret);  // count mhi_ac_ctrl_core.loop()'s return towards FrameErrors/FrameTimeouts (fork #21)
const char* outdoor_id();                                     // HA_OUTDOOR_ID, or the one derived from GROUP_ROOT (fork #22)
uint32_t uptime_seconds();                                    // the Uptime counter, advanced to now; the group record carries it (fork #22)

void setupOTA();                                              // initialize and start OTA
void setup_ds18x20();                                         // setup the temperature measurement
byte getDs18x20Temperature(int temp_hysterese);               // read the temperature from the DS18x20 sensor

#define WIFI_CONNECT_SCANNING 4
#define WIFI_CONNECT_SCANNING_DONE 3
#define WIFI_CONNECT_TIMEOUT 2
#define WIFI_CONNECT_ONGOING 1
#define WIFI_CONNECT_OK 0

#define MQTT_NOT_CONNECTED 2
#define MQTT_RECONNECTED 1
#define MQTT_CONNECT_OK 0

#define DS18X20_NOT_CONNECTED 1