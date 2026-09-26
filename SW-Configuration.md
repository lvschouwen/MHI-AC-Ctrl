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

### Rescue access point
Wi-Fi credentials are compiled in, so a wrong value, a replaced router or a new password would take a unit off the network for good. With `RESCUE_AP_PASSWORD` defined (8..63 characters), a unit that has had no Wi-Fi link for `RESCUE_AP_AFTER_MIN` (15) minutes opens its own WPA2 access point `RESCUE_AP_SSID` (default `<HOSTNAME>-rescue`) for `RESCUE_AP_MIN` (10) minutes, then tries the normal join again, and repeats while there is no link. The access point serves only OTA, at 192.168.4.1 with the usual `OTA_PASSWORD`; while a station is connected to it, it stays up. The station is off while the access point is up. The AC keeps running throughout (MQTT is already down at that point); upload an image with the corrected credentials as described in [Troubleshooting](Troubleshooting.md#fire-unit-stopped-joining-wifi-after-a-router-change). Without `RESCUE_AP_PASSWORD` there is no access point: it is never open.
```cpp
//#define RESCUE_AP_PASSWORD "..."      // 8..63 characters; defining it turns the rescue access point on
#define RESCUE_AP_SSID HOSTNAME "-rescue"
#define RESCUE_AP_AFTER_MIN 15          // long enough for a router reboot and the 11g fallback
#define RESCUE_AP_MIN 10
```

## MQTT ([support.h](src/support.h))
The program uses the MQTT client library [PubSubClient3](https://github.com/hmueller01/pubsubclient3) from Holger Müller (hmueller01), originally written by Nick O'Leary (knolleary). Unfamiliar with MQTT? [HiveMQ's intro](https://www.hivemq.com/blog/how-to-get-started-with-mqtt/) and a client such as [MQTT Explorer](http://mqtt-explorer.com/) cover the basics.

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
Tsetpoint|r/w|10 ... 30 in heat, 18 ... 30 otherwise|Target room temperature (float) in °C, resolution is 0.5°C; a value off the 0.5 step, or anything but a number, is refused. Heat accepts down to 10 °C; every other mode refuses below 18, and a change from heat to another mode with a setpoint below 18 writes 18 with it. The AC itself does not heat below 18, so a heat target below 18 is reached with a shifted room temperature (see [Heating below 18 °C](#heating-below-18-c-supporth-fork-30)); `Tsetpoint` then shows that target
Fan|r/w|1,2,3,4,"Auto"|Fan level; define PAYLOAD_FAN_1..PAYLOAD_FAN_4 for named levels (default "1".."4", unchanged on the wire)
Vanes|r/w|"Up","UpCenter","CenterDown","Down","Swing"|Vanes up/down position, top to bottom, also after a change on the infrared remote; writing 1,2,3,4 or 5 (= "Swing") still works <sup>1</sup>; define `PAYLOAD_VANES_1` .. `PAYLOAD_VANES_4` as `"1"` .. `"4"` in `config_defaults.h` to keep v2.8's texts
Troom|r/w|above -10, below 48|Room temperature (float) in °C, resolution is 0.25°C; anything but a number is refused <sup>2</sup>
TroomExternal|r|"On", "Off"|"On" while a `set/Troom` value is the AC's room temperature, "Off" once it fell back to its own sensor <sup>2</sup>
Cleaning|r|"On", "Off"|"On" while the remote's ALLERGEN CLEAR runs (1.5 h, the unit reads off with its mode on fan). Not seen when it was started from fan mode, or before the ESP8266 booted
Tds1820|r|-10 ... 48|Temperature (float) by the additional DS18x20 sensor in °C, resolution is 0.5°C; readings outside this range are ignored <sup>3</sup>
Errorcode|r|0 .. 255|error code (unsigned int), 0 when there is none; what a code means is in [Error codes](#error-codes)
Action|r|"off", "idle", "cooling", "heating", "drying", "fan"|what the AC is doing <sup>5</sup>
Silent|r/w|"On", "Off"|Silent operation of the outdoor unit, read from the AC and settable <sup>6</sup>
Remote|r|"On", "Off"|"On" when the last change came from the infrared remote, "Off" once the controller wrote anything <sup>7</sup>
Discovery|r|"ok", "modes", "skipped"|Only with `HA_DISCOVERY`: the Home Assistant discovery configs were published; "modes" means the climate config was skipped because the mode texts are not Home Assistant's; "skipped" means a config did not fit its buffer and was not published: after the unit rows, and after the outdoor rows when one of those did not fit. See [Home Assistant discovery](#home-assistant-discovery-supporth)
Group|r|0, 1, 2, 3|this unit's part in the outdoor election: `0` member, `1` publisher, `2` its outdoor ID differs from the group's, `3` a unit with another group protocol version leads the group; see [Several indoor units on one outdoor unit](#several-indoor-units-on-one-outdoor-unit)
ErrOpData|w||triggers the reading of last error operating data
VanesLR|r/w|"Left","LeftCenter","Center","CenterRight","Right","Wide","Spot","Swing"|Vanes left/right position, as seen on the unit: 1 leftmost .. 7 spot; writing 1..7 or 8 (="Swing") still works; define `PAYLOAD_VANESLR_1`..`PAYLOAD_VANESLR_7` as `"1"`..`"7"` to keep the numeric texts <sup>4</sup>
3Dauto|r/w|"On", "Off"|3D auto only works for mode Auto, Cool and heat; choosing a left/right louver position leaves it on <sup>4</sup>

<sup>1</sup> The remote reports its vane setting in the same bits a write uses (`DB1 & 0x30`, swing `DB0 & 0x40`) and leaves out only the set flags a write adds; measured on an SRK20ZS-WF (fork #38). v2.8 published `?` after a remote press; the text `?` (`PAYLOAD_VANES_UNKNOWN`) stays in Home Assistant's option lists for older configurations but is no longer published.
<sup>2</sup> Please compare with section [Room temperature](#room-temperature) for writing.
<sup>3</sup> Only available when a DS18x20 is connected, please see the description in [Hardware.md](Hardware.md) and in section [External Temperature Sensor Settings](#external-temperature-sensor-settings-supporth).
<sup>4</sup> Only available if USE_EXTENDED_FRAME_SIZE is enabled in [support.h](src/support.h).
<sup>5</sup> From the outdoor unit state in `DB13`: `idle` while the unit is on but its compressor is stopped, e.g. when the room has reached the setpoint. In auto mode, heating or cooling comes from the outdoor unit too. `fan` in fan mode, `off` while the unit is off. The payloads are Home Assistant's `hvac_action` names, so `action_topic` needs no template.

<sup>6</sup> The state comes from operating-data code `0xDD`, which the AC reports after every SILENT press on the remote and which the firmware also polls once per operating-data cycle. Writing sends the command traced from a ProtoArt controller ([hberntsen PR #42](https://github.com/hberntsen/mhi-ac-ctrl-esp32/pull/42)); the `Silent` topic confirms it within a second or two. Two quirks of the AC: a Silent set from the infrared remote cannot be cleared over `set/Silent` and vice versa, and on a multi-split each indoor unit can hold the shared outdoor unit in Silent.

<sup>7</sup> After a write over SPI the AC echoes a set flag per field in its status frame (`DB0` 0x80/0x20/0x02, `DB1` 0x80/0x08, `DB2` 0x80, with the extended frame `DB16` 0x10 and `DB17` 0x0a) until the remote is used; one remote press clears them all. `Remote` is "On" while none is set, so it also reads "On" after the AC lost mains power, before the controller wrote anything. HI POWER and ECO are not on the bus; in Home Assistant, `OpData/IU-FANSPEED` 8 while `Remote` is "On" means HI POWER, and `OpData/Tsetpoint` 0.5 above the setpoint with fan 1 means ECO (fork #39).

Additionally, the following program status topics are available:

topic    |r/w| value |comment
---------|---|---|---
cmd_received|r|"o.k.", "unknown command" or "invalid parameter"|feedback for last set command
connected|r  |0, 1|MQTT connection status to broker
fMISO    |r  |unsigned integer|frequency of the MISO pin in Hz during boot
fMOSI    |r  |unsigned integer|frequency of the MOSI pin in Hz during boot
fSCK     |r  |unsigned integer|frequency of the SCK pin in Hz during boot
Wiring   |r  |"o.k." or a pin list|result of the boot-time wiring check, e.g. `MISO` or `SCK,MOSI`. A fault is reported and the unit keeps running so it stays reachable over OTA. After a `MISO` fault the MISO pin stays an input: the AC status is still read, but no commands reach the AC <sup>5</sup>
reset|w|"reset", "crash"|"reset" restarts the ESP8266; "crash" (only in a build with `RESET_CRASH_COMMAND`, never in production) raises one deliberate exception (reset reason 2), the proof of [crash-loop safe mode](#crash-loop-safe-mode). Never send it retained, see there
RSSI     |r  |integer         |WiFI RSSI / signal Strength in dBm at MQTT (re-)connect and every `TELEMETRY_PERIOD` seconds
Uptime   |r  |integer         |seconds since boot, at MQTT (re-)connect and every `TELEMETRY_PERIOD` seconds; keeps counting past the 49.7-day `millis()` wrap
FreeHeap |r  |integer         |free heap in bytes, at MQTT (re-)connect and every `TELEMETRY_PERIOD` seconds
FrameErrors|r  |integer         |frames rejected for a bad signature or checksum since boot, at MQTT (re-)connect and, when it changed, at the next `TELEMETRY_PERIOD` tick; saturates, never wraps
FrameTimeouts|r|integer         |SCK timeouts since boot, same publishing rhythm; typically only at boot
ResetReason|r|string          |why the ESP8266 last started, at MQTT (re-)connect: `Power On`, `Software/System restart` (also after an OTA flash or `set/reset`), `Hardware Watchdog`, `Software Watchdog`, `Exception`, `External System`
SafeMode |r  |integer         |boots into [crash-loop safe mode](#crash-loop-safe-mode) since power-on, at MQTT (re-)connect; `0` on a healthy unit
CrashInfo|r  |JSON            |the last crash before this boot, at MQTT (re-)connect: `{"exccause":29,"reason":2,"epc1":"0x4020abcd","excvaddr":"0x00000000"}`, or `{"exccause":-1}` when the boot was not after a crash. `exccause` is the CPU's exception cause and means something only with `reason` 2; `epc1` is the crashing instruction: `xtensa-lx106-elf-addr2line -e firmware.elf <epc1>` names the source line. Also filled after `abort()`, `panic()` or a stack overflow; a safe-mode boot keeps it for the normal boot after it
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
#define TELEMETRY_PERIOD 300   // seconds between the periodic publishes, 1..86400; the group record goes out at the same rhythm, so the build refuses 0
```

### MQTT operating data
MHI-AC-Ctrl can provide operating data of the indoor and outdoor unit. This data is not needed for daily use, but might be interesting in specific use cases. Operating data is only published when there is a change of the content. The retained flag is `true`.
The path to the operating data topic can be adapted.

```cpp
#define MQTT_OP_PREFIX "OpData/"    // prefix for publishing operating data
```

Without changes of the path, subscribe to `MHI-AC-Ctrl/OpData/#` for receiving all operating data. Please see section [Operating data](#operating-data-mhi-ac-ctrl-coreh) to find all supported operating data. (Topic and payload text are adaptable by defines in [MHI-AC-Ctrl.h](src/MHI-AC-Ctrl.h), as noted under [MQTT status](#mqtt-status).)

### Several indoor units on one outdoor unit

On a multi-split every indoor unit reads the same outdoor unit, so eleven operating values are the same on all of them. The indoor units of one outdoor unit elect one of them, the publisher, to write those eleven values under a topic root they share; everything else stays under each unit. A single split needs no configuration: its root is its own `MQTT_PREFIX`, so it elects itself and its topics stay where they are.

```cpp
#define GROUP_ROOT "airco/outdoor/"             // topic root shared by the units of one outdoor unit; default MQTT_PREFIX. 1..64 characters, ends in "/", no + # ;
//#define GROUP_OP_PREFIX GROUP_ROOT "OpData/"  // where the publisher writes the eleven values; without a GROUP_ROOT the default is MQTT_OP_PREFIX
```

Give every unit of one outdoor unit the same `GROUP_ROOT`, and the same `TOPIC_CONNECTED`, `PAYLOAD_CONNECTED_TRUE` and `PAYLOAD_CONNECTED_FALSE`: each unit watches the others' `<prefix>connected`. `GROUP_ROOT` is at most 64 characters so that the largest record message (5 bytes of header, 2 of topic length, the root, `members/`, a 32-character hostname and a 140-byte record: 251 bytes) fits PubSubClient's 256-byte receive buffer, which drops a larger message whole: with a longer root the units would never see each other's records, and two of them could publish at once. The build refuses a longer root, and a `HOSTNAME` longer than 32 characters.

Which values go where, on a multi-split:
- written by the publisher only, under `GROUP_OP_PREFIX`: `OUTDOOR`, `CT`, `COMP`, `DEFROST`, `TOTAL-COMP-RUN`, `PROTECTION-NO`, `TD`, `TDSH`, `THO-R1`, `THI-R2`, `OU-FANSPEED`;
- written by every unit under its own `MQTT_OP_PREFIX`, as before: `RETURN-AIR`, `THI-R1`, `THI-R3`, `IU-FANSPEED`, `TOTAL-IU-RUN`, `Tsetpoint`, `Mode`, `unknown`, `OU-EEV1` (each indoor circuit has its own valve) and `KWH`;
- `ErrOpData/` stays per unit, the eleven included: it is the snapshot the unit read from its own indoor unit.

`KWH` is the outdoor unit's energy counted while *this* indoor unit is on, and it starts from 0 again when this unit is switched on (it follows the integral of `CT` × 230 V only while the unit is on). For the whole outdoor unit's energy, integrate the power, `CT` × 230 V, in Home Assistant.

The election in short:
- Every unit keeps a retained record at `<GROUP_ROOT>members/<HOSTNAME>`: `<proto>;<role>;<term>;<uptime>;<period>;<outdoor_id>;<prefix>`, e.g. `1;1;1;41382;60;ac_outdoor;airco/slaapkamer/`. That is the group protocol version (1), the role (0 member, 1 publisher), the publisher generation, the unit's `Uptime` in seconds, its `TELEMETRY_PERIOD`, its outdoor ID and its `MQTT_PREFIX`. The unit sends it again every `TELEMETRY_PERIOD`, which is why `TELEMETRY_PERIOD` must be 1..86400.
- Each unit publishes its part on `<MQTT_PREFIX>Group`: `0` member, `1` publisher, `2` outdoor ID mismatch, `3` protocol version mismatch.
- After every MQTT connect a unit only listens for 5 s. A publisher that rebooted finds its own record then and carries on without a handover.
- A unit counts as gone when its record has not changed for 3 of its own periods, or when its `connected` has read 0 for 30 s.
- When no publisher has been left for 5 s, the live unit with the lowest hostname takes over with a new generation. A takeover therefore takes 30 s + 5 s after the publisher's `connected` went to 0.
- A live publisher is never replaced, so a unit that comes back stays a member. When two units take over at the same moment, the newer generation keeps the role, and at equal generations the lower hostname. If the broker loses its retained data (restarted without persistence), the units re-elect as at a cold start, so the publisher can change once.
- The live unit with the lowest hostname sets the group's outdoor ID and protocol version. A unit that differs stays out (`Group` `2` or `3`).
- The new publisher writes the eleven values within one operating-data cycle (20 s), and sends the outdoor device's Home Assistant configs 30 s after it took over.

Limits:
- A publisher that stays connected but cannot read its AC keeps the role: the others still see its `connected` 1 and its record changing.
- A dead unit whose retained `connected` still reads 1 (it died while the broker was down, so no will was sent) counts as gone only after 3 of its periods of continuous connection. A unit that reconnects more often than that keeps finding it fresh: if the dead unit has the lowest hostname, or was the publisher, it holds off a takeover for as long as the reconnects go on.
- To remove a unit from the group for good, delete its record: `mosquitto_pub -h <broker> -r -n -t <GROUP_ROOT>members/<HOSTNAME>`.

### MQTT last error operating data
When an error in the AC occurs, some operating data of this error are stored in the AC and can be read out.
The path to the operating data topic is defined in
```cpp
#define MQTT_ERR_OP_PREFIX "ErrOpData/"    // prefix for publishing operating data from last error
```
The readout of last error operating data is triggered by publishing `ErrOpData` to topic ErrOpData. Not all of the operating data from section [Operating data](#operating-data-mhi-ac-ctrl-coreh) might be available as last error operating data.

### Error codes

The `Errorcode` topic carries the AC's own byte, and `OpData/PROTECTION-NO` the compressor-protection number. The firmware publishes the numbers and nothing else: what they mean belongs to whatever reads them, e.g. a Home Assistant template sensor that maps the number to a text. That way a correction needs no flash.

Value |meaning
------|-----
0  |no error
1  |Wired remote control communication error
3  |Indoor-outdoor signal transmission error
5  |Indoor-outdoor signal transmission error
7  |Room temperature sensor fault
9  |Drain fault (float switch or pump)
16 |Indoor fan motor fault
21 |Limit switch fault (air inlet panel)
35 |Cooling high pressure protection
36 |Compressor overheat
37 |Outdoor heat exchanger sensor fault
38 |Outdoor air temperature sensor fault
39 |Discharge pipe temperature sensor fault
40 |Service valve closed or outdoor PCB fault
42 |Current cut (compressor overcurrent)
47 |Active filter voltage error
48 |Outdoor fan motor fault
51 |Power transistor fault
53 |Suction temperature sensor fault
54 |High pressure sensor fault
57 |Refrigerant shortage or service valve closed
58 |Current safe stop (overload)
59 |Outdoor unit fault (compressor, PCB or wiring)
60 |Compressor rotor lock

The meanings come from MHI / Beijer Ref's *Service Support Handbook 2021.11*, "RAC INDICATION & FAULT CODES" (PDF pages 16-17) plus the RAC multisplit table for codes 53 and 54, https://mhi-hvac.co.uk/wp-content/uploads/MHI-Service-Support-Handbook-2021.11-1-1.pdf, cross-checked row for row against the *SRK-ZSP-S Service Support Handbook*, page 14. The same handbook's PAC (FD\*) and KX (VRF) tables give some of these numbers other meanings and are deliberately not used, so a code that is not in the table above is simply not known here.

Note that "byte n means E n" is MHI's numbering taken at face value: nobody, upstream included, has published a captured non-zero code next to the number a unit displayed. One observation supports it: the AC reports `1` exactly when the controller stops answering it for about two minutes ([Passive Mode](#passive-mode)), and E1 is the wired remote control communication error -- which is what this controller is to the AC.

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

The same rule holds here: the topic carries the number, the table above it is the meaning, and nothing in the firmware turns one into the other.

## OpData/ topics and retention

Every `OpData/` topic, like every other status topic, is published retained (`output_P()`, `src/support.cpp`). A retained topic keeps its last value on the broker across a Home Assistant restart: the entity does not go to "unknown", it shows the value it last had until the AC's next report changes it.

## OTA Settings ([support.h](src/support.h))
OTA (Over the Air) update is the process of loading the firmware to ESP module using Wi-Fi connection rather than a serial port.
The OTA hostname can be adapted, per default it is the hostname used by WiFi.
```cpp
#define OTA_HOSTNAME HOSTNAME     // default for the OTA_HOSTNAME is the HOSTNAME
#define OTA_PASSWORD ""           // Enter an OTA password if required
```

## Crash-loop safe mode

OTA is the only way to reach a unit inside an AC without tools. A build that crashes shortly after it connects reboots every few seconds and is never up long enough for an OTA upload, which takes about 15-20 s for a 350 KB image. So the unit counts crashes:
- a boot after a hardware watchdog, an exception or a software watchdog reset counts one up;
- so does a boot after `abort()`, `panic()`, a failed `assert`, a failed `new` or a stack overflow. The SDK reports those as a plain restart (`ResetReason` `Software/System restart`), so the firmware defines the core's crash hook, which sets a crashed bit in the record; the next boot counts the bit once, unless it is a power-on, and every boot clears it;
- any other boot sets the count to 0: power-on, `ESP.restart()` (which is also how an OTA update and `set/reset` end), deep-sleep wake, external reset;
- once the unit has been up for 120 s, it sets the count back to 0. So only crashes within 120 s of a boot count towards a loop.

The count lives in three words of RTC user memory, which survive a reset but not a power loss. They sit at user block 32, right after the 128 bytes where the bootloader keeps an OTA update's command, so an OTA update leaves them alone.

After three such crashes in a row the unit starts in **safe mode**:
- it joins Wi-Fi and serves OTA, and does nothing else: no MQTT, no AC communication (MISO is never driven; the AC runs on its remote meanwhile), no discovery, no DS18x20;
- after 10 minutes it restarts normally. If the fault is still there, three more crashes bring it back: about 11 minutes a cycle, 10 of them reachable over OTA;
- a crash in safe mode counts as a crash, so the unit stays in safe mode.

In safe mode Home Assistant shows the unit unavailable: its will set `connected` to 0 when the crashed session dropped. Afterwards the retained `SafeMode` topic, published at every connect, says how many times the unit entered safe mode since it was powered on; `0` on a healthy unit. It goes back to 0 only at a power-on.

`set/reset` with the payload `crash` raises one deliberate exception, reset reason 2 (`ResetReason` `Exception`). Sent three times, each within 120 s of the unit's boot, it puts the unit in safe mode: the controlled proof that the path works. Every crash is one you send; if safe mode did not engage, the unit would simply boot normally again.

Never send `set/reset` with retain, whatever the payload. The broker hands a retained message over again at every connect: `reset` would restart the unit after every connect, and `crash` would crash it after every normal boot, so safe mode would come back every 11 minutes. To clear one sent retained by mistake, send an empty retained message to the same topic: `mosquitto_pub -h <broker> -r -n -t <MQTT_PREFIX>set/reset`. [Troubleshooting](Troubleshooting.md#fire-the-unit-is-unavailable-for-10-minutes-safe-mode) says how to recognise safe mode and use it.

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
Default: the AC's own room sensor. Instead, use the DS18x20 on the MHI-AC-Ctrl board (`ROOM_TEMP_DS18X20`) or a temperature published to MQTT topic `Troom` (works by default; adapt `ROOM_TEMP_MQTT_SET_TIMEOUT`).
```cpp
//#define ROOM_TEMP_DS18X20           // use room temperature from DS18x20

#define ROOM_TEMP_MQTT_SET_TIMEOUT  300   // time in seconds, after this time w/o receiving a valid room temperature
                                      // via MQTT fallback to IU temperature sensor value
#define TROOM_FILTER_LIMIT 0.25       // A changed Troom is published only when it differs from the last published value by MORE than this.
                                      // With 0.25 a single 0.25°C step is held back and a 0.5°C change is published. Use 0 to publish every step.

```
`ROOM_TEMP_MQTT_SET_TIMEOUT` must be greater than the period of room temperature update via MQTT. E.g. when the room temperature update via MQTT is done every minute, then `ROOM_TEMP_MQTT_SET_TIMEOUT` could be 2 minutes.
If the timeout occurs, and the system falls back to IU temperature, it will return to using the MQTT room temperature if the MQTT messages resume. The default is 300 s (upstream: 40 s), above a Home Assistant automation that repeats the value every minute; `TroomExternal` says which sensor is in use. A value on `set/Troom` is rounded to the nearest 0.25 °C, and while it is in use `Troom` publishes every step: `TROOM_FILTER_LIMIT` is for the AC's own sensor only, so a DS18x20 used as Troom publishes every step too.

## Heating below 18 °C ([support.h](src/support.h), fork #30)
The AC accepts a heat setpoint of 10-17 °C on the bus but clamps it to 18 inside (the remote's NIGHT SETBACK gets below 18 through a state that is not visible on the bus). So `set/Tsetpoint` below 18 in heat writes 18 and remembers the target T, and while a room temperature arrives on `set/Troom` the AC is sent

```
room + (18 - T) + HEAT_SHIFT_OFFSET
#define HEAT_SHIFT_OFFSET 2.0   // °C the AC adds to its own heat setpoint: OpData/Tsetpoint reads 20 at a setpoint of 18
```

The AC then regulates the room to T. `Tsetpoint` shows T and `Troom` the room value that was sent, not the shifted value the AC reports back.
- It needs a room sensor on `set/Troom`, repeated well inside `ROOM_TEMP_MQTT_SET_TIMEOUT`. The AC's own sensor cannot be the base: the AC reports back the room temperature it is sent, and in heat its intake reads several degrees high. Without a fresh value (`TroomExternal` `Off`) the AC heats to 18 on its own sensor and T is kept; the shift resumes with the next value.
- The shift ends, and the `set/Troom` value is dropped at once, when the mode leaves heat, when `set/Tsetpoint` is 18 or more, or when the IR remote sets another temperature.
- T is kept in RAM only: after a reboot the AC reports 18, and the controller (Home Assistant) sends T again.

## Enhance resolution of `Tsetpoint` ([support.h](src/support.h))
The AC is only accepting a setpoint in x.0 degrees. If you send x.5 degrees, the AC will convert this to (x+1).0 degrees. So using .5 degrees as a setpoint will increase the setpoint on the AC not with .5 degrees but with 1 degree. This behaviour can be changed with:
```cpp
#define ENHANCED_RESOLUTION true                    // when using Tsetpoint with x.5 degrees, airco will use (x+1).0 setpoint
                                                    // uncomment this to compensate (offset) Troom for this.
                                                    // this will simulate .x degrees resolution
```
The AC's setpoint stays at (x+1).0, but the `Troom` sent to the AC gets the same .5° offset, so the AC ends up regulating .5° warmer/cooler than its own setpoint suggests — simulating .5° resolution. Example: setpoint 20.5, incoming `Troom` 19.5 (MQTT or DS18x20) is sent to the AC as 20.0, and the `Troom` topic also shows 20.0.

**Requires an external `Troom` source (MQTT or DS18x20) — does not work with the AC's internal temperature sensor.**

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

Connecting to the SPI port [disables the IR remote's timer](Troubleshooting.md#known-limitations). Workaround: `set/PassiveMode` `On`/`Off` toggles between:
- **active** (default): monitor and control the AC, RC timer disabled;
- **passive**: monitor only, RC timer works (e.g. _SLEEP_ lights the indoor unit's orange LED).

Both commands ack `o.k.` on `cmd_received`. Switching to `On` takes effect after a ~2 minute delay: the AC then reports `Errorcode` `1` and turns off — it is now in passive mode and cannot be controlled via MHI-AC-Ctrl; `Errorcode` resets to `0` once the AC is turned on again. Switching to `Off` re-enables active mode immediately, without powering the AC off.

## Finding out what a remote button does

Some remote functions are not decoded (ECO, HI POWER, night setback), and which byte of the AC's status carries them differs per model. Three diagnostic topics turn that into a five-minute test: press the button, watch the topic. The state of the tool and the boot default:

```cpp
#define DIAG_DEFAULT true    // whether diag/frame is on after boot; set/Diag switches it at runtime
```

topic | r/w | value | comment
---|---|---|---
`diag/frame` | r | `DB5 00>10 \| 6c 80 04 …` | the bytes of the AC's status frame that changed since the last publish, old>new, then the whole frame in hex. At most six changed bytes are named; a `+` marks that there were more. At most once a second while `Diag` is `On`; `first \| …` after every MQTT connect. The bytes that change on their own are left out of the compare: the header, DB3 (the raw room temperature, which dithers at a temperature boundary; `Troom` carries the filtered value), the operating-data bytes DB9-DB12 and the request-prefix bits of DB6, the checksum, the frame toggle in DB14. Not retained
`diag/opdata` | r | `dd 80 01 00` | an operating-data answer the firmware does not decode, as DB9 DB10 DB11 DB12. `OpData/unknown` still publishes the same answer as a number. Not retained
`Diag` | r | `On`, `Off` | whether `diag/frame` is published
`set/Diag` | w | `On`, `Off` | switch `diag/frame` at runtime; answers on `cmd_received`
`set/OpDataRequest` | w | four hex digits, e.g. `c021` | ask the AC once for operating-data code `0x21` with request prefix `c0` (indoor) or `40` (outdoor), the same request the built-in codes use, in place of the next code of the normal cycle. The answer arrives on `diag/opdata`, or on the code's own topic if it is a known one and its value changed. `cmd_received` answers `o.k.`, or `invalid parameter` for any other prefix or length

Example: SILENT produced `OpData/unknown 32989` (`0x80DD`), i.e. `DB9 = 0xDD`, `DB10 = 0x80`, on/off in `DB11` — now decoded into the `Silent` topic instead. HI/ECO produce no operating data at all; their only trace is `Fan` and the internal setpoint changing (see `Remote` footnote above). With `diag/frame` running, a press that flips a bit anywhere in the status frame shows up as one line naming the byte.

## Home Assistant discovery ([support.h](src/support.h))

With `HA_DISCOVERY` defined, the unit publishes [MQTT discovery](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery) configs after every MQTT connect, retained, one per `loop()` pass, so Home Assistant creates and updates the entities itself and no YAML is needed. Per unit, under one device:
- a climate (mode, setpoint, room temperature, fan, vane position as swing mode, `Action`, and with `USE_EXTENDED_FRAME_SIZE` the left/right louvers as swing_horizontal mode);
- a select for the vane position, a switch for `Silent`;
- binary sensors: `Problem` (Errorcode != 0), `Wiring`, `Cleaning` (Allergen Clear running, fork #25), `External Troom` (a fresh `set/Troom` is in use, fork #25), `Remote` (fork #39: the `Remote` topic);
- diagnostic sensors: `Uptime`, `FreeHeap`, `RSSI`, `ResetReason`, `WIFI_PHY`, `FrameErrors`, `FrameTimeouts`, `Errorcode`, `Group role` (fork #22), `Crash info` (the last crash's exccause, fork #25), `Run time` (fork #27: the indoor unit's own run hours from `OpData/TOTAL-IU-RUN`, in hours with 100 h steps, the base for a filter-cleaning reminder), `Indoor fan speed` and `Internal setpoint` (fork #39: `OpData/IU-FANSPEED` and `OpData/Tsetpoint`, from which Home Assistant derives the remote's HI POWER and ECO);
- a Restart button (fork #24: sends `set/reset` `reset`, entity category config).

With `USE_EXTENDED_FRAME_SIZE`, also a select for the left/right louvers and a switch for `3Dauto`: **22 entities per unit without that option, 24 with it** (counted from the row table in `lib/mhi_pure/mhi_discovery.h`). The outdoor unit has a device of its own with **six entities** (temperature, current, compressor frequency, defrost, compressor run time, compressor-protection number), linked with `via_device` to the unit that publishes them: only the group's publisher sends these configs, 30 s after it took over, reading the group root's `OpData/` topics and available while the publisher is (see [Several indoor units on one outdoor unit](#several-indoor-units-on-one-outdoor-unit)). There is no energy entity: `KWH` counts per indoor unit. Availability comes from `connected`. `HA_OUTDOOR_DEVICE` is gone: a build that still defines it stops with an error; give the units of one outdoor unit the same `GROUP_ROOT` instead.

Home Assistant's climate accepts only its own mode names, so a discovery build also needs `POWERON_WHEN_CHANGING_MODE` (the climate's `off` mode is `set/Mode off`; the build refuses `HA_DISCOVERY` without the option) and the `PAYLOAD_MODE_*` texts of [Topic and payload text](#topic-and-payload-text-mhi-ac-ctrlh). The `PAYLOAD_ACTION_*` texts must stay Home Assistant's `hvac_action` names as well: the climate reads `Action` without a template. The firmware checks them at boot: with other texts the climate config is skipped, Serial says so and the retained `Discovery` topic reads `modes` instead of `ok`. A config that does not fit its 1024-byte buffer is not published either: Serial says so, and `Discovery` reads `skipped`, after the unit rows, and after the outdoor rows only when one of those did not fit.

```cpp
#define HA_DISCOVERY true                 // publish the discovery configs
#define HA_DISCOVERY_PREFIX "homeassistant"
#define HA_DEVICE_NAME HOSTNAME           // the device; Home Assistant shows every entity as "<device> <entity name>"
#define HA_CLIMATE_ID HOSTNAME            // unique_id of the climate
#define HA_ID_PREFIX HOSTNAME             // unique_id prefix of the other entities: <prefix>_vanes, _silent, _problem, _wiring, _uptime, _free_heap, _rssi, _reset_reason, _wifi_phy, _vanes_lr, _3d_auto, _frame_errors, _frame_timeouts, _error_code, _group_role, _restart, _run_time, _cleaning, _external_troom, _crash_info, _remote, _iu_fan_speed, _internal_setpoint
//#define HA_ENTITY_PREFIX "ac_bedroom"   // optional: pins the entity IDs to climate.ac_bedroom and <domain>.ac_bedroom_<slug of the entity name> (select.ac_bedroom_vanes, sensor.ac_bedroom_free_heap, ...), what Home Assistant derives itself for a device without an area, so an area or a lost registry never changes them (lower case a-z 0-9 _; needs Home Assistant 2025.10 or newer, which knows default_entity_id)
#define HA_NAME_VANES "Vanes"             // entity names; likewise HA_NAME_SILENT, _PROBLEM, _WIRING, _UPTIME, _FREE_HEAP, _RSSI, _RESET_REASON, _WIFI_PHY
//#define HA_RESET_REASON_TPL "{{ value }}" // optional value_template of the reset-reason sensor
//#define HA_OUTDOOR_ID "ac_outdoor"        // the outdoor device's id and unique_id prefix, 1..40 characters; default <slug of GROUP_ROOT>_outdoor, derived at boot so every unit of the group derives the same
#define HA_OUTDOOR_NAME "AC outdoor unit"
//#define HA_OUTDOOR_ENTITY_PREFIX "ac_outdoor"
#define HA_NAME_VANES_LR "Vanes left/right"  // entity names; likewise HA_NAME_3DAUTO, _FRAME_ERRORS, _FRAME_TIMEOUTS, _ERROR_CODE, _GROUP_ROLE, _RESTART, _RUN_TIME, _CLEANING, _REMOTE, _IU_FAN_SPEED, _INTERNAL_SETPOINT, _CRASH_INFO, _TROOM_EXTERNAL, _OU_OUTDOOR, _OU_CT, _OU_COMP, _OU_DEFROST, _OU_COMP_RUN, _OU_PROTECTION
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
You can find some hints related to the meaning of the operating data [here](https://mhi-hvac.co.uk/wp-content/uploads/MHI-Service-Support-Handbook-2021.11-1-1.pdf). Addtional opdata information is available [here](https://github.com/absalom-muc/MHI-AC-Trace/blob/main/SPI.md#operation-data-details).

Note 2: The MQTT topic names are the `TOPIC_*` defines in [MHI-AC-Ctrl.h](src/MHI-AC-Ctrl.h), not the comment text above: `SET-TEMP` is published as `OpData/Tsetpoint`, `energy-used` as `OpData/KWH`, `OU-EEV` as `OpData/OU-EEV1`, `PROTECTION-No` as `OpData/PROTECTION-NO` and `MODE` as `OpData/Mode`. An opcode the program does not know is published on `OpData/unknown`. `OpData/TD` publishes the text `<=30` for values below 41 °C. `SILENT` is published on the status topic `Silent`, not under `OpData/`.

Note 3: The energy-used (`KWH`) is the outdoor unit's energy in kWh counted while this indoor unit is on. It starts from 0 again when this indoor unit is switched on, and it is not the outdoor unit's total when several indoor units share it, see [Several indoor units on one outdoor unit](#several-indoor-units-on-one-outdoor-unit).

Hint: The error operating data is usually a sub-set of the operating data above. If user requests error operating data, all available error operating data is provided independent from the list above.

## Access Speed ([MHI-AC-Ctrl-core.h](src/MHI-AC-Ctrl-core.h))
All operating data above is requested within 400 frames (20 frames/s, so a 20 s update interval); disabling some operating data does not shorten it.
```cpp
#define NoFramesPerOpDataCycle 400             // number of frames used for a OpData request cycle; will be 20s (20 frames are 1s)
```
Codes 32-38 take the AC some time to process, so don't lower this much. `Power`, `Mode`, `Tsetpoint`, `Fan` and `Vanes` writes reach the AC right away regardless of this setting.

## Jitter internal temperature sensor ([MHI-AC-Ctrl-core.h](src/MHI-AC-Ctrl-core.h))
The AC's own temperature sensor can report `Troom` changes (±0.25 °C) several times a second, bursting MQTT messages. Only when that sensor is in use, a changed value is published at most every `minTimeInternalTroom` ms:
```cpp
#define minTimeInternalTroom 5000              // minimal time in ms used for Troom internal sensor changes for publishing to avoid jitter
```
`TROOM_FILTER_LIMIT` (above) also dampens this jitter and additionally applies to an external/DS18B20 sensor, at the cost of hiding smaller changes.

## Not switching off AC when MQTT connections fails ([support.h](src/support.h))
By default the module stops talking to the AC when MQTT disconnects, and the AC [powers off after 120 s](Troubleshooting.md#fire-ac-switches-power-off-sometimes). This can be unwanted, e.g. with a DS18x20 room sensor or while away from home.
```cpp
#define CONTINUE_WITHOUT_MQTT true
```
**Warning**: possible safety implication, enable at your own risk. With it, the AC keeps running and its last setting (but is not controllable, and no MQTT topics update) until MQTT is back, when its state is republished; the remote still works throughout. Useful when the broker or Home Assistant restarts routinely for updates or a host reboot.

## MHI-AC-Ctrl partitioning
MHI-AC-Ctrl-core implements the core functions (SPI read/write, communication with the wrapper).
Wifi, MQTT, OTA and DS18x20 stuff is located in `support.h` and `support.cpp`.
`main.cpp` and `MHI-AC-Ctrl.h` contain the wrapper for `MHI-AC-Ctrl-core.cpp` and `support.cpp`.
`lib/mhi_pure` holds the logic that needs neither Arduino nor hardware - the frame checksums and the room temperature conversions - so it can be tested on the build machine with `pio test -e native`.

### `MHI-AC-Ctrl-core.h` and `MHI-AC-Ctrl-core.cpp`
Configured via [MHI-AC-Ctrl-core.h](src/MHI-AC-Ctrl-core.h), otherwise left alone. An AC status change triggers the callback `cbiStatusFunction` in [main.cpp](src/main.cpp). Controlled via the functions:
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

#### `void init(bool drive_miso = true)`
Configures the input /output state of the SPI pins. Resets old values. Pass `false` when the boot-time wiring check found a signal on MISO: the pin then stays an input, so the ESP8266 never drives against it. Frames are still received, but none are sent.

### `reset_old_values()`
This should be called if you want to ensure that the receiver of the status data has the latest data. E.g. in case of a MQTT broker disconnect it should be called. It calls `reset_system_values()`, which does the same for the eleven values the group's publisher writes; the group also calls that one when a unit becomes the publisher, so each of the eleven goes out at its next reading.

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
Called for incoming MQTT messages: analyzes the message and translates it into function calls. Lives in [main.cpp](src/main.cpp) for simplicity.

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
