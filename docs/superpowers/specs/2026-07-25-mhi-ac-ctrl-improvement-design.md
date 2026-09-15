# MHI-AC-Ctrl Improvement Programme — Design

**Date:** 2026-07-25
**Status:** Approved (pending written-spec review)
**Baseline commit:** `821185b` (identical to upstream `absalom-muc/MHI-AC-Ctrl` HEAD)
**Target hardware:** 2× ESP8266 (Wemos D1 mini class), each installed in an MHI split AC indoor unit

---

## 1. Context and the decision

The repository is at upstream HEAD. The last tagged release is v2.8 (Oct 2023), but `master`
carries two unreleased changes (pubsubclient3 migration, passive mode). The project is alive but
minimally maintained: **4 commits in the last 12 months**, most of them README edits adding
supported AC model numbers.

Three alternative projects were evaluated before committing to this one.

| Project | Stack | Commits/12mo | Verdict |
|---|---|---|---|
| `ginkage/MHI-AC-Ctrl-ESPHome` | ESPHome component, MIT | 51 | Core is a verbatim copy of ours. Not adopted. |
| `hberntsen/mhi-ac-ctrl-esp32` | ESPHome + ESP-IDF, MIT | 79 | Hard-locked to ESP32. Not portable. |
| `SmOeL-swdev/MHI-AC-Ctrl` | Arduino/PlatformIO, MIT | — | Good ideas, unsafe dependencies. Not adopted. |
| **`absalom-muc/MHI-AC-Ctrl` (this)** | Arduino/MQTT, MIT | 4 | **Chosen.** |

**Decision: improve this firmware in place.** On any of these paths the user ends up maintaining
their own two units — nobody upstream is going to ship the wanted features. Given that, the right
choice is the stack already flashed, already understood, and already running: Arduino + MQTT on
ESP8266, no new hardware.

### 1.1 What is genuinely portable from the alternatives

Verified by full-file diff, not assumption.

**From `ginkage` — nothing at the SPI core level.** A complete `diff` of both `MHI-AC-Ctrl-core.h`
and `MHI-AC-Ctrl-core.cpp` shows their differences are: pins made `extern`, our passive mode
*removed*, `vanes_unknown` handling commented out, a debug `printf` commented out, and whitespace.
Zero new opcodes, masks, or decode branches. They are downstream of us.

**From `ginkage` — sensor-layer calibration fixes, yes.** Their PR #159 (2025-07-08) corrects the
THI-R2 formula. Ours reads `0.327f * value - 11.4f` (`MHI-AC-Ctrl.ino:372`, commented *"formula for
calculation not known"*); the corrected form is `0.275f * value - 47.0f`. Their issue #145 was this
bug: a sensor reporting 71 °C on a powered-off unit.

**From `hberntsen` — the code is ESP32-only, the protocol knowledge is not.** Their component
requires `driver/gpio.h`, `freertos/*`, `esp_timer.h`, `SPI2_HOST` and the ESP32 RMT peripheral, and
is gated by `esp32.only_on_variant(...)`. Porting the code is out of the question; porting the
findings is cheap. Their operation-data opcode set is *identical* to ours
(`0x02 0x05 0x0c 0x11 0x13 0x1e 0x1f 0x45 0x7c 0x80 0x81 0x82 0x85 0x87 0x90 0x94 0xb1`). The
tested delta is:

| Item | Value |
|---|---|
| `DB13 & 0x02` — heating vs cooling | Enables HA `hvac_action` |
| `DB13 & 0x04` — compressor running | Enables `idle` vs actively-conditioning |
| `FAN_DB6_MASK 0x40` | Correct fan-speed-4 detection |
| Per-datapoint discriminators, `(DB10 & 0x30) == 0x10` vs `0x20` applied consistently | Robustness; ours is looser in places |
| Named vane positions incl. `Wide`, `Spot`, `SeeIRRemote(255)` | Semantics for bare integers |

`DB13` is `SB2 + 14` — inside the standard 20-byte frame — so `hvac_action` works on both units
**without** `USE_EXTENDED_FRAME_SIZE`. Independently corroborated by the protocol spec
(`MHI-AC-Trace/SPI.md`): *"In MOSI-DB13 some bits seem to represent the status of the outdoor unit."*

### 1.2 Eco / Silent / HIGH-ECO — what is actually possible

Three sources agree that **ECO and Silent cannot be written** over CNS: this repo's own
`Troubleshooting.md` ("Known limitations"), the upstream maintainer in issue #219, and
`SmOeL-swdev/docs/ECO_Silent_Research.md` ("No MISO bit has been discovered for activating ECO or
Silent mode" → "Not implementable as of May 2025").

**Reading them is possible.** The research identifies `DB9 = 0x21` with sub-codes `0x10` / `0x11`,
which reconciles exactly with the `unknown: 4385, unknown: 4129` values reported in issue #219,
given our own encoding `cbiStatusFunction(opdata_unknown, DB10 << 8 | DB9)`:
`4385 = 0x1121` → DB10 `0x11`; `4129 = 0x1021` → DB10 `0x10`. The data already flows through our
firmware and is discarded as `opdata_unknown`.

**A third unknown is unexplained:** `32989 = 0x80DD` → `DB10 = 0x80, DB9 = 0xDD`. This is a
candidate for the HIGH/ECO button. It is deliberately **not** guessed at in this design; §5.5
specifies the diagnostic tooling that makes identifying it an empirical exercise.

**Out of scope, recorded for the future:** ProtoArt ClimateControl (clima.protoart.net), a
commercial controller on the same bus, reportedly ships firmware that *can* set Silent mode (but
still not ECO). Method undocumented. "Impossible" should therefore be read as "unsolved in the
open-source ecosystem."

---

## 2. Constraints

These drive the sequencing and are not negotiable within this design.

1. **No spare board.** Both ESP8266s are installed in ACs. One unit is physically reachable and
   serves as the guinea pig; the second stays on known-good firmware until a change has proven
   itself.
2. **OTA is the only recovery path** for the guinea pig, and reflashing the second unit means
   opening an AC. Anything that can prevent `loop()` from running is an outage.
3. **It is July.** These are daily-driver air conditioners in a home; extended downtime is a real
   cost, not a theoretical one.
4. **The user is the maintainer.** No upstream will fix regressions.

---

## 3. Dependency baseline

Audited 2026-07-25.

| Dependency | Pinned version | Notes |
|---|---|---|
| `platform-espressif8266` | **4.2.1** (Jul 2023) → ESP8266 core 3.1.2 | Latest; platform itself is frozen |
| `hmueller01/pubsubclient3` | **3.3.0** (Dec 2025) | Actively maintained |
| `OneWire` | **2.3.8** (May 2024) | No change needed |
| `DallasTemperature` | **4.0.6** (Feb 2026) | Safe upgrade — verified |
| `WiFiManager` (tzapu) | **2.0.17** (Mar 2024) | New; §5.3 |
| `ArduinoJson` | **7.4.3** (Mar 2026) | New; §5.4. v7 API, not v6 |
| `LittleFS` | built into ESP8266 core 3.x | **Not** `lorol/LittleFS_esp32` |

### 3.1 The `knolleary/PubSubClient` warning is still live

The README warns against PubSubClient v2.8 due to issue #747. That warning is **not** stale:

- Issue #747 (`publish_P()` crashes on ESP8266 with a PROGMEM payload) was filed 2020-06-14 and
  **remains open**.
- `knolleary/PubSubClient` has had **no code release since v2.8 (May 2020)** — the only commit since
  is a README edit in June 2026. 567 open issues.

The bug is not fixed in a newer version because there is no newer version. It also describes our
exact usage: `support.cpp:229` calls `publish_P()` with `PSTR()` payloads throughout. The project
already avoids it by using `hmueller01/pubsubclient3`. `SmOeL-swdev` instead pins
`knolleary/PubSubClient@~2.7`, dodging the bug by staying on a 2018 library — one reason that fork
was not adopted as a base.

### 3.2 DallasTemperature 4.x compatibility — verified

Every API this codebase calls is present in 4.0.6: `getTemp`, `rawToCelsius`, `getDS18Count`,
`getAddress`, `setResolution`, `setWaitForConversion`, `requestTemperatures`,
`DEVICE_DISCONNECTED_RAW`. 4.x additionally provides `DEVICE_POWER_ON_RESET_RAW` (the 85 °C
power-on glitch) and `DEVICE_INSUFFICIENT_POWER_RAW`, which will replace the current accidental
catch — those conditions are presently swallowed only because they exceed the `> 48 °C` sanity
clamp in `support.cpp:248`.

### 3.3 Toolchain

PlatformIO is already installed (`~/.local/bin/pio`) with **both** `espressif8266` and `native`
platforms present. §5.1 and §5.5 require no toolchain installation.

---

## 4. Approach

**Safety net → hardening → configuration → features.** Each phase is independently shippable to the
guinea pig and independently revertible. The deliberate trade-off, stated plainly: **nothing is
visible in Home Assistant until §5.4.** Phases 5.1–5.3 are invisible. This ordering is chosen
because with no spare board and OTA as the only lifeline, protecting the recovery path must precede
adding features to it.

Alternatives considered and rejected:

- *Features first* — fastest to visible value, but ships to a live AC with no compile safety net and
  the OTA-recovery bugs still present.
- *Full foundation first* — cleanest structurally, but weeks of reflashing a live unit with nothing
  to show, and a big-bang refactor is itself the riskiest thing to ship blind.
- *Greenfield rewrite, applying what was learned from all three alternative projects* — rejected;
  see §5.6 for the reasoning and for the preserve-core / rewrite-periphery split that replaces it.

---

## 5. Design

### 5.1 Toolchain and dependency baseline

Convert to a PlatformIO project with three environment groups:

- **`d1_mini`** — production firmware. `platform = espressif8266@4.2.1`,
  `board_build.f_cpu = 160000000L` (README notes better stability at 160 MHz), dependencies pinned
  per §3.
- **`native`** — host build for pure logic (checksums, temperature conversions, frame
  encode/decode) with Unity. No hardware; runs in CI.
- **`ci-*`** — build-only environments covering the `#ifdef` matrix: `USE_EXTENDED_FRAME_SIZE`,
  `ENHANCED_RESOLUTION`, `ROOM_TEMP_DS18X20`, `POWERON_WHEN_CHANGING_MODE`,
  `CONTINUE_WITHOUT_MQTT`. Nothing currently verifies these combinations compile.

`MHI-AC-Ctrl.ino` becomes `src/main.cpp`. GitHub Actions runs the matrix plus native tests on push.
`.gitignore` covers `.pio/` and `config_defaults.h`.

**Trade-off:** Arduino IDE compatibility is dropped. Keeping it means keeping the `.ino` layout,
which is what blocks CI. PlatformIO is already installed locally.

### 5.2 Reliability and OTA-safety

**Governing principle: `setup()` must always reach `loop()`.** Any hardware validation failure
degrades to a reported fault, never a halt. OTA only becomes reachable once `loop()` runs.

**Fail-safe boot.** `MeasureFrequency()` ends in `while (1);` (`support.cpp:59`) when MISO reads out
of range, and sits *before* `setupOTA()`. A wiring fault therefore produces an endless
hardware-WDT reboot loop with no OTA recovery — a screwdriver job. It becomes non-fatal: record the
measured frequencies, set a `degraded` flag, return. `loop()` brings up WiFi/MQTT normally and
publishes the fault to a diagnostics topic. The 1-second busy-wait gains a `yield()`.
*Corrected 2026-09-15 (#7):* a MISO fault means a foreign driver on our own output line, and the
halt existed to prevent exactly that contention. So after a MISO fault MISO is never switched to
output; the unit listens only. The original text missed that `init()` drives MISO regardless.
Only the MISO bit decides: a short to SCK or MOSI puts their edges on MISO, while a static short to
ground or supply produces no edges and is not caught by this check.

**Timeout symmetry.** `MHI-AC-Ctrl-core.cpp:257` — `while (!digitalRead(SCK_PIN)) {}` has no
timeout, unlike its sibling at line 249. A clock stall mid-byte spins until the hardware WDT fires.
It gains the same `max_time_ms` guard, returning `err_msg_timeout_SCK_low`.

**Bounded MQTT string handling.**
- `main.cpp:18` — `payload[length] = 0` writes into pubsubclient3's receive buffer. Traced:
  `payload = _buffer + payloadOffset`, `payloadLen = length - payloadOffset`, therefore
  `payload[length]` *is* `_buffer[length]` — a one-byte overflow when a message fills the 256-byte
  buffer. Replaced by copying into a bounded local before parsing.
- `support.cpp:228` — `strncat_P(dst, src, size - strlen(dst))` can write one past `mqtt_topic[100]`
  because `strncat` appends *n* characters **plus** a NUL. Replaced with `snprintf_P`.
- `support.cpp:222` — `mqtt_topic` is uninitialised if `status` matches none of the three type
  branches, then `strncat`-ed onto. Gains an explicit default and error path.

**Honest return contracts.**
- `support.cpp:203` returns `MQTT_CONNECTED` — PubSubClient's constant, not ours, working only
  because both happen to equal `0`. Becomes `MQTT_CONNECT_OK`.
- `MHI-AC-Ctrl-core.cpp:603` returns `call_counter`, which at ~20 frames/sec turns negative after
  ~3.4 years and makes `loop()` log phantom errors permanently. `loop()` returns a pure error code;
  the counter moves to its own accessor.

**Small correctness.**
- `main.cpp:517` — `byte tmp = offset * 4` with `offset` in −0.5…+0.5. Negative-float-to-unsigned
  conversion is undefined behaviour; it currently produces the right answer only via modular
  arithmetic on xtensa-gcc. Becomes explicit signed arithmetic.
- `support.cpp:263` — the DS18x20 sub-zero path returns `0`, which the caller's `> 21` guard
  silently drops. Result: the MQTT path accepts −10 °C but the sensor path cannot go below 0 °C.
- `support.cpp:283` — `printf("...0x%02x", insideThermometer)` prints a pointer, not the 8 address
  bytes. The correct `printAddress()` helper sits unused directly below.
- `support.cpp:311` — `printf_P("Error[%u]: %i\n", error)` has two specifiers and one argument.

**Shipping:** four independently flashable, independently revertible groups. Verification is
bench-observable via serial plus the new diagnostics topic; none of it changes AC behaviour.

### 5.3 Runtime configuration

**WiFiManager provisions; it does not own the connection.** The existing WiFi code is not naive:
`setupWiFi()` (`support.cpp:76-139`) runs an async-scan state machine that selects the strongest
matching BSSID and re-scans every 12 minutes to roam. WiFiManager would replace that with a plain
`WiFi.begin()`. It is therefore scoped to **provisioning only** — capture credentials and custom
parameters, persist, hand off. The roaming state machine is retained unchanged, reading SSID and
password from storage rather than `#define`s.

**Runtime-configurable:** WiFi SSID/password; MQTT server, port, user, password; **device name**
(drives hostname, OTA hostname, MQTT topic prefix, and the HA discovery `unique_id`); frame size
(20/33, per AC model). Device name is what allows one binary to serve both units.

**Storage:** LittleFS + ArduinoJson v7.

**The portal is time-boxed.** An indefinite AP-mode wait would stop the unit talking to the AC —
precisely what `CONTINUE_WITHOUT_MQTT` exists to prevent. It runs with a timeout, then falls back to
normal operation and retries. Triggers: no stored config, or an explicit `set/config_portal` MQTT
command. Not on transient WiFi loss.

**Migration.** Both units currently run compile-time credentials. New firmware booting with empty
storage would open a portal and **drop off the network** — acceptable for the guinea pig,
unacceptable for the far unit. First boot therefore seeds storage from an optional gitignored
`config_defaults.h` when present. The far unit is OTA'd, comes up with its existing credentials
already populated, and never needs the portal. **The portal is the recovery path, not the install
path.**

`support.h` loses `WIFI_SSID`, `WIFI_PASSWORD`, `MQTT_USER`, `MQTT_PASSWORD`.

**Cost:** ~40 KB flash for WiFiManager + ArduinoJson. A flash-size assertion is added to the CI
matrix so overruns surface at build time.

**Risk note:** WiFiManager carries 669 open issues and no release since March 2024. Sampling the 100
most recent open issues: 81 issues (19 were PRs), **74 of 81 unlabelled**, 3 labelled `bug`, with
titles such as *"Wifi connection"*, *"5GHz issue"*, *"Loading for the first time"*. This is an
untriaged support inbox for a 7.2k-star library, not a defect backlog. Provisioning-only scoping
further limits exposure. This is nonetheless the first phase that can knock a unit off the network,
so it ships to the guinea pig alone.

### 5.4 Feature work

**DB13 → `hvac_action`.** `DB13 & 0x02` (heating vs cooling) and `DB13 & 0x04` (compressor running),
per §1.1. `DB13` is not currently defined in `MHI-AC-Ctrl-core.h` — the header defines `DB0`–`DB4`,
`DB6`, `DB9`–`DB12`, `DB14` — and is added as `SB2 + 14`. Combined with power and mode this yields
HA's `heating` / `cooling` / `idle` / `off`.

**Eco/Silent, read-only.** `DB9 == 0x21` with `DB10` of `0x10` / `0x11` currently falls through to
`default:` and is published as `opdata_unknown`. It becomes an explicit case publishing two status
topics. **Which sub-code maps to which mode is unconfirmed in the source research.** It will be
determined empirically on the guinea pig by toggling each from the IR remote and observing the
topic. If it proves ambiguous, they ship as `mode_a` / `mode_b` rather than being guessed.

**Named vanes.** UD as Up/UpCenter/CenterDown/Down/Swing; LR as
Left/LeftCenter/Center/CenterRight/Right/Wide/Spot/Swing. The `set/` topics continue to accept the
existing numeric payloads; names are additive so no current configuration breaks.

**THI-R2 calibration fix.** `0.327f * value - 11.4f` → `0.275f * value - 47.0f` per §1.1.

**Home Assistant MQTT discovery.** Retained config payloads under `homeassistant/`, entities
decomposed following ginkage's model:

| Entity | Covers |
|---|---|
| `climate` | mode, setpoint, room temperature, fan, **action** |
| `select` ×2 | vanes UD, vanes LR |
| `switch` | 3D auto |
| `binary_sensor` | defrost, compressor, eco, silent |
| `sensor` | outdoor temp, current, power, kWh, remaining opdata |

Grouped under a `device` block keyed off the §5.3 device name, so both units appear in HA as
distinct devices with no hand-written YAML. Availability rides the existing LWT (`connected`).

**Resource constraint.** ~20 discovery payloads of a few hundred bytes each against pubsubclient3's
default `MQTT_MAX_PACKET_SIZE` of **256**. That is raised to 1024, and discovery is published **one
entity at a time, staggered across `loop()` iterations** rather than as a burst — the ESP8266 has
~40 KB free heap and the SPI bit-banging loop must not be starved while JSON is serialised. This is
the phase most likely to surface a RAM problem, which is why it lands last, on an already-hardened
base.

### 5.5 Protocol discovery tooling

`MHI-AC-Ctrl-core.cpp:599` publishes unknown operating data as `DB10 << 8 | DB9`, **discarding
`DB11` — the value byte**. Unknown data is therefore visible in the fact of its occurrence but never
in its content.

A diagnostics topic is added publishing `DB9` / `DB10` / `DB11` for unrecognised opcodes, plus
`DB5` and `DB13` (both flagged as undocumented in `MHI-AC-Trace/SPI.md`). This converts identifying
the unexplained `0xDD` opcode — and the HIGH/ECO button generally — into a bounded experiment:
press the button, observe the topic. It generalises to any undocumented remote function.

This is diagnostic only. No write path to ECO, Silent, or HIGH/ECO is in scope.

### 5.6 Structural cleanup — preserve the core, rewrite the periphery

**Rewrite-vs-improve was evaluated explicitly and rewriting was rejected**, for one decisive reason:
a greenfield rewrite has no incremental validation path, and incremental validation is the only kind
available here. With no spare board, one reachable unit and two live ACs, a rewrite is flashed whole
or not at all — structurally incompatible with the constraints in §2. Secondarily, the SPI core
encodes years of empirical protocol derivation across many AC models, and can be validated here
against exactly one.

The resulting split is deliberate, not timid:

| File | Treatment |
|---|---|
| `MHI-AC-Ctrl-core.cpp/.h` | **Preserved.** Surgical edits only: SCK timeout guard, `DB13` define, Eco/Silent case, discovery topic. The hard-won part. |
| `support.cpp` | **Effectively greenfield.** Only the WiFi roaming state machine is retained; configuration, MQTT plumbing, OTA and DS18x20 handling are rewritten across §5.2–§5.4 regardless. |
| `main.cpp` | **Substantially rewritten.** MQTT callback replaced (§5.2), the ~250-line status `switch` restructured as HA discovery lands (§5.4). |

Files are split and pure logic extracted as each phase touches that code, with host tests added at
the same time. No speculative refactor of the core; no timidity about the periphery.

---

## 6. Verification

| Phase | Method |
|---|---|
| 5.1 | CI: `#ifdef` matrix compiles; native tests pass; flash-size assertion holds |
| 5.2 | Serial + diagnostics topic on the guinea pig; deliberate fault injection (SCK unplugged at power-up → expect `Wiring` "SCK,MOSI" and a still-reachable device, not a reboot loop). Unplugging MISO cannot trigger a fault, because it only removes a driver; never inject a MISO fault by connecting MISO to a live signal (#7) |
| 5.3 | Guinea pig only. Portal reachable; roaming preserved; **`config_defaults.h` seeding confirmed before the far unit is touched** |
| 5.4 | Guinea pig + IR remote: toggle Eco/Silent/HIGH-ECO and observe topics; confirm `hvac_action` transitions on compressor start/stop; verify HA entities appear and are controllable |
| 5.5 | Press each undocumented remote button; confirm DB9/DB10/DB11 appear on the diagnostics topic |

**Host-testable (native, in CI):** frame checksums, temperature conversions, `hvac_action` derivation,
discovery-payload construction.
**Bench-only:** everything touching the SPI bus, WiFi provisioning, and HA integration. Per the
project's embedded testing policy, coverage targets apply to the pure-logic modules; hardware glue
is verified on the device.

**Promotion rule:** no change reaches the second unit until it has run clean on the guinea pig.

---

## 7. Explicitly out of scope

- Writing ECO, Silent, or HIGH/ECO (no known MISO bit; see §1.2)
- Reverse-engineering the ProtoArt Silent-write method
- ESP32 migration or new hardware
- Adopting ESPHome
- MQTT TLS
- Porting `hberntsen`'s ESP32 implementation
- Speculative refactoring beyond §5.6

---

## 8. Risks

| Risk | Mitigation |
|---|---|
| A bad flash bricks the far unit | It is not touched until a phase runs clean on the guinea pig; §5.3 seeding preserves credentials across the config change |
| WiFiManager misconfiguration drops a unit off the network | Provisioning-only scope; time-boxed portal; `config_defaults.h` seeding; guinea pig first |
| HA discovery exhausts ESP8266 RAM | Staggered one-at-a-time publishing; flash-size assertion in CI; last phase, on a hardened base |
| Eco/Silent sub-codes mapped backwards | Determined empirically, not guessed; ship as `mode_a`/`mode_b` if ambiguous |
| ESP8266 core is frozen (no release since Mar 2023) | Accepted. Platform pinned at 4.2.1; no upstream fixes expected |
| Diverging further from upstream | Accepted — upstream is at 4 commits/year and the user is the maintainer regardless |

---

## 9. Attribution

`hberntsen/mhi-ac-ctrl-esp32` (MIT) and `ginkage/MHI-AC-Ctrl-ESPHome` (MIT) are the sources of the
DB13 decode, vane semantics, per-datapoint discriminators and THI-R2 calibration. Both are MIT, as
is this project; copyright notices are preserved and both credited in the README and in comments at
each ported site.
