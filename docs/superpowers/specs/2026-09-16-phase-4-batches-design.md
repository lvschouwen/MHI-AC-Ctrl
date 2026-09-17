# Phase 4 in two batches — design addendum

Addendum to `2026-07-25-mhi-ac-ctrl-improvement-design.md` §5.4 and §5.5 (fork issue #4). Written 2026-09-16 after both units were flashed with `5f1d94e` (#17 PHY fallback, #18 telemetry) and after the remote test of the same evening. The July spec fixed the shape of phase 4; this addendum records what the test changed, the decisions Lucas took, and the design of the two flash batches.

## 1. What changed since July

1. **The remote test (16 Sep, Uitkijk, capture in the fork #4 comments).** `0x80DD` (`DB9 = 0xDD`, `DB10 = 0x80`) is **Silent**, not the HI/ECO candidate: the AC reports it once per Silent press, on and off alike, so the state is in `DB11`, which `MHI-AC-Ctrl-core.cpp` discards for unknown operating data. **HI POWER and ECO report nothing** on these units: the request table (`MHI-AC-Ctrl-core.h`, 20 codes) has no `0x21`, and the units do not volunteer it as the upstream #219 reporter's unit did. Their only trace is the side effect: HI = fan 3 with indoor fan speed 8, ECO = fan 1 with the internal setpoint eased by 0.5 °C. The remote has one combined HI/ECO button (HI → ECO → normal) and a separate SILENT button.
2. **The HA side moved.** hass-config `packages/airco.yaml` holds two hand-written MQTT climates (`unique_id` `AC_Slaapkamer`, `AC_Uitkijk`) and, since hass-config#304 (16 Sep), five diagnostic sensors per unit (`ac_<unit>_uptime`, `_free_heap`, `_rssi`, `_reset_reason`, `_wifi_phy`). `bedtijd.yaml` and `zonnekoeling.yaml` reference `climate.ac_slaapkamer` and `climate.ac_uitkijk` by entity ID in ten places; `sensor.ac_<unit>_herstarts` and the `airco_controller_herstart` automation read the uptime and reset-reason sensors by entity ID.
3. **Home Assistant behaviour (docs, verified 16 Sep).** MQTT entities are keyed by `unique_id`; a discovery config with the `unique_id` of a YAML entity that still exists is rejected as a duplicate; `object_id` was removed in HA 2026.4 in favour of `default_entity_id`.
4. **Both indoor units share one outdoor unit** (verified 16 Sep): `CT`, `TD`, `TDSH`, `OUTDOOR` are system values, published live by both units.
5. **Discovery conventions** from Lucas's split-flap firmware are recorded on #4: retained on every connect, one device block per unit, abbreviated keys, `snprintf_P` into a fixed buffer, append-only entity table, `ent_cat` diagnostic, `problem` binary sensors.

## 2. Decisions (Lucas, 16 Sep)

| # | Decision |
|---|---|
| 1 | Discovery **replaces** every AC MQTT entity in `airco.yaml` (climates and #304 sensors), keeping the existing `unique_id`s so entity IDs and automations survive. The cutover is rehearsed by hand on **Uitkijk** before any firmware. |
| 2 | Phase 4 ships as **two flash batches**: A = protocol discovery tooling, B = named vanes, Silent, discovery, cutover. |
| 3 | Discovery covers, per unit: climate, vanes select, Silent / problem / wiring binary sensors, the five diagnostic sensors. **No OpData sensors** in the first cut; the table is append-only, so they can follow. |
| 4 | The Eco/Silent decode is no longer built on `OpData/unknown`. |
| 5 | The goal of the tooling is the **HI/ECO button** visible in Home Assistant in both states. Batch A must be able to catch it whether it is an undecoded status-frame bit or an answer the AC only gives when asked. |
| 6 | Dropped: "apply `DB10` discriminators consistently" (the decode is correct on both units; a rewrite risks a regression for no visible gain), the ginkage THI-R2 formula, `FAN_DB6_MASK`, the July `0x21` = Eco/Silent reading. |
| 7 | (16 Sep evening, after the batch A test) `Silent` is a **switch**, not a binary sensor: the write is known from hberntsen PR #42 (4.3). `Eco`/`HiPower` are dropped; HI/ECO in Home Assistant needs an IR blaster or an HA-side emulation, a choice still open. |

## 3. Batch A: protocol discovery tooling

Diagnostic only. No write path to the AC beyond an operating-data request of the form the 20 built-in codes already use.

### 3.0 How raw bytes leave the core

The status callback is `cbiStatusFunction(ACStatus, int)` (`MHI-AC-Ctrl-core.h:84`), and shifting four bytes into an `int` is undefined for `DB12 >= 0x80`. Raw bytes therefore get their own path: a second virtual on the callback interface, `cbiRawFunction(ACStatus status, const uint8_t* bytes, size_t len)`, with an empty default body so nothing else implementing the interface changes. The core calls it twice: `raw_opdata` with `&MOSI_frame[DB9]`, 4 bytes, from the `default:` branch of the operating-data switch; `raw_frame` with the whole MOSI frame after checksum validation (`MHI-AC-Ctrl-core.cpp:264-274`, before decoding starts at 276), once per valid frame. Everything else, the diff, the formatting, the rate limit and the publishing, lives in `main.cpp` and a pure helper, so the SPI core changes by two calls.

### 3.1 `diag/opdata`: unknown operating data with its value bytes

- `main.cpp` publishes `diag/opdata` with `DB9 DB10 DB11 DB12` in lower-case hex, e.g. `dd 80 01 00`, not retained, every time `raw_opdata` arrives (these are events, not state). The existing `opdata_unknown` callback and its numeric `OpData/unknown` payload stay for compatibility.
- Formatting in `lib/mhi_pure/mhi_diag_frame` (see 3.2), host-tested.

### 3.2 `diag/frame`: status-frame changes outside the decoded bytes

- On every `raw_frame`, `main.cpp` asks a pure helper to compare the frame with the last *published* copy under a per-byte ignore mask and, when something differs, to format `DB5 00>10 DB13 05>07 | <full frame in hex>`.
- Compared: `DB0`-`DB5`, `DB6` low six bits, `DB7`, `DB8`, `DB13`, `DB14`, and `DB15` onwards in extended-frame builds. Ignored: the three header bytes; `DB6` bits `0xc0`, which echo the request prefix of the cycling operating data (`0x40`/`0xc0`, see `core.cpp:528` and the request table); `DB9`-`DB12`, the operating-data code and value; the checksum bytes; and `DB14` bit 2, the frame toggle the core already uses as `doubleframe`. The mask is a byte array in the helper, so a byte that proves noisy on hardware is masked with a one-line change and a test.
- Rate limit: at most one publish per second (`MhiRetryPacer` from `mhi_link`). A change inside the second is folded into the next publish, because the compare is always against the last published frame.
- Runtime switch `set/Diag` `On`/`Off` (the casing of `set/PassiveMode`, overridable like every payload), state on retained topic `Diag`, on at boot by default (compile-time `DIAG_DEFAULT`, overridable in `config_defaults.h`). `Off` stops `diag/frame` only; `diag/opdata` is event-driven and stays. After every MQTT connect the first `diag/frame` is the whole frame (`first | …`).
- Cost per frame while nothing changes: one virtual call and a masked compare of at most 33 bytes; no publish. Payload when it does change: 15 bytes x 3 characters plus the change list, at most 168 characters (six named changes, the `+`, a 33-byte frame; the text buffer is 200 B and the host test pins the worst case), and PubSubClient3 streams payloads anyway (4.1), so batch A does not need a buffer change.

### 3.3 `set/OpDataRequest <4 hex digits>`: one-shot operating-data request

- Payload `c021` or `4021`: the `DB6` request prefix, then the code, as in the request table. Only `40` and `c0` prefixes are accepted; anything else, or a payload that is not four hex digits, answers `invalid parameter` on `cmd_received`; a valid one answers `o.k.` and stores the pending request (one pending at a time; a second command before it is sent replaces it).
- Slot semantics, pinned to `MHI-AC-Ctrl-core.cpp:158-190`: the pending request is written into `MISO_frame[DB6]`/`[DB9]` in the same place the normal cycle writes its next code, i.e. only in the `frame++ <= 2 && doubleframe && erropdataCnt == 0` window, and only then is it consumed. It takes the slot *instead of* the next normal code, and `opdataNo` is not advanced, so the normal cycle resumes with the code it would have sent. While `erropdataCnt > 0` (error operating data being read out, `core.cpp:183-186`, `586-587`) the request waits for the next window; it is never dropped by the error path, and never retried after it was sent.
- The answer arrives on `diag/opdata` if the decoder does not know the code. If it does, the answer goes through the normal decode, which publishes a status topic only when its value changed: a probe of a known code whose value is unchanged shows nothing.
- Parsing in a pure helper (`mhi_opdata_request_parse`), host-tested; the slot logic is in the core and is verified on the bench by the `0x21` probes of 3.5.

### 3.4 Files

- `lib/mhi_pure/mhi_diag_frame.{h,cpp}` + `test/test_mhi_diag_frame`: frame diff with mask, hex formatting, opdata formatting, request parsing.
- `MHI-AC-Ctrl-core.{h,cpp}`: `cbiRawFunction` on the callback interface, the two raw calls, the one-shot request slot.
- `main.cpp`: `cbiRawFunction` implementation, the two topics, `set/Diag`, `set/OpDataRequest`.
- `MHI-AC-Ctrl.h`: `TOPIC_DIAG_OPDATA`, `TOPIC_DIAG_FRAME`, `TOPIC_DIAG`, `TOPIC_OPDATA_REQUEST` and payloads, `#ifndef`-guarded.
- `support.h`: `DIAG_DEFAULT`.
- Docs: `SW-Configuration.md` (new section "Finding out what a remote button does", with the 16 Sep worked example), `Troubleshooting.md`, `Version.md`.
- Toolkit: `post-flash-check.sh` lists `Diag`; a `remote-test.sh <unit>` capture script that prints the timestamped `diag/*`, `Fan`, `OpData/Tsetpoint`, `OpData/unknown` lines for a remote session.

### 3.5 Verification (Slaapkamer first, then Uitkijk, each with its own go)

Host tests and CI; flash size under budget. On hardware: the remote test again with `remote-test.sh` running: HI, ECO, normal, Silent on, Silent off. Then `set/OpDataRequest c021` and `4021`. Expected: Silent on/off shows as two different `DB11` values on `diag/opdata`; HI/ECO shows either as a `diag/frame` line or as an answer to `0x21`, or as nothing, which is also an answer. The outcome is recorded on #4 and decides what batch B decodes.

## 4. Batch B: named vanes, Silent, discovery, cutover

### 4.1 Buffers

**No client buffer change.** PubSubClient3 3.3.1 streams a publish: `beginPublishImpl()` (`PubSubClient.cpp:608`) only needs the fixed header, the topic and, for QoS > 0, the packet id to fit the buffer, then `write()` appends the payload through the buffer and flushes it whenever it fills (`appendBuffer()`, `flushBuffer()`), so a 1 KB discovery payload goes out through the default 256 B buffer. The buffer bounds *incoming* packets (`readPacket()` drops one larger than the buffer), and the unit only receives `set/` commands of a few bytes. `setBufferSize(1024)` came from the July spec, written for a library that did not stream; the batch A review found the streaming, and batch B drops the call and its 768 B of heap.

The discovery builder buffer is **1024 B**, static (BSS, not the stack: the ESP8266 `loop()` stack is 4 KB and the publish path runs on it), and the host test measures every row of every build and fails at 1024. The climate row is the longest, estimated at 720-760 B with Home Assistant's mode names, six vane names, the `dev` block and Lucas's Dutch names, too close to the 768 B first agreed; the test prints the measured length. Topic (`homeassistant/climate/AC_Slaapkamer/config`, ~45 B) plus header stay far inside the client buffer.

### 4.2 Named vanes

`Vanes` publishes `Up`, `UpCenter`, `CenterDown`, `Down`, `Swing`, `?`; `set/Vanes` accepts those names and, as today (`main.cpp:123-126`), `1`–`4` and `5` as an alias of `Swing`. Texts are `#ifndef`-guarded in `MHI-AC-Ctrl.h`. Left/right names only under `USE_EXTENDED_FRAME_SIZE`. Parsing and formatting in `lib/mhi_pure/mhi_vanes`, host-tested. Because the YAML climate reads `Vanes`, this ships in the same flash as the cutover.

### 4.3 `Silent` topic, and whatever batch A finds

`Silent` publishes `On`/`Off` (the casing of the repo's other switches, overridable) from `DB11` bit 5 of the `0xDD` answer, retained, on change. The code `{0xc0, 0xdd}` joins the request table, so the state is known within one operating-data cycle (about 20 s) of boot or reconnect without waiting for a remote press; the AC also reports the code unasked after every remote press (batch A).

**Silent is written too** (decision 7). `set/Silent` `On`/`Off` puts `DB6 = 0x80, DB9 = 0x21, DB10 = 0x01/0x00` in the next MISO frame pair: the write mreijnde traced from a ProtoArt controller and hberntsen carries in [PR #42](https://github.com/hberntsen/mhi-ac-ctrl-esp32/pull/42) (open: "test more", including an AC reboot and a multi-split). The core idles `DB10` together with `DB6`/`DB9` on the frame after the command (the parameter is never on the wire without the command; the error-data request has the same one-frame exposure and works), waits for a running error-data dump to end, uses one `0x80` command slot for `ErrOpData` and Silent (the error-data request goes first, Silent the next pair), holds a pending `set/OpDataRequest` while either is pending (which also fixes batch A's parked case: `set/ErrOpData` in the same pair swallowed a one-shot), and queues a `c0dd` read after the write unless a one-shot is already pending, so the `Silent` topic confirms it. Two quirks from PR #42 go in the docs: a Silent set from the IR remote cannot be cleared over SPI and vice versa, and on a multi-split each indoor unit can hold the shared outdoor unit in Silent. `Eco` and `HiPower` were dropped with the evidence below.

Two more batch B items from the same evening: `DB3` (raw Troom) joins the compare mask (one line plus a test), and `DB7 & 0x02` is recorded next to the `DB13` decode as "outdoor unit running" (it moved with `DB13` 5↔1 every time, comment on #4), a named constant with its evidence, not a topic.

**Result of the batch A test** (Uitkijk on `2eab73c`, 16 Sep 22:31 CEST, `remote-test.sh uitkijk 600`, the five presses about ten seconds apart): SILENT on answered `dd 80 20 00` on `diag/opdata`, SILENT off `dd 80 00 00`. **Silent is `DB11` bit 5 (`0x20`) of the `0xDD` answer.** `set/OpDataRequest c0dd` makes the AC answer that code within two seconds at any time (22:03, Silent off: `dd 80 00 00`), so batch B decodes `Silent` from `DB11 & 0x20` and can read it on demand instead of waiting for a press; `40dd` is ignored. HI/ECO left nothing in the status frame beyond their side effects: HI changed `DB1` `06>02` (published as `Fan` 3), ECO `DB1` `02>00` (`Fan` 1) with `OpData/Tsetpoint` 18.0→18.5, normal restored `DB1` `06` (`Fan` 4) and 18.0; no other status byte moved while each state was held (about twelve seconds, enough for a persistent bit at the 1 Hz sampling), and `c021`/`4021` get no answer. **`Eco` and `HiPower` are dropped from #4** with this evidence; `diag/frame` and the probe remain for a later attempt. Two side observations: `DB2` bit 7 (`a4>24`) was cleared by the first remote press and stayed cleared through "normal", consistent with an echo of the SPI-written setpoint (`0x80 | 2×T`) rather than a mode; and raw `DB3` (Troom) dithers between adjacent values at a temperature boundary, one `diag/frame` line a second, so batch B adds `DB3` to the compare mask (one line plus a test).

### 4.4 Discovery

- **Opt-in.** Discovery is compiled only with `HA_DISCOVERY` defined (in `config_defaults.h`, like every other option; Lucas's build sets it, the repo default does not). Reason: HA accepts only its own mode names in a climate's `modes` (`off`, `auto`, `cool`, `dry`, `fan_only`, `heat`), while the repo's default `PAYLOAD_MODE_*` are `Off`, `Auto`, `Dry`, `Cool`, `Fan`, `Heat` (`MHI-AC-Ctrl.h:181-200`). A build with the default payloads would publish a climate config HA rejects. Rather than carry mode templates in every payload, discovery **builds every list from the active payload macros**: `modes` from the six `PAYLOAD_MODE_*`, `fan_modes` from `PAYLOAD_FAN_AUTO` plus the literal `"1"`-`"4"` the command parser hard-codes (`main.cpp:117-130`; there are no `PAYLOAD_FAN_1..4` macros and none are added), `swing_modes` from the vane names, availability from `PAYLOAD_CONNECTED_*`. The preprocessor cannot compare string-literal macros, so the guard is not a `#error` but two checks: a pure helper `mhi_discovery_modes_valid()` compares the six mode payloads with HA's names; the host test runs it on the lists the CI environment supplies, and `setup()` runs it once at boot and, when it fails, skips the climate row and says so on Serial and on a retained `Discovery` topic (`ok`, or `modes` when the climate row was skipped), so a wrong build is visible instead of silently rejected by HA. New CI environment `ci-ha-discovery` supplies `-D HA_DISCOVERY` and the HA-style mode names as build flags, the way `ci-custom-payloads` supplies its payloads in `platformio.ini`.
- `lib/mhi_pure/mhi_discovery.{h,cpp}`: an append-only table of rows `{component, uniq_id suffix, template}`; `mhi_discovery_build(row, ctx, buf, len)` fills one row's `snprintf_P` template with the context (`MQTT_PREFIX`, `HOSTNAME`, `VERSION`, the names, the payload lists) into the 768 B buffer and returns the length or 0 when it does not fit. Host tests: every row builds, fits, is valid JSON (a minimal checker: balanced braces and quotes, no unfilled `%`, no `(null)`), carries `uniq_id`, `avty_t` and `dev`, and the climate's `modes` are HA's names. The test writes the reference payloads (repo defaults with HA mode names) to `test/fixtures/discovery/`, committed, and CI fails when they differ from the code. For Lucas's units, `tools/discovery_payloads.cpp` (the same builder compiled on the host with `g++`, the unit's names as arguments) renders the exact payloads the firmware will publish; the rehearsal in 4.5, `post-flash-check.sh` (which compares them with the retained configs after the flash) and hass-config's tests use that output.
- `src/discovery.{h,cpp}` (Arduino side, real code only with `HA_DISCOVERY`): the context from the macros, the boot-time modes check, and after every MQTT connect one row per `loop()` pass, retained, to `homeassistant/<component>/<uniq_id>/config`, then the retained `Discovery` topic; `main.cpp` calls its three functions. `MQTT_SET_PREFIX` must start with `MQTT_PREFIX` for the `~/set/...` topics, asserted at compile time.
- Rows per unit, `<u>` = unit, `<U>` = capitalised unit:

| component | uniq_id | content |
|---|---|---|
| climate | `AC_<U>` | `modes` = the six `PAYLOAD_MODE_*` (HA's names, see the opt-in rule), `fan_modes` = `PAYLOAD_FAN_AUTO` + `"1"`-`"4"`, `swing_modes` = the vane names + `?`, mode/temperature/fan/swing command and state topics, `curr_temp_t` `Troom`, `act_t` `Action`, `min_temp` 18, `max_temp` 30, `temp_step` 0.5, `avty_t` `connected` with `pl_avail` `PAYLOAD_CONNECTED_TRUE` / `pl_not_avail` `PAYLOAD_CONNECTED_FALSE`, `name` `null` (the device's main feature: HA names it after the device) |
| select | `ac_<u>_vanes` | `Vanes` / `set/Vanes`, options = the vane names + `?`, so a position set from the remote (published as `?`) logs no invalid-option line, as with `swing_modes`; selecting `?` answers `invalid parameter` and changes nothing |
| switch | `ac_<u>_silent` | `Silent` / `set/Silent`, `pl_on` `On`, `pl_off` `Off` (the `PAYLOAD_SILENT_*` texts), `ic` `mdi:volume-low` |
| binary_sensor | `ac_<u>_problem` | `Errorcode`, `val_tpl` `{{ 'ON' if value|int(0) != 0 else 'OFF' }}`, `dev_cla` `problem`, `ent_cat` diagnostic |
| binary_sensor | `ac_<u>_wiring` | `Wiring`, `val_tpl` `{{ 'OFF' if value == 'o.k.' else 'ON' }}`, `dev_cla` `problem`, `ent_cat` diagnostic |
| sensor | `ac_<u>_uptime` | `Uptime`, `dev_cla` duration, `unit_of_meas` s, `sug_dsp_prc` 0, no state class; plain seconds, because the hass-config reboot counter compares the raw state |
| sensor | `ac_<u>_free_heap` | `FreeHeap`, `dev_cla` data_size, `unit_of_meas` B, `stat_cla` measurement |
| sensor | `ac_<u>_rssi` | `RSSI`, `dev_cla` signal_strength, `unit_of_meas` dBm, `stat_cla` measurement |
| sensor | `ac_<u>_reset_reason` | `ResetReason`, `val_tpl` from `config_defaults.h` (`HA_RESET_REASON_TPL`; repo default: none; Lucas's build: hass-config's Dutch mapping), `ic` `mdi:restart-alert` |
| sensor | `ac_<u>_wifi_phy` | `WIFI_PHY`, `ic` `mdi:wifi-cog` |

All rows: `~` = `MQTT_PREFIX` without its trailing slash, `avty_t` `~/connected` (`1`/`0`), `dev` `{ids: [HOSTNAME], name: HA_DEVICE_NAME, mf: "Mitsubishi Heavy Industries", mdl: "MHI-AC-Ctrl", sw: VERSION}`; the sensors and problem/wiring rows `ent_cat` `diagnostic`. **Names:** since HA 2024.2 an MQTT entity's friendly name is the device name plus the entity's own name, so each row's `name` is the suffix only (`Vanes`, `Uptime`, ...) and hass-config's `AC Slaapkamer tijd sinds opstart` is device `AC Slaapkamer` + name `tijd sinds opstart`: the friendly names of the five sensors do not change. The climate's `name` is `null`, so its friendly name becomes the device name (`AC Slaapkamer` instead of `AC_Slaapkamer`); hass-config greps for friendly-name readers before the cutover. **Entity IDs:** existing entities keep their registry IDs by `unique_id` whatever the payload says. The four new ones (vanes, Silent, problem, wiring) get `default_entity_id` `<domain>.<HA_ENTITY_PREFIX>_<suffix>` when `HA_ENTITY_PREFIX` is defined (Lucas: `ac_slaapkamer`), so their IDs are predictable whatever HA's entity-ID format setting (2026.x builds new IDs from area, device and name by default); the repo default leaves it out, because `HOSTNAME` is not a valid entity-ID text. The options are `#ifndef`-guarded in `support.h`: `HA_DISCOVERY` (off), `HA_DISCOVERY_PREFIX` (`homeassistant`), `HA_DEVICE_NAME`, `HA_CLIMATE_ID` and `HA_ID_PREFIX` (all `HOSTNAME`), `HA_ENTITY_PREFIX` (undefined), `HA_NAME_VANES` ... `HA_NAME_WIFI_PHY` (English), `HA_RESET_REASON_TPL` (undefined). `airco-config.py` writes Lucas's values per unit.

- No automatic blanking of old configs: a rename here is a re-flash anyway. `SW-Configuration.md` documents the manual cleanup (`mosquitto_pub -r -n -t homeassistant/<component>/<uniq_id>/config`).

### 4.5 Cutover, per unit, with the hass-config session

1. **Rehearsal on Uitkijk, before any firmware.** Its YAML climate and five sensors commented out, MQTT YAML reloaded, the intended payloads (rendered by `tools/discovery_payloads` for `uitkijk`) published by hand with `mqtt.publish`, retained. Check: `climate.ac_uitkijk` and the five sensors keep their entity IDs and history, `bedtijd`/`zonnekoeling` unchanged, `sensor.ac_uitkijk_herstarts` still reads. Then blank the retained configs and restore the YAML. Any deviation changes the payloads before firmware is written.
2. **Cutover per unit:** remove that unit's YAML block, reload MQTT YAML, flash the unit, discovery arrives on connect, `post-flash-check.sh` verifies via HA's API that the climate and the sensors exist with the expected entity IDs and follow the topics. Flash order per Lucas's go; the promotion rule says Slaapkamer first.
3. hass-config rewrites `tests/test_airco_diagnose_304.py` and `tests/test_airco_topics_302.py` against the discovery payloads; the tool's output for both units is the fixture.

### 4.6 Verification

Both units: entities present and controllable from HA (fan round trip, a vane change by name), `Silent` follows the remote, `FreeHeap` steady around 43 KB over a night, health check green the next day.

## 5. Out of scope

Writing Eco/HI (IR-only on these units, batch A; an IR blaster or an HA-side emulation is the route, Lucas's choice). Silent is written, see 4.3. OpData sensors in discovery (append later on demand). Vanes LR / 3D auto (extended frame not used here). Blanking old discovery configs automatically. Runtime configuration (#3, parked).

## 6. Risks

- **Heap in batch B:** a 1024 B static builder buffer against ~44 KB free, no client buffer change (4.1). Mitigation: one row per loop pass, `FreeHeap` before and after, rollback image kept.
- **A noisy status byte** floods `diag/frame`: the 1 s rate limit bounds it, `set/Diag off` stops it, the mask silences it in the next build.
- **The Silent write** (`0x80 21 xx`) is a command this firmware has never sent, and PR #42 is still open. Same frame shape as the error-data request, one slot, once per command, `DB10` restored after; tried on Slaapkamer first with `remote-test.sh` running, the remote at hand (it clears a Silent the SPI cannot), the AC off, and the rollback image kept.
- **Cutover ordering:** a discovery config while the YAML entity exists is rejected as a duplicate; the per-unit procedure removes the YAML first. Rehearsed on Uitkijk.
