# Phase 4 batch B: named vanes, Silent switch, Home Assistant discovery, cutover — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Retire the hand-written `airco.yaml` in hass-config: the firmware publishes Home Assistant MQTT discovery configs for a climate, a vanes select with named positions, a Silent switch, problem/wiring binary sensors and five diagnostic sensors per unit, and the two units are cut over one at a time without losing entity IDs, history or automations.

**Architecture:** Three pure modules in `lib/mhi_pure` carry the logic and the host tests: `mhi_vanes` (names ↔ the core's numbers), `mhi_discovery` (one JSON row per entity, `vsnprintf` into a 1024 B buffer, the same code compiled on the host by `tools/discovery_payloads.cpp` to render a unit's real payloads), and one more byte in `mhi_diag_frame`'s mask. The SPI core gains a Silent write in the `0x80` command slot it already uses for `ErrOpData`, a `case 0xdd` decode and a `status_silent` status; `src/discovery.cpp` (compiled only with `HA_DISCOVERY`) publishes one row per `loop()` pass after every MQTT connect; `main.cpp` gains the `set/Silent` command, the `Silent` topic and the vane names. The cutover is rehearsed by hand on Uitkijk with hass-config before any firmware is flashed.

**Tech Stack:** PlatformIO, ESP8266 Arduino core 3.1.2, PubSubClient3 3.3.1, Unity host tests (`pio test -e native`), GitHub Actions CI (host tests + eleven build environments + flash-size guard), the toolkit in `~/.config/hass/tools/mhi/` (bash, Python 3, `node tools/ha-mqtt-capture.mjs` from hass-config).

**Spec:** `docs/superpowers/specs/2026-09-16-phase-4-batches-design.md` §4 (batch B, revised 17 Sep: no client buffer change, Silent as a switch, `DB3` mask, `DB7 & 0x02`, entity naming), with §5.4 of `docs/superpowers/specs/2026-07-25-mhi-ac-ctrl-improvement-design.md` as background. Fork issue #4 tracks the checklist; its "Batch A outcome" section holds the hardware evidence this plan builds on.

## Global Constraints

- `lib/mhi_pure` compiles without Arduino: plain `stdint.h`/`stddef.h`/`stdio.h`/`string.h`/`stdarg.h` only; every function there has a Unity test in `test/test_<module>/test_<module>.cpp` that failed before the implementation. The only Arduino-dependent thing allowed in `lib/mhi_pure` is the `#if defined(ARDUINO)` PROGMEM format-string macro of Task 5, because a 1 KB template must not live in RAM on the ESP8266.
- `-Werror` applies to `src/` (`build_src_flags`), `-Wall -Wextra -Werror` to the native tests. `main.cpp`'s status switch is exhaustive under `-Werror=switch`: every new `ACStatus` value needs a `case`.
- Flash budget 460000 bytes, asserted by `scripts/check_flash_size.py`; d1_mini was 343840 bytes at `2eab73c`.
- Every new topic and payload text is an `#ifndef`-guarded define in `src/MHI-AC-Ctrl.h`; every new option an `#ifndef`-guarded define in `src/support.h`, overridable from the gitignored `src/config_defaults.h`. **Never read or print `src/config_defaults.h`**: it holds real passwords.
- Payload casing follows the repo's existing switches: `Silent` and `set/Silent` use `On`/`Off`.
- No `setBufferSize()` call: PubSubClient3 3.3.1 streams publishes (spec §4.1). The discovery buffer is a `static char[MHI_DISCOVERY_BUF]` (1024) in BSS, never on the `loop()` stack.
- The discovery row table is append-only (`MhiDiscoveryRow`), and the `uniq_id`s are the ones hass-config uses today: `AC_<Unit>` for the climate, `ac_<unit>_<suffix>` for the rest.
- Nothing is flashed without Lucas's explicit go for that unit; the cutover order is rehearsal on Uitkijk (no firmware), then Slaapkamer (the reachable guinea pig, first Silent write with the AC off), then Uitkijk; a rollback image of the running version must exist (`flash-unit.sh` refuses otherwise). Lucas runs `flash-unit.sh` and `rollback-unit.sh` himself (`!`), they read secrets.
- Commits: conventional message referencing `#4`, ending with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`. Push over HTTPS (SSH is refused from this machine): `git -c credential.helper='!gh auth git-credential' push https://github.com/lvschouwen/MHI-AC-Ctrl.git <branch>`.
- Branch `feat/4b-vanes-silent-discovery` off `master`; merge `--ff-only` after CI is green; delete the branch.

---

## File map

| File | Responsibility |
|---|---|
| `lib/mhi_pure/mhi_vanes.h` / `.cpp` (new) | vane names ↔ the core's 0..5 |
| `test/test_mhi_vanes/test_mhi_vanes.cpp` (new) | Unity tests for it |
| `lib/mhi_pure/mhi_diag_frame.cpp`, `test/test_mhi_diag_frame/test_mhi_diag_frame.cpp` | `DB3` joins the compare mask |
| `lib/mhi_pure/mhi_action.h` | `MHI_DB7_OUTDOOR_RUNNING`, the pinned observation |
| `lib/mhi_pure/mhi_diag.h` / `.cpp` | `MHI_WIRING_OK` text, shared with discovery |
| `lib/mhi_pure/mhi_discovery.h` / `.cpp` (new) | the row table, the context, `mhi_discovery_build()`, `mhi_discovery_topic()`, `mhi_discovery_modes_valid()` |
| `test/test_mhi_discovery/test_mhi_discovery.cpp` (new), `test/fixtures/discovery/*.txt` (new) | tests, size measurement, the committed reference payloads |
| `tools/discovery_payloads.cpp` (new) | the builder as a host program: a unit's real payloads for the rehearsal and the checks |
| `src/MHI-AC-Ctrl-core.h` / `.cpp` | `status_silent`, `set_silent()`, the Silent write slot, `case 0xdd`, `{0xc0, 0xdd}` in the request table, the one-shot guard |
| `src/MHI-AC-Ctrl.h` | `PAYLOAD_VANES_1..4`, `TOPIC_SILENT`, `PAYLOAD_SILENT_*`, `TOPIC_DISCOVERY`, `PAYLOAD_DISCOVERY_*` |
| `src/support.h` | the `HA_*` options |
| `src/discovery.h` / `.cpp` (new) | the Arduino side of discovery: context from the macros, modes check, one row per pass, the `Discovery` topic |
| `src/main.cpp` | vane names, `set/Silent`, `Silent`, the three discovery calls |
| `platformio.ini`, `.github/workflows/ci.yml` | `ci-ha-discovery`, `HA_DISCOVERY` in `ci-all-options`, the fixture check |
| `SW-Configuration.md`, `Troubleshooting.md`, `Version.md` | docs |
| `~/.config/hass/tools/mhi/airco-config.py`, `test_airco_config.py`, `discovery-payloads.sh` (new), `post-flash-check.sh`, `remote-test.sh`, `lib.sh`, `README.md` | toolkit (outside the repo) |

---

### Task 1: Branch, and the vane names (pure)

**Files:**
- Create: `lib/mhi_pure/mhi_vanes.h`, `lib/mhi_pure/mhi_vanes.cpp`, `test/test_mhi_vanes/test_mhi_vanes.cpp`

**Interfaces:**
- Produces: `struct MhiVanesNames { const char* pos[4]; const char* swing; const char* unknown; }`, `const char* mhi_vanes_text(const MhiVanesNames*, int value)`, `int mhi_vanes_parse(const MhiVanesNames*, const char* payload)`, `MHI_VANES_UNKNOWN` (0), `MHI_VANES_SWING` (5). The numbers are the core's `ACVanes` (`vanes_1..vanes_4` = 1..4, `vanes_swing` = 5, `vanes_unknown` = 0).

- [ ] **Step 1: Create the branch**

```bash
cd /home/lucas/coding/MHI-AC-Ctrl && git checkout -b feat/4b-vanes-silent-discovery master
```

- [ ] **Step 2: Write the failing tests**

`test/test_mhi_vanes/test_mhi_vanes.cpp`:

```cpp
// Host tests for the named vane positions (fork issue #4, batch B; spec
// docs/superpowers/specs/2026-09-16-phase-4-batches-design.md §4.2).
//
// The texts belong to the caller (PAYLOAD_VANES_* in MHI-AC-Ctrl.h, overridable
// from config_defaults.h); this module only maps them to the core's numbers.

#include <string.h>
#include <unity.h>

#include "mhi_vanes.h"

void setUp(void) {}
void tearDown(void) {}

static const MhiVanesNames kNames = {{"Up", "UpCenter", "CenterDown", "Down"}, "Swing", "?"};

static void test_text_names_the_four_positions_and_swing(void) {
  TEST_ASSERT_EQUAL_STRING("Up", mhi_vanes_text(&kNames, 1));
  TEST_ASSERT_EQUAL_STRING("UpCenter", mhi_vanes_text(&kNames, 2));
  TEST_ASSERT_EQUAL_STRING("CenterDown", mhi_vanes_text(&kNames, 3));
  TEST_ASSERT_EQUAL_STRING("Down", mhi_vanes_text(&kNames, 4));
  TEST_ASSERT_EQUAL_STRING("Swing", mhi_vanes_text(&kNames, MHI_VANES_SWING));
}

static void test_text_is_unknown_for_anything_else(void) {
  // vanes_unknown: the last change came from the IR remote, the AC does not say where the vanes are.
  TEST_ASSERT_EQUAL_STRING("?", mhi_vanes_text(&kNames, MHI_VANES_UNKNOWN));
  TEST_ASSERT_EQUAL_STRING("?", mhi_vanes_text(&kNames, 6));
  TEST_ASSERT_EQUAL_STRING("?", mhi_vanes_text(&kNames, -1));
}

static void test_parse_accepts_the_names(void) {
  TEST_ASSERT_EQUAL_INT(1, mhi_vanes_parse(&kNames, "Up"));
  TEST_ASSERT_EQUAL_INT(2, mhi_vanes_parse(&kNames, "UpCenter"));
  TEST_ASSERT_EQUAL_INT(3, mhi_vanes_parse(&kNames, "CenterDown"));
  TEST_ASSERT_EQUAL_INT(4, mhi_vanes_parse(&kNames, "Down"));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_SWING, mhi_vanes_parse(&kNames, "Swing"));
}

static void test_parse_still_accepts_the_numbers_of_v2_8(void) {
  // Existing configurations write 1..4 and 5 for swing; they keep working.
  TEST_ASSERT_EQUAL_INT(1, mhi_vanes_parse(&kNames, "1"));
  TEST_ASSERT_EQUAL_INT(4, mhi_vanes_parse(&kNames, "4"));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_SWING, mhi_vanes_parse(&kNames, "5"));
}

static void test_parse_rejects_everything_else(void) {
  TEST_ASSERT_EQUAL_INT(MHI_VANES_UNKNOWN, mhi_vanes_parse(&kNames, "0"));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_UNKNOWN, mhi_vanes_parse(&kNames, "6"));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_UNKNOWN, mhi_vanes_parse(&kNames, "?"));   // a state, not a command
  TEST_ASSERT_EQUAL_INT(MHI_VANES_UNKNOWN, mhi_vanes_parse(&kNames, "up"));  // case-sensitive, like every payload
  TEST_ASSERT_EQUAL_INT(MHI_VANES_UNKNOWN, mhi_vanes_parse(&kNames, "Up "));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_UNKNOWN, mhi_vanes_parse(&kNames, "11"));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_UNKNOWN, mhi_vanes_parse(&kNames, ""));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_UNKNOWN, mhi_vanes_parse(&kNames, NULL));
}

static void test_a_configuration_that_keeps_the_numbers_as_names_still_works(void) {
  // A user who defines PAYLOAD_VANES_1 "1" keeps v2.8's texts on the topic too.
  const MhiVanesNames numeric = {{"1", "2", "3", "4"}, "Swing", "?"};
  TEST_ASSERT_EQUAL_STRING("3", mhi_vanes_text(&numeric, 3));
  TEST_ASSERT_EQUAL_INT(3, mhi_vanes_parse(&numeric, "3"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_text_names_the_four_positions_and_swing);
  RUN_TEST(test_text_is_unknown_for_anything_else);
  RUN_TEST(test_parse_accepts_the_names);
  RUN_TEST(test_parse_still_accepts_the_numbers_of_v2_8);
  RUN_TEST(test_parse_rejects_everything_else);
  RUN_TEST(test_a_configuration_that_keeps_the_numbers_as_names_still_works);
  return UNITY_END();
}
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `pio test -e native -f test_mhi_vanes`
Expected: build error, `mhi_vanes.h: No such file or directory`.

- [ ] **Step 4: Write the implementation**

`lib/mhi_pure/mhi_vanes.h`:

```cpp
// Named vane positions (fork issue #4, batch B; spec §4.2).
//
// The AC knows five vane settings: four up/down positions and swing, and it
// reports "unknown" after the IR remote moved them. The texts on the Vanes
// topic are the caller's (PAYLOAD_VANES_* in MHI-AC-Ctrl.h), so a
// configuration may keep v2.8's "1".."4"; set/Vanes accepts the names and the
// numbers either way.
//
// Pure logic, no Arduino.

#pragma once

// The core's ACVanes values.
#define MHI_VANES_UNKNOWN 0
#define MHI_VANES_SWING 5

struct MhiVanesNames {
  const char* pos[4];   // positions 1..4, top to bottom
  const char* swing;
  const char* unknown;  // published when the AC does not say where the vanes are
};

// 1..4 -> the position's name, MHI_VANES_SWING -> swing, anything else -> unknown.
const char* mhi_vanes_text(const MhiVanesNames* names, int value);

// A set/Vanes payload: one of the five names, or "1".."5" (5 = swing).
// MHI_VANES_UNKNOWN when it is none of them (NULL and "" included).
int mhi_vanes_parse(const MhiVanesNames* names, const char* payload);
```

`lib/mhi_pure/mhi_vanes.cpp`:

```cpp
#include "mhi_vanes.h"

#include <string.h>

const char* mhi_vanes_text(const MhiVanesNames* names, int value) {
  if (value >= 1 && value <= 4) return names->pos[value - 1];
  if (value == MHI_VANES_SWING) return names->swing;
  return names->unknown;
}

int mhi_vanes_parse(const MhiVanesNames* names, const char* payload) {
  if (!payload || !*payload) return MHI_VANES_UNKNOWN;
  for (int i = 0; i < 4; i++)
    if (strcmp(payload, names->pos[i]) == 0) return i + 1;
  if (strcmp(payload, names->swing) == 0) return MHI_VANES_SWING;
  // v2.8's numbers: exactly one digit 1..5.
  if (payload[1] == '\0' && payload[0] >= '1' && payload[0] <= '5') return payload[0] - '0';
  return MHI_VANES_UNKNOWN;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `pio test -e native -f test_mhi_vanes`
Expected: `6 Tests 0 Failures 0 Ignored`.

- [ ] **Step 6: Commit**

```bash
git add lib/mhi_pure/mhi_vanes.h lib/mhi_pure/mhi_vanes.cpp test/test_mhi_vanes/test_mhi_vanes.cpp
git commit -m "feat: vane position names and their parser, host-tested (#4)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: Named vanes on the `Vanes` topic and in `set/Vanes`

**Files:**
- Modify: `src/MHI-AC-Ctrl.h` (the `PAYLOAD_VANES_*` block near line 242), `src/main.cpp` (includes near line 10, the `set/Vanes` branch at lines 153-166, the `status_vanes` case at lines 372-384)

**Interfaces:**
- Consumes: `MhiVanesNames`, `mhi_vanes_text()`, `mhi_vanes_parse()`, `MHI_VANES_UNKNOWN` from Task 1.
- Produces: `PAYLOAD_VANES_1` `"Up"`, `PAYLOAD_VANES_2` `"UpCenter"`, `PAYLOAD_VANES_3` `"CenterDown"`, `PAYLOAD_VANES_4` `"Down"` (Task 6's discovery context reads them); `static const MhiVanesNames vanes_names` in `main.cpp`.

- [ ] **Step 1: Add the four name macros**

In `src/MHI-AC-Ctrl.h`, directly after the `PAYLOAD_VANES_SWING` block:

```cpp
// Vane positions, top to bottom (fork #4 batch B). set/Vanes also accepts 1..4
// and 5 (= swing) whatever these say; define them as "1".."4" to keep v2.8's
// texts on the topic.
#ifndef PAYLOAD_VANES_1
#define PAYLOAD_VANES_1 "Up"
#endif
#ifndef PAYLOAD_VANES_2
#define PAYLOAD_VANES_2 "UpCenter"
#endif
#ifndef PAYLOAD_VANES_3
#define PAYLOAD_VANES_3 "CenterDown"
#endif
#ifndef PAYLOAD_VANES_4
#define PAYLOAD_VANES_4 "Down"
#endif
```

- [ ] **Step 2: Use them in `main.cpp`**

Add `#include "mhi_vanes.h"` to the include list (alphabetically after `mhi_troom_filter.h`). After the `diag_on` definition (line 39) add:

```cpp
// The texts on the Vanes topic; set/Vanes accepts these and 1..5 (fork #4 batch B).
static const MhiVanesNames vanes_names = {{PAYLOAD_VANES_1, PAYLOAD_VANES_2, PAYLOAD_VANES_3, PAYLOAD_VANES_4},
                                          PAYLOAD_VANES_SWING, PAYLOAD_VANES_UNKNOWN};
```

Replace the `set/Vanes` branch (the whole `else if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_VANES)) == 0) { ... }` block, lines 153-166) with:

```cpp
  else if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_VANES)) == 0) {
    const int vanes = mhi_vanes_parse(&vanes_names, payload_str);
    if (vanes != MHI_VANES_UNKNOWN) {
      mhi_ac_ctrl_core.set_vanes(vanes);
      publish_cmd_ok();
    }
    else
      publish_cmd_invalidparameter();
  }
```

Replace the `case status_vanes:` block (lines 372-384, up to and including its `break;`) with:

```cpp
        case status_vanes:
          output_P(status, PSTR(TOPIC_VANES), mhi_vanes_text(&vanes_names, value));
          break;
```

(`output_P` takes a `PGM_P` payload; a RAM string is fine there, `case status_fan` already passes `strtmp`. `vanes_unknown` and `vanes_swing` are 0 and 5, the same numbers `mhi_vanes_text` maps.)

- [ ] **Step 3: Build**

Run: `pio run -e d1_mini -e ci-custom-payloads -e ci-extended-frame`
Expected: three builds succeed, no warnings from `src/`.

- [ ] **Step 4: Commit**

```bash
git add src/MHI-AC-Ctrl.h src/main.cpp
git commit -m "feat: Vanes publishes Up/UpCenter/CenterDown/Down/Swing, set/Vanes accepts the names and 1..5 (#4)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: `DB3` in the compare mask, `DB7 & 0x02` pinned, the wiring text shared

**Files:**
- Modify: `lib/mhi_pure/mhi_diag_frame.cpp:9-18` (`mhi_diag_mask_default`), `lib/mhi_pure/mhi_diag_frame.h` (the mask comment), `test/test_mhi_diag_frame/test_mhi_diag_frame.cpp` (the two mask tests near lines 68-95), `lib/mhi_pure/mhi_action.h`, `lib/mhi_pure/mhi_diag.h`, `lib/mhi_pure/mhi_diag.cpp:35`, `test/test_mhi_diag/test_mhi_diag.cpp`

**Interfaces:**
- Produces: `MHI_DB7_OUTDOOR_RUNNING` (0x02) in `mhi_action.h`; `MHI_WIRING_OK` (`"o.k."`) in `mhi_diag.h`, used by Task 6's discovery context.

- [ ] **Step 1: Write the failing tests**

In `test/test_mhi_diag_frame/test_mhi_diag_frame.cpp`, in `test_default_mask_ignores_what_changes_on_its_own`, add after the `SB2` assertion:

```cpp
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[DB3]);   // raw Troom dithers at a temperature boundary (16 Sep 2026)
```

and add a new test after `test_default_mask_compares_the_status_bytes_in_full`:

```cpp
static void test_a_room_temperature_dither_alone_publishes_nothing(void) {
  // Uitkijk, 16 Sep 2026: "DB3 89>8a" up to 13 times a minute while the room
  // sat on a boundary. Troom already carries the filtered value.
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 20);
  char out[MHI_DIAG_TEXT_MAX];
  TEST_ASSERT_TRUE(mhi_diag_frame_changes(&d, kFrame, 20, mask, out, sizeof(out)) > 0);  // the first frame
  uint8_t dither[20];
  memcpy(dither, kFrame, 20);
  dither[DB3] = kFrame[DB3] + 1;
  TEST_ASSERT_EQUAL_size_t(0, mhi_diag_frame_changes(&d, dither, 20, mask, out, sizeof(out)));
}
```

Register it in `main()` with `RUN_TEST(test_a_room_temperature_dither_alone_publishes_nothing);` after the mask tests. (`kFrame` is defined above the frame-diff tests; move the new test below that definition.)

In `test/test_mhi_diag/test_mhi_diag.cpp`, add a test next to the existing `mhi_wiring_fault_text` tests:

```cpp
static void test_the_ok_text_is_the_shared_constant(void) {
  // Discovery's wiring binary sensor compares the Wiring topic with this text.
  char out[16];
  mhi_wiring_fault_text(0, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING(MHI_WIRING_OK, out);
  TEST_ASSERT_EQUAL_STRING("o.k.", MHI_WIRING_OK);
}
```

and register it in `main()`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `pio test -e native -f test_mhi_diag_frame -f test_mhi_diag`
Expected: `test_mhi_diag_frame` fails on `mask[DB3]` (expected 0x00, was 0xff); `test_mhi_diag` fails to compile (`MHI_WIRING_OK` undeclared).

- [ ] **Step 3: Implement**

`lib/mhi_pure/mhi_diag_frame.cpp`, in `mhi_diag_mask_default`, after the `SB0..SB2` loop:

```cpp
  if (len > DB3) mask[DB3] = 0x00;   // raw Troom dithers at a boundary; Troom carries the filtered value
```

`lib/mhi_pure/mhi_diag_frame.h`: in the mask comment, change "The header, the operating-data bytes DB9-DB12, the checksum bytes and the frame toggle in DB14 bit 2 are ignored" to "The header, DB3 (raw Troom, which dithers at a temperature boundary), the operating-data bytes DB9-DB12, the checksum bytes and the frame toggle in DB14 bit 2 are ignored".

`lib/mhi_pure/mhi_action.h`, after `#define MHI_DB13_COMPRESSOR 0x04`:

```cpp
// Undocumented in SPI.md, observed on Uitkijk on 16 Sep 2026 (fork #4, batch A
// remote test): DB7 bit 1 changed in the same frame as DB13 5<->1 both times
// the compressor stopped and started (~8 min on, ~3 min off all evening).
// Recorded, not decoded: Action already follows DB13.
#define MHI_DB7_OUTDOOR_RUNNING 0x02
```

`lib/mhi_pure/mhi_diag.h`, after the `MhiWiringFault` enum:

```cpp
// What mhi_wiring_fault_text writes for a clean board. Discovery's wiring
// binary sensor compares the Wiring topic with it.
#define MHI_WIRING_OK "o.k."
```

`lib/mhi_pure/mhi_diag.cpp:35`: replace the literal `"o.k."` in `for (const char* c = "o.k."; ...` with `MHI_WIRING_OK`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `pio test -e native`
Expected: `0 Failures` in every suite (batch A had 126 tests; Task 1 added 6, this task adds 2).

- [ ] **Step 5: Commit**

```bash
git add lib/mhi_pure/mhi_diag_frame.cpp lib/mhi_pure/mhi_diag_frame.h lib/mhi_pure/mhi_action.h lib/mhi_pure/mhi_diag.h lib/mhi_pure/mhi_diag.cpp test/test_mhi_diag_frame/test_mhi_diag_frame.cpp test/test_mhi_diag/test_mhi_diag.cpp
git commit -m "fix: diag/frame ignores the raw room temperature, DB7 bit 1 recorded as outdoor unit running (#4)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: Silent in the core, `set/Silent` and the `Silent` topic

**Files:**
- Modify: `src/MHI-AC-Ctrl-core.h` (request table lines 8-29, `ACStatus` enum line 52-53, the `status_*_old` members near line 96-104, the `request_*` members near line 138-143, the public methods near line 172-173), `src/MHI-AC-Ctrl-core.cpp` (`reset_old_values()` line 11-25, after `request_OpData()` line 105-109, the request window line 168-186, the doubleframe branch lines 222-226, the operating-data switch before `case 0x00:` line 609), `src/MHI-AC-Ctrl.h` (topics near line 156, payloads near line 268), `src/main.cpp` (the command chain before the final `else publish_cmd_unknown();`, the status switch after `case status_action`)

**Interfaces:**
- Produces: `status_silent` in `ACStatus` (type_status group, value 1 = on); `void MHI_AC_Ctrl_Core::set_silent(bool on)`; `TOPIC_SILENT` `"Silent"`, `PAYLOAD_SILENT_ON` `"On"`, `PAYLOAD_SILENT_OFF` `"Off"` (Task 6 reads them).

This task has no host test: the core is Arduino code. The bench proof is Task 10 (Silent on/off from HA with `remote-test.sh` running, the `Silent` topic following, then the remote's SILENT button following on the topic). The pure parts touched here are none.

- [ ] **Step 1: The core header**

In `src/MHI-AC-Ctrl-core.h`, request table: add after the `energy-used` line (keep the trailing blank line and the closing brace):

```cpp
  { 0xc0, 0xdd},  //    "SILENT" (fork #4): DB11 bit 5; the AC also reports it unasked after a remote press
```

`ACStatus` enum, line 52: change `status_errorcode, status_action,` to `status_errorcode, status_action, status_silent,` (before the `raw_frame, raw_opdata,` line).

Members: after `byte status_action_old;` add `byte status_silent_old;`. After the `request_opdata_code` member add:

```cpp
    // Silent operation write (fork #4 batch B): DB6 0x80, DB9 0x21, DB10 0/1
    // in the next MISO frame pair, the write hberntsen/mhi-ac-ctrl-esp32 PR #42
    // carries (traced from a ProtoArt controller by mreijnde). Shares the 0x80
    // command slot with request_erropData.
    bool request_silent_pending = false;
    byte new_silent = 0;
```

Public methods: after `void request_OpData(byte prefix, byte code);` add:

```cpp
    void set_silent(bool on);             // Silent operation on/off; the Silent status confirms it within a second or two
```

- [ ] **Step 2: The core implementation**

`src/MHI-AC-Ctrl-core.cpp`, `reset_old_values()`: after `status_action_old = 0xff;` add `status_silent_old = 0xff;`.

After `request_OpData()` add:

```cpp
void MHI_AC_Ctrl_Core::set_silent(bool on) {
  new_silent = on ? 0x01 : 0x00;
  request_silent_pending = true;  // a second command before it is sent replaces it
}
```

The request window (line 171): change `if (request_opdata_pending) {` to

```cpp
        if (request_opdata_pending && !request_erropData && !request_silent_pending) {
          // The 0x80 command slot below would overwrite DB6/DB9 in this same
          // pair; the probe waits for the next window instead of being lost.
```

The doubleframe branch: replace

```cpp
    if (request_erropData) {
      MISO_frame[DB6] = 0x80;
      MISO_frame[DB9] = 0x45;
      request_erropData = false;
    }
```

with

```cpp
    MISO_frame[DB10] = 0xff;  // its idle value again after a Silent write
    if (request_erropData) {
      MISO_frame[DB6] = 0x80;
      MISO_frame[DB9] = 0x45;
      request_erropData = false;
    }
    else if (request_silent_pending) {  // the same 0x80 command slot; waits one pair behind ErrOpData
      MISO_frame[DB6] = 0x80;
      MISO_frame[DB9] = 0x21;
      MISO_frame[DB10] = new_silent;
      request_silent_pending = false;
      if (!request_opdata_pending)      // read it back so the Silent topic confirms the write; a user's probe is not replaced
        request_OpData(0xc0, 0xdd);
    }
```

(`MISO_frame[DB10]` starts as `0xff` in the frame's initialiser and nothing else ever writes it, so the first line changes nothing until a Silent write has happened.)

The operating-data switch: before `case 0x00:  // dummy` add:

```cpp
      case 0xdd:                              // Silent operation (fork #4): reported unasked after a remote press, and on request
        if (MOSI_frame[DB10] == 0x80) {       // the answer's type byte; 'dd 80 20 00' on, 'dd 80 00 00' off (Uitkijk, 16 Sep 2026)
          const byte silenttmp = (MOSI_frame[DB11] & 0x20) != 0;
          if (silenttmp != status_silent_old) {
            status_silent_old = silenttmp;
            m_cbiStatus->cbiStatusFunction(status_silent, silenttmp);
          }
        }
        break;
```

- [ ] **Step 3: Topics and payloads**

`src/MHI-AC-Ctrl.h`: after the `TOPIC_REQUEST_PASSIVEMODE` block add

```cpp
#ifndef TOPIC_SILENT
#define TOPIC_SILENT "Silent"                 // status and set/ command (fork #4 batch B)
#endif
```

and after the `PAYLOAD_DIAG_OFF` block add

```cpp
#ifndef PAYLOAD_SILENT_ON
#define PAYLOAD_SILENT_ON "On"
#endif
#ifndef PAYLOAD_SILENT_OFF
#define PAYLOAD_SILENT_OFF "Off"
#endif
```

- [ ] **Step 4: `main.cpp`**

In `MQTT_subscribe_callback`, before the final `else publish_cmd_unknown();` add:

```cpp
  else if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_SILENT)) == 0) {
    if (strcmp_P(payload_str, PSTR(PAYLOAD_SILENT_ON)) == 0) {
      mhi_ac_ctrl_core.set_silent(true);
      publish_cmd_ok();
    }
    else if (strcmp_P(payload_str, PSTR(PAYLOAD_SILENT_OFF)) == 0) {
      mhi_ac_ctrl_core.set_silent(false);
      publish_cmd_ok();
    }
    else
      publish_cmd_invalidparameter();
  }
```

In `cbiStatusFunction`, after the `case status_action:` block's `break;` add:

```cpp
        case status_silent:
          if (value)
            output_P(status, PSTR(TOPIC_SILENT), PSTR(PAYLOAD_SILENT_ON));
          else
            output_P(status, PSTR(TOPIC_SILENT), PSTR(PAYLOAD_SILENT_OFF));
          break;
```

- [ ] **Step 5: Build**

Run: `pio run -e d1_mini -e ci-all-options -e ci-custom-payloads`
Expected: all succeed. A missing `case status_silent` would fail `d1_mini` with `-Werror=switch`; this build proves the switch is complete.

- [ ] **Step 6: Commit**

```bash
git add src/MHI-AC-Ctrl-core.h src/MHI-AC-Ctrl-core.cpp src/MHI-AC-Ctrl.h src/main.cpp
git commit -m "feat: Silent topic and set/Silent, decoded from the 0xdd answer and written as hberntsen PR #42 does (#4)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 5: The discovery builder (pure) and the reference fixtures

**Files:**
- Create: `lib/mhi_pure/mhi_discovery.h`, `lib/mhi_pure/mhi_discovery.cpp`, `test/test_mhi_discovery/test_mhi_discovery.cpp`, `test/fixtures/discovery/` (ten `.txt` files the test writes)

**Interfaces:**
- Consumes: `MHI_WIRING_OK` from Task 3 (in the test only, as a literal `"o.k."`).
- Produces: `enum MhiDiscoveryRow` (ten rows + `MHI_DISCOVERY_ROWS`), `struct MhiDiscoveryCtx` (field order below is binding), `bool mhi_discovery_modes_valid(const MhiDiscoveryCtx*)`, `size_t mhi_discovery_topic(MhiDiscoveryRow, const MhiDiscoveryCtx*, char*, size_t)`, `size_t mhi_discovery_build(MhiDiscoveryRow, const MhiDiscoveryCtx*, char*, size_t)`, `MHI_DISCOVERY_BUF` (1024), `MHI_DISCOVERY_TOPIC_MAX` (96). Tasks 6 and 7 fill the context.

- [ ] **Step 1: Write the header first (the tests include it)**

`lib/mhi_pure/mhi_discovery.h`:

```cpp
// Home Assistant MQTT discovery payloads (fork issue #4, batch B; spec
// docs/superpowers/specs/2026-09-16-phase-4-batches-design.md §4.4).
//
// One JSON config per entity, published retained under
// <discovery_prefix>/<component>/<uniq_id>/config after every MQTT connect.
// Every text comes in through the context (topics, payloads and names are the
// firmware's #ifndef-guarded macros), so this file is the same on the ESP8266,
// in the host tests and in tools/discovery_payloads.cpp.
//
// The row table is append-only: Home Assistant keeps a retained config per
// topic, and the topic carries the uniq_id, so renumbering orphans entities.

#pragma once

#include <stddef.h>
#include <stdint.h>

#define MHI_DISCOVERY_BUF 1024      // the payload buffer; the host test measures every row against it
#define MHI_DISCOVERY_TOPIC_MAX 96

enum MhiDiscoveryRow : uint8_t {
  MHI_DISCOVERY_CLIMATE,       // climate  <climate_id>
  MHI_DISCOVERY_VANES,         // select   <id_prefix>_vanes
  MHI_DISCOVERY_SILENT,        // switch   <id_prefix>_silent
  MHI_DISCOVERY_PROBLEM,       // binary_sensor <id_prefix>_problem (Errorcode != 0)
  MHI_DISCOVERY_WIRING,        // binary_sensor <id_prefix>_wiring (Wiring != o.k.)
  MHI_DISCOVERY_UPTIME,        // sensor   <id_prefix>_uptime
  MHI_DISCOVERY_FREE_HEAP,     // sensor   <id_prefix>_free_heap
  MHI_DISCOVERY_RSSI,          // sensor   <id_prefix>_rssi
  MHI_DISCOVERY_RESET_REASON,  // sensor   <id_prefix>_reset_reason
  MHI_DISCOVERY_WIFI_PHY,      // sensor   <id_prefix>_wifi_phy
  MHI_DISCOVERY_ROWS
};

// Everything a payload is made of. Field order is binding: main.cpp and the
// tests fill it with designated initialisers, which GCC requires in order.
struct MhiDiscoveryCtx {
  const char* discovery_prefix;  // "homeassistant"
  const char* base;              // MQTT_PREFIX without its trailing slash, the payload's "~"
  const char* set_prefix;        // what MQTT_SET_PREFIX adds to MQTT_PREFIX, "set/"
  const char* hostname;          // the device identifier (dev.ids)
  const char* device_name;       // dev.name; HA prefixes every entity name with it
  const char* version;           // dev.sw
  const char* climate_id;        // uniq_id of the climate, e.g. "AC_Slaapkamer"
  const char* id_prefix;         // uniq_id prefix of the other rows, e.g. "ac_slaapkamer"
  const char* entity_prefix;     // default_entity_id prefix, e.g. "ac_slaapkamer" -> select.ac_slaapkamer_vanes; NULL: none
  const char* names[MHI_DISCOVERY_ROWS];  // entity names; [MHI_DISCOVERY_CLIMATE] is unused (the climate is named after the device)
  const char* reset_reason_tpl;  // value template of the reset-reason sensor; NULL: none
  // Topic texts (TOPIC_*), relative to base.
  const char* t_mode;
  const char* t_tsetpoint;
  const char* t_fan;
  const char* t_vanes;
  const char* t_troom;
  const char* t_action;
  const char* t_connected;
  const char* t_silent;
  const char* t_errorcode;
  const char* t_wiring;
  const char* t_uptime;
  const char* t_free_heap;
  const char* t_rssi;
  const char* t_reset_reason;
  const char* t_wifi_phy;
  // Payload texts.
  const char* modes[6];          // PAYLOAD_MODE_OFF, _AUTO, _DRY, _COOL, _FAN, _HEAT
  const char* fan_auto;          // PAYLOAD_FAN_AUTO; the levels are the literal "1".."4" main.cpp hard-codes
  const char* vanes[6];          // PAYLOAD_VANES_1..4, _SWING, _UNKNOWN
  const char* connected_on;      // PAYLOAD_CONNECTED_TRUE
  const char* connected_off;     // PAYLOAD_CONNECTED_FALSE
  const char* silent_on;         // PAYLOAD_SILENT_ON
  const char* silent_off;        // PAYLOAD_SILENT_OFF
  const char* wiring_ok;         // MHI_WIRING_OK
};

// Home Assistant's climate accepts only its own mode names: off, auto, dry,
// cool, fan_only, heat. False means the climate row must not be published.
bool mhi_discovery_modes_valid(const MhiDiscoveryCtx* ctx);

// "<discovery_prefix>/<component>/<uniq_id>/config". Returns the length,
// 0 when it does not fit out_len.
size_t mhi_discovery_topic(MhiDiscoveryRow row, const MhiDiscoveryCtx* ctx, char* out, size_t out_len);

// One row's JSON. Returns the length, 0 (and an empty string) when it does
// not fit out_len. out_len should be MHI_DISCOVERY_BUF.
size_t mhi_discovery_build(MhiDiscoveryRow row, const MhiDiscoveryCtx* ctx, char* out, size_t out_len);
```

- [ ] **Step 2: Write the failing tests**

`test/test_mhi_discovery/test_mhi_discovery.cpp`:

```cpp
// Host tests for the Home Assistant discovery payloads (fork issue #4, batch B;
// spec docs/superpowers/specs/2026-09-16-phase-4-batches-design.md §4.4).
//
// Beyond correctness, this suite is the size check the design relies on: every
// row of the reference build is measured against MHI_DISCOVERY_BUF and the
// longest is printed. It also writes the reference payloads to
// test/fixtures/discovery/ so a reviewer sees the JSON and CI fails when the
// committed copies no longer match the code.

#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "mhi_discovery.h"

void setUp(void) {}
void tearDown(void) {}

// The repo defaults with Home Assistant's mode names (ci-custom-payloads).
static const MhiDiscoveryCtx kDefault = {
  .discovery_prefix = "homeassistant",
  .base = "MHI-AC-Ctrl",
  .set_prefix = "set/",
  .hostname = "MHI-AC-Ctrl",
  .device_name = "MHI-AC-Ctrl",
  .version = "fixture",
  .climate_id = "MHI-AC-Ctrl",
  .id_prefix = "MHI-AC-Ctrl",
  .entity_prefix = NULL,
  .names = {NULL, "Vanes", "Silent", "Problem", "Wiring", "Uptime", "Free heap", "Wi-Fi signal", "Reset reason", "Wi-Fi PHY"},
  .reset_reason_tpl = NULL,
  .t_mode = "Mode", .t_tsetpoint = "Tsetpoint", .t_fan = "Fan", .t_vanes = "Vanes", .t_troom = "Troom", .t_action = "Action",
  .t_connected = "connected", .t_silent = "Silent", .t_errorcode = "Errorcode", .t_wiring = "Wiring",
  .t_uptime = "Uptime", .t_free_heap = "FreeHeap", .t_rssi = "RSSI", .t_reset_reason = "ResetReason", .t_wifi_phy = "WIFI_PHY",
  .modes = {"off", "auto", "dry", "cool", "fan_only", "heat"},
  .fan_auto = "Auto",
  .vanes = {"Up", "UpCenter", "CenterDown", "Down", "Swing", "?"},
  .connected_on = "1", .connected_off = "0",
  .silent_on = "On", .silent_off = "Off",
  .wiring_ok = "o.k.",
};

// Lucas's Uitkijk: the names hass-config uses, a template, an entity prefix,
// and a quote in the device name to exercise the escaping.
static const MhiDiscoveryCtx kUitkijk = {
  .discovery_prefix = "homeassistant",
  .base = "airco/uitkijk",
  .set_prefix = "set/",
  .hostname = "airco-uitkijk",
  .device_name = "AC \"Uitkijk\"",
  .version = "2eab73c",
  .climate_id = "AC_Uitkijk",
  .id_prefix = "ac_uitkijk",
  .entity_prefix = "ac_uitkijk",
  .names = {NULL, "lamellen", "stil", "storing", "bedrading", "tijd sinds opstart", "vrij geheugen", "wifi-signaal", "herstartreden", "wifi-standaard"},
  .reset_reason_tpl = "{{ {'Power On': 'stroom ingeschakeld', 'Software/System restart': 'software-herstart (update of reset)', 'Hardware Watchdog': 'hardware-watchdog', 'Software Watchdog': 'software-watchdog', 'Exception': 'crash', 'Deep-Sleep Wake': 'wakker uit diepe slaap', 'External System': 'externe reset'}.get(value, value) }}",
  .t_mode = "Mode", .t_tsetpoint = "Tsetpoint", .t_fan = "Fan", .t_vanes = "Vanes", .t_troom = "Troom", .t_action = "Action",
  .t_connected = "connected", .t_silent = "Silent", .t_errorcode = "Errorcode", .t_wiring = "Wiring",
  .t_uptime = "Uptime", .t_free_heap = "FreeHeap", .t_rssi = "RSSI", .t_reset_reason = "ResetReason", .t_wifi_phy = "WIFI_PHY",
  .modes = {"off", "auto", "dry", "cool", "fan_only", "heat"},
  .fan_auto = "Auto",
  .vanes = {"Up", "UpCenter", "CenterDown", "Down", "Swing", "?"},
  .connected_on = "1", .connected_off = "0",
  .silent_on = "On", .silent_off = "Off",
  .wiring_ok = "o.k.",
};

static const char* const kFixtureName[MHI_DISCOVERY_ROWS] = {
  "climate", "vanes", "silent", "problem", "wiring", "uptime", "free_heap", "rssi", "reset_reason", "wifi_phy"};

// Balanced braces and brackets outside strings, every string closed, no
// printf conversion left over and no NULL argument printed.
static bool json_shape_ok(const char* s) {
  int depth = 0;
  bool in_str = false;
  if (strstr(s, "(null)") != NULL) return false;
  for (; *s; s++) {
    if (in_str) {
      if (*s == '\\' && s[1]) s++;
      else if (*s == '"') in_str = false;
      continue;
    }
    if (*s == '"') in_str = true;
    else if (*s == '{' || *s == '[') depth++;
    else if (*s == '}' || *s == ']') { if (--depth < 0) return false; }
    else if (*s == '%') return false;
  }
  return depth == 0 && !in_str;
}

// --- mode names -------------------------------------------------------------

static void test_ha_mode_names_are_valid(void) {
  TEST_ASSERT_TRUE(mhi_discovery_modes_valid(&kDefault));
}

static void test_the_repo_default_mode_names_are_not(void) {
  // MHI-AC-Ctrl.h's defaults: HA rejects a climate whose modes say "Fan" or "Off".
  MhiDiscoveryCtx c = kDefault;
  c.modes[0] = "Off";
  TEST_ASSERT_FALSE(mhi_discovery_modes_valid(&c));
  c = kDefault;
  c.modes[4] = "Fan";
  TEST_ASSERT_FALSE(mhi_discovery_modes_valid(&c));
  c = kDefault;
  c.modes[5] = NULL;
  TEST_ASSERT_FALSE(mhi_discovery_modes_valid(&c));
}

// --- topics -----------------------------------------------------------------

static void test_topics_carry_the_component_and_the_uniq_id(void) {
  char t[MHI_DISCOVERY_TOPIC_MAX];
  TEST_ASSERT_TRUE(mhi_discovery_topic(MHI_DISCOVERY_CLIMATE, &kUitkijk, t, sizeof(t)) > 0);
  TEST_ASSERT_EQUAL_STRING("homeassistant/climate/AC_Uitkijk/config", t);
  TEST_ASSERT_TRUE(mhi_discovery_topic(MHI_DISCOVERY_VANES, &kUitkijk, t, sizeof(t)) > 0);
  TEST_ASSERT_EQUAL_STRING("homeassistant/select/ac_uitkijk_vanes/config", t);
  TEST_ASSERT_TRUE(mhi_discovery_topic(MHI_DISCOVERY_SILENT, &kUitkijk, t, sizeof(t)) > 0);
  TEST_ASSERT_EQUAL_STRING("homeassistant/switch/ac_uitkijk_silent/config", t);
  TEST_ASSERT_TRUE(mhi_discovery_topic(MHI_DISCOVERY_WIRING, &kUitkijk, t, sizeof(t)) > 0);
  TEST_ASSERT_EQUAL_STRING("homeassistant/binary_sensor/ac_uitkijk_wiring/config", t);
  TEST_ASSERT_TRUE(mhi_discovery_topic(MHI_DISCOVERY_RESET_REASON, &kUitkijk, t, sizeof(t)) > 0);
  TEST_ASSERT_EQUAL_STRING("homeassistant/sensor/ac_uitkijk_reset_reason/config", t);
}

static void test_a_topic_that_does_not_fit_is_refused(void) {
  char t[30];
  TEST_ASSERT_EQUAL_size_t(0, mhi_discovery_topic(MHI_DISCOVERY_CLIMATE, &kUitkijk, t, sizeof(t)));
  TEST_ASSERT_EQUAL_STRING("", t);
}

// --- every row builds, fits and is well-formed ------------------------------

static void every_row_fits(const MhiDiscoveryCtx* ctx, const char* label) {
  size_t longest = 0;
  int longest_row = -1;
  for (int r = 0; r < MHI_DISCOVERY_ROWS; r++) {
    char out[MHI_DISCOVERY_BUF];
    const size_t n = mhi_discovery_build((MhiDiscoveryRow)r, ctx, out, sizeof(out));
    char msg[64];
    snprintf(msg, sizeof(msg), "%s row %d", label, r);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, msg);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(strlen(out), n, msg);
    TEST_ASSERT_TRUE_MESSAGE(json_shape_ok(out), msg);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"uniq_id\":\""), msg);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"avty_t\":\"~/connected\",\"pl_avail\":\"1\",\"pl_not_avail\":\"0\""), msg);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"dev\":{\"ids\":[\""), msg);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"mf\":\"Mitsubishi Heavy Industries\",\"mdl\":\"MHI-AC-Ctrl\",\"sw\":\""), msg);
    if (n > longest) { longest = n; longest_row = r; }
  }
  printf("  %s: longest row %d, %u of %u bytes\n", label, longest_row, (unsigned)longest, (unsigned)MHI_DISCOVERY_BUF);
}

static void test_every_default_row_fits(void) { every_row_fits(&kDefault, "default"); }
static void test_every_uitkijk_row_fits(void) { every_row_fits(&kUitkijk, "uitkijk"); }

static void test_a_payload_that_does_not_fit_is_refused(void) {
  char out[200];
  TEST_ASSERT_EQUAL_size_t(0, mhi_discovery_build(MHI_DISCOVERY_CLIMATE, &kDefault, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
}

// --- the climate ------------------------------------------------------------

static void test_the_climate_is_the_device_and_lists_come_from_the_payload_texts(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_CLIMATE, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "{\"~\":\"airco/uitkijk\",\"name\":null,\"uniq_id\":\"AC_Uitkijk\","));
  TEST_ASSERT_NULL(strstr(out, "default_entity_id"));  // the registry keeps climate.ac_uitkijk by uniq_id
  TEST_ASSERT_NOT_NULL(strstr(out, "\"mode_cmd_t\":\"~/set/Mode\",\"mode_stat_t\":\"~/Mode\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"temp_cmd_t\":\"~/set/Tsetpoint\",\"temp_stat_t\":\"~/Tsetpoint\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"fan_mode_cmd_t\":\"~/set/Fan\",\"fan_mode_stat_t\":\"~/Fan\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"swing_mode_cmd_t\":\"~/set/Vanes\",\"swing_mode_stat_t\":\"~/Vanes\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"curr_temp_t\":\"~/Troom\",\"act_t\":\"~/Action\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"modes\":[\"off\",\"auto\",\"dry\",\"cool\",\"fan_only\",\"heat\"],"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"fan_modes\":[\"1\",\"2\",\"3\",\"4\",\"Auto\"],"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"swing_modes\":[\"Up\",\"UpCenter\",\"CenterDown\",\"Down\",\"Swing\",\"?\"],"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"min_temp\":18,\"max_temp\":30,\"temp_step\":0.5,"));
  TEST_ASSERT_NULL(strstr(out, "ent_cat"));  // the climate card, not the diagnostics list
}

// --- the other rows ---------------------------------------------------------

static void test_the_vanes_select_offers_the_names_and_the_unknown_state(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_VANES, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"name\":\"lamellen\",\"uniq_id\":\"ac_uitkijk_vanes\",\"default_entity_id\":\"select.ac_uitkijk_vanes\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/Vanes\",\"cmd_t\":\"~/set/Vanes\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ops\":[\"Up\",\"UpCenter\",\"CenterDown\",\"Down\",\"Swing\",\"?\"],"));
  TEST_ASSERT_NULL(strstr(out, "ent_cat"));
}

static void test_the_silent_switch_uses_the_payload_texts(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_SILENT, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"uniq_id\":\"ac_uitkijk_silent\",\"default_entity_id\":\"switch.ac_uitkijk_silent\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/Silent\",\"cmd_t\":\"~/set/Silent\",\"pl_on\":\"On\",\"pl_off\":\"Off\",\"ic\":\"mdi:volume-low\","));
}

static void test_the_problem_sensors_are_diagnostic_and_template_the_topics(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_PROBLEM, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"default_entity_id\":\"binary_sensor.ac_uitkijk_problem\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/Errorcode\",\"val_tpl\":\"{{ 'ON' if value|int(0) != 0 else 'OFF' }}\",\"dev_cla\":\"problem\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_WIRING, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/Wiring\",\"val_tpl\":\"{{ 'OFF' if value == 'o.k.' else 'ON' }}\",\"dev_cla\":\"problem\",\"ent_cat\":\"diagnostic\","));
}

static void test_the_five_sensors_match_hass_config_304(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_UPTIME, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"name\":\"tijd sinds opstart\",\"uniq_id\":\"ac_uitkijk_uptime\",\"default_entity_id\":\"sensor.ac_uitkijk_uptime\","));
  // Plain seconds, no state class: hass-config's reboot counter compares the raw state.
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/Uptime\",\"dev_cla\":\"duration\",\"unit_of_meas\":\"s\",\"sug_dsp_prc\":0,\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_NULL(strstr(out, "stat_cla"));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_FREE_HEAP, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/FreeHeap\",\"dev_cla\":\"data_size\",\"unit_of_meas\":\"B\",\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_RSSI, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/RSSI\",\"dev_cla\":\"signal_strength\",\"unit_of_meas\":\"dBm\",\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_RESET_REASON, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/ResetReason\",\"val_tpl\":\"{{ {'Power On': 'stroom ingeschakeld', "));
  TEST_ASSERT_NOT_NULL(strstr(out, "}.get(value, value) }}\",\"ic\":\"mdi:restart-alert\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_WIFI_PHY, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/WIFI_PHY\",\"ic\":\"mdi:wifi-cog\",\"ent_cat\":\"diagnostic\","));
}

static void test_without_a_template_or_entity_prefix_those_keys_are_absent(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_RESET_REASON, &kDefault, out, sizeof(out)) > 0);
  TEST_ASSERT_NULL(strstr(out, "val_tpl"));
  TEST_ASSERT_NULL(strstr(out, "default_entity_id"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"name\":\"Reset reason\",\"uniq_id\":\"MHI-AC-Ctrl_reset_reason\",\"stat_t\":\"~/ResetReason\",\"ic\":\"mdi:restart-alert\","));
}

static void test_quotes_in_a_name_are_escaped(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_SILENT, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"dev\":{\"ids\":[\"airco-uitkijk\"],\"name\":\"AC \\\"Uitkijk\\\"\",\"mf\":"));
  TEST_ASSERT_TRUE(json_shape_ok(out));
}

// --- the committed reference payloads ---------------------------------------

static void test_reference_fixtures_are_written(void) {
  for (int r = 0; r < MHI_DISCOVERY_ROWS; r++) {
    char path[80], topic[MHI_DISCOVERY_TOPIC_MAX], payload[MHI_DISCOVERY_BUF];
    snprintf(path, sizeof(path), "test/fixtures/discovery/%s.txt", kFixtureName[r]);
    TEST_ASSERT_TRUE(mhi_discovery_topic((MhiDiscoveryRow)r, &kDefault, topic, sizeof(topic)) > 0);
    TEST_ASSERT_TRUE(mhi_discovery_build((MhiDiscoveryRow)r, &kDefault, payload, sizeof(payload)) > 0);
    FILE* f = fopen(path, "w");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "cannot write test/fixtures/discovery/: run pio test from the project root");
    fprintf(f, "%s\n%s\n", topic, payload);
    fclose(f);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_ha_mode_names_are_valid);
  RUN_TEST(test_the_repo_default_mode_names_are_not);
  RUN_TEST(test_topics_carry_the_component_and_the_uniq_id);
  RUN_TEST(test_a_topic_that_does_not_fit_is_refused);
  RUN_TEST(test_every_default_row_fits);
  RUN_TEST(test_every_uitkijk_row_fits);
  RUN_TEST(test_a_payload_that_does_not_fit_is_refused);
  RUN_TEST(test_the_climate_is_the_device_and_lists_come_from_the_payload_texts);
  RUN_TEST(test_the_vanes_select_offers_the_names_and_the_unknown_state);
  RUN_TEST(test_the_silent_switch_uses_the_payload_texts);
  RUN_TEST(test_the_problem_sensors_are_diagnostic_and_template_the_topics);
  RUN_TEST(test_the_five_sensors_match_hass_config_304);
  RUN_TEST(test_without_a_template_or_entity_prefix_those_keys_are_absent);
  RUN_TEST(test_quotes_in_a_name_are_escaped);
  RUN_TEST(test_reference_fixtures_are_written);
  return UNITY_END();
}
```

Create the fixture directory with a placeholder the test overwrites: `mkdir -p test/fixtures/discovery && touch test/fixtures/discovery/.gitkeep`.

- [ ] **Step 3: Run the tests to verify they fail**

Run: `pio test -e native -f test_mhi_discovery`
Expected: link error, `undefined reference to mhi_discovery_build` (and the other two).

- [ ] **Step 4: Write the implementation**

`lib/mhi_pure/mhi_discovery.cpp`:

```cpp
#include "mhi_discovery.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// The templates below add up to about 1.5 KB. On the ESP8266 a plain string
// literal lives in RAM, so there they go to flash and are formatted with
// vsnprintf_P; on the host they are ordinary literals. This is the one place
// in lib/mhi_pure that knows about Arduino.
#if defined(ARDUINO)
#include <pgmspace.h>
#define FMT(s) PSTR(s)
#define MHI_VSNPRINTF vsnprintf_P
#else
#define FMT(s) s
#define MHI_VSNPRINTF vsnprintf
#endif

static const char* const kComponent[MHI_DISCOVERY_ROWS] = {
  "climate", "select", "switch", "binary_sensor", "binary_sensor", "sensor", "sensor", "sensor", "sensor", "sensor"};
static const char* const kSuffix[MHI_DISCOVERY_ROWS] = {
  "", "vanes", "silent", "problem", "wiring", "uptime", "free_heap", "rssi", "reset_reason", "wifi_phy"};
static const char* const kHaModes[6] = {"off", "auto", "dry", "cool", "fan_only", "heat"};

struct Out {
  char* buf;
  size_t len;
  size_t n;
  bool overflow;
};

static void put(Out* o, const char* fmt, ...) {
  if (o->overflow) return;
  va_list ap;
  va_start(ap, fmt);
  const int r = MHI_VSNPRINTF(o->buf + o->n, o->len - o->n, fmt, ap);
  va_end(ap);
  if (r < 0 || (size_t)r >= o->len - o->n) {
    o->overflow = true;
    return;
  }
  o->n += (size_t)r;
}

static void put_char(Out* o, char c) {
  if (o->overflow) return;
  if (o->n + 1 >= o->len) {
    o->overflow = true;
    return;
  }
  o->buf[o->n++] = c;
  o->buf[o->n] = '\0';
}

// A JSON string: " and \ escaped. Names and templates carry no control characters.
static void put_str(Out* o, const char* s) {
  put_char(o, '"');
  for (; s && *s; s++) {
    if (*s == '"' || *s == '\\') put_char(o, '\\');
    put_char(o, *s);
  }
  put_char(o, '"');
}

static void put_list(Out* o, const char* key, const char* const* items, size_t count) {
  put(o, FMT("\"%s\":["), key);
  for (size_t i = 0; i < count; i++) {
    if (i) put_char(o, ',');
    put_str(o, items[i]);
  }
  put(o, FMT("],"));
}

static void head(Out* o, const MhiDiscoveryCtx* c, MhiDiscoveryRow row) {
  put(o, FMT("{\"~\":\"%s\","), c->base);
  if (row == MHI_DISCOVERY_CLIMATE) {
    // null: the climate is the device's main feature, HA names it after the device.
    put(o, FMT("\"name\":null,\"uniq_id\":\"%s\","), c->climate_id);
    return;
  }
  put(o, FMT("\"name\":"));
  put_str(o, c->names[row]);
  put(o, FMT(",\"uniq_id\":\"%s_%s\","), c->id_prefix, kSuffix[row]);
  if (c->entity_prefix)
    put(o, FMT("\"default_entity_id\":\"%s.%s_%s\","), kComponent[row], c->entity_prefix, kSuffix[row]);
}

static void tail(Out* o, const MhiDiscoveryCtx* c, bool diagnostic) {
  if (diagnostic) put(o, FMT("\"ent_cat\":\"diagnostic\","));
  put(o, FMT("\"avty_t\":\"~/%s\",\"pl_avail\":\"%s\",\"pl_not_avail\":\"%s\",\"dev\":{\"ids\":[\"%s\"],\"name\":"),
      c->t_connected, c->connected_on, c->connected_off, c->hostname);
  put_str(o, c->device_name);
  put(o, FMT(",\"mf\":\"Mitsubishi Heavy Industries\",\"mdl\":\"MHI-AC-Ctrl\",\"sw\":\"%s\"}}"), c->version);
}

static void state_topic(Out* o, const char* topic) {
  put(o, FMT("\"stat_t\":\"~/%s\","), topic);
}

bool mhi_discovery_modes_valid(const MhiDiscoveryCtx* c) {
  for (size_t i = 0; i < 6; i++)
    if (!c->modes[i] || strcmp(c->modes[i], kHaModes[i]) != 0) return false;
  return true;
}

size_t mhi_discovery_topic(MhiDiscoveryRow row, const MhiDiscoveryCtx* c, char* out, size_t out_len) {
  if (!out || out_len == 0 || row >= MHI_DISCOVERY_ROWS) return 0;
  int r;
  if (row == MHI_DISCOVERY_CLIMATE)
    r = snprintf(out, out_len, "%s/%s/%s/config", c->discovery_prefix, kComponent[row], c->climate_id);
  else
    r = snprintf(out, out_len, "%s/%s/%s_%s/config", c->discovery_prefix, kComponent[row], c->id_prefix, kSuffix[row]);
  if (r < 0 || (size_t)r >= out_len) {
    out[0] = '\0';
    return 0;
  }
  return (size_t)r;
}

size_t mhi_discovery_build(MhiDiscoveryRow row, const MhiDiscoveryCtx* c, char* out, size_t out_len) {
  if (!out || out_len == 0 || row >= MHI_DISCOVERY_ROWS) return 0;
  Out o = {out, out_len, 0, false};
  out[0] = '\0';
  head(&o, c, row);
  bool diagnostic = true;
  switch (row) {
    case MHI_DISCOVERY_CLIMATE: {
      diagnostic = false;
      const char* fan_modes[5] = {"1", "2", "3", "4", c->fan_auto};
      put(&o, FMT("\"mode_cmd_t\":\"~/%s%s\",\"mode_stat_t\":\"~/%s\",\"temp_cmd_t\":\"~/%s%s\",\"temp_stat_t\":\"~/%s\","),
          c->set_prefix, c->t_mode, c->t_mode, c->set_prefix, c->t_tsetpoint, c->t_tsetpoint);
      put(&o, FMT("\"fan_mode_cmd_t\":\"~/%s%s\",\"fan_mode_stat_t\":\"~/%s\",\"swing_mode_cmd_t\":\"~/%s%s\",\"swing_mode_stat_t\":\"~/%s\","),
          c->set_prefix, c->t_fan, c->t_fan, c->set_prefix, c->t_vanes, c->t_vanes);
      put(&o, FMT("\"curr_temp_t\":\"~/%s\",\"act_t\":\"~/%s\","), c->t_troom, c->t_action);
      put_list(&o, "modes", c->modes, 6);
      put_list(&o, "fan_modes", fan_modes, 5);
      put_list(&o, "swing_modes", c->vanes, 6);
      put(&o, FMT("\"min_temp\":18,\"max_temp\":30,\"temp_step\":0.5,"));
      break;
    }
    case MHI_DISCOVERY_VANES:
      diagnostic = false;
      put(&o, FMT("\"stat_t\":\"~/%s\",\"cmd_t\":\"~/%s%s\","), c->t_vanes, c->set_prefix, c->t_vanes);
      put_list(&o, "ops", c->vanes, 6);
      break;
    case MHI_DISCOVERY_SILENT:
      diagnostic = false;
      put(&o, FMT("\"stat_t\":\"~/%s\",\"cmd_t\":\"~/%s%s\",\"pl_on\":\"%s\",\"pl_off\":\"%s\",\"ic\":\"mdi:volume-low\","),
          c->t_silent, c->set_prefix, c->t_silent, c->silent_on, c->silent_off);
      break;
    case MHI_DISCOVERY_PROBLEM:
      state_topic(&o, c->t_errorcode);
      put(&o, FMT("\"val_tpl\":\"{{ 'ON' if value|int(0) != 0 else 'OFF' }}\",\"dev_cla\":\"problem\","));
      break;
    case MHI_DISCOVERY_WIRING:
      state_topic(&o, c->t_wiring);
      put(&o, FMT("\"val_tpl\":\"{{ 'OFF' if value == '%s' else 'ON' }}\",\"dev_cla\":\"problem\","), c->wiring_ok);
      break;
    case MHI_DISCOVERY_UPTIME:
      state_topic(&o, c->t_uptime);
      // No state class: hass-config's reboot counter compares the raw seconds.
      put(&o, FMT("\"dev_cla\":\"duration\",\"unit_of_meas\":\"s\",\"sug_dsp_prc\":0,"));
      break;
    case MHI_DISCOVERY_FREE_HEAP:
      state_topic(&o, c->t_free_heap);
      put(&o, FMT("\"dev_cla\":\"data_size\",\"unit_of_meas\":\"B\",\"stat_cla\":\"measurement\","));
      break;
    case MHI_DISCOVERY_RSSI:
      state_topic(&o, c->t_rssi);
      put(&o, FMT("\"dev_cla\":\"signal_strength\",\"unit_of_meas\":\"dBm\",\"stat_cla\":\"measurement\","));
      break;
    case MHI_DISCOVERY_RESET_REASON:
      state_topic(&o, c->t_reset_reason);
      if (c->reset_reason_tpl) {
        put(&o, FMT("\"val_tpl\":"));
        put_str(&o, c->reset_reason_tpl);
        put_char(&o, ',');
      }
      put(&o, FMT("\"ic\":\"mdi:restart-alert\","));
      break;
    case MHI_DISCOVERY_WIFI_PHY:
      state_topic(&o, c->t_wifi_phy);
      put(&o, FMT("\"ic\":\"mdi:wifi-cog\","));
      break;
    case MHI_DISCOVERY_ROWS:  // excluded above; keeps -Wswitch exhaustive
      return 0;
  }
  tail(&o, c, diagnostic);
  if (o.overflow) {
    out[0] = '\0';
    return 0;
  }
  return o.n;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `pio test -e native -f test_mhi_discovery -v`
Expected: `15 Tests 0 Failures 0 Ignored`, and two lines like `default: longest row 0, 6xx of 1024 bytes` and `uitkijk: longest row 0, 7xx of 1024 bytes`. **Record both numbers in the commit message.** If the Uitkijk climate row is above 900, stop and say so on #4 before going on (the buffer is a static 1 KB; the design expects ~760).

- [ ] **Step 6: Check the fixtures and commit them with the code**

```bash
ls test/fixtures/discovery/ && cat test/fixtures/discovery/climate.txt
rm -f test/fixtures/discovery/.gitkeep   # never committed; the ten .txt files keep the directory
git add lib/mhi_pure/mhi_discovery.h lib/mhi_pure/mhi_discovery.cpp test/test_mhi_discovery/test_mhi_discovery.cpp test/fixtures/discovery/
git commit -m "feat: Home Assistant discovery payloads, one row per entity, host-tested with the reference payloads as fixtures (#4)

Longest rows: default <n> B, Uitkijk <n> B, of the 1024 B buffer.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 6: Discovery on the unit: options, `src/discovery.cpp`, the `Discovery` topic, CI

**Files:**
- Create: `src/discovery.h`, `src/discovery.cpp`
- Modify: `src/support.h` (after the `DIAG_DEFAULT` block, line 47-49), `src/MHI-AC-Ctrl.h` (topics near `TOPIC_DIAG`, payloads at the end), `src/main.cpp` (includes; `setup()` after `mhi_ac_ctrl_core.MHIAcCtrlStatus(&mhiStatusHandler);`; `loop()` in the `MQTT_RECONNECTED` block and after `ArduinoOTA.handle();`), `platformio.ini` (after `ci-custom-payloads`, and `ci-all-options`), `.github/workflows/ci.yml` (matrix, and the test job)

**Interfaces:**
- Consumes: everything from Task 5; `PAYLOAD_VANES_1..4` (Task 2); `TOPIC_SILENT`, `PAYLOAD_SILENT_*` (Task 4); `MHI_WIRING_OK` (Task 3).
- Produces: `void discovery_setup()`, `void discovery_restart()`, `void discovery_loop()`; options `HA_DISCOVERY`, `HA_DISCOVERY_PREFIX`, `HA_DEVICE_NAME`, `HA_CLIMATE_ID`, `HA_ID_PREFIX`, `HA_ENTITY_PREFIX`, `HA_NAME_VANES`, `HA_NAME_SILENT`, `HA_NAME_PROBLEM`, `HA_NAME_WIRING`, `HA_NAME_UPTIME`, `HA_NAME_FREE_HEAP`, `HA_NAME_RSSI`, `HA_NAME_RESET_REASON`, `HA_NAME_WIFI_PHY`, `HA_RESET_REASON_TPL`; `TOPIC_DISCOVERY` `"Discovery"`, `PAYLOAD_DISCOVERY_OK` `"ok"`, `PAYLOAD_DISCOVERY_MODES` `"modes"`. Task 9's `airco-config.py` writes the `HA_*` values.

- [ ] **Step 1: The options in `support.h`**

After the `DIAG_DEFAULT` block:

```cpp
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
//#define HA_ENTITY_PREFIX "ac_slaapkamer"          // when defined, every entity but the climate gets default_entity_id <domain>.<prefix>_<suffix>; lower case a-z 0-9 _
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
```

- [ ] **Step 2: Topic and payloads in `MHI-AC-Ctrl.h`**

After the `TOPIC_REQUEST_OPDATA` block:

```cpp
#ifndef TOPIC_DISCOVERY
#define TOPIC_DISCOVERY "Discovery"           // retained, after the discovery configs went out: ok, or modes when the climate row was skipped (HA_DISCOVERY)
#endif
```

After the `PAYLOAD_SILENT_OFF` block:

```cpp
#ifndef PAYLOAD_DISCOVERY_OK
#define PAYLOAD_DISCOVERY_OK "ok"
#endif
#ifndef PAYLOAD_DISCOVERY_MODES
#define PAYLOAD_DISCOVERY_MODES "modes"
#endif
```

- [ ] **Step 3: `src/discovery.h` and `src/discovery.cpp`**

`src/discovery.h`:

```cpp
// Home Assistant MQTT discovery on the unit (fork #4 batch B, spec §4.4).
// Real code only with HA_DISCOVERY; otherwise three empty functions, so
// main.cpp needs no #ifdef.

#pragma once

void discovery_setup();    // once at boot: checks the mode names, says so on Serial
void discovery_restart();  // after every MQTT connect: the rows go out again, one per loop() pass
void discovery_loop();     // every loop() pass: publishes the next row while connected, then the Discovery topic
```

`src/discovery.cpp`:

```cpp
#include "discovery.h"

#include <Arduino.h>

#include "MHI-AC-Ctrl-core.h"
#include "MHI-AC-Ctrl.h"
#include "mhi_diag.h"
#include "mhi_discovery.h"
#include "support.h"

#ifdef HA_DISCOVERY

// The ~/set/... topics assume the set prefix sits under the status prefix,
// as the defaults do (MQTT_SET_PREFIX MQTT_PREFIX "set/").
constexpr bool starts_with(const char* s, const char* prefix) {
  return *prefix == '\0' || (*s == *prefix && starts_with(s + 1, prefix + 1));
}
static_assert(starts_with(MQTT_SET_PREFIX, MQTT_PREFIX), "HA_DISCOVERY needs MQTT_SET_PREFIX to start with MQTT_PREFIX");
static_assert(sizeof(MQTT_PREFIX) > 1, "HA_DISCOVERY needs a non-empty MQTT_PREFIX");

// MQTT_PREFIX without its trailing slash: the payload's "~".
static char base_topic[sizeof(MQTT_PREFIX)];

static const MhiDiscoveryCtx ctx = {
  .discovery_prefix = HA_DISCOVERY_PREFIX,
  .base = base_topic,
  .set_prefix = MQTT_SET_PREFIX + (sizeof(MQTT_PREFIX) - 1),
  .hostname = HOSTNAME,
  .device_name = HA_DEVICE_NAME,
  .version = VERSION,
  .climate_id = HA_CLIMATE_ID,
  .id_prefix = HA_ID_PREFIX,
#ifdef HA_ENTITY_PREFIX
  .entity_prefix = HA_ENTITY_PREFIX,
#else
  .entity_prefix = NULL,
#endif
  .names = {NULL, HA_NAME_VANES, HA_NAME_SILENT, HA_NAME_PROBLEM, HA_NAME_WIRING, HA_NAME_UPTIME, HA_NAME_FREE_HEAP,
            HA_NAME_RSSI, HA_NAME_RESET_REASON, HA_NAME_WIFI_PHY},
#ifdef HA_RESET_REASON_TPL
  .reset_reason_tpl = HA_RESET_REASON_TPL,
#else
  .reset_reason_tpl = NULL,
#endif
  .t_mode = TOPIC_MODE, .t_tsetpoint = TOPIC_TSETPOINT, .t_fan = TOPIC_FAN, .t_vanes = TOPIC_VANES, .t_troom = TOPIC_TROOM,
  .t_action = TOPIC_ACTION, .t_connected = TOPIC_CONNECTED, .t_silent = TOPIC_SILENT, .t_errorcode = TOPIC_ERRORCODE,
  .t_wiring = TOPIC_WIRING, .t_uptime = TOPIC_UPTIME, .t_free_heap = TOPIC_FREE_HEAP, .t_rssi = TOPIC_RSSI,
  .t_reset_reason = TOPIC_RESET_REASON, .t_wifi_phy = TOPIC_WIFI_PHY,
  .modes = {PAYLOAD_MODE_OFF, PAYLOAD_MODE_AUTO, PAYLOAD_MODE_DRY, PAYLOAD_MODE_COOL, PAYLOAD_MODE_FAN, PAYLOAD_MODE_HEAT},
  .fan_auto = PAYLOAD_FAN_AUTO,
  .vanes = {PAYLOAD_VANES_1, PAYLOAD_VANES_2, PAYLOAD_VANES_3, PAYLOAD_VANES_4, PAYLOAD_VANES_SWING, PAYLOAD_VANES_UNKNOWN},
  .connected_on = PAYLOAD_CONNECTED_TRUE, .connected_off = PAYLOAD_CONNECTED_FALSE,
  .silent_on = PAYLOAD_SILENT_ON, .silent_off = PAYLOAD_SILENT_OFF,
  .wiring_ok = MHI_WIRING_OK,
};

static bool modes_ok = false;
static uint8_t next_row = MHI_DISCOVERY_ROWS;  // nothing to publish until a connect

void discovery_setup() {
  strncpy(base_topic, MQTT_PREFIX, sizeof(base_topic));
  base_topic[sizeof(base_topic) - 1] = '\0';
  const size_t n = strlen(base_topic);
  if (n > 0 && base_topic[n - 1] == '/') base_topic[n - 1] = '\0';
  modes_ok = mhi_discovery_modes_valid(&ctx);
  if (!modes_ok)
    Serial.println(F("HA_DISCOVERY: the PAYLOAD_MODE_* texts are not Home Assistant's mode names, the climate config will not be published (Discovery: modes)"));
}

void discovery_restart() {
  next_row = 0;
}

void discovery_loop() {
  // Static, not on the stack: loop() runs on the ESP8266's 4 KB cont stack and
  // the publish path runs below this frame.
  static char payload[MHI_DISCOVERY_BUF];
  char topic[MHI_DISCOVERY_TOPIC_MAX];
  if (next_row >= MHI_DISCOVERY_ROWS || !MQTTclient.connected()) return;
  const MhiDiscoveryRow row = (MhiDiscoveryRow)next_row++;
  if (row == MHI_DISCOVERY_CLIMATE && !modes_ok) {
    // skipped: said so at boot, and the Discovery topic says "modes"
  }
  else if (mhi_discovery_topic(row, &ctx, topic, sizeof(topic)) == 0 ||
           mhi_discovery_build(row, &ctx, payload, sizeof(payload)) == 0) {
    Serial.printf_P(PSTR("HA_DISCOVERY: row %u does not fit, not published\n"), (unsigned)row);
  }
  else {
    MQTTclient.publish(topic, payload, true);
  }
  if (next_row == MHI_DISCOVERY_ROWS) {
    if (modes_ok)
      output_P((ACStatus)type_status, PSTR(TOPIC_DISCOVERY), PSTR(PAYLOAD_DISCOVERY_OK));
    else
      output_P((ACStatus)type_status, PSTR(TOPIC_DISCOVERY), PSTR(PAYLOAD_DISCOVERY_MODES));
  }
}

#else

void discovery_setup() {}
void discovery_restart() {}
void discovery_loop() {}

#endif
```

(`MQTT_SET_PREFIX + (sizeof(MQTT_PREFIX) - 1)` is the `"set/"` tail of the literal, valid because the `static_assert` proved the prefix. `VERSION` comes from the generated `build_version.h` through `support.h`.)

- [ ] **Step 4: Hook it into `main.cpp`**

Add `#include "discovery.h"` after `#include "MHI-AC-Ctrl.h"`. In `setup()`, after `mhi_ac_ctrl_core.MHIAcCtrlStatus(&mhiStatusHandler);` add `discovery_setup();`. In `loop()`, inside `if (MQTTStatus == MQTT_RECONNECTED) { ... }` add `discovery_restart();` as the last line, and after `ArduinoOTA.handle();` add `discovery_loop();`.

- [ ] **Step 5: Build environments**

`platformio.ini`, after `[env:ci-custom-payloads]`:

```ini
; Home Assistant discovery: the custom-payload build with HA_DISCOVERY on. The
; host test covers the payloads; this proves the Arduino side compiles with
; every option, including the two that are normally undefined.
[env:ci-ha-discovery]
extends = ci
build_flags =
	${esp8266.build_flags}
	-D POWERON_WHEN_CHANGING_MODE=true
	-D PAYLOAD_POWER_OFF=\"off\"
	-D PAYLOAD_MODE_AUTO=\"auto\"
	-D PAYLOAD_MODE_DRY=\"dry\"
	-D PAYLOAD_MODE_COOL=\"cool\"
	-D PAYLOAD_MODE_FAN=\"fan_only\"
	-D PAYLOAD_MODE_HEAT=\"heat\"
	-D HA_DISCOVERY=true
	-D HA_ENTITY_PREFIX=\"mhi_ac_ctrl_ci\"
	-D HA_RESET_REASON_TPL=\"{{value}}\"
```

In `[env:ci-all-options]` add `-D HA_DISCOVERY=true` as the last flag (the mode names stay the repo's there; the runtime check handles that, and the flash high-water mark now includes discovery).

`.github/workflows/ci.yml`: add `- ci-ha-discovery` after `- ci-custom-payloads` in the matrix. In the `test` job, after the `Run tests` step:

```yaml
      - name: Discovery fixtures match the code
        run: |
          git diff --exit-code -- test/fixtures/discovery
          test -z "$(git status --porcelain -- test/fixtures/discovery)"
```

- [ ] **Step 6: Build**

Run: `pio run -e d1_mini -e ci-ha-discovery -e ci-all-options -e ci-config-defaults && pio test -e native`
Expected: four builds succeed; note the `ci-all-options` flash size (the guard is 460000). Host tests all pass. `git status` shows no fixture change.

- [ ] **Step 7: Commit**

```bash
git add src/support.h src/MHI-AC-Ctrl.h src/discovery.h src/discovery.cpp src/main.cpp platformio.ini .github/workflows/ci.yml
git commit -m "feat: HA_DISCOVERY publishes one discovery config per loop pass after every MQTT connect, Discovery topic, ci-ha-discovery (#4)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 7: `tools/discovery_payloads`: a unit's real payloads on the host

**Files:**
- Create: `tools/discovery_payloads.cpp`

**Interfaces:**
- Consumes: Task 5's builder.
- Produces: a program printing one line per row, `<topic>\t<payload>`, for the names given on the command line. Task 9's `discovery-payloads.sh` runs it; Task 10's rehearsal and `post-flash-check.sh` use its output.

- [ ] **Step 1: Write the tool**

`tools/discovery_payloads.cpp`:

```cpp
// The discovery payloads the firmware publishes for one unit, rendered on the
// build machine from the same builder (lib/mhi_pure/mhi_discovery). One line
// per row: the topic, a tab, the JSON. Used for the cutover rehearsal, the
// post-flash check and hass-config's tests (fork #4 batch B, spec §4.4/4.5).
//
// Build:
//   g++ -std=gnu++17 -Wall -Wextra -Werror -I lib/mhi_pure lib/mhi_pure/mhi_discovery.cpp tools/discovery_payloads.cpp -o .pio/discovery_payloads
// Usage:
//   .pio/discovery_payloads --base airco/uitkijk --hostname airco-uitkijk --device-name "AC Uitkijk" \
//     --climate-id AC_Uitkijk --id-prefix ac_uitkijk --entity-prefix ac_uitkijk --version 2eab73c \
//     --name-uptime "tijd sinds opstart" ... --reset-reason-tpl "{{ ... }}"
// Every option has the repo default; --modes and --vanes take comma-separated lists.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mhi_discovery.h"

static const char* kNameOption[MHI_DISCOVERY_ROWS] = {
  NULL, "--name-vanes", "--name-silent", "--name-problem", "--name-wiring", "--name-uptime",
  "--name-free-heap", "--name-rssi", "--name-reset-reason", "--name-wifi-phy"};

// Splits "a,b,c" in place into exactly n items.
static bool split(char* list, const char** items, size_t n) {
  size_t i = 0;
  for (char* p = strtok(list, ","); p && i < n; p = strtok(NULL, ","))
    items[i++] = p;
  return i == n;
}

int main(int argc, char** argv) {
  MhiDiscoveryCtx c = {
    .discovery_prefix = "homeassistant",
    .base = "MHI-AC-Ctrl",
    .set_prefix = "set/",
    .hostname = "MHI-AC-Ctrl",
    .device_name = "MHI-AC-Ctrl",
    .version = "unknown",
    .climate_id = "MHI-AC-Ctrl",
    .id_prefix = "MHI-AC-Ctrl",
    .entity_prefix = NULL,
    .names = {NULL, "Vanes", "Silent", "Problem", "Wiring", "Uptime", "Free heap", "Wi-Fi signal", "Reset reason", "Wi-Fi PHY"},
    .reset_reason_tpl = NULL,
    .t_mode = "Mode", .t_tsetpoint = "Tsetpoint", .t_fan = "Fan", .t_vanes = "Vanes", .t_troom = "Troom", .t_action = "Action",
    .t_connected = "connected", .t_silent = "Silent", .t_errorcode = "Errorcode", .t_wiring = "Wiring",
    .t_uptime = "Uptime", .t_free_heap = "FreeHeap", .t_rssi = "RSSI", .t_reset_reason = "ResetReason", .t_wifi_phy = "WIFI_PHY",
    .modes = {"off", "auto", "dry", "cool", "fan_only", "heat"},
    .fan_auto = "Auto",
    .vanes = {"Up", "UpCenter", "CenterDown", "Down", "Swing", "?"},
    .connected_on = "1", .connected_off = "0",
    .silent_on = "On", .silent_off = "Off",
    .wiring_ok = "o.k.",
  };
  for (int i = 1; i + 1 < argc; i += 2) {
    const char* opt = argv[i];
    char* val = argv[i + 1];
    bool known = true;
    if (strcmp(opt, "--discovery-prefix") == 0) c.discovery_prefix = val;
    else if (strcmp(opt, "--base") == 0) c.base = val;
    else if (strcmp(opt, "--set-prefix") == 0) c.set_prefix = val;
    else if (strcmp(opt, "--hostname") == 0) c.hostname = val;
    else if (strcmp(opt, "--device-name") == 0) c.device_name = val;
    else if (strcmp(opt, "--version") == 0) c.version = val;
    else if (strcmp(opt, "--climate-id") == 0) c.climate_id = val;
    else if (strcmp(opt, "--id-prefix") == 0) c.id_prefix = val;
    else if (strcmp(opt, "--entity-prefix") == 0) c.entity_prefix = val;
    else if (strcmp(opt, "--reset-reason-tpl") == 0) c.reset_reason_tpl = val;
    else if (strcmp(opt, "--fan-auto") == 0) c.fan_auto = val;
    else if (strcmp(opt, "--silent-on") == 0) c.silent_on = val;
    else if (strcmp(opt, "--silent-off") == 0) c.silent_off = val;
    else if (strcmp(opt, "--modes") == 0) known = split(val, c.modes, 6);
    else if (strcmp(opt, "--vanes") == 0) known = split(val, c.vanes, 6);
    else {
      known = false;
      for (int r = 1; r < MHI_DISCOVERY_ROWS; r++)
        if (strcmp(opt, kNameOption[r]) == 0) { c.names[r] = val; known = true; }
    }
    if (!known) {
      fprintf(stderr, "discovery_payloads: unknown or malformed option %s\n", opt);
      return 2;
    }
  }
  if ((argc - 1) % 2 != 0) {
    fprintf(stderr, "discovery_payloads: every option takes one value\n");
    return 2;
  }
  if (!mhi_discovery_modes_valid(&c)) {
    fprintf(stderr, "discovery_payloads: the modes are not Home Assistant's; the firmware would skip the climate row\n");
    return 1;
  }
  for (int r = 0; r < MHI_DISCOVERY_ROWS; r++) {
    char topic[MHI_DISCOVERY_TOPIC_MAX], payload[MHI_DISCOVERY_BUF];
    if (mhi_discovery_topic((MhiDiscoveryRow)r, &c, topic, sizeof(topic)) == 0 ||
        mhi_discovery_build((MhiDiscoveryRow)r, &c, payload, sizeof(payload)) == 0) {
      fprintf(stderr, "discovery_payloads: row %d does not fit\n", r);
      return 1;
    }
    printf("%s\t%s\n", topic, payload);
  }
  return 0;
}
```

- [ ] **Step 2: Build and run it**

```bash
mkdir -p .pio && g++ -std=gnu++17 -Wall -Wextra -Werror -I lib/mhi_pure lib/mhi_pure/mhi_discovery.cpp tools/discovery_payloads.cpp -o .pio/discovery_payloads
.pio/discovery_payloads --base airco/uitkijk --hostname airco-uitkijk --device-name "AC Uitkijk" --climate-id AC_Uitkijk --id-prefix ac_uitkijk --entity-prefix ac_uitkijk --version test | cut -c1-120
.pio/discovery_payloads --modes Off,Auto,Dry,Cool,Fan,Heat; echo "exit $?"
```

Expected: ten lines starting `homeassistant/climate/AC_Uitkijk/config<TAB>{"~":"airco/uitkijk","name":null,...`; the second run prints the modes error and `exit 1`.

Check the reference output matches the committed fixtures:

```bash
.pio/discovery_payloads --version fixture | while IFS=$'\t' read -r t p; do printf '%s\n%s\n' "$t" "$p"; done | diff - <(cat test/fixtures/discovery/climate.txt test/fixtures/discovery/vanes.txt test/fixtures/discovery/silent.txt test/fixtures/discovery/problem.txt test/fixtures/discovery/wiring.txt test/fixtures/discovery/uptime.txt test/fixtures/discovery/free_heap.txt test/fixtures/discovery/rssi.txt test/fixtures/discovery/reset_reason.txt test/fixtures/discovery/wifi_phy.txt) && echo "tool matches the fixtures"
```

Expected: `tool matches the fixtures`.

- [ ] **Step 3: Commit**

```bash
git add tools/discovery_payloads.cpp
git commit -m "feat: tools/discovery_payloads renders a unit's discovery payloads on the host (#4)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 8: Docs

**Files:**
- Modify: `SW-Configuration.md` (the status table lines 84-98, the operating-data list lines 313-336, a new section after "Finding out what a remote button does" and before `# Advanced settings`), `Troubleshooting.md:76-77`, `Version.md` (after the batch A bullet)

- [ ] **Step 1: `SW-Configuration.md`, the status table**

Replace the `Vanes` row:

```
Vanes|r/w|"Up","UpCenter","CenterDown","Down","Swing","?"|Vanes up/down position, top to bottom; writing 1,2,3,4 or 5 (= "Swing") still works <sup>1</sup>
```

Add after the `Action` row:

```
Silent|r/w|"On", "Off"|Silent operation of the outdoor unit, read from the AC and settable <sup>6</sup>
Discovery|r|"ok", "modes"|Only with `HA_DISCOVERY`: the Home Assistant discovery configs were published; "modes" means the climate config was skipped because the mode texts are not Home Assistant's, see [Home Assistant discovery](#home-assistant-discovery-supporth)
```

Add after footnote 5 (find the `<sup>5</sup>` line):

```
<sup>6</sup> The state comes from operating-data code `0xDD`, which the AC reports after every SILENT press on the remote and which the firmware also polls once per operating-data cycle. Writing sends the command traced from a ProtoArt controller ([hberntsen PR #42](https://github.com/hberntsen/mhi-ac-ctrl-esp32/pull/42)); the `Silent` topic confirms it within a second or two. Two quirks of the AC: a Silent set from the infrared remote cannot be cleared over `set/Silent` and vice versa, and on a multi-split each indoor unit can hold the shared outdoor unit in Silent.
```

In the operating-data list, add after the `energy-used` line: `  { 0xc0, 0xdd},  //    "SILENT" (fork #4): DB11 bit 5, the Silent topic`, and to Note 2 add the sentence: "`SILENT` is published on the status topic `Silent`, not under `OpData/`."

- [ ] **Step 2: `SW-Configuration.md`, the discovery section**

Insert before `# Advanced settings`:

````markdown
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
````

- [ ] **Step 3: `Troubleshooting.md`**

Line 77, append to the paragraph: " The `Vanes` topic then reads `?`, and so do the climate's swing mode and the vane select in Home Assistant, until the vanes are set over MQTT again."

- [ ] **Step 4: `Version.md`**

After the batch A bullet add:

```
- named vanes, Silent and Home Assistant discovery (#4, batch B): `Vanes` publishes `Up`, `UpCenter`, `CenterDown`, `Down`, `Swing` (or `?`) and `set/Vanes` accepts the names as well as 1-5; the new `Silent` topic follows the AC's silent operation (operating-data code `0xDD`, polled and reported after every remote press) and `set/Silent` sets it, the command traced from a ProtoArt controller by mreijnde (hberntsen PR #42); with `HA_DISCOVERY` the unit publishes Home Assistant discovery configs for a climate, the vane select, the Silent switch, problem/wiring binary sensors and five diagnostic sensors after every MQTT connect, so no YAML is needed. `diag/frame` now ignores the raw room temperature byte, which dithers at a temperature boundary
```

- [ ] **Step 5: Check the links and commit**

Run: `grep -n "home-assistant-discovery-supporth\|<sup>6</sup>" SW-Configuration.md | head`
Expected: the anchor text and both footnote occurrences appear.

```bash
git add SW-Configuration.md Troubleshooting.md Version.md
git commit -m "docs: vane names, the Silent topic, Home Assistant discovery (#4)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 9: CI green, merge, toolkit

**Files:**
- Modify (outside the repo): `~/.config/hass/tools/mhi/airco-config.py`, `test_airco_config.py`, `post-flash-check.sh`, `remote-test.sh`, `lib.sh`, `README.md`; create `discovery-payloads.sh`

**Interfaces:**
- Consumes: Task 7's tool and its options; Task 6's `HA_*` defines; `Silent` and `Discovery` topics.
- Produces: `discovery-payloads.sh <unit> [version]` printing `<topic>\t<payload>` lines for the unit's real names; `airco-config.py --discovery-args <unit>` printing the tool's arguments one per line; `post-flash-check.sh` gates `Silent`, `Discovery` and the retained configs on the batch B commit.

- [ ] **Step 1: Push, wait for CI, merge**

```bash
git -c credential.helper='!gh auth git-credential' push -u https://github.com/lvschouwen/MHI-AC-Ctrl.git feat/4b-vanes-silent-discovery
gh run list --repo lvschouwen/MHI-AC-Ctrl --branch feat/4b-vanes-silent-discovery --limit 1
```

Poll `gh run watch <id> --repo lvschouwen/MHI-AC-Ctrl --exit-status` until it finishes. Expected: host tests, the fixture check and all eleven builds green. Then:

```bash
git checkout master && git merge --ff-only feat/4b-vanes-silent-discovery && git -c credential.helper='!gh auth git-credential' push https://github.com/lvschouwen/MHI-AC-Ctrl.git master && git branch -d feat/4b-vanes-silent-discovery && git -c credential.helper='!gh auth git-credential' push https://github.com/lvschouwen/MHI-AC-Ctrl.git --delete feat/4b-vanes-silent-discovery
git log --oneline -1   # <batch B commit>: the value post-flash-check.sh gates on below
```

- [ ] **Step 2: `airco-config.py` writes the discovery options**

In `~/.config/hass/tools/mhi/airco-config.py`: extend `IDENTITY` so the HA block is dropped and rewritten on every run (idempotent):

```python
IDENTITY = re.compile(
    r'^[ \t]*#[ \t]*define[ \t]+(HOSTNAME|OTA_HOSTNAME|MQTT_PREFIX|MQTT_SET_PREFIX|MQTT_OP_PREFIX|'
    r'MQTT_ERR_OP_PREFIX|CONTINUE_WITHOUT_MQTT|HA_\w+)\b.*$', re.M)
```

Add after `MARKER`:

```python
# Home Assistant discovery (fork issue #4 batch B). The five sensor names are
# the ones hass-config packages/airco.yaml used (#304), so their friendly names
# do not change at the cutover; the four new entities get Dutch names too.
RESET_REASON_TPL = ("{{ {'Power On': 'stroom ingeschakeld', 'Software/System restart': 'software-herstart (update of reset)', "
                    "'Hardware Watchdog': 'hardware-watchdog', 'Software Watchdog': 'software-watchdog', 'Exception': 'crash', "
                    "'Deep-Sleep Wake': 'wakker uit diepe slaap', 'External System': 'externe reset'}.get(value, value) }}")
HA_NAMES = {'vanes': 'lamellen', 'silent': 'stil', 'problem': 'storing', 'wiring': 'bedrading',
            'uptime': 'tijd sinds opstart', 'free_heap': 'vrij geheugen', 'rssi': 'wifi-signaal',
            'reset_reason': 'herstartreden', 'wifi_phy': 'wifi-standaard'}


def ha_defines(unit: str) -> list:
    cap = unit.capitalize()
    lines = ['#define HA_DISCOVERY true', f'#define HA_DEVICE_NAME "AC {cap}"', f'#define HA_CLIMATE_ID "AC_{cap}"',
             f'#define HA_ID_PREFIX "ac_{unit}"', f'#define HA_ENTITY_PREFIX "ac_{unit}"']
    lines += [f'#define HA_NAME_{k.upper()} "{v}"' for k, v in HA_NAMES.items()]
    lines.append(f'#define HA_RESET_REASON_TPL "{RESET_REASON_TPL}"')
    return lines


def discovery_args(unit: str, version: str) -> list:
    """Arguments for tools/discovery_payloads (the repo) giving this unit's payloads."""
    cap = unit.capitalize()
    args = ['--base', f'airco/{unit}', '--hostname', f'airco-{unit}', '--device-name', f'AC {cap}',
            '--climate-id', f'AC_{cap}', '--id-prefix', f'ac_{unit}', '--entity-prefix', f'ac_{unit}',
            '--version', version, '--reset-reason-tpl', RESET_REASON_TPL]
    for k, v in HA_NAMES.items():
        args += ['--name-' + k.replace('_', '-'), v]
    return args
```

In `build()`, change the `block = [...]` line to append the HA lines: `block = [MARKER, ..., '#define CONTINUE_WITHOUT_MQTT true'] + ha_defines(unit)`.

In `main()`, before the `if len(argv) != 5:` check:

```python
    if len(argv) == 4 and argv[1] == '--discovery-args':
        print('\n'.join(discovery_args(argv[2], argv[3])))
        return
```

and change the final print to also say `HA_DISCOVERY on`.

`test_airco_config.py`: add

```python
def test_discovery_is_on_with_the_unit_names_hass_config_uses():
    d = defines(cfg.build(TEMPLATE, 'uitkijk', SECRETS))
    assert d['HA_DISCOVERY'] == 'true'
    assert d['HA_DEVICE_NAME'] == '"AC Uitkijk"'
    assert d['HA_CLIMATE_ID'] == '"AC_Uitkijk"'
    assert d['HA_ID_PREFIX'] == '"ac_uitkijk"' and d['HA_ENTITY_PREFIX'] == '"ac_uitkijk"'
    assert d['HA_NAME_UPTIME'] == '"tijd sinds opstart"'
    assert d['HA_NAME_WIFI_PHY'] == '"wifi-standaard"'
    assert d['HA_RESET_REASON_TPL'].startswith('"{{ {\'Power On\': \'stroom ingeschakeld\'')
    assert '"' not in cfg.RESET_REASON_TPL  # a double quote would end the C string


def test_an_older_ha_block_is_rewritten_not_duplicated():
    once = cfg.build(TEMPLATE, 'slaapkamer', SECRETS)
    stale = once.replace('#define HA_NAME_SILENT "stil"', '#define HA_NAME_SILENT "oud"')
    again = cfg.build(stale, 'slaapkamer', SECRETS)
    assert again == once
    assert again.count('HA_NAME_SILENT') == 1


def test_discovery_args_name_every_entity():
    args = cfg.discovery_args('slaapkamer', 'abc1234')
    assert args[:2] == ['--base', 'airco/slaapkamer']
    assert '--version' in args and args[args.index('--version') + 1] == 'abc1234'
    for k in cfg.HA_NAMES:
        assert '--name-' + k.replace('_', '-') in args
```

Run: `python3 -m pytest -q -p no:cacheprovider ~/.config/hass/tools/mhi/test_airco_config.py`
Expected: all pass (the existing `test_running_on_its_own_output_changes_nothing` proves the HA block is idempotent too).

- [ ] **Step 3: `discovery-payloads.sh`**

`~/.config/hass/tools/mhi/discovery-payloads.sh`:

```bash
#!/usr/bin/env bash
# The discovery configs the firmware publishes for one unit, rendered on this PC
# from the repo's builder: one line per entity, topic TAB payload.
# Usage: discovery-payloads.sh slaapkamer|uitkijk [version, default master HEAD]
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$HERE/units.sh"
unit_vars "${1:-}" || { echo "usage: $0 slaapkamer|uitkijk [version]" >&2; exit 2; }
VERSION=${2:-$(git -C "$REPO" rev-parse --short=7 "$BRANCH")}
TOOL=$REPO/.pio/discovery_payloads
SRC="$REPO/lib/mhi_pure/mhi_discovery.cpp $REPO/lib/mhi_pure/mhi_discovery.h $REPO/tools/discovery_payloads.cpp"
# shellcheck disable=SC2086
if [ ! -x "$TOOL" ] || [ -n "$(find $SRC -newer "$TOOL")" ]; then
  mkdir -p "$REPO/.pio"
  g++ -std=gnu++17 -Wall -Wextra -Werror -I "$REPO/lib/mhi_pure" "$REPO/lib/mhi_pure/mhi_discovery.cpp" "$REPO/tools/discovery_payloads.cpp" -o "$TOOL"
fi
mapfile -t ARGS < <(python3 "$HERE/airco-config.py" --discovery-args "$UNIT" "$VERSION")
"$TOOL" "${ARGS[@]}"
```

`chmod +x` it. Run: `~/.config/hass/tools/mhi/discovery-payloads.sh uitkijk | cut -f1`
Expected: ten topics, `homeassistant/climate/AC_Uitkijk/config` first.

- [ ] **Step 4: `post-flash-check.sh`**

Add a constant after `DIAG_COMMIT`: `DISCOVERY_COMMIT=<batch B commit>   # fork issue #4 batch B: Vanes names, Silent, Discovery`.

Add `Silent Discovery` to the `for t in ...` list of retained topics printed.

After the `Diag` check block add:

```bash
silent=$(last Silent "$W/a.log"); disc=$(last Discovery "$W/a.log"); vanes=$(last Vanes "$W/a.log")
if git -C "$REPO" merge-base --is-ancestor "$DISCOVERY_COMMIT" "$EXPECT" 2>/dev/null; then
  case "$silent" in On|Off) ok "Silent $silent" ;; *) bad "Silent is '$silent' (the 0xdd poll answers within ~20 s of a connect)" ;; esac
  case "$vanes" in Up|UpCenter|CenterDown|Down|Swing|"?") ok "Vanes $vanes" ;; *) bad "Vanes is '$vanes', expected a name" ;; esac
  [ "$disc" = ok ] && ok "Discovery ok" || bad "Discovery is '$disc', expected ok"
  # Every retained config on the broker must be byte for byte what the tool renders for this unit and version.
  "$HERE/discovery-payloads.sh" "$UNIT" "$EXPECT" > "$W/expected.tsv"
  capture 6 "$W/d.log" "homeassistant/#" &
  wait
  while IFS=$'\t' read -r topic expected; do
    got=$({ grep -F " $topic " "$W/d.log" 2>/dev/null || true; } | tail -1 | sed -E 's/^[^ ]+ [^ ]+ //; s/ \(retained\)$//' \
          | python3 -c 'import json,sys; s=sys.stdin.read().strip(); print(json.loads(s) if s else "")')
    [ "$got" = "$expected" ] && ok "config $topic matches" || bad "config $topic differs or is missing"
  done < "$W/expected.tsv"
else
  [ -z "$silent$disc" ] && ok "no retained Silent/Discovery ($EXPECT predates them)" || bad "stale retained Silent '$silent' Discovery '$disc'"
fi
```

`capture` currently takes `(seconds, logfile)` and subscribes to `$PREFIX/#`; change it to `capture() { (cd "$HASS" && timeout "$1" node tools/ha-mqtt-capture.mjs "${3:-$PREFIX/#}" "$2" >/dev/null 2>&1); }` so the third argument overrides the topic.

In the `== Home Assistant` section add, inside the same `merge-base` condition (repeat the `if git -C ... DISCOVERY_COMMIT ...` test):

```bash
  for e in select.ac_${UNIT}_vanes switch.ac_${UNIT}_silent binary_sensor.ac_${UNIT}_problem binary_sensor.ac_${UNIT}_wiring sensor.ac_${UNIT}_uptime; do
    s=$(curl -s -m 10 -H "Authorization: Bearer $T" "$H/api/states/$e" | jq -r '.state // empty')
    case "$s" in ""|unavailable|unknown) bad "$e is '${s:-missing}'" ;; *) ok "$e = $s" ;; esac
  done
  s=$(curl -s -m 10 -H "Authorization: Bearer $T" "$H/api/states/switch.ac_${UNIT}_silent" | jq -r .state)
  [ "$s" = "$(echo "$silent" | tr 'A-Z' 'a-z')" ] && ok "switch.ac_${UNIT}_silent follows Silent" || bad "switch is '$s', Silent is '$silent'"
```

Run it against Uitkijk now (still on `2eab73c`): `~/.config/hass/tools/mhi/post-flash-check.sh uitkijk 2eab73c`
Expected: the new block takes the `else` branch (`no retained Silent/Discovery (2eab73c predates them)`), everything else as before, `ALL CHECKS PASSED`.

- [ ] **Step 5: `remote-test.sh`, `lib.sh`, `README.md`**

`remote-test.sh`: add `Silent` to the topics it prints (next to `Fan`, `Vanes`).

`lib.sh`, `check_image`: add `grep -qx 'homeassistant' "$s" || die "image lacks Home Assistant discovery (HA_DISCOVERY)"` and `grep -qx "ac_$UNIT" "$s" || die "image lacks the discovery id prefix ac_$UNIT"`. (Both texts are literals in the image; `strings -n 5` lists them whole.)

`README.md`: add the row `| discovery-payloads.sh <unit> [version] | the discovery configs the firmware publishes for the unit, from the repo's builder | Claude or Lucas |` and a line under the flash order: "Batch B (#4): remove the unit's YAML block in hass-config and reload the MQTT YAML *before* flashing it, or Home Assistant rejects the discovery configs as duplicates."

- [ ] **Step 6: Record on #4**

```bash
gh issue comment 4 --repo lvschouwen/MHI-AC-Ctrl --body "Batch B merged: master <commit>, CI <run url>, host tests <n>, d1_mini <bytes> B, ci-all-options <bytes> B. Longest discovery row: default <n> B, Uitkijk <n> B of 1024. Toolkit: discovery-payloads.sh, post-flash-check.sh gates Silent/Vanes/Discovery/configs on <commit>, airco-config.py writes the HA_* block (Dutch names for the four new entities: lamellen, stil, storing, bedrading; rename in HA_NAMES if wanted). Nothing flashed."
```

---

### Task 10: Hardware and cutover, with hass-config (needs Lucas at every step)

**Files:** none in the repo. hass-config `packages/airco.yaml` and its tests are the hass-config session's.

**Interfaces:**
- Consumes: `discovery-payloads.sh`, `post-flash-check.sh`, `remote-test.sh`, `flash-unit.sh` (Lucas), `rollback-unit.sh` (Lucas).

Every flash and every hass-config edit below needs Lucas's explicit go for that step; a general go does not carry over ([[conservative-with-live-units]] in memory). Slaapkamer still runs `5f1d94e` and Uitkijk `2eab73c` when this plan is written; the pending Slaapkamer flash of `2eab73c` is Lucas's call to skip in favour of batch B.

- [ ] **Step 1: Rehearsal on Uitkijk, no firmware (hass-config session)**

Message the hass-config session with the ten `discovery-payloads.sh uitkijk` lines and this procedure: comment out Uitkijk's climate and five sensors in `packages/airco.yaml`, reload the MQTT YAML (Developer tools → YAML → MQTT), confirm `climate.ac_uitkijk` and `sensor.ac_uitkijk_*` are gone, publish the ten payloads retained with `mqtt.publish`, then check: `climate.ac_uitkijk` is back with its history and controls Uitkijk (a fan round trip), the five sensors keep their entity IDs and history, `sensor.ac_uitkijk_herstarts` still reads, `bedtijd`/`zonnekoeling` unchanged, the four new entities exist with the IDs `select.ac_uitkijk_vanes`, `switch.ac_uitkijk_silent`, `binary_sensor.ac_uitkijk_problem`, `binary_sensor.ac_uitkijk_wiring` (the select and the switch show `unknown`: the unit does not publish those topics yet), and the friendly names read `AC Uitkijk`, `AC Uitkijk tijd sinds opstart`, and so on. Then blank the ten retained configs (`mqtt.publish` with an empty payload, retain true) and restore the YAML block. Any deviation changes the payloads (Task 5) before firmware is flashed; hass-config greps `friendly_name` readers for `AC_Uitkijk` first.

- [ ] **Step 2: Cutover Slaapkamer (Lucas's go; the guinea pig, and the first Silent write)**

1. hass-config: remove Slaapkamer's YAML block, reload the MQTT YAML.
2. Lucas: `! ~/.config/hass/tools/mhi/flash-unit.sh slaapkamer`.
3. `~/.config/hass/tools/mhi/post-flash-check.sh slaapkamer` (expects `Silent` within ~20 s, `Discovery ok`, the ten configs identical to the tool's, the entities present).
4. With the AC **off** and `remote-test.sh slaapkamer 300` running: switch `switch.ac_slaapkamer_silent` on in HA, expect `cmd_received o.k.` and `Silent On` within two seconds (the `c0dd` read-back), then off. Then the remote's SILENT button on and off: the topic follows. Then vanes by name from the select (`Down`, then back), and the climate's swing mode.
5. `FreeHeap` before and after (the telemetry topic; expect ~1 KB less than `2eab73c`'s 45000 for the static buffer).
6. A failure at any step: `! ~/.config/hass/tools/mhi/rollback-unit.sh slaapkamer 5f1d94e` and restore the YAML block; report on #4 before touching Uitkijk.

- [ ] **Step 3: Cutover Uitkijk (Lucas's go)**

The same six steps with `uitkijk`; the rollback image is `2eab73c`.

- [ ] **Step 4: hass-config tests, overnight, close**

hass-config rewrites `tests/test_airco_diagnose_304.py` and `tests/test_airco_topics_302.py` against the tool's output for both units and removes the YAML entities for good. Next day: `health-check.sh 1` green, `FreeHeap` steady on both. Tick the batch B lines on #4, record the numbers (heap, the Silent timings, the longest row), update `ac-units` and `phase-4-design-state` in memory, and close #4 when every line is ticked.

---

## Self-review

**Spec coverage (addendum §4, revised 17 Sep):** §4.1 no client buffer change, 1024 B static builder buffer measured by the test → Tasks 5, 6. §4.2 names published, names and 1-5 accepted, `mhi_vanes` host-tested, extended-frame left/right names untouched → Tasks 1, 2. §4.3 `Silent` from `DB11 & 0x20`, the table entry, `On`/`Off`, the write with `DB10` restored, one `0x80` slot shared with `ErrOpData`, the one-shot guard, the `c0dd` read-back, the quirks in the docs → Tasks 4, 8; `DB3` mask and `DB7 & 0x02` → Task 3. §4.4 opt-in, `mhi_discovery_modes_valid()` at boot and in the test, the `Discovery` topic, lists from the payload macros, `ci-ha-discovery`, the row table with today's `uniq_id`s, `name: null` climate, `default_entity_id` under `HA_ENTITY_PREFIX`, the `dev` block, one row per pass retained, fixtures committed and checked by CI, the host tool for a unit's real payloads, manual cleanup documented → Tasks 5, 6, 7, 8. §4.5 rehearsal on Uitkijk, cutover per unit, hass-config's tests from the tool's output → Tasks 9, 10. §4.6 proof → Task 10. The `airco-config.py` values for Lucas's build → Task 9.

**Placeholder scan:** the `<n>`/`<commit>`/`<bytes>` in Task 5 step 6 and Task 9 steps 4 and 6 are values the executor measures at that step, named as such. No "TBD", no "similar to Task N" without the code.

**Type consistency:** `MhiVanesNames`/`mhi_vanes_text`/`mhi_vanes_parse`/`MHI_VANES_UNKNOWN` (Task 1) used in Task 2; `MHI_WIRING_OK` (Task 3) in Tasks 5 (as the literal in the test), 6; `status_silent`, `set_silent(bool)`, `TOPIC_SILENT`, `PAYLOAD_SILENT_ON/OFF` (Task 4) in Tasks 6, 8; `MhiDiscoveryCtx` field order in Task 5's header = the designated initialisers in Task 5's test, Task 6's `discovery.cpp` and Task 7's tool; `MhiDiscoveryRow`, `MHI_DISCOVERY_ROWS`, `MHI_DISCOVERY_BUF`, `MHI_DISCOVERY_TOPIC_MAX`, `mhi_discovery_topic/build/modes_valid` (Task 5) in Tasks 6, 7; `discovery_setup/restart/loop` (Task 6) in `main.cpp`; `discovery_args()` and `--discovery-args` (Task 9) in `discovery-payloads.sh`; `capture`'s third argument (Task 9) used in the same script.
