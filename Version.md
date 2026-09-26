MHI-AC-Ctrl by absalom-muc

**Adaptions since version 2.8** (not released)
- [Move from unmaintained knolleary/pubsubclient to hmueller01/pubsubclient3](https://github.com/absalom-muc/MHI-AC-Ctrl/pull/212)
- [Introduce a passive mode to allow RC timer](https://github.com/absalom-muc/MHI-AC-Ctrl/pull/220)
- converted to a PlatformIO project with pinned dependencies; `MHI-AC-Ctrl.ino` is now `src/main.cpp` and the Arduino IDE is no longer supported
- configuration can be supplied in the gitignored `src/config_defaults.h` instead of editing `support.h`, so credentials stay out of commits
- frame checksums and the room temperature conversions moved to `lib/mhi_pure`, covered by host tests that run without hardware
- GitHub Actions builds the whole `#ifdef` matrix on every push, runs the host tests and asserts the firmware still fits its flash budget
- a failed boot-time wiring check no longer halts in `while (1)`. It is published to the new `Wiring` topic and the unit keeps running, so it stays reachable over OTA
- the SPI read loop now times out waiting for a rising clock edge, as it already did for a falling one
- MQTT payloads are copied into a bounded buffer instead of being NUL-terminated inside the client's receive buffer
- `output_P()` no longer builds the topic with an off-by-one `strncat_P`, and refuses to publish rather than using an uninitialised topic for an unknown status type
- `mhi_ac_ctrl_core.loop()` returns an error code rather than a call counter that goes negative after ~3.4 years
- a DS18x20 can now report temperatures below 0 °C, and DallasTemperature 4.x fault codes are recognised explicitly instead of being caught by the sanity clamp
- with `POWERON_WHEN_CHANGING_MODE`, `Mode` no longer shows the last operating mode of an AC that is off. After every MQTT reconnect or reboot, v2.8 published it just before `Off`, which Home Assistant recorded as a brief switch-on
- topic and payload text can be set in `src/config_defaults.h` like every other option, so a build using Home Assistant's lower-case mode names no longer means editing `MHI-AC-Ctrl.h`, where a fresh checkout silently reverts the change
- a boot-time MISO fault now leaves MISO an input instead of driving it against whatever else drives the line. The unit keeps WiFi, MQTT, OTA and status reading; the AC gets no frames and enters its error state after about 120 s, as it did during v2.8's reboot loop
- bug sweep (#10): the Wi-Fi scan state gets a deadline instead of waiting forever for a callback the SDK never sends; `WIFI_LOST` and `MQTT_LOST` now count (they were always 0); `set/Tsetpoint` answers on `cmd_received`; `Troom` is re-sent after an MQTT reconnect like every other status; `set/PassiveMode` rejects unknown payloads; `set/Mode` compares the same "off" text the Mode topic publishes; DS18x20 handling re-applies the sensor after an MQTT Troom timeout and re-initialises a replugged sensor in `Tds1820`-only builds
- new status topic `Action` (#16): what the AC is doing (`off`, `idle`, `cooling`, `heating`, `drying`, `fan`), decoded from the outdoor unit state in DB13 as hberntsen/mhi-ac-ctrl-esp32 does, in Home Assistant's `hvac_action` names
- MQTT reconnect attempts are paced to one every 5 s (#15). They used to run on every loop pass, so a broker that refused at once used up the ten failed attempts that reset Wi-Fi within milliseconds, and every broker restart also dropped Wi-Fi
- dependencies refreshed on 2026-09-15: PubSubClient3 3.3.1 (buffer bounds checks, `setBufferSize()` fix, QoS 2 handling this firmware does not use) and the CI actions on their current majors
- WiFi PHY mode fallback (#17): after five minutes without a link the unit tries 802.11g instead of the default 802.11n, and alternates back after another five minutes, so a router that refuses the 11n join ([#224](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/224)) no longer strands it until a USB flash. The new `WIFI_PHY` status topic reports the mode the unit joined with
- periodic telemetry (#18): `RSSI`, the new `Uptime` (seconds, counting past the `millis()` wrap) and `FreeHeap` are published at MQTT connect and every `TELEMETRY_PERIOD` seconds (default 300); the new `ResetReason` says why the ESP8266 last started. A silent watchdog reboot, which used to look like `WIFI_LOST` and `MQTT_LOST` going back to 0, is now visible
- protocol discovery tooling (#4, batch A): `diag/frame` publishes the bytes of the AC's status frame that changed (old>new, then the whole frame), at most once a second while `Diag` is `On`; `diag/opdata` publishes unknown operating data with its value bytes (`OpData/unknown` dropped them); `set/OpDataRequest` asks the AC once for any operating-data code. Together they turn "what does this remote button do" into a five-minute test on the unit
- named vanes, Silent and Home Assistant discovery (#4, batch B): `Vanes` publishes `Up`, `UpCenter`, `CenterDown`, `Down`, `Swing` (or `?`) and `set/Vanes` accepts the names as well as 1-5; the new `Silent` topic follows the AC's silent operation (operating-data code `0xDD`, polled and reported after every remote press) and `set/Silent` sets it, the command traced from a ProtoArt controller by mreijnde (hberntsen PR #42); with `HA_DISCOVERY` the unit publishes Home Assistant discovery configs for a climate, the vane select, the Silent switch, problem/wiring binary sensors and five diagnostic sensors after every MQTT connect, so no YAML is needed. `diag/frame` now ignores the raw room temperature byte, which dithers at a temperature boundary
- discovery entity IDs (#4): with `HA_ENTITY_PREFIX`, every entity's `default_entity_id` is the domain, the prefix and Home Assistant's own slug of the entity name (`climate.<prefix>` for the climate), so the IDs automations read survive an area assignment or a lost registry
- left/right louvers, 3D auto, the outdoor device, frame counters, fan names and error and protection codes (#20, #19, #21): `set_vanesLR()`/`set_3Dauto()` no longer step on each other's set flag; `VanesLR` publishes named positions (`Left`..`Spot`, `Swing`); with `HA_OUTDOOR_DEVICE` the shared outdoor unit gets its own Home Assistant device, linked by `via_device`, reading the publishing unit's own `OpData/` topics; new `FrameErrors`/`FrameTimeouts` counters; `Fan` accepts named levels via `PAYLOAD_FAN_1`..`_4` (default `"1"`..`"4"`, unchanged on the wire); Home Assistant sensors for the `Errorcode` and `OpData/PROTECTION-NO` numbers, whose meanings are now tabulated in `SW-Configuration.md` (the firmware publishes numbers only)
- the outdoor election, crash-loop safe mode and the ride-alongs (#22, #23, #24): the indoor units of one outdoor unit share a `GROUP_ROOT` and elect, over retained records under `<GROUP_ROOT>members/`, the one that writes the eleven outdoor values under the group root and sends the outdoor device's Home Assistant configs; another unit takes over when it drops out, and the per-unit `Group` topic and a diagnostic sensor show each unit's part. `HA_OUTDOOR_DEVICE` is gone, `KWH` stays per unit (the outdoor device has no energy entity any more) and `TELEMETRY_PERIOD` must be 1..86400. Three crashes in a row within 120 s of boot start safe mode, Wi-Fi and OTA only for 10 minutes, counting `abort()`, `panic()`, failed asserts and stack overflows through the core's crash hook; the new `SafeMode` topic counts the entries since power-on, and `set/reset` `crash` proves the path. A Restart button in Home Assistant; `Discovery` reads `skipped` when a config did not fit its buffer
- the outdoor device stays available while any unit of the group is connected (#29): its six configs list every unit's `connected` (`avty_mode` `any`) and name the lowest hostname as `via_device`, so every unit sends the same payload and a publisher's reboot or a takeover changes nothing in Home Assistant; the 30 s config delay and the re-send after a beaten claim are gone, and `Group` reads `0` from the moment a unit connects
- vanes after the IR remote and remote use (#38, #39): `Vanes` shows the up/down setting after a remote press too (the remote reports it in the same bits a write uses; only the echoed set flags are missing), so `?` is no longer published; the new `Remote` topic is `On` when no set flag is echoed, i.e. the remote made the last change, with a Home Assistant binary sensor; discovery also adds diagnostic sensors for the unit's `OpData/IU-FANSPEED` and `OpData/Tsetpoint`, from which Home Assistant derives the remote's HI POWER and ECO
- heating below 18 °C (#30): the AC clamps a heat setpoint below 18 to 18, so a heat target below 18 keeps the setpoint at 18 and sends the AC the `set/Troom` room temperature shifted by (18 − target) + `HEAT_SHIFT_OFFSET`; `Tsetpoint` shows the target and `Troom` the room
- sweep (#33): `set/Troom` and `set/Tsetpoint` refuse anything but a number (a junk `set/Troom` was sent to the AC as a room of 0 °C), and `set/Tsetpoint` refuses values off the 0.5 step; `TOTAL-IU-RUN` publishes a counter of 0 too; `Tsetpoint` goes out once, not twice, when the IR remote ends a heat shift; a DS18x20 used as Troom publishes every step; debug texts moved to flash (~0.6 KB RAM)

**v2.8** (September 2023)
- when ds18x20 used and get disconnected, fallback to  IU temperature sensor by [glsf91](https://github.com/glsf91)
- added status and control 3D auto and vanes L/R (extended frame size (33) used like WF-RAC module). You have to enable this! [3d auto and vanes l/r seems possible over CNS #77](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/77) by [glsf91](https://github.com/glsf91) 
- added SRK35ZC-S to unsupported list [Addition to unsupported list #154](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/154) by [glsf91](https://github.com/glsf91)

**v2.7R4** (April 2023)
- changed setup WiFi connection to async; module starts already communicating with AC during WiFi setup and also during scanning when WiFI_SEARCHStrongestAP is used by [glsf91](https://github.com/glsf91)
- added CONTINUE_WITHOUT_MQTT; module keeps communicating with AC if MQTT is disconnected. See also description in SW-Configuration.md and [Question: why mhi_ac_ctrl_core.loop only when MQTT connected? #144](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/144) by [glsf91](https://github.com/glsf91)
- fix bug for publish list of access points by [glsf91](https://github.com/glsf91)
- added SRK71ZEA-S1 to unsupported list [Addition to unsupported list #143](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/143) by [glsf91](https://github.com/glsf91)
- added Web page [Webpage for accessing MQTT data #141](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/141) to integration list by [glsf91](https://github.com/glsf91)

**v2.7R3** (March 2023)
-  fix some compiler warnings by [glsf91](https://github.com/glsf91)
-  change sending MISO with faster refresh of data every 20s by [glsf91](https://github.com/glsf91)
-  avoid jitter with internal temperature sensor, now updating atmost every 5s by [glsf91](https://github.com/glsf91)
-  fix print wifi encryption type by [glsf91](https://github.com/glsf91)
-  skip not usable values for DB18B20 by [glsf91](https://github.com/glsf91)

**v2.7R2** (March 2023)
-  Added energy kWh from airco by [glsf91](https://github.com/glsf91).

**v2.7R1** (February 2023)
-  Fix for fan not showing 4 and Auto after powerdown AC [pull request #132](https://github.com/absalom-muc/MHI-AC-Ctrl/pull/132#) according to [issue #99](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/99#issuecomment-1407615341) by [glsf91](https://github.com/glsf91).

**v2.6** (January 2023)
- Fix of [Wifi reconnect takes too long #125](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/125)
- Typo related to the unit of TD fixed in MHI-AC-Ctrl-core.h and SW-Configuration.md

**v2.6R5** (December 2022, draft version)
- Added ROOM_TEMP_DS18X20_OFFSET for issue [Adding offset for external temperature sensor #119](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/119)

**v2.6R4** (December 2022, draft version)
- Added Enhance resolution of Tsetpoint according to [pull request #123](https://github.com/absalom-muc/MHI-AC-Ctrl/pull/123) by [glsf91](https://github.com/glsf91).

**v2.6R3** (December 2022, draft version)
- Added switch for Troom filtering to address [issue #82](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/82).

**v2.6R2** (August 2022, draft version)
- final implementation for fan control incl. 'Auto', see [issue](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/99). This is a breaking change, because in the past only the values 1, 2 , 3, 4 were supported for fan status and fan control. Now "Auto" was added.
- added temporary operating data "unknwon" to find out which functions are supported by SPI dependent on the indoor AC unit.

**v2.6R1** (August 2022, draft version)
- new implementation for fan control incl. 'Auto', see [issue](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/99)

**v2.5** (August 2022)
- release w/o changes


**v2.5R3** (June 2022, draft version)
- moved the functions for frequency measurement from MHI-AC-Ctrl.ino to support.cpp
- moved some MQTT init handling from MHI-AC-Ctrl.ino void setup() to MQTTloop()
- moved long previousMillis = millis to inside void loop() as static
- added parameter WiFI_SEARCH_FOR_STRONGER_AP_INTERVALL
- Reworked WiFi network re-scan function
- added work around for "not updating WiFi.status()", see https://github.com/esp8266/Arduino/issues/7432
- ICACHE_RAM_ATTR is deprecated, replaced by IRAM_ATTR
- removed switch ROOM_TEMP_MQTT in support.h, a Troom MQTT set command is always considered  


**v2.5R21** (December 2021, draft version)
- calculation of RETURN-AIR in MHI-AC-Ctrl.ino corrected
- Removed the option #define ROOM_TEMP_IU for adaption by the user in support.h. Now it is generated automatically when ROOM_TEMP_DS18X20 and ROOM_TEMP_MQTT are not defined
- Reworked WiFi network re-scan function
- increased resolution of Tsetpoint to 0.5°C

**v2.5R1** (November 2021, draft version)
- Version number now located in support.h, published once MQTT connection is available

**Versioning changed**

Released versions are availabe on the [release page](https://github.com/absalom-muc/MHI-AC-Ctrl/releases)
All other versions are draft. A new version ends with "R1", "R2"-postfix etc. before it is released w/o "R"-postfix, e.g. v2.5R1 => v2.5R2 => v2.5
For documentation updates usually no new version number is created.

**v2.3** (September 2021, draft version)
- for MHI_AC_Ctrl_Core::loop return value err_msg_timeout replaced by the more detailed return values err_msg_timeout_SCK_low and err_msg_timeout_SCK_high added
- output of the number of WiFi and MQTT lost since last reset
- output of the BSSID used for the WiFi connection
- use of the strongest WiFi access point (testing incomplete)
- option added to use the room temperature from DS18x20 sensor or from MQTT topic instead of AC IU sensor

**v2.2** (February 2021)
- PCB layout update to v2.2 allows to plug-in MHI-AC-Ctrl directly into the AC without using a cable

**v2.03** (July 2020)
- Functionality for POWERON_WHEN_CHANGING_MODE improved in MHI-AC-Ctrl.ino
- WiFi mode restricted to STA (no more AP) in support.cpp

**v2.02** (June 2020)
- Functionality for POWERON_WHEN_CHANGING_MODE enhanced, the according #define shifted to support.h

**v2.01** (June 2020)
- No functional changes
- Further reduction of memory usage
- Compiler warnings removed
- set-mechanism reworked
- SCK by SCK_PIN replaced according to [pull request 19](https://github.com/absalom-muc/MHI-AC-Ctrl/pull/19)
- Shift void MeasureFrequency() from support.cpp to MHI-AC-Ctrl-core.cpp

**v2.0** (June 2020)
- code refactored to allow simplified adaption for other frameworks (e.g. Tasmota, Home Assistant), main changes
	- MHI-AC-Ctrl-core separated according to [Issue #13](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/13)
	- Changeable defs for all topics and payload according to this [Pull request](https://github.com/absalom-muc/MHI-AC-Ctrl/pull/15)
- Option added to switch on the AC when the mode is changed according to [Issue #14](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/14)
- Added operating data THO-R1, THO-R2, THO-R3, THO-R1 and TD added
- Handling for operating data "PROTECTION-No" and "CT" corrected
- Output of the frame raw data is no longer supported
- MQTT.md replaced by [SW-Configuration.md](SW-Configuration.md)

**v1.4** (April 2020)
- Vanes MQTT status corrected
- Frequency measurement limits adapted, ESP is halted only when there is a toggle on MISO detected, results of the measurement will be published via MQTT
- some [operating data](https://github.com/absalom-muc/MHI-AC-Ctrl/blob/master/MQTT.md#mqtt-topics-related-to-operating-data) added (MQTT path changed)
- [error operating data](https://github.com/absalom-muc/MHI-AC-Ctrl/blob/master/MQTT.md#mqtt-topics-related-to-error-data) added
- reset via MQTT for ESP8266 added
- support of an external DS18x20

**v1.3** (March 2020)
- MHI-AC-Ctrl.h added
- Support of Mitsubishi's ZSK series by acceptance of additionally 0x6d as the first MOSI signature byte (SB0) according to [this issue](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/6).
- MQTT authentication support added
- AC operating data are supported
- Outdoor temperature MQTT topic is shifted to operating data
- Frequency measurement of the signals to check correct connection of MHI-AC-Ctrl board to AC
- MQTT topic connection values changed from false/true to 0/1 to be aligned with the documentation

**v1.2** (January 2020)

- Mode setting corrected
- Vanes is now only published if DB0[7]=1 or DB1[7]=1, else last vanes change was via IR-RC (so not visible via SPI)
- Options for vanes MQTT values adapted, it is now 1,2,3,4,Swing (before it was 1..5)
- Data types of some variables adapted
- MQTT status will be published after broker was down
- Raw data publishing commented out to reduce the broker load (worst case 20/s)
- Remove leading space characters for MQTT room and outdoor temperature
- MQTT topic Runtime removed

**v1.1** (December 2019)

- Error code (DB4) added
 OTA is now also working when waiting for a MQTT connection

**v1.0** (December 2019)

- initial
