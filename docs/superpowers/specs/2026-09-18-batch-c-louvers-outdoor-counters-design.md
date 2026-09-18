# Batch C: left/right louvers and 3D auto in Home Assistant, the outdoor device, frame counters, fan names, error text

Design for one firmware build carrying fork issues **#20**, **#19** and **#21**. Decided with Lucas on 18 Sep 2026: one build, one flash per unit, then a soak on both. An addendum to `2026-09-16-phase-4-batches-design.md`; its conventions (pure logic in `lib/mhi_pure` with host tests first, `#ifndef`-guarded defines, the append-only discovery table, the 1024 B static buffer, streamed publishes, reference fixtures checked by CI) hold unchanged.

## 1. What is known (all measured on Uitkijk, SRK20ZS-WF, 18 Sep 2026)

- The unit accepts the 33-byte frame (`USE_EXTENDED_FRAME_SIZE`). `DB15` and `DB18`-`DB26` are static at rest and through every remote button; no mask change is needed.
- Left/right position = `DB16 & 0x07` (0-based), swing = `DB17 & 0x01`, 3D auto = `DB17 & 0x04`. Seen by Lucas on the unit: 1 leftmost, 2 left, 3 middle, 4 right, 5 rightmost, 6 wide, 7 spot. The remote's "louver stopped" reads as 3.
- After a write over SPI the AC echoes the set flags in its own frame (`DB16 & 0x10`, `DB17 & 0x0a`) until the remote is used. The decode must mask them (it does today).
- **The upstream commands are coupled.** `set_3Dauto()` sends `0b00001010 | on/off`: the 3D-auto set flag `0x08` plus the swing set flag `0x02` with swing 0, so 3D auto on *or off* stops the left/right swing. `set_vanesLR()` raises `0x08` with 3D auto 0, so any position or swing command switches 3D auto off. The remote does neither (3D AUTO on: `DB17 01>05`, swing bit kept). hberntsen's core sets only the flag of what changes.
- HI POWER and ECO are not on the bus in either frame. Out of scope here (hass-config #311 emulates them).
- Both indoor units share one outdoor unit; the outdoor operating data (request prefix `0x40`) and `KWH` (`0xc0 0x94`) are system values, identical from both units up to their ~20 s polling lag (#19, 17 Sep).

## 2. #20: left/right louvers and 3D auto

### 2.1 Core: one flag per command
New pure module `lib/mhi_pure/mhi_vanes_lr.{h,cpp}`:

- `void mhi_vanes_lr_command(int value, uint8_t* db16, uint8_t* db17)`: for 1..7 `*db16 = 0x10 | (value - 1)`, `*db17 = 0x02` (swing set flag, swing off); for `MHI_VANES_LR_SWING` (8, the core's `vanesLR_swing`) `*db16 = 0`, `*db17 = 0x03`. **No `0x08`.**
- `uint8_t mhi_3dauto_command(bool on)`: `0x08 | (on ? 0x04 : 0)`. **No `0x02`.**
- `int mhi_vanes_lr_decode(uint8_t db16, uint8_t db17)`: swing bit set → `MHI_VANES_LR_SWING`, else `(db16 & 0x07) + 1`. `bool mhi_3dauto_decode(uint8_t db17)`: `db17 & 0x04`. The set-flag echoes never change the result.
- Names, as `mhi_vanes` does for up/down: `struct MhiVanesLrNames { const char* pos[7]; const char* swing; }`, `mhi_vanes_lr_text()`, `mhi_vanes_lr_parse()` (the eight names, or `"1"`..`"8"` with 8 = swing; 0 when none).

The core's `set_vanesLR()`, `set_3Dauto()` and its status decode call these. `new_VanesLR0`/`new_3Dauto` are OR-ed into `DB17` as today, so a position command and a 3D-auto command queued in the same frame still combine. The command stays on the wire for one frame, which was enough for all 27 commands of 18 Sep.

Not known, to be observed on the unit after the flash and written into the docs: whether the AC itself drops 3D auto when a position is chosen, and what a position command does while 3D auto is on.

### 2.2 Topics and payloads
`VanesLR` publishes names: defines `PAYLOAD_VANESLR_1`..`_7` defaulting to `Left`, `LeftCenter`, `Center`, `CenterRight`, `Right`, `Wide`, `Spot`, next to the existing `PAYLOAD_VANESLR_SWING` (`Swing`). `set/VanesLR` accepts the names and `1`..`8`. A configuration may set the defines back to `"1"`..`"7"` (v2.8's texts). `3Dauto` stays `On`/`Off`. All of it only in a build with `USE_EXTENDED_FRAME_SIZE`.

### 2.3 Discovery
`MhiDiscoveryCtx` gains `bool has_lr` plus the topic and payload texts. With `has_lr`:

- the climate row adds `swing_horizontal_mode_command_topic`, `swing_horizontal_mode_state_topic` and `swing_horizontal_modes` (the eight names). Use Home Assistant's abbreviations only after checking them against `homeassistant/components/mqtt/abbreviations.py`; otherwise the full keys. The host test measures the row: it must stay under 1024 B for the longest unit name.
- two rows are appended: `MHI_DISCOVERY_VANES_LR` (select, `<id_prefix>_vanes_lr`, options = the eight names) and `MHI_DISCOVERY_3DAUTO` (switch, `<id_prefix>_3d_auto`, `On`/`Off`).

New `bool mhi_discovery_row_enabled(MhiDiscoveryRow, const MhiDiscoveryCtx*)`; `src/discovery.cpp` skips a disabled row (no publish, no blanking). Without `has_lr` every existing payload is byte for byte what `4530de1` publishes: the committed fixtures prove it.

## 3. #19: the outdoor device

**Ruling while writing this (deviation from the issue text):** the issue proposed a shared topic root `airco/buiten/OpData/` fed by both units; the 17 Sep comments reduced that to one publisher because two units alternate a value and its predecessor. With one publisher the shared root buys nothing: the outdoor rows simply read the publishing unit's own `OpData/` topics. So there is **no change to the publish path** and no `MQTT_OUTDOOR_PREFIX`. Entity identity lives in the `uniq_id`s, so the publisher can move to the other unit later without Home Assistant noticing more than a topic change.

- Option `HA_OUTDOOR_DEVICE` (`#ifdef` switch, off by default; on for Slaapkamer in the toolkit), with `HA_OUTDOOR_ID` (default `"<HA_ID_PREFIX>_outdoor"`; Lucas: `ac_buitenunit`), `HA_OUTDOOR_NAME` (default `"AC outdoor unit"`; Lucas: `AC buitenunit`) and `HA_OUTDOOR_ENTITY_PREFIX` (Lucas: `ac_buitenunit`). Requires `HA_DISCOVERY`.
- The outdoor rows carry their own `dev` block (`ids` = `HA_OUTDOOR_ID`, `name`, `mf`, `mdl` "outdoor unit", `via_device` = the unit's device id) and the publishing unit's availability.
- Rows, per Lucas's pruning of 18 Sep, `uniq_id` = `<HA_OUTDOOR_ID>_<suffix>`:

| row | suffix | component | topic | HA |
|---|---|---|---|---|
| `MHI_DISCOVERY_OU_OUTDOOR` | `outdoor_temp` | sensor | `OpData/OUTDOOR` | temperature, °C, measurement |
| `MHI_DISCOVERY_OU_CT` | `current` | sensor | `OpData/CT` | current, A, measurement |
| `MHI_DISCOVERY_OU_KWH` | `energy` | sensor | `OpData/KWH` | energy, kWh, total_increasing |
| `MHI_DISCOVERY_OU_COMP` | `comp_freq` | sensor | `OpData/COMP` | frequency, Hz, measurement, diagnostic |
| `MHI_DISCOVERY_OU_DEFROST` | `defrost` | binary_sensor | `OpData/DEFROST` | `On`/`Off`, diagnostic |
| `MHI_DISCOVERY_OU_COMP_RUN` | `comp_run` | sensor | `OpData/TOTAL-COMP-RUN` | duration, h, total_increasing, diagnostic |
| `MHI_DISCOVERY_OU_PROTECTION` | `protection` | sensor | `OpData/PROTECTION-TEXT` (§4.3) | diagnostic, text |

The implementer verifies each topic's real payload format in `main.cpp` before writing the row (number formatting, `On`/`Off` texts) and whether the `OpData/` publishes are retained; a sensor whose topic is not retained reads "unknown" after a Home Assistant restart until the value next changes, which is acceptable for these and must be said in the docs.

## 4. #21

### 4.1 Frame counters
Pure `lib/mhi_pure/mhi_frame_stats.{h,cpp}`: `struct MhiFrameStats { uint32_t errors; uint32_t timeouts; }`, `void mhi_frame_stats_count(MhiFrameStats*, int err_msg)`: invalid signature or checksum → `errors`, either SCK timeout → `timeouts`, anything else (valid frame, the MISO-not-driven codes if any) → nothing; both saturate at `UINT32_MAX`. `main.cpp` counts every `mhi_ac_ctrl_core.loop()` return. Topics `FrameErrors` and `FrameTimeouts`, retained, published with the periodic telemetry and at every connect, next to `Uptime`. Discovery rows `MHI_DISCOVERY_FRAME_ERRORS` (`<id_prefix>_frame_errors`) and `MHI_DISCOVERY_FRAME_TIMEOUTS` (`<id_prefix>_frame_timeouts`): sensor, diagnostic, `total_increasing`, no unit. What is normal for `FrameTimeouts` (boot, OTA, Wi-Fi scans) is unknown until it has run: the soak defines the baseline, the docs get the numbers afterwards.

### 4.2 Fan-speed names (F6)
Defines `PAYLOAD_FAN_1`..`PAYLOAD_FAN_4`, defaults `"1"`..`"4"`, next to `PAYLOAD_FAN_AUTO`. Pure `lib/mhi_pure/mhi_fan.{h,cpp}` in the shape of `mhi_vanes`: text for the core's fan value, parse of a `set/Fan` payload (the five names, or `"1"`..`"4"`; the core's numbering as `main.cpp` uses it today). The discovery climate's `fan_modes` come from the five texts (`MhiDiscoveryCtx` gets `fan[4]`). With the defaults nothing changes on the wire or in the fixtures. The names for Lucas's units are the toolkit's and hass-config's choice, because Home Assistant rejects `climate.set_fan_mode` with a text outside `fan_modes`: its automations change in the same step as the flash, or the names stay numeric.

### 4.3 Error and protection text (F2)
Pure `lib/mhi_pure/mhi_error_text.{h,cpp}`: `size_t mhi_error_text(uint8_t code, char* out, size_t out_len)` → `OK` for 0, `E<n>: <meaning>` for a code in the table, `E<n>` otherwise; `size_t mhi_protection_text(uint8_t no, char* out, size_t out_len)` → the 0-17 table of `SW-Configuration.md` (`Normal` for 0), the number itself otherwise. Tables in PROGMEM on the ESP (`#if defined(ARDUINO)` as `mhi_discovery.cpp` does). Topics `ErrorText` (next to `Errorcode`, published whenever `Errorcode` is) and `OpData/PROTECTION-TEXT` (whenever `OpData/PROTECTION-NO` is). Discovery row `MHI_DISCOVERY_ERROR_TEXT` (`<id_prefix>_error_text`, sensor, diagnostic) on the unit's device; the outdoor device's protection row reads the text topic.

The error table comes from MHI's service documentation for the residential RAC series, two sources wanted (being researched on 18 Sep; upstream's handbook link is dead). Upstream never verified that the byte equals the E number, so the docs say where the table comes from and that the mapping is MHI's numbering taken at face value. If no reliable table turns up in time, the build ships `E<n>` without meanings and the table follows later: the topic, the row and the tests do not depend on the table's content.

### 4.4 Telemetry every minute
Toolkit only: `#define TELEMETRY_PERIOD 60` in the two units' derived configs. The repo default stays 300.

## 5. Discovery table after this batch
Append-only, in this order after `MHI_DISCOVERY_WIFI_PHY`: `VANES_LR`, `3DAUTO`, `FRAME_ERRORS`, `FRAME_TIMEOUTS`, `ERROR_TEXT`, `OU_OUTDOOR`, `OU_CT`, `OU_KWH`, `OU_COMP`, `OU_DEFROST`, `OU_COMP_RUN`, `OU_PROTECTION`. 22 rows; a unit without `has_lr` and without the outdoor device publishes 13. `tools/discovery_payloads.cpp` gets the matching options (`--lr`, `--outdoor-id`, `--outdoor-name`, `--outdoor-entity-prefix`, `--fan-1`..`--fan-4`, the new `--name-*`), and the committed fixtures grow by a second set rendered with everything on; CI checks both sets.

Heap: the row descriptors and templates are PROGMEM; the static buffer stays 1024 B. Expected DRAM cost: the two counters, the name tables' pointers. The flash check after the build and `FreeHeap` after the flash are compared with `9352cb0` (349104 B; 42072 B at boot on Uitkijk).

## 6. Toolkit (outside the repo)
`units.sh`: `FRAME=33` for both units. `airco-config.py`: Dutch names for the new rows, `TELEMETRY_PERIOD 60`, the outdoor block for Slaapkamer only, the fan names once hass-config has chosen them (until then none, i.e. numeric), all covered by its tests; `--discovery-args` grows to match. `post-flash-check.sh`: the new retained topics (`FrameErrors`, `FrameTimeouts`, `ErrorText OK`, `VanesLR` a name), the new entities, the outdoor device on Slaapkamer, gated on the batch's first commit. `health-check.sh`: the two counters in the status line.

## 7. Rollout
Lucas authorized this session to build and flash both units for this build (18 Sep), with a health check before and after each flash, and to tell the hass-config session when the firmware has landed. Order: `health-check.sh 1` → Uitkijk (already on 33 bytes, so the smaller step) → `post-flash-check.sh uitkijk` → a left/right and 3D-auto round trip over MQTT, including "3D auto on leaves the swing on" → Slaapkamer (first time on the 33-byte frame; rollback image `4530de1`) → `post-flash-check.sh slaapkamer` → hass-config told (version, entities, fixtures) → soak: `health-check.sh 1` the next day, `FrameErrors`/`FrameTimeouts`/`FreeHeap` from Home Assistant's history.

## 8. Out of scope
HI POWER / ECO (hass-config #311), F3, F7-F11, C1, C3, #3, a release tag.
