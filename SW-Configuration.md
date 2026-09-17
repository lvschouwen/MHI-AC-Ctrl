# Introduction
The following descriptions address:
1. Basic settings for your configuration
2. Advanced settings for other frameworks (e.g. Tasmota, Home Assistant, Homie)
3. Integration examples

# Basic settings

Every setting in [support.h](src/support.h) is a default. Rather than editing
that file, put your own `#define`s in `src/config_defaults.h`: it is included
first, so it wins, and it is in `.gitignore`, so credentials cannot end up in a
commit. Build flags work too, which is how the `ci-*` environments in
[platformio.ini](platformio.ini) exercise the feature switches.

The basic settings will be adapted in three files:
- [support.h](src/support.h) for general settings related to WiFi, MQTT, OTA and the external temperature sensor DS18x20
- [MHI-AC-Ctrl.h](src/MHI-AC-Ctrl.h) for input / output settings (i.e. topic / payload text)
- [MHI-AC-Ctrl-core.h](src/MHI-AC-Ctrl-core.h) for settings related to the behaviour of MHI-AC-Ctrl (e.g. selection of operating data)

## WiFi ([support.h](src/support.h))
WiFi STA mode is supported.

### WiFi Settings (SSID, Password, hostname)
Adapt the SSID and the password. Changing the hostname is usually not required.
```cpp
#define WIFI_SSID "your SSID"
#define WIFI_PASSWORD "your WiFi password"
#define HOSTNAME "MHI-AC-Ctrl"
```
Changing the hostname is required when multiple ACs should be supported. E.g. replace `"MHI-AC-Ctrl"` by `"Living-Room-MHI-AC-Ctrl"`

Per default ESP8266 uses the first WiFi access point (AP) with matching SSID. This behaviour can be changed.
```cpp
#define WiFI_SEARCHStrongestAP true     // when false then the first WiFi access point with matching SSID found is used.
                                        // when true then the strongest WiFi access point with matching SSID found is used, it doesn't work with hidden SSID
```
Configure the time interval for searching a stronger AP.
```cpp
#define WiFI_SEARCH_FOR_STRONGER_AP_INTERVALL 12    // WiFi network re-scan interval in minutes with alternate to a +10dB stronger signal if detected
```

### WiFi PHY mode fallback
The ESP8266 joins in its default 802.11n mode. Some routers with 802.11ax (WiFi 6) enabled on 2.4 GHz refuse that join and the ESP8266 reports a wrong password although the password is right ([#224](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/224)); forcing 802.11g gets in. A unit that is off the network cannot be told to change, so the firmware falls back on its own: after five minutes without a link it tries 11g, after another five minutes 11n again, and so on until a join succeeds. A router reboot is shorter than that, so a normal outage keeps 11n. Once joined, the mode is kept until the link is lost; after a loss the unit tries the mode it last joined with first. A reboot starts in 11n again. The `WIFI_PHY` status topic reports the mode the unit joined with, so a fallback shows in Home Assistant.

## MQTT ([support.h](src/support.h))
The program uses the MQTT client library [PubSubClient3](https://github.com/hmueller01/pubsubclient3) from Holger Müller (hmueller01), originally written by Nick O'Leary (knolleary).
If you are not familiar with MQTT you find on the Internet endless numbers of descriptions and tutorials. My favorites are [here](https://www.hivemq.com/blog/how-to-get-started-with-mqtt/) and [here](https://www.heise.de/developer/artikel/Kommunikation-ueber-MQTT-3238975.html).
I recommend [MQTT Explorer](http://mqtt-explorer.com/) a great all-round MQTT client that provides a structured topic overview for the first steps.

### MQTT General Settings (broker, port, account data)
Adapt the server (broker) name and the port if needed:

```cpp
#define MQTT_SERVER "MQTT broker name"  // broker name or IP address of the broker
#define MQTT_PORT 1883                  // port number used by the broker
```

If you want to use MQTT authentication enter user name and password:
```cpp
#define MQTT_USER ""          // if authentication is not used, leave it empty
#define MQTT_PASSWORD ""      // if authentication is not used, leave it empty
```
Note: TLS/SSL is not supported

While the broker cannot be reached, a connection attempt is made at most every 5 seconds. After ten failed attempts in a row, about 50 seconds, Wi-Fi is reset once as a workaround for [esp8266/Arduino#7432](https://github.com/esp8266/Arduino/issues/7432). A broker restart of a few seconds therefore no longer drops Wi-Fi.

The following sections show the configuration for the MQTT paths.

### MQTT status
The topic level for status information from the AC consists of the `MQTT_PREFIX` and the function name separated by a slash, e.g. `MHI-AC-Ctrl/Power`.
For writes the topic level consists of the `MQTT_PREFIX`, the prefix for set commands (`MQTT_SET_PREFIX`) and the function name, each separated by a slash, e.g. `MHI-AC-Ctrl/set/Power`.
You can change `MQTT_PREFIX` and `MQTT_SET_PREFIX`, default is
```cpp
#define MQTT_PREFIX HOSTNAME "/"           // basic prefix used for publishing AC data
#define MQTT_SET_PREFIX MQTT_PREFIX "set/" // prefix for subscribing set commands
```
Please pay attention to the case sensitivity.

The following status data is available (prefix is not listed).
They are only published when there is a change of the message. The retained flag is `true`.
When writing data, the retain flag shall be `false`!

topic|r/w|value|comment
-----|---|-----|------
Power|r/w|"On", "Off"|Not writable when [POWERON_WHEN_CHANGING_MODE](#behaviour-when-changing-ac-mode-supporth) is selected: `set/Power` then answers `unknown command`, switch off with `set/Mode` "Off" instead.
Mode|r/w|"Auto", "Dry", "Cool", "Fan", "Heat" and "Off"|"Off" is only supported when option [POWERON_WHEN_CHANGING_MODE](#behaviour-when-changing-ac-mode-supporth) is selected. `ErrOpData/Mode` publishes "Stop" in place of "Auto".
Tsetpoint|r/w|18 ... 30|Target room temperature (float) in °C, resolution is 0.5°C
Fan|r/w|1,2,3,4,"Auto"|Fan level
Vanes|r/w|"Up","UpCenter","CenterDown","Down","Swing","?"|Vanes up/down position, top to bottom; writing 1,2,3,4 or 5 (= "Swing") still works <sup>1</sup>
Troom|r/w|above -10, below 48|Room temperature (float) in °C, resolution is 0.25°C <sup>2</sup>
Tds1820|r|-10 ... 48|Temperature (float) by the additional DS18x20 sensor in °C, resolution is 0.5°C; readings outside this range are ignored <sup>3</sup>
Errorcode|r|0 .. 255|error code (unsigned int)
Action|r|"off", "idle", "cooling", "heating", "drying", "fan"|what the AC is doing <sup>5</sup>
Silent|r/w|"On", "Off"|Silent operation of the outdoor unit, read from the AC and settable <sup>6</sup>
Discovery|r|"ok", "modes"|Only with `HA_DISCOVERY`: the Home Assistant discovery configs were published; "modes" means the climate config was skipped because the mode texts are not Home Assistant's, see [Home Assistant discovery](#home-assistant-discovery-supporth)
ErrOpData|w||triggers the reading of last error operating data
VanesLR|r/w|1,2,3,4,5,6,7,"Swing"|Vanes left/right position <sup>4</sup>
3Dauto|r/w|"On", "Off"|3D auto only works for mode Auto, Cool and heat <sup>4</sup>

<sup>1</sup> When the last command was received via the infrared remote control then the Vanes status is unknown and the `?` is published.
<sup>2</sup> Please compare with section [Room temperature](#room-temperature) for writing.
<sup>3</sup> Only available when a DS18x20 is connected, please see the description in [Hardware.md](Hardware.md) and in section [External Temperature Sensor Settings](#external-temperature-sensor-settings-supporth).
<sup>4</sup> Only available if USE_EXTENDED_FRAME_SIZE is enabled in [support.h](src/support.h).
<sup>5</sup> From the outdoor unit state in `DB13`: `idle` while the unit is on but its compressor is stopped, e.g. when the room has reached the setpoint. In auto mode, heating or cooling comes from the outdoor unit too. `fan` in fan mode, `off` while the unit is off. The payloads are Home Assistant's `hvac_action` names, so `action_topic` needs no template.

<sup>6</sup> The state comes from operating-data code `0xDD`, which the AC reports after every SILENT press on the remote and which the firmware also polls once per operating-data cycle. Writing sends the command traced from a ProtoArt controller ([hberntsen PR #42](https://github.com/hberntsen/mhi-ac-ctrl-esp32/pull/42)); the `Silent` topic confirms it within a second or two. Two quirks of the AC: a Silent set from the infrared remote cannot be cleared over `set/Silent` and vice versa, and on a multi-split each indoor unit can hold the shared outdoor unit in Silent.

Additionally, the following program status topics are available:

topic    |r/w| value |comment
---------|---|---|---
cmd_received|r|"o.k.", "unknown command" or "invalid parameter"|feedback for last set command
connected|r  |0, 1|MQTT connection status to broker
fMISO    |r  |unsigned integer|frequency of the MISO pin in Hz during boot
fMOSI    |r  |unsigned integer|frequency of the MOSI pin in Hz during boot
fSCK     |r  |unsigned integer|frequency of the SCK pin in Hz during boot
Wiring   |r  |"o.k." or a pin list|result of the boot-time wiring check, e.g. `MISO` or `SCK,MOSI`. A fault is reported and the unit keeps running so it stays reachable over OTA. After a `MISO` fault the MISO pin stays an input: the AC status is still read, but no commands reach the AC <sup>5</sup>
reset|w|"reset"|resets the ESP8266
RSSI     |r  |integer         |WiFI RSSI / signal Strength in dBm at MQTT (re-)connect and every `TELEMETRY_PERIOD` seconds
Uptime   |r  |integer         |seconds since boot, at MQTT (re-)connect and every `TELEMETRY_PERIOD` seconds; keeps counting past the 49.7-day `millis()` wrap
FreeHeap |r  |integer         |free heap in bytes, at MQTT (re-)connect and every `TELEMETRY_PERIOD` seconds
ResetReason|r|string          |why the ESP8266 last started, at MQTT (re-)connect: `Power On`, `Software/System restart` (also after an OTA flash or `set/reset`), `Hardware Watchdog`, `Software Watchdog`, `Exception`, `External System`
WIFI_BSSID|r |string          |BSSID of the access point in use after MQTT (re-)connect
WIFI_PHY |r  |"11b", "11g", "11n"|802.11 mode the unit joined with, after MQTT (re-)connect. `11n` unless the [PHY mode fallback](#wifi-phy-mode-fallback) had to switch to `11g`
Version  |r  |string          |Short git commit hash the firmware was built from, e.g. `9d8886d`; `-dirty` is appended when the build had uncommitted changes, `unknown` when built without git
WIFI_LOST|r  |integer         |number of lost WiFi connections since last reset; a deliberate change to a stronger AP is not counted
MQTT_LOST|r  |integer         |number of lost MQTT connections since last reset; a change to a stronger AP drops the broker connection and is counted
APs      |r  |string          |Matched APs seen at scan with RSSI value, one message per AP; the topic name is fixed

<sup>5</sup> The frequencies in `fSCK`, `fMOSI` and `fMISO` say what was measured; `Wiring` says whether it was acceptable. Expect SCK above 3000 Hz, MOSI between 30 Hz and the SCK frequency, and MISO at or below 10 Hz.

Note: The topic and the payload text of the status data is adaptable by defines in [MHI-AC-Ctrl.h](src/MHI-AC-Ctrl.h), except `APs`.

`RSSI`, `Uptime` and `FreeHeap` are published at MQTT (re-)connect and then periodically, so a unit can be watched without waiting for a reconnect: a live signal strength, an uptime that shows a reboot, a heap that shows a leak. `ResetReason` is published at connect only, since it does not change.
```cpp
#define TELEMETRY_PERIOD 300   // seconds between the periodic publishes; 0 publishes them at MQTT connect only
```

### MQTT operating data
MHI-AC-Ctrl can provide operating data of the indoor and outdoor unit. This data is not needed for daily use, but might be interesting in specific use cases. Operating data is only published when there is a change of the content. The retained flag is `true`.
The path to the operating data topic can be adapted.

```cpp
#define MQTT_OP_PREFIX "OpData/"    // prefix for publishing operating data
```

Without changes of the path, subscribe to `MHI-AC-Ctrl/OpData/#` for receiving all operating data. Please see section [Operating data](#operating-data-mhi-ac-ctrl-coreh) to find all supported operating data.

Note: The topic and the payload text is adaptable by defines in [MHI-AC-Ctrl.h](src/MHI-AC-Ctrl.h).

### MQTT last error operating data
When an error in the AC occurs, some operating data of this error are stored in the AC and can be read out.
The path to the operating data topic is defined in
```cpp
#define MQTT_ERR_OP_PREFIX "ErrOpData/"    // prefix for publishing operating data from last error
```
The readout of last error operating data is triggered by publishing `ErrOpData` to topic ErrOpData. Not all of the operating data from section [Operating data](#operating-data-mhi-ac-ctrl-coreh) might be available as last error operating data.

Note: The topic and the payload text is adaptable by defines in [MHI-AC-Ctrl.h](src/MHI-AC-Ctrl.h).

### MQTT operating data PROTECTION-NO topic
This is the Protection state number of the compressor (Compressor protection status).
The meaning of this numeric value is:

Value |meaning
------|-----
0  |Normal
1  |Discharge pipe temperature protection control
2  |Discharge pipe temperature anomaly
3  |Current safe control of inverter primary current
4  |High pressure protection control
5  |High pressure anomaly
6  |Low pressure protection control
7  |Low pressure anomaly
8  |Anti-frost prevention control
9  |Current cut
10 |Power transistor protection control
11 |Power transistor anomaly (Overheat)
12 |Compression ratio control
13 |-
14 |Condensation prevention control
15 |Current safe control of inverter secondary current
16 |Stop by compressor rotor lock
17 |Stop by compressor startup failure

## OTA Settings ([support.h](src/support.h))
OTA (Over the Air) update is the process of loading the firmware to ESP module using Wi-Fi connection rather than a serial port.
The OTA hostname can be adapted, per default it is the hostname used by WiFi.
```cpp
#define OTA_HOSTNAME HOSTNAME     // default for the OTA_HOSTNAME is the HOSTNAME
#define OTA_PASSWORD ""           // Enter an OTA password if required
```

## External Temperature Sensor Settings ([support.h](src/support.h))
When an external temperature sensor is connected, you can configure the pin where DQ of the the DS18x20 is connected, default is Pin 4 (D2)
and how often the sensor should be read. To use reading of the external sensor you must adapt the `TEMP_MEASURE_PERIOD`.

```cpp
#define TEMP_MEASURE_PERIOD 0   // period in seconds for temperature measurement with the external DS18x20 temperature sensor
                                // set to e.g. 30 to read the sensor every 30 seconds.
#define ONE_WIRE_BUS 4          // D2, PIN for connecting temperature sensor DS18x20 DQ pin
#define ROOM_TEMP_DS18X20_OFFSET 0.0   // Temperature offset for DS18x20 sensor, can be positive or negative (examples: 0.0, -1.0, 1.5)
```
Note: the according libraries [OneWire](https://www.pjrc.com/teensy/td_libs_OneWire.html) and [DallasTemperature](https://github.com/milesburton/Arduino-Temperature-Control-Library) are only used if `TEMP_MEASURE_PERIOD > 0`.

If the DS18x20 should replace the room temperature sensor of the AC, you have to configure it as described in the next clause.

## Room temperature
Usage of the room temperature sensor inside the AC is the default, but instead you can use the DS18x20 sensor on the MHI-AC-Ctrl board or the received temperature via the MQTT topic `Troom`. Setting `Troom` via MQTT works per default. But you should adapt `ROOM_TEMP_MQTT_TIMEOUT`. For using DS18x20 as `Troom` you have to use `ROOM_TEMP_DS18X20`.
```cpp
//#define ROOM_TEMP_DS18X20           // use room temperature from DS18x20

#define ROOM_TEMP_MQTT_SET_TIMEOUT  40    // time in seconds, after this time w/o receiving a valid room temperature
                                      // via MQTT fallback to IU temperature sensor value
#define TROOM_FILTER_LIMIT 0.25       // A changed Troom is published only when it differs from the last published value by MORE than this.
                                      // With 0.25 a single 0.25°C step is held back and a 0.5°C change is published. Use 0 to publish every step.

```
`ROOM_TEMP_MQTT_SET_TIMEOUT` must be greater than the period of room temperature update via MQTT. E.g. when the room temperature update via MQTT is done every minute, then `ROOM_TEMP_MQTT_SET_TIMEOUT` could be 2 minutes.
If the timeout occurs, and the system falls back to IU temperature, it will return to using the MQTT room temperature if the MQTT messages resume.

## Enhance resolution of `Tsetpoint` ([support.h](src/support.h))
The AC is only accepting a setpoint in x.0 degrees. If you send x.5 degrees, the AC will convert this to (x+1).0 degrees. So using .5 degrees as a setpoint will increase the setpoint on the AC not with .5 degrees but with 1 degree. This behaviour can be changed with:
```cpp
#define ENHANCED_RESOLUTION true                    // when using Tsetpoint with x.5 degrees, airco will use (x+1).0 setpoint
                                                    // uncomment this to compensate (offset) Troom for this.
                                                    // this will simulate .x degrees resolution
```
If you now send x.5 degrees as setpoint, still the setpoint on the AC will be (x+1). But when sending the received `Troom` (from MQTT or the external temperature sensor) to the AC, `Troom` with an offset of .5 degrees will be send to the AC. This way the AC will increase the temperature in the room with .5 degrees instead of 1 degree.

**This behaviour won't work if you are using the internal temperature sensor of the AC!**

The MQTT topic `Troom` will show (like before) the `Troom` received by the AC (including the offset).

For example: when setpoint is 20.5. When `Troom` 19.5 is received (from MQTT or DS18x20), `Troom` sent to the AC will be 20.0. Topic `Troom` will also show 20.0.

## Behaviour when changing AC mode ([support.h](src/support.h))
Per default the power on/off state is not changed, when you change the AC mode (e.g. heat, dry, cold etc.).
But when you uncomment the following line, then the AC is switched on, once you change the AC mode and switched off if you publish `Off` to `Mode` (instead of `Power`). This beahviour is requested for use with [Home Assistant](https://www.home-assistant.io/).
With this option `Mode` also reports the power state: it shows `Off` while the AC is off, and the operating mode only while the AC is on. A mode changed while the AC is off is reported once it switches on.
```cpp
//#define POWERON_WHEN_CHANGING_MODE true           // uncomment it to switch on the AC when the mode (heat, cool, dry etc.) is changed
```

## Using extended frame size for enabling 3D auto and vanes L/R ([support.h](src/support.h))
Per default the 3D auto and vanes left/right is not supported.
But when you uncomment the following line, the frame size is extended to 33 bytes (like the WF-RAC module). This will make it possible to use 3D auto and vanes L/R.
```cpp
//#define USE_EXTENDED_FRAME_SIZE true                // uncomment if you want to use de extended frame size (33) which is used by the WF-RAC module
                                                    // Then it will be possible to get and set the 3D auto and vanes left/right
```

## Passive Mode

A [known limitation](../Troubleshooting.md#known-limitations) is
that the timer of the IR remote control does no longer work
as soon as a device is connected to the SPI port.

There's a workaround for this limitation: toggle the AC from _active mode_ to _passive mode_.
In _active mode_, the default behavior of MHI-AC-Ctrl,
it is possible to both monitor and control the AC while the RC timer is not working.
In _passive mode_, the AC is monitored but cannot be controlled, and the RC timer is working.

It is possible to switch between active and passive mode via MQTT topic `set/PassiveMode`:
publish `On` to enable and `Off` to disable passive mode.
Both commands send an acknowledge value of `o.k.` to topic `cmd_received` on success.

Enabling passive mode does not have immediate effect but involves a delay of ~2 minutes.
Setting `PassiveMode` to `On` while the AC is running causes that after the delay,
the AC reports a value of `1` on topic `Errorcode` and turns off.
Now the AC is in passive mode: it cannot be controlled via MHI-AC-Ctrl anymore.
The `Errorcode` will automatically be reset to `0` as soon as the AC it turned on next time.
The RC timer is working now, i.e. pressing _SLEEP_ enlightens the indoor unit's orange LED.

Publishing `Off` to `set/PassiveMode` enables active mode immediately.
The RC timer is disabled and the AC can be controlled by MHI-AC-Ctrl again.
The AC does not power off when active mode is enabled.

## Finding out what a remote button does

Some remote functions are not decoded (ECO, HI POWER, night setback), and which byte of the AC's status carries them differs per model. Three diagnostic topics turn that into a five-minute test: press the button, watch the topic. The state of the tool and the boot default:

```cpp
#define DIAG_DEFAULT true    // whether diag/frame is on after boot; set/Diag switches it at runtime
```

topic | r/w | value | comment
---|---|---|---
`diag/frame` | r | `DB5 00>10 \| 6c 80 04 …` | the bytes of the AC's status frame that changed since the last publish, old>new, then the whole frame in hex. At most six changed bytes are named; a `+` marks that there were more. At most once a second while `Diag` is `On`; `first \| …` after every MQTT connect. The bytes that change on their own are left out of the compare: the header, the operating-data bytes DB9-DB12 and the request-prefix bits of DB6, the checksum, the frame toggle in DB14. Not retained
`diag/opdata` | r | `dd 80 01 00` | an operating-data answer the firmware does not decode, as DB9 DB10 DB11 DB12. `OpData/unknown` still publishes the same answer as a number. Not retained
`Diag` | r | `On`, `Off` | whether `diag/frame` is published
`set/Diag` | w | `On`, `Off` | switch `diag/frame` at runtime; answers on `cmd_received`
`set/OpDataRequest` | w | four hex digits, e.g. `c021` | ask the AC once for operating-data code `0x21` with request prefix `c0` (indoor) or `40` (outdoor), the same request the built-in codes use, in place of the next code of the normal cycle. The answer arrives on `diag/opdata`, or on the code's own topic if it is a known one and its value changed. `cmd_received` answers `o.k.`, or `invalid parameter` for any other prefix or length

Worked example (16 Sep 2026, `airco/uitkijk/#` captured while pressing the remote): SILENT on and off each produced `OpData/unknown 32989` (`0x80DD`), so Silent is reported as `DB9 = 0xDD`, `DB10 = 0x80`, with the on/off state in `DB11`, which `diag/opdata` now shows. HI/ECO produced no operating data at all; its only trace was `Fan` and the internal setpoint changing. With `diag/frame` running, a press that flips a bit anywhere in the status frame shows up as one line naming the byte.

## Home Assistant discovery ([support.h](src/support.h))

With `HA_DISCOVERY` defined, the unit publishes [MQTT discovery](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery) configs after every MQTT connect, retained, one per `loop()` pass, so Home Assistant creates and updates the entities itself and no YAML is needed. Per unit: a climate (mode, setpoint, room temperature, fan, vane position as swing mode, `Action`), a select for the vane position, a switch for `Silent`, two problem binary sensors (`Errorcode` ≠ 0, `Wiring` ≠ `o.k.`) and five diagnostic sensors (`Uptime`, `FreeHeap`, `RSSI`, `ResetReason`, `WIFI_PHY`), all under one device. Availability comes from `connected`.

Home Assistant's climate accepts only its own mode names, so a discovery build also needs the `PAYLOAD_MODE_*` texts of [Topic and payload text](#topic-and-payload-text-mhi-ac-ctrlh). The firmware checks them at boot: with other texts the climate config is skipped, Serial says so and the retained `Discovery` topic reads `modes` instead of `ok`.

```cpp
#define HA_DISCOVERY true                 // publish the discovery configs
#define HA_DISCOVERY_PREFIX "homeassistant"
#define HA_DEVICE_NAME HOSTNAME           // the device; Home Assistant shows every entity as "<device> <entity name>"
#define HA_CLIMATE_ID HOSTNAME            // unique_id of the climate
#define HA_ID_PREFIX HOSTNAME             // unique_id prefix of the other entities: <prefix>_vanes, _silent, _problem, _wiring, _uptime, _free_heap, _rssi, _reset_reason, _wifi_phy
//#define HA_ENTITY_PREFIX "ac_bedroom"   // optional: gives those entities the IDs select.ac_bedroom_vanes, switch.ac_bedroom_silent, ... (lower case a-z 0-9 _)
#define HA_NAME_VANES "Vanes"             // entity names; likewise HA_NAME_SILENT, _PROBLEM, _WIRING, _UPTIME, _FREE_HEAP, _RSSI, _RESET_REASON, _WIFI_PHY
//#define HA_RESET_REASON_TPL "{{ value }}" // optional value_template of the reset-reason sensor
```

The `unique_id`s never change once entities exist: Home Assistant keys entities by them and keeps their entity IDs, history and automations across firmware updates and renames. A config with a `unique_id` that a YAML entity still uses is rejected as a duplicate, so remove the YAML entity (and reload the MQTT YAML) before the unit's first discovery build connects.

Configs stay retained on the broker after a hostname or prefix change. Remove the old ones by hand, one per component and `unique_id`:

```
mosquitto_pub -h <broker> -r -n -t homeassistant/climate/<old unique_id>/config
```

`tools/discovery_payloads.cpp` renders the payloads a build will publish on your PC (build line in the file), which is handy to check them before flashing.

# Advanced settings

## Topic and payload text ([MHI-AC-Ctrl.h](src/MHI-AC-Ctrl.h))
All topic and payload text is included in defines. To change one, define it in `src/config_defaults.h` rather than editing the header, e.g.
```cpp
#define PAYLOAD_POWER_ON "on"
```
if your framework prefers lower case. These topics and payloads are used for MQTT topics and payloads.

Home Assistant's MQTT climate only accepts its own mode names, so a build for it with `POWERON_WHEN_CHANGING_MODE` also needs
```cpp
#define PAYLOAD_POWER_OFF "off"
#define PAYLOAD_MODE_AUTO "auto"
#define PAYLOAD_MODE_DRY "dry"
#define PAYLOAD_MODE_COOL "cool"
#define PAYLOAD_MODE_FAN "fan_only"
#define PAYLOAD_MODE_HEAT "heat"
```
The `Action` topic already uses Home Assistant's names and can be used as the climate's `action_topic` as it is.

## Operating data ([MHI-AC-Ctrl-core.h](src/MHI-AC-Ctrl-core.h))
Currently the following operating data in double quotes are supported
```cpp
  { 0xc0, 0x02},  //  1 "MODE"
  { 0xc0, 0x05},  //  2 "SET-TEMP" [°C]
  { 0xc0, 0x80},  //  3 "RETURN-AIR" [°C]
  { 0xc0, 0x81},  //  5 "THI-R1" [°C]
  { 0x40, 0x81},  //  6 "THI-R2" [°C]
  { 0xc0, 0x87},  //  7 "THI-R3" [°C]
  { 0xc0, 0x1f},  //  8 "IU-FANSPEED"
  { 0xc0, 0x1e},  // 12 "TOTAL-IU-RUN" [h]
  { 0x40, 0x80},  // 21 "OUTDOOR" [°C]
  { 0x40, 0x82},  // 22 "THO-R1" [°C]
  { 0x40, 0x11},  // 24 "COMP" [Hz]
  { 0x40, 0x85},  // 27 "TD" [°C]
  { 0x40, 0x90},  // 29 "CT" [A]
  { 0x40, 0xb1},  // 32 "TDSH" [°C]
  { 0x40, 0x7c},  // 33 "PROTECTION-No"
  { 0x40, 0x1f},  // 34 "OU-FANSPEED"
  { 0x40, 0x0c},  // 36 "DEFROST"
  { 0x40, 0x1e},  // 37 "TOTAL-COMP-RUN" [h]
  { 0x40, 0x13},  // 38 "OU-EEV" [Puls]
  { 0xc0, 0x94},  //    "energy-used" [kWh]
  { 0xc0, 0xdd},  //    "SILENT" (fork #4): DB11 bit 5, the Silent topic
```

Note 1: If you are not interested in these operating modes (e.g. to reduce the MQTT load) you can comment out the according lines. But at least 1 line has to stay.
For `THI-R2`, `THO-R1` and `TDSH` the formula for calculation is not known yet; `THI-R1` and `THI-R3` use a rough approximation and the `COMP` formula is unconfirmed.
You can find some hints related to the meaning of the operating data [here](https://www.hrponline.co.uk/media/pdf/41/42/ed/Beijer-Ref-Service-Support-Handbook-19cWKESQUhzVIy5.pdf#page=7). Addtional opdata information is available [here](https://github.com/absalom-muc/MHI-AC-Trace/blob/main/SPI.md#operation-data-details).

Note 2: The MQTT topic names are the `TOPIC_*` defines in [MHI-AC-Ctrl.h](src/MHI-AC-Ctrl.h), not the comment text above: `SET-TEMP` is published as `OpData/Tsetpoint`, `energy-used` as `OpData/KWH`, `OU-EEV` as `OpData/OU-EEV1`, `PROTECTION-No` as `OpData/PROTECTION-NO` and `MODE` as `OpData/Mode`. An opcode the program does not know is published on `OpData/unknown`. `OpData/TD` publishes the text `<=30` for values below 41 °C. `SILENT` is published on the status topic `Silent`, not under `OpData/`.

Note 3: The energy-used is the energy in kWh counting from power on the AC. If you power off the AC, the value (in kWh) will keep the last value. When you power on the AC again, it will start from 0 again.

Hint: The error operating data is usually a sub-set of the operating data above. If user requests error operating data, all available error operating data is provided independent from the list above.

## Access Speed ([MHI-AC-Ctrl-core.h](src/MHI-AC-Ctrl-core.h))
Default the above operating data is requested within 400 frames. Because 20 frames takes 1 second, the update interval for all operating data will be  400/20 = 20 seconds. If you disable some operating data (above), the update interval will stay 20 seconds.
With the following parameter you can change this interval of 20 seconds.
```cpp
#define NoFramesPerOpDataCycle 400             // number of frames used for a OpData request cycle; will be 20s (20 frames are 1s)
```
Some operating data (32 -38) will take some time at the AC for processing. So don't decrease this value too much.

Changes to Power, Mode, Tsetpoint, Fan and Vanes wll be written to the AC right away. Above parameter will not influence this.

## Jitter internal temperature sensor ([MHI-AC-Ctrl-core.h](src/MHI-AC-Ctrl-core.h))
If the AC internal temperature sensor is used, the received `Troom` can changes (+/- 0.25 degrees) sometimes several times in a second. This will give a burst of MQTT `Troom` messages.
To avoid this behaviour, the changed received `Troom` will only be published after at least 5 seconds. This is ONLY the case when the AC internal temperature sensor is used.
With the following parameter you can change this minimum interval of 5 seconds.
```cpp
#define minTimeInternalTroom 5000              // minimal time in ms used for Troom internal sensor changes for publishing to avoid jitter
```

This jitter can also be avoided by using the `TROOM_FILTER_LIMIT` as descibed above. But this filter is also used if the temperature is provided by an external temperature sensor or a connected DS18B20. With above it will be also possible to see smaller changes.

## Not switching off AC when MQTT connections fails ([support.h](src/support.h))
Default the module stops communicating with the AC when the MQTT connection get disconnected. After 120 sec the AC will power off because of [this](https://github.com/absalom-muc/MHI-AC-Ctrl/blob/master/Troubleshooting.md#fire-ac-switches-power-off-sometimes).
When using a DS18x20 as room temperature sensor, this can be unwanted behaviour. Also at night or when not at home when this happens, can be unwanted behaviour.
This behaviour can be changed by changing the following line:
```cpp
//#define CONTINUE_WITHOUT_MQTT true
```
to
```cpp
#define CONTINUE_WITHOUT_MQTT true
```

Warning: be aware that there might be some safety implication and that the deactivation of this feature is on your own risk.
The AC now keeps running and no control is possible anymore when MQTT is disconnected. Also of course no MQTT topics are updated anymore. Of course control is still possible with the remote control.

Consider it when the broker or Home Assistant is restarted from time to time, for updates or a host reboot: without it, an outage of 120 seconds or more switches the AC off. With it, the AC keeps its last setting until MQTT is back, and its current state is published again after the reconnect.

## MHI-AC-Ctrl partitioning
MHI-AC-Ctrl-core implements the core functions (SPI read/write, communication with the wrapper).
Wifi, MQTT, OTA and DS18x20 stuff is located in `support.h` and `support.cpp`.
`main.cpp` and `MHI-AC-Ctrl.h` contain the wrapper for `MHI-AC-Ctrl-core.cpp` and `support.cpp`.
`lib/mhi_pure` holds the logic that needs neither Arduino nor hardware - the frame checksums and the room temperature conversions - so it can be tested on the build machine with `pio test -e native`.

### `MHI-AC-Ctrl-core.h` and `MHI-AC-Ctrl-core.cpp`
Usually it should be not touched, only configured via [MHI-AC-Ctrl-core.h](src/MHI-AC-Ctrl-core.h).
AC status information change will trigger the callback function `cbiStatusFunction` located in [main.cpp](src/main.cpp)
It is controlled via the functions:
```cpp
void init(bool drive_miso = true);    // initialization called once after boot
void reset_old_values();              // resets the 'old' variables ensuring that all status information are resend
int loop(uint max_time_ms);           // receive / transmit a frame of 20 bytes
void set_power(boolean power);        // power on/off the AC
void set_mode(ACMode mode);           // change AC mode (e.g. heat, dry, cool etc.)
void set_tsetpoint(uint tsetpoint);   // set the target temperature of the AC)
void set_fan(uint fan);               // set the requested fan speed
void set_vanes(uint vanes);           // set the vanes horizontal position (or swing)
void set_troom(byte temperature);     // set the room temperature used by AC
void set_passive_mode(bool mode);     // enable or disable passive mode
void request_ErrOpData();             // request that the AC provides the error data
```
The following sections describe the usage of these functions.

#### `void init(bool drive_miso = true)`
Configures the input /output state of the SPI pins. Resets old values. Pass `false` when the boot-time wiring check found a signal on MISO: the pin then stays an input, so the ESP8266 never drives against it. Frames are still received, but none are sent.

### `reset_old_values()`
This should be called if you want to ensure that the receiver of the status data has the latest data. E.g. in case of a MQTT broker disconnect it should be called.

### `int loop(uint max_time_ms)`
For receiving / transmitting a frame of 20 bytes.
The input parameter is the maximum time which should be consumed by the loop function. Use a value > T<sub>Frame</sub> + T<sub>FramePause</sub> to ensure there is sufficient time to receive a frame.

This is a blocking function which takes - dependent on the AC model - about 10 ... 50ms. Inside the loop function no delay() or yield() call is used.
The following return values are supported:

return value|meaning
------------|-------------
err_msg_valid_frame     |a valid frame was received in the given time
err_msg_invalid_signature | a frame with invalid signature bytes was received
err_msg_invalid_checksum | a frame with an invalid checksum was received
err_msg_timeout_SCK_low | the specified time max_time_ms has been exceeded because SCK is const low and not toggling
err_msg_timeout_SCK_high | the specified time max_time_ms has been exceeded because SCK is const high and not toggling

Note: The input parameters and return values could be changed in future.

### `set_*()`
Controls the AC.

### `request_ErrOpData()`
The error operating data will be read upon a request via this function.

### `support.*`
Provides the interface between MHI-AC-Ctrl and the user interfaces.
It contains helper functions for serving WiFi, MQTT, OTA and the external temperature sensor DS18x20.

### `MHI-AC-Ctrl.h` and `main.cpp`
This is the wrapper for MHI-AC-Ctrl-core and support.
It provides beside the standard `setup()` and `loop()` functions the following two functions.

#### `void cbiStatusFunction(ACStatus status, int value)`
This is a member of the class `StatusHandler : public CallbackInterface_Status`. It is a callback function called by MHI-AC-Ctrl-core in case of AC status changes.

#### `void MQTT_subscribe_callback(char* topic, byte* payload, unsigned int length)`
This function is called for incoming MQTT messages, the message is analyzed and translated to function calls.
From systematic point of view this function should actually be located in [support.h](src/support.h) but in order to keep it simple it resides in [main.cpp](src/main.cpp).

# Integration examples
You find here some examples for integration of MHI-AC-Ctrl
- [Node-Red / Google Assistant](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/60)
- [openHAB](https://community.openhab.org/t/control-mhi-aircon-by-mqtt/104972)
- [IoT MQTT Panel](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/59)
- [Home Assistant](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/23)
- [Home Assistant with ESPHome](https://github.com/ginkage/MHI-AC-Ctrl-ESPHome)
- [Tasmota](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/13#issuecomment-630425714)
- [ioBroker](https://forum.iobroker.net/topic/17041/anfrage-airconwithme-intesishome-klimasteuerung-adapter/14)
- [FHEM](https://forum.fhem.de/index.php/topic,88841.0/all.html)
- [WiFi SSID, hostname and MQTT server dynamic](https://github.com/absalom-muc/MHI-AC-Ctrl/pull/69)
- [Web page with MQTT](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/141)
- [Display with AC controller](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/173)
