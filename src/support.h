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
#define HA_ID_PREFIX HOSTNAME                       // unique_id prefix of the other entities: <prefix>_vanes, _silent, _problem, ...
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
//#define HA_RESET_REASON_TPL "{{ value }}"         // when defined, the reset-reason sensor's value_template (a Jinja template, e.g. a translation table)

#ifndef TELEMETRY_PERIOD
#define TELEMETRY_PERIOD 300                        // seconds between publishes of RSSI, Uptime and FreeHeap while MQTT is connected; 0 publishes them at MQTT connect only
#endif
#if TELEMETRY_PERIOD * 1000UL > 0xFFFFFFFFUL
#error "TELEMETRY_PERIOD must be below 4294967 seconds (49.7 days): the interval is kept in 32-bit milliseconds"
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