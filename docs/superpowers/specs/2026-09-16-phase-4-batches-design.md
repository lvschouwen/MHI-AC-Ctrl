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
- Runtime switch `set/Diag` `on`/`off`, state on retained topic `Diag`, default `on` at boot (compile-time default `DIAG_DEFAULT`, overridable in `config_defaults.h`). `off` stops `diag/frame` only; `diag/opdata` is event-driven and stays.
- Cost per frame while nothing changes: one virtual call and a masked compare of at most 33 bytes; no publish. Payload when it does change: 15 bytes x 3 characters plus the change list, under 160 characters, inside the 256 B client buffer, so batch A does not need the buffer change.

### 3.3 `set/OpDataRequest <4 hex digits>`: one-shot operating-data request

- Payload `c021` or `4021`: the `DB6` request prefix, then the code, as in the request table. Only `40` and `c0` prefixes are accepted; anything else, or a payload that is not four hex digits, answers `invalid parameter` on `cmd_received`; a valid one answers `o.k.` and stores the pending request (one pending at a time; a second command before it is sent replaces it).
- Slot semantics, pinned to `MHI-AC-Ctrl-core.cpp:158-190`: the pending request is written into `MISO_frame[DB6]`/`[DB9]` in the same place the normal cycle writes its next code, i.e. only in the `frame++ <= 2 && doubleframe && erropdataCnt == 0` window, and only then is it consumed. It takes the slot *instead of* the next normal code, and `opdataNo` is not advanced, so the normal cycle resumes with the code it would have sent. While `erropdataCnt > 0` (error operating data being read out, `core.cpp:183-186`, `586-587`) the request waits for the next window; it is never dropped by the error path, and never retried after it was sent.
- The answer arrives on `diag/opdata` if the decoder does not know the code, or on the code's normal topic if it does.
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

`MQTTclient.setBufferSize(1024)` in `setup()` before the first connect (PubSubClient3 3.3.1 `setBufferSize()`, runtime; `PubSubClient.cpp:608` publishes only when header + topic + payload fit), +768 B heap. The discovery builder buffer is **768 B**; the longest row, the climate, is estimated at about 700 B and the host test measures every row of every build and fails above 768, so the estimate is checked, not trusted. Topic (`homeassistant/climate/AC_Slaapkamer/config`, ~45 B) plus header stay well inside the remaining 256 B.

### 4.2 Named vanes

`Vanes` publishes `Up`, `UpCenter`, `CenterDown`, `Down`, `Swing`, `?`; `set/Vanes` accepts those names and, as today (`main.cpp:123-126`), `1`–`4` and `5` as an alias of `Swing`. Texts are `#ifndef`-guarded in `MHI-AC-Ctrl.h`. Left/right names only under `USE_EXTENDED_FRAME_SIZE`. Parsing and formatting in `lib/mhi_pure/mhi_vanes`, host-tested. Because the YAML climate reads `Vanes`, this ships in the same flash as the cutover.

### 4.3 `Silent` topic, and whatever batch A finds

`Silent` publishes `on`/`off` from `DB11` of the `0xDD` answer with the two values batch A's test shows, retained, on change. If batch A exposes HI/ECO, `Eco` and `HiPower` topics follow the same pattern; if it does not, they are dropped from #4 with the evidence.

### 4.4 Discovery

- **Opt-in.** Discovery is compiled only with `HA_DISCOVERY` defined (in `config_defaults.h`, like every other option; Lucas's build sets it, the repo default does not). Reason: HA accepts only its own mode names in a climate's `modes` (`off`, `auto`, `cool`, `dry`, `fan_only`, `heat`), while the repo's default `PAYLOAD_MODE_*` are `Off`, `Auto`, `Dry`, `Cool`, `Fan`, `Heat` (`MHI-AC-Ctrl.h:181-200`). A build with the default payloads would publish a climate config HA rejects. Rather than carry mode templates in every payload, discovery **builds every list from the active payload macros**: `modes` from the six `PAYLOAD_MODE_*`, `fan_modes` from `PAYLOAD_FAN_AUTO` plus the literal `"1"`-`"4"` the command parser hard-codes (`main.cpp:117-130`; there are no `PAYLOAD_FAN_1..4` macros and none are added), `swing_modes` from the vane names, availability from `PAYLOAD_CONNECTED_*`. The preprocessor cannot compare string-literal macros, so the guard is not a `#error` but two checks: a pure helper `mhi_discovery_modes_valid()` compares the six mode payloads with HA's names; the host test runs it on the lists the CI environment supplies, and `setup()` runs it once at boot and, when it fails, skips the climate row and says so on Serial and on a retained `Discovery` topic (`ok`, or `modes` when the climate row was skipped), so a wrong build is visible instead of silently rejected by HA. New CI environment `ci-ha-discovery` supplies `-D HA_DISCOVERY` and the HA-style mode names as build flags, the way `ci-custom-payloads` supplies its payloads in `platformio.ini`.
- `lib/mhi_pure/mhi_discovery.{h,cpp}`: an append-only table of rows `{component, uniq_id suffix, template}`; `mhi_discovery_build(row, ctx, buf, len)` fills one row's `snprintf_P` template with the context (`MQTT_PREFIX`, `HOSTNAME`, `VERSION`, the names, the payload lists) into the 768 B buffer and returns the length or 0 when it does not fit. Host tests: every row builds, fits, is valid JSON (a minimal checker: balanced braces and quotes, no unfilled `%`), carries `uniq_id`, `avty_t` and `dev`, and the climate's `modes` are HA's names. The test also writes every payload to a file, which is the fixture for the rehearsal in 4.5 and for hass-config's tests.
- `support.cpp`: after every MQTT connect, publish one row per `loop()` pass, retained, to `homeassistant/<component>/<uniq_id>/config`, using `~` = `MQTT_PREFIX` without the trailing slash to shorten topics.
- Rows per unit, `<u>` = unit, `<U>` = capitalised unit:

| component | uniq_id | content |
|---|---|---|
| climate | `AC_<U>` | `modes` = the six `PAYLOAD_MODE_*` (HA's names, see the opt-in rule), `fan_modes` = `PAYLOAD_FAN_AUTO` + `"1"`-`"4"`, `swing_modes` = the vane names + `?`, mode/temperature/fan/swing command and state topics, `curr_temp_t` `Troom`, `act_t` `Action`, `min_temp` 18, `max_temp` 30, `temp_step` 0.5, `avty_t` `connected` with `pl_avail` `PAYLOAD_CONNECTED_TRUE` / `pl_not_avail` `PAYLOAD_CONNECTED_FALSE`, `name` from `config_defaults.h` |
| select | `ac_<u>_vanes` | `Vanes` / `set/Vanes`, options = the vane names, `default_entity_id` `select.ac_<u>_vanes` |
| binary_sensor | `ac_<u>_silent` | `Silent`, `pl_on` `on`, `pl_off` `off`, `dev_cla` `running` |
| binary_sensor | `ac_<u>_problem` | `Errorcode`, `val_tpl` `{{ 'ON' if value|int(0) != 0 else 'OFF' }}`, `dev_cla` `problem`, `ent_cat` diagnostic |
| binary_sensor | `ac_<u>_wiring` | `Wiring`, `val_tpl` `{{ 'OFF' if value == 'o.k.' else 'ON' }}`, `dev_cla` `problem`, `ent_cat` diagnostic |
| sensor | `ac_<u>_uptime` | `Uptime`, `dev_cla` duration, `unit_of_meas` s, `sug_dsp_prc` 0, no state class; plain seconds, because the hass-config reboot counter compares the raw state |
| sensor | `ac_<u>_free_heap` | `FreeHeap`, `dev_cla` data_size, `unit_of_meas` B, `stat_cla` measurement |
| sensor | `ac_<u>_rssi` | `RSSI`, `dev_cla` signal_strength, `unit_of_meas` dBm, `stat_cla` measurement |
| sensor | `ac_<u>_reset_reason` | `ResetReason`, `val_tpl` from `config_defaults.h` (repo default: none; Lucas's build: hass-config's Dutch mapping) |
| sensor | `ac_<u>_wifi_phy` | `WIFI_PHY` |

All rows: `avty_t` `connected` (`1`/`0`), `dev` `{ids: [HOSTNAME], name: <device name>, mf: "Mitsubishi Heavy Industries", sw: VERSION}`; the sensors and problem/wiring rows `ent_cat` `diagnostic`. Entity and device names come from `config_defaults.h` with English repo defaults; Lucas's build carries the names hass-config uses today (`airco-config.py` writes them), so dashboards see no change. The climate's friendly name changes from `AC_Slaapkamer` to the device+entity name; hass-config greps for friendly-name readers before the cutover.

- No automatic blanking of old configs: a rename here is a re-flash anyway. `SW-Configuration.md` documents the manual cleanup (`mosquitto_pub -r -n -t homeassistant/<component>/<uniq_id>/config`).

### 4.5 Cutover, per unit, with the hass-config session

1. **Rehearsal on Uitkijk, before any firmware.** Its YAML climate and five sensors commented out, MQTT YAML reloaded, the intended payloads (produced by the host test as files) published by hand with `mqtt.publish`, retained. Check: `climate.ac_uitkijk` and the five sensors keep their entity IDs and history, `bedtijd`/`zonnekoeling` unchanged, `sensor.ac_uitkijk_herstarts` still reads. Then blank the retained configs and restore the YAML. Any deviation changes the payloads before firmware is written.
2. **Cutover per unit:** remove that unit's YAML block, reload MQTT YAML, flash the unit, discovery arrives on connect, `post-flash-check.sh` verifies via HA's API that the climate and the sensors exist with the expected entity IDs and follow the topics. Flash order per Lucas's go; the promotion rule says Slaapkamer first.
3. hass-config rewrites `tests/test_airco_diagnose_304.py` and `tests/test_airco_topics_302.py` against the discovery payloads; the payload files from the host test are the fixture.

### 4.6 Verification

Both units: entities present and controllable from HA (fan round trip, a vane change by name), `Silent` follows the remote, `FreeHeap` steady around 43 KB over a night, health check green the next day.

## 5. Out of scope

Writing Eco/Silent/HI (no MISO bit known). OpData sensors in discovery (append later on demand). Vanes LR / 3D auto (extended frame not used here). Blanking old discovery configs automatically. Runtime configuration (#3, parked).

## 6. Risks

- **Heap in batch B:** 1024 B client buffer plus a 768 B builder buffer against ~44 KB free. Mitigation: one row per loop pass, the telemetry shows `FreeHeap` before and after, rollback image kept.
- **A noisy status byte** floods `diag/frame`: the 1 s rate limit bounds it, `set/Diag off` stops it, the mask silences it in the next build.
- **The `0x21` request** is a request the AC has not been seen to answer: same frame shape as the built-in codes, one slot, once; tried on the guinea pig first.
- **Cutover ordering:** a discovery config while the YAML entity exists is rejected as a duplicate; the per-unit procedure removes the YAML first. Rehearsed on Uitkijk.
