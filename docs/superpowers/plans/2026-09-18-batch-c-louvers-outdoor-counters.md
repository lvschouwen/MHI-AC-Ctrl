# Batch C: left/right louvers, 3D auto, the outdoor device, frame counters, fan names, error text — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Carry fork issues **#20** (left/right louvers and 3D auto, decoupled and named), **#19** (a Home Assistant device for the shared outdoor unit) and **#21** (frame-error/timeout counters, named fan levels, error/protection text) in one firmware build, one flash per unit, per the design's decision of 18 Sep 2026.

**Architecture:** Four new pure modules in `lib/mhi_pure`, all host-tested: `mhi_vanes_lr` (decoupled set/decode for VanesLR and 3Dauto, plus names), `mhi_frame_stats` (counts `MHI_AC_Ctrl_Core::loop()`'s return into `errors`/`timeouts`), `mhi_fan` (names ↔ the four fan levels, mirroring `mhi_vanes`), `mhi_error_text` (error and compressor-protection text). `mhi_discovery` gains twelve rows (append-only), a `has_lr`/`has_outdoor` gate (`mhi_discovery_row_enabled`), the climate's `swing_horizontal_mode_*`, and an outdoor `dev` block linked with `via_device`. `MHI-AC-Ctrl-core.cpp` uses `mhi_vanes_lr` for `set_vanesLR()`, `set_3Dauto()` and the frame-33 decode — decoupling two commands that today silently touch each other's set flag. `main.cpp` counts every `mhi_ac_ctrl_core.loop()` return and publishes `FrameErrors`/`FrameTimeouts` next to `Uptime`, and gains `ErrorText`/`OpData/PROTECTION-TEXT`. `src/discovery.cpp` fills the new context fields, including the outdoor device's own identity, and skips a disabled row.

**Tech Stack:** PlatformIO, ESP8266 Arduino core 3.1.2, PubSubClient3 3.3.1, Unity host tests (`pio test -e native`), GitHub Actions CI.

**Spec:** `docs/superpowers/specs/2026-09-18-batch-c-louvers-outdoor-counters-design.md`, an addendum to `2026-09-16-phase-4-batches-design.md`. Fork issues #20, #19, #21 track the checklist.

**Branch:** already `feat/20-19-21-batch-c` off `master` (checked out, clean at `5b0ed4e`). Merge `--ff-only` after CI is green.

---

## Global Constraints

- `lib/mhi_pure` compiles without Arduino: `stdint.h`/`stddef.h`/`stdio.h`/`string.h`/`stdarg.h` only. Every function has a Unity test that failed first. The only Arduino-dependent thing allowed there is the `#if defined(ARDUINO)` PROGMEM macro pair (`FMT`/`MHI_VSNPRINTF`) `mhi_discovery.cpp` already uses; `mhi_error_text.cpp` reuses the identical two lines, because its tables are text and must not sit in RAM once the real error table lands.
- `-Werror` on `src/`; `-Wall -Wextra -Werror` on native tests. `main.cpp`'s status switches are exhaustive under `-Werror=switch`; this batch adds no new `ACStatus` value, so no new case label is needed.
- Flash budget 460000 B (`scripts/check_flash_size.py`). `d1_mini` at `9352cb0` (this branch's parent) is 349104 B; Task 9 reports the new size.
- Every new topic/payload text is an `#ifndef`-guarded define in `src/MHI-AC-Ctrl.h`; every new option in `src/support.h`, overridable from the gitignored `src/config_defaults.h`. **Never read or print `src/config_defaults.h`**.
- No `setBufferSize()`. The discovery buffer stays a `static char[1024]` in BSS.
- `MhiDiscoveryRow` is append-only. The twelve new rows go after `MHI_DISCOVERY_WIFI_PHY`, in exactly this order (spec §5): `VANES_LR, 3DAUTO, FRAME_ERRORS, FRAME_TIMEOUTS, ERROR_TEXT, OU_OUTDOOR, OU_CT, OU_KWH, OU_COMP, OU_DEFROST, OU_COMP_RUN, OU_PROTECTION`. 22 rows total; without `USE_EXTENDED_FRAME_SIZE`/`HA_OUTDOOR_DEVICE` a build publishes 13 (the existing 10 plus the always-on `FRAME_ERRORS`/`FRAME_TIMEOUTS`/`ERROR_TEXT`).
- The ten committed fixtures in `test/fixtures/discovery/` must not change one byte; three new files join them. A second, everything-on fixture set goes to `test/fixtures/discovery_all/` (22 files). CI checks both directories are clean after `pio test -e native` rewrites them.
- Commit format `type: text (#20)` / `(#19)` / `(#21)` — cite every issue a task touches — ending with these two trailer lines, spelled exactly (subagents must not paraphrase them or substitute their own model name): `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_01KYBzXU6ciProQ7qkoN43Jk`.
- Never weaken or delete an existing test to make a change pass.
- This plan does not push or open a PR; that is the orchestrator's call after Task 9.

---

## File map

| File | Responsibility |
|---|---|
| `lib/mhi_pure/mhi_vanes_lr.h`/`.cpp` (new) | decoupled VanesLR/3Dauto set+decode, VanesLR names |
| `lib/mhi_pure/mhi_frame_stats.h`/`.cpp` (new) | `FrameErrors`/`FrameTimeouts` counting |
| `lib/mhi_pure/mhi_fan.h`/`.cpp` (new) | fan level names ↔ 1..4/Auto |
| `lib/mhi_pure/mhi_error_text.h`/`.cpp` (new) | `ErrorText`, `OpData/PROTECTION-TEXT` lookups |
| `test/test_mhi_vanes_lr,test_mhi_frame_stats,test_mhi_fan,test_mhi_error_text/*.cpp` (new) | Unity tests for the above |
| `lib/mhi_pure/mhi_discovery.h`/`.cpp` | twelve new rows, `has_lr`/`has_outdoor`, `mhi_discovery_row_enabled`, climate `swing_h_*`, outdoor `dev` block |
| `test/test_mhi_discovery/test_mhi_discovery.cpp`, `test/fixtures/discovery/*.txt` (+3), `test/fixtures/discovery_all/*.txt` (new, 22) | tests, both fixture sets |
| `tools/discovery_payloads.cpp` | new CLI options matching the new context fields |
| `src/MHI-AC-Ctrl-core.h`/`.cpp` | `set_vanesLR()`/`set_3Dauto()` use `mhi_vanes_lr`; frame-33 decode too |
| `src/MHI-AC-Ctrl.h` | `PAYLOAD_VANESLR_1..7`, `PAYLOAD_FAN_1..4`, `TOPIC_FRAME_ERRORS`, `TOPIC_FRAME_TIMEOUTS`, `TOPIC_ERROR_TEXT`, `TOPIC_PROTECTION_TEXT` |
| `src/support.h`/`.cpp` | `note_frame_result()`, `HA_OUTDOOR_*`, `HA_NAME_*` for the new rows, two more counters in `publishTelemetryNow` |
| `src/discovery.cpp` | context fill, the `HA_OUTDOOR_DEVICE` guard |
| `src/main.cpp` | `vanes_lr_names`, `fan_names`, decoupled VanesLR/3Dauto, `ErrorText`/`PROTECTION-TEXT`, `note_frame_result()` |
| `platformio.ini`, `.github/workflows/ci.yml` | `ci-ha-discovery-outdoor`, `HA_OUTDOOR_DEVICE` in `ci-all-options`, second fixture-gate directory |
| `SW-Configuration.md`, `Version.md` | docs |

---

### Task 1: `mhi_vanes_lr` — decoupled commands, decode, names (pure)

**Files:** create `lib/mhi_pure/mhi_vanes_lr.h`, `lib/mhi_pure/mhi_vanes_lr.cpp`, `test/test_mhi_vanes_lr/test_mhi_vanes_lr.cpp`.

**Interfaces produced:** `MHI_VANES_LR_UNKNOWN` (0), `MHI_VANES_LR_SWING` (8, the core's `vanesLR_swing`), `struct MhiVanesLrNames { const char* pos[7]; const char* swing; }`, `void mhi_vanes_lr_command(int value, uint8_t* db16, uint8_t* db17)`, `uint8_t mhi_3dauto_command(bool on)`, `int mhi_vanes_lr_decode(uint8_t db16, uint8_t db17)`, `bool mhi_3dauto_decode(uint8_t db17)`, `const char* mhi_vanes_lr_text(...)`, `int mhi_vanes_lr_parse(...)`.

Today's coupling (`src/MHI-AC-Ctrl-core.cpp:74-96`): `set_3Dauto()` sends `0b00001010 | Dauto` (3D-auto set flag `0x08` plus swing set flag `0x02`, swing bit 0); `set_vanesLR()`'s position branch sends `new_VanesLR0 = 0b00001010` (same pair). Either command touches both features' set flags. This task decouples them; Task 2 wires it into the core.

- [ ] **Step 1: Write the failing tests**

`test/test_mhi_vanes_lr/test_mhi_vanes_lr.cpp`:

```cpp
// Host tests for the left/right louvers and 3D auto (fork #20; spec
// docs/superpowers/specs/2026-09-18-batch-c-louvers-outdoor-counters-design.md §2.1).
// The upstream commands are coupled (see MHI-AC-Ctrl-core.cpp before Task 2);
// decode must mask the AC's echo of both set flags (DB16 & 0x10, DB17 & 0x0a).

#include <string.h>
#include <unity.h>

#include "mhi_vanes_lr.h"

void setUp(void) {}
void tearDown(void) {}

static const MhiVanesLrNames kNames = {
  {"Left", "LeftCenter", "Center", "CenterRight", "Right", "Wide", "Spot"}, "Swing"};

static void test_command_sets_the_position_and_the_swing_set_flag_only(void) {
  uint8_t db16, db17;
  mhi_vanes_lr_command(1, &db16, &db17);
  TEST_ASSERT_EQUAL_HEX8(0x10, db16);  // set flag (0x10) + position 0
  TEST_ASSERT_EQUAL_HEX8(0x02, db17);  // swing set flag, swing off; no 0x08
  mhi_vanes_lr_command(7, &db16, &db17);
  TEST_ASSERT_EQUAL_HEX8(0x16, db16);  // set flag + position 6
  TEST_ASSERT_EQUAL_HEX8(0x02, db17);
}

static void test_command_swing_sets_no_position(void) {
  uint8_t db16, db17;
  mhi_vanes_lr_command(MHI_VANES_LR_SWING, &db16, &db17);
  TEST_ASSERT_EQUAL_HEX8(0x00, db16);
  TEST_ASSERT_EQUAL_HEX8(0x03, db17);  // swing set flag + swing on; no 0x08
}

static void test_3dauto_command_sets_no_swing_flag(void) {
  TEST_ASSERT_EQUAL_HEX8(0x0c, mhi_3dauto_command(true));   // 0x08 | 0x04; no 0x02
  TEST_ASSERT_EQUAL_HEX8(0x08, mhi_3dauto_command(false));  // 0x08 | 0; no 0x02
}

static void test_decode_reads_position_and_swing(void) {
  TEST_ASSERT_EQUAL_INT(1, mhi_vanes_lr_decode(0x00, 0x00));
  TEST_ASSERT_EQUAL_INT(7, mhi_vanes_lr_decode(0x06, 0x00));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_LR_SWING, mhi_vanes_lr_decode(0x00, 0x01));
}

static void test_decode_masks_the_acs_echo_of_the_set_flags(void) {
  // Uitkijk, 18 Sep 2026: after a write the AC echoes DB16 & 0x10, DB17 & 0x0a
  // until the remote is used next.
  TEST_ASSERT_EQUAL_INT(3, mhi_vanes_lr_decode(0x10 | 0x02, 0x0a));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_LR_SWING, mhi_vanes_lr_decode(0x10, 0x0a | 0x01));
}

static void test_3dauto_decode_reads_bit_2_only(void) {
  TEST_ASSERT_FALSE(mhi_3dauto_decode(0x0a));        // set-flag echo, 3D auto off
  TEST_ASSERT_TRUE(mhi_3dauto_decode(0x0a | 0x04));  // set-flag echo, 3D auto on
}

static void test_text_and_parse_round_trip(void) {
  TEST_ASSERT_EQUAL_STRING("Left", mhi_vanes_lr_text(&kNames, 1));
  TEST_ASSERT_EQUAL_STRING("Spot", mhi_vanes_lr_text(&kNames, 7));
  TEST_ASSERT_EQUAL_STRING("Swing", mhi_vanes_lr_text(&kNames, MHI_VANES_LR_SWING));
  TEST_ASSERT_NULL(mhi_vanes_lr_text(&kNames, 0));
  TEST_ASSERT_NULL(mhi_vanes_lr_text(&kNames, 9));
  TEST_ASSERT_EQUAL_INT(1, mhi_vanes_lr_parse(&kNames, "Left"));
  TEST_ASSERT_EQUAL_INT(7, mhi_vanes_lr_parse(&kNames, "7"));  // v2.8-style numbers still work
  TEST_ASSERT_EQUAL_INT(MHI_VANES_LR_SWING, mhi_vanes_lr_parse(&kNames, "8"));
}

static void test_parse_rejects_everything_else(void) {
  TEST_ASSERT_EQUAL_INT(MHI_VANES_LR_UNKNOWN, mhi_vanes_lr_parse(&kNames, "0"));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_LR_UNKNOWN, mhi_vanes_lr_parse(&kNames, "9"));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_LR_UNKNOWN, mhi_vanes_lr_parse(&kNames, "left"));  // case-sensitive
  TEST_ASSERT_EQUAL_INT(MHI_VANES_LR_UNKNOWN, mhi_vanes_lr_parse(&kNames, ""));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_LR_UNKNOWN, mhi_vanes_lr_parse(&kNames, NULL));
}

static void test_a_configuration_that_keeps_the_numbers_as_names_still_works(void) {
  const MhiVanesLrNames numeric = {{"1", "2", "3", "4", "5", "6", "7"}, "8"};
  TEST_ASSERT_EQUAL_STRING("3", mhi_vanes_lr_text(&numeric, 3));
  TEST_ASSERT_EQUAL_INT(3, mhi_vanes_lr_parse(&numeric, "3"));
  TEST_ASSERT_EQUAL_INT(MHI_VANES_LR_SWING, mhi_vanes_lr_parse(&numeric, "8"));  // "8" is the swing NAME here
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_command_sets_the_position_and_the_swing_set_flag_only);
  RUN_TEST(test_command_swing_sets_no_position);
  RUN_TEST(test_3dauto_command_sets_no_swing_flag);
  RUN_TEST(test_decode_reads_position_and_swing);
  RUN_TEST(test_decode_masks_the_acs_echo_of_the_set_flags);
  RUN_TEST(test_3dauto_decode_reads_bit_2_only);
  RUN_TEST(test_text_and_parse_round_trip);
  RUN_TEST(test_parse_rejects_everything_else);
  RUN_TEST(test_a_configuration_that_keeps_the_numbers_as_names_still_works);
  return UNITY_END();
}
```

- [ ] **Step 2: Run and see it fail** — `pio test -e native -f test_mhi_vanes_lr` → build error, `mhi_vanes_lr.h: No such file or directory`.

- [ ] **Step 3: Implement**

`lib/mhi_pure/mhi_vanes_lr.h`:

```cpp
// Left/right louvers and 3D auto, decoupled (fork #20; spec §2.1). Pure
// logic, no Arduino.
#pragma once
#include <stdint.h>

#define MHI_VANES_LR_UNKNOWN 0  // mhi_vanes_lr_parse's failure value
#define MHI_VANES_LR_SWING 8    // the core's ACVanesLR::vanesLR_swing

// DB16/DB17 for a position (1..7) or swing. Raises only the swing set flag
// (DB17 0x02); the 3D-auto set flag (0x08) is never touched.
void mhi_vanes_lr_command(int value, uint8_t* db16, uint8_t* db17);

// DB17 for 3D auto on/off. Raises only the 3D-auto set flag (0x08); the
// swing set flag (0x02) is never touched.
uint8_t mhi_3dauto_command(bool on);

// Decodes DB16/DB17 into 1..7 or MHI_VANES_LR_SWING, masking the AC's echo
// of both set flags (DB16 & 0x10, DB17 & 0x0a) after a write.
int mhi_vanes_lr_decode(uint8_t db16, uint8_t db17);

// Decodes DB17 bit 2 into 3D auto's state; also unaffected by the echoes.
bool mhi_3dauto_decode(uint8_t db17);

struct MhiVanesLrNames {
  const char* pos[7];  // positions 1..7, as seen on the unit (1 leftmost .. 7 spot)
  const char* swing;
};

// 1..7 -> the position's name, MHI_VANES_LR_SWING -> swing, else NULL.
const char* mhi_vanes_lr_text(const MhiVanesLrNames* names, int value);

// A set/VanesLR payload: one of the eight names, or "1".."8" (8 = swing).
// MHI_VANES_LR_UNKNOWN when it is none of them (NULL and "" included).
int mhi_vanes_lr_parse(const MhiVanesLrNames* names, const char* payload);
```

`lib/mhi_pure/mhi_vanes_lr.cpp`:

```cpp
#include "mhi_vanes_lr.h"
#include <string.h>

void mhi_vanes_lr_command(int value, uint8_t* db16, uint8_t* db17) {
  if (value == MHI_VANES_LR_SWING) {
    *db16 = 0x00;
    *db17 = 0x03;  // swing set flag (0x02) + swing on (0x01)
  }
  else {
    *db16 = 0x10 | (uint8_t)(value - 1);  // set flag + position, 0-based
    *db17 = 0x02;                          // swing set flag, swing off
  }
}

uint8_t mhi_3dauto_command(bool on) {
  return 0x08 | (on ? 0x04 : 0);
}

int mhi_vanes_lr_decode(uint8_t db16, uint8_t db17) {
  if (db17 & 0x01) return MHI_VANES_LR_SWING;
  return (db16 & 0x07) + 1;
}

bool mhi_3dauto_decode(uint8_t db17) {
  return (db17 & 0x04) != 0;
}

const char* mhi_vanes_lr_text(const MhiVanesLrNames* names, int value) {
  if (value >= 1 && value <= 7) return names->pos[value - 1];
  if (value == MHI_VANES_LR_SWING) return names->swing;
  return NULL;
}

int mhi_vanes_lr_parse(const MhiVanesLrNames* names, const char* payload) {
  if (!payload || !*payload) return MHI_VANES_LR_UNKNOWN;
  for (int i = 0; i < 7; i++)
    if (strcmp(payload, names->pos[i]) == 0) return i + 1;
  if (strcmp(payload, names->swing) == 0) return MHI_VANES_LR_SWING;
  if (payload[1] == '\0' && payload[0] >= '1' && payload[0] <= '8') return payload[0] - '0';
  return MHI_VANES_LR_UNKNOWN;
}
```

- [ ] **Step 4: Run and see it pass** — `pio test -e native -f test_mhi_vanes_lr` → `9 Tests 0 Failures 0 Ignored`.

- [ ] **Step 5: Commit**

```bash
git add lib/mhi_pure/mhi_vanes_lr.h lib/mhi_pure/mhi_vanes_lr.cpp test/test_mhi_vanes_lr/test_mhi_vanes_lr.cpp
git commit -m "feat: decoupled VanesLR/3Dauto commands and decode, and VanesLR names, host-tested (#20)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KYBzXU6ciProQ7qkoN43Jk"
```

---

### Task 2: Wire `mhi_vanes_lr` into the core and `main.cpp`

**Files:** modify `src/MHI-AC-Ctrl-core.cpp` (`set_3Dauto()`/`set_vanesLR()` at lines 74-96, frame-33 decode at lines 314-328), `src/MHI-AC-Ctrl.h` (after line 280), `src/main.cpp` (includes; after line 46; `set/VanesLR` at lines 170-183; `status_vanesLR` at lines 392-403).

**Produces:** `PAYLOAD_VANESLR_1` `"Left"` .. `PAYLOAD_VANESLR_7` `"Spot"` (Task 6 reads them); `static const MhiVanesLrNames vanes_lr_names` in `main.cpp`.

- [ ] **Step 1: Seven name macros** — in `src/MHI-AC-Ctrl.h`, after the `PAYLOAD_VANESLR_SWING` block:

```cpp
// Left/right louver positions, as seen on the unit: 1 leftmost .. 7 spot
// (fork #20). set/VanesLR also accepts 1..7 and 8 (= swing) whatever these
// say; define them as "1".."7" to keep the numeric texts.
#ifndef PAYLOAD_VANESLR_1
#define PAYLOAD_VANESLR_1 "Left"
#endif
#ifndef PAYLOAD_VANESLR_2
#define PAYLOAD_VANESLR_2 "LeftCenter"
#endif
#ifndef PAYLOAD_VANESLR_3
#define PAYLOAD_VANESLR_3 "Center"
#endif
#ifndef PAYLOAD_VANESLR_4
#define PAYLOAD_VANESLR_4 "CenterRight"
#endif
#ifndef PAYLOAD_VANESLR_5
#define PAYLOAD_VANESLR_5 "Right"
#endif
#ifndef PAYLOAD_VANESLR_6
#define PAYLOAD_VANESLR_6 "Wide"
#endif
#ifndef PAYLOAD_VANESLR_7
#define PAYLOAD_VANESLR_7 "Spot"
#endif
```

- [ ] **Step 2: The core uses the decoupled functions** — add `#include "mhi_vanes_lr.h"` to `MHI-AC-Ctrl-core.cpp`'s includes (after `mhi_action.h`). Replace `set_3Dauto()`/`set_vanesLR()` (lines 74-96):

```cpp
void MHI_AC_Ctrl_Core::set_3Dauto(AC3Dauto Dauto) {
  new_3Dauto = mhi_3dauto_command(Dauto == Dauto_on);
}

void MHI_AC_Ctrl_Core::set_vanesLR(uint vanesLR) {
  uint8_t db16, db17;
  mhi_vanes_lr_command((int)vanesLR, &db16, &db17);
  new_VanesLR1 = db16;  // ORed into MISO_frame[DB16] in loop()
  new_VanesLR0 = db17;  // ORed into MISO_frame[DB17] in loop()
}
```

(`loop()` at line 253-256 still does `MISO_frame[DB16] |= new_VanesLR1; MISO_frame[DB17] |= new_VanesLR0; MISO_frame[DB17] |= new_3Dauto;` unchanged, so a queued position command and a queued 3D-auto command still combine in one frame.)

Replace the frame-33 decode block (lines 314-328):

```cpp
    if (frameSize == 33 ) { // Only for framesize 33 (WF-RAC)
      const byte vanesLRtmp = (byte)mhi_vanes_lr_decode(MOSI_frame[DB16], MOSI_frame[DB17]);
      if (vanesLRtmp != status_vanesLR_old) {
        status_vanesLR_old = vanesLRtmp;
        m_cbiStatus->cbiStatusFunction(status_vanesLR, vanesLRtmp);  // 1..7, or MHI_VANES_LR_SWING == vanesLR_swing
      }

      const byte dauto_tmp = mhi_3dauto_decode(MOSI_frame[DB17]) ? Dauto_on : Dauto_off;
      if (dauto_tmp != status_3Dauto_old) {
        status_3Dauto_old = dauto_tmp;
        m_cbiStatus->cbiStatusFunction(status_3Dauto, dauto_tmp);
      }
    }
```

(`status_vanesLR_old`/`status_3Dauto_old` reset to `0xff` in `reset_old_values()`, distinct from every real value, so the first frame after a reconnect still always publishes.)

- [ ] **Step 3: `main.cpp`** — add `#include "mhi_vanes_lr.h"` (alphabetically after `mhi_vanes.h`, since `'.'` sorts before `'_'`). After the existing `vanes_names`/`static_assert` block:

```cpp
// The texts on the VanesLR topic; set/VanesLR accepts these and 1..8 (fork #20).
static const MhiVanesLrNames vanes_lr_names = {
  {PAYLOAD_VANESLR_1, PAYLOAD_VANESLR_2, PAYLOAD_VANESLR_3, PAYLOAD_VANESLR_4, PAYLOAD_VANESLR_5, PAYLOAD_VANESLR_6,
   PAYLOAD_VANESLR_7},
  PAYLOAD_VANESLR_SWING};
static_assert(MHI_VANES_LR_SWING == vanesLR_swing, "mhi_vanes_lr numbers swing as the core's ACVanesLR does");
```

Replace the `set/VanesLR` branch (lines 170-183):

```cpp
  else if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_VANESLR)) == 0) {
    const int vaneslr = mhi_vanes_lr_parse(&vanes_lr_names, payload_str);
    if (vaneslr != MHI_VANES_LR_UNKNOWN) {
      mhi_ac_ctrl_core.set_vanesLR(vaneslr);
      publish_cmd_ok();
    }
    else
      publish_cmd_invalidparameter();
  }
```

(`set/3Dauto`'s branch directly below is unchanged: the coupling lived in the core, not here.) Replace the `status_vanesLR` case (lines 392-403):

```cpp
        case status_vanesLR:
#ifdef USE_EXTENDED_FRAME_SIZE
          output_P(status, PSTR(TOPIC_VANESLR), mhi_vanes_lr_text(&vanes_lr_names, value));
#endif
          break;
```

- [ ] **Step 4: Build** — `pio run -e d1_mini -e ci-extended-frame -e ci-all-options` → all succeed, no `src/` warnings.

- [ ] **Step 5: Commit**

```bash
git add src/MHI-AC-Ctrl-core.cpp src/MHI-AC-Ctrl.h src/main.cpp
git commit -m "feat: VanesLR and 3Dauto commands no longer step on each other's set flag; VanesLR publishes named positions (#20)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KYBzXU6ciProQ7qkoN43Jk"
```

---

### Task 3: `mhi_frame_stats` — `FrameErrors`/`FrameTimeouts`

**Files:** create `lib/mhi_pure/mhi_frame_stats.h`/`.cpp`, `test/test_mhi_frame_stats/test_mhi_frame_stats.cpp`; modify `src/MHI-AC-Ctrl.h` (after line 37), `src/support.h` (after line 206), `src/support.cpp` (includes at line 4-8; line 204-224), `src/main.cpp` (line 704).

**Produces:** `struct MhiFrameStats { uint32_t errors; uint32_t timeouts; }`, `void mhi_frame_stats_count(MhiFrameStats*, int err_msg)`, `void note_frame_result(int ret)`; `TOPIC_FRAME_ERRORS`, `TOPIC_FRAME_TIMEOUTS`.

- [ ] **Step 1: Write the failing tests**

`test/test_mhi_frame_stats/test_mhi_frame_stats.cpp`:

```cpp
// Host tests for the frame-error/timeout counters (fork #21 F1; spec §4.1).
// err_msg is MHI-AC-Ctrl-core.h's ErrMsg as loop() returns it: 0 valid,
// -1 invalid signature, -2 invalid checksum, -3/-4 SCK timeouts. Passed as a
// plain int so this module need not include MHI-AC-Ctrl-core.h (Arduino.h).

#include <stdint.h>
#include <unity.h>

#include "mhi_frame_stats.h"

void setUp(void) {}
void tearDown(void) {}

static void test_a_valid_frame_counts_as_neither(void) {
  MhiFrameStats s = {0, 0};
  mhi_frame_stats_count(&s, 0);
  TEST_ASSERT_EQUAL_UINT32(0, s.errors);
  TEST_ASSERT_EQUAL_UINT32(0, s.timeouts);
}

static void test_a_bad_signature_or_checksum_counts_as_an_error(void) {
  MhiFrameStats s = {0, 0};
  mhi_frame_stats_count(&s, -1);
  mhi_frame_stats_count(&s, -2);
  TEST_ASSERT_EQUAL_UINT32(2, s.errors);
  TEST_ASSERT_EQUAL_UINT32(0, s.timeouts);
}

static void test_either_sck_timeout_counts_as_a_timeout(void) {
  MhiFrameStats s = {0, 0};
  mhi_frame_stats_count(&s, -3);
  mhi_frame_stats_count(&s, -4);
  TEST_ASSERT_EQUAL_UINT32(0, s.errors);
  TEST_ASSERT_EQUAL_UINT32(2, s.timeouts);
}

static void test_an_unrecognised_value_counts_as_neither(void) {
  MhiFrameStats s = {0, 0};
  mhi_frame_stats_count(&s, 42);
  TEST_ASSERT_EQUAL_UINT32(0, s.errors);
  TEST_ASSERT_EQUAL_UINT32(0, s.timeouts);
}

static void test_counts_saturate(void) {
  MhiFrameStats s = {UINT32_MAX, UINT32_MAX};
  mhi_frame_stats_count(&s, -1);
  mhi_frame_stats_count(&s, -3);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, s.errors);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, s.timeouts);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_valid_frame_counts_as_neither);
  RUN_TEST(test_a_bad_signature_or_checksum_counts_as_an_error);
  RUN_TEST(test_either_sck_timeout_counts_as_a_timeout);
  RUN_TEST(test_an_unrecognised_value_counts_as_neither);
  RUN_TEST(test_counts_saturate);
  return UNITY_END();
}
```

- [ ] **Step 2: Run and see it fail** — `pio test -e native -f test_mhi_frame_stats` → `mhi_frame_stats.h: No such file or directory`.

- [ ] **Step 3: Implement**

`lib/mhi_pure/mhi_frame_stats.h`:

```cpp
// Running counts of protocol errors/timeouts (fork #21 F1; spec §4.1), for
// FrameErrors/FrameTimeouts and the soak that establishes a normal
// FrameTimeouts rate. Pure logic, no Arduino.
#pragma once
#include <stdint.h>

struct MhiFrameStats {
  uint32_t errors;
  uint32_t timeouts;
};

// err_msg is ErrMsg as MHI_AC_Ctrl_Core::loop() returns it: -1/-2 (invalid
// signature/checksum) count as errors, -3/-4 (SCK timeouts) as timeouts, 0
// and anything else as neither. Both fields saturate at UINT32_MAX. A plain
// int so this file need not include MHI-AC-Ctrl-core.h (pulls in Arduino.h).
void mhi_frame_stats_count(MhiFrameStats* stats, int err_msg);
```

`lib/mhi_pure/mhi_frame_stats.cpp`:

```cpp
#include "mhi_frame_stats.h"

static void bump(uint32_t* n) {
  if (*n < UINT32_MAX) (*n)++;
}

void mhi_frame_stats_count(MhiFrameStats* stats, int err_msg) {
  switch (err_msg) {
    case -1: case -2: bump(&stats->errors); break;
    case -3: case -4: bump(&stats->timeouts); break;
    default: break;  // err_msg_valid_frame (0) and anything else
  }
}
```

- [ ] **Step 4: Run and see it pass** — `pio test -e native -f test_mhi_frame_stats` → `5 Tests 0 Failures 0 Ignored`.

- [ ] **Step 5: The topics** — in `src/MHI-AC-Ctrl.h`, after `TOPIC_FREE_HEAP`:

```cpp
#ifndef TOPIC_FRAME_ERRORS
#define TOPIC_FRAME_ERRORS "FrameErrors"       // invalid-signature/checksum frames since boot (fork #21)
#endif
#ifndef TOPIC_FRAME_TIMEOUTS
#define TOPIC_FRAME_TIMEOUTS "FrameTimeouts"   // SCK timeouts since boot (fork #21)
#endif
```

- [ ] **Step 6: Wire the counting and the publish** — `src/support.h`, after `void output_P(...)`:

```cpp
void note_frame_result(int ret);  // count mhi_ac_ctrl_core.loop()'s return towards FrameErrors/FrameTimeouts (fork #21)
```

`src/support.cpp`: add `#include "mhi_frame_stats.h"` (after `mhi_diag.h`). Add `static MhiFrameStats frame_stats = {0, 0};` next to `uptime_counter`/`telemetry_pacer` (line 204-205). Add after `publishTelemetry()`:

```cpp
void note_frame_result(int ret) {
  mhi_frame_stats_count(&frame_stats, ret);
}
```

Extend `publishTelemetryNow()` (line 207-215), appending after the `FreeHeap` publish:

```cpp
  ultoa(frame_stats.errors, strtmp, 10);
  output_P((ACStatus)type_status, PSTR(TOPIC_FRAME_ERRORS), strtmp);
  ultoa(frame_stats.timeouts, strtmp, 10);
  output_P((ACStatus)type_status, PSTR(TOPIC_FRAME_TIMEOUTS), strtmp);
```

`src/main.cpp`, after `int ret = mhi_ac_ctrl_core.loop(80);` (line 704): add `note_frame_result(ret);` before the `if (ret < 0)` line. (`publishTelemetryNow` is also called directly from `MQTTreconnect()` at connect, so both new topics follow `Uptime` there too — matches spec §4.1.)

- [ ] **Step 7: Build** — `pio run -e d1_mini -e ci-continue-without-mqtt` → both succeed.

- [ ] **Step 8: Commit**

```bash
git add lib/mhi_pure/mhi_frame_stats.h lib/mhi_pure/mhi_frame_stats.cpp test/test_mhi_frame_stats/test_mhi_frame_stats.cpp src/MHI-AC-Ctrl.h src/support.h src/support.cpp src/main.cpp
git commit -m "feat: FrameErrors/FrameTimeouts counters, published with the periodic telemetry (#21)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KYBzXU6ciProQ7qkoN43Jk"
```

---

### Task 4: `mhi_fan` — named fan levels (F6)

**Files:** create `lib/mhi_pure/mhi_fan.h`/`.cpp`, `test/test_mhi_fan/test_mhi_fan.cpp`; modify `src/MHI-AC-Ctrl.h` (after line 228), `src/main.cpp` (includes; after the vanes_lr_names block; `set/Fan` at lines 136-159; `status_fan` at lines 362-385).

**Produces:** `MHI_FAN_NONE` (0), `MHI_FAN_AUTO` (5), `struct MhiFanNames { const char* levels[4]; const char* auto_name; }`, `mhi_fan_text`, `mhi_fan_parse`; `PAYLOAD_FAN_1` `"1"` .. `PAYLOAD_FAN_4` `"4"` (defaults keep the wire and the fixtures byte-identical — the two real units keep the numeric texts, spec F6). This module does not know the core's own fan bytes (0,1,2,6,7); `main.cpp` keeps that mapping unchanged.

- [ ] **Step 1: Write the failing tests**

`test/test_mhi_fan/test_mhi_fan.cpp`:

```cpp
// Host tests for named fan levels (fork #21 F6; spec §4.2). The texts belong
// to the caller (PAYLOAD_FAN_* in MHI-AC-Ctrl.h); this module only maps them
// to levels 1..4 and Auto.

#include <string.h>
#include <unity.h>

#include "mhi_fan.h"

void setUp(void) {}
void tearDown(void) {}

static const MhiFanNames kNames = {{"1", "2", "3", "4"}, "Auto"};

static void test_text_and_parse_round_trip(void) {
  TEST_ASSERT_EQUAL_STRING("1", mhi_fan_text(&kNames, 1));
  TEST_ASSERT_EQUAL_STRING("4", mhi_fan_text(&kNames, 4));
  TEST_ASSERT_EQUAL_STRING("Auto", mhi_fan_text(&kNames, MHI_FAN_AUTO));
  TEST_ASSERT_NULL(mhi_fan_text(&kNames, 0));
  TEST_ASSERT_NULL(mhi_fan_text(&kNames, 6));
  TEST_ASSERT_EQUAL_INT(1, mhi_fan_parse(&kNames, "1"));
  TEST_ASSERT_EQUAL_INT(4, mhi_fan_parse(&kNames, "4"));
  TEST_ASSERT_EQUAL_INT(MHI_FAN_AUTO, mhi_fan_parse(&kNames, "Auto"));
}

static void test_parse_with_custom_names_still_accepts_the_digits(void) {
  const MhiFanNames named = {{"Low", "Medium", "High", "Turbo"}, "Auto"};
  TEST_ASSERT_EQUAL_INT(3, mhi_fan_parse(&named, "High"));
  TEST_ASSERT_EQUAL_INT(3, mhi_fan_parse(&named, "3"));  // v2.8's numbers still work
}

static void test_parse_rejects_everything_else(void) {
  TEST_ASSERT_EQUAL_INT(MHI_FAN_NONE, mhi_fan_parse(&kNames, "0"));
  TEST_ASSERT_EQUAL_INT(MHI_FAN_NONE, mhi_fan_parse(&kNames, "5"));
  TEST_ASSERT_EQUAL_INT(MHI_FAN_NONE, mhi_fan_parse(&kNames, "auto"));  // case-sensitive
  TEST_ASSERT_EQUAL_INT(MHI_FAN_NONE, mhi_fan_parse(&kNames, ""));
  TEST_ASSERT_EQUAL_INT(MHI_FAN_NONE, mhi_fan_parse(&kNames, NULL));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_text_and_parse_round_trip);
  RUN_TEST(test_parse_with_custom_names_still_accepts_the_digits);
  RUN_TEST(test_parse_rejects_everything_else);
  return UNITY_END();
}
```

- [ ] **Step 2: Run and see it fail** — `pio test -e native -f test_mhi_fan` → `mhi_fan.h: No such file or directory`.

- [ ] **Step 3: Implement**

`lib/mhi_pure/mhi_fan.h`:

```cpp
// Named fan levels (fork #21 F6; spec §4.2). main.cpp maps these to/from the
// core's own fan bytes (0, 1, 2, 6 for the four levels, 7 for Auto),
// unchanged from today. Pure logic, no Arduino.
#pragma once

#define MHI_FAN_NONE 0  // mhi_fan_parse's failure value
#define MHI_FAN_AUTO 5

struct MhiFanNames {
  const char* levels[4];  // PAYLOAD_FAN_1..4
  const char* auto_name;  // PAYLOAD_FAN_AUTO
};

// 1..4 -> the level's name, MHI_FAN_AUTO -> auto_name, else NULL.
const char* mhi_fan_text(const MhiFanNames* names, int level);

// A set/Fan payload: one of the four level names, "1".."4", or auto_name.
// MHI_FAN_NONE when it is none of them (NULL and "" included).
int mhi_fan_parse(const MhiFanNames* names, const char* payload);
```

`lib/mhi_pure/mhi_fan.cpp`:

```cpp
#include "mhi_fan.h"
#include <string.h>

const char* mhi_fan_text(const MhiFanNames* names, int level) {
  if (level >= 1 && level <= 4) return names->levels[level - 1];
  if (level == MHI_FAN_AUTO) return names->auto_name;
  return NULL;
}

int mhi_fan_parse(const MhiFanNames* names, const char* payload) {
  if (!payload || !*payload) return MHI_FAN_NONE;
  for (int i = 0; i < 4; i++)
    if (strcmp(payload, names->levels[i]) == 0) return i + 1;
  if (strcmp(payload, names->auto_name) == 0) return MHI_FAN_AUTO;
  if (payload[1] == '\0' && payload[0] >= '1' && payload[0] <= '4') return payload[0] - '0';
  return MHI_FAN_NONE;
}
```

- [ ] **Step 4: Run and see it pass** — `pio test -e native -f test_mhi_fan` → `3 Tests 0 Failures 0 Ignored`.

- [ ] **Step 5: The four name macros** — in `src/MHI-AC-Ctrl.h`, after `PAYLOAD_FAN_AUTO`:

```cpp
// Fan levels (fork #21 F6). set/Fan also accepts 1..4 whatever these say;
// default to the numeric texts because the two real units' Home Assistant
// automations already use them (decided 18 Sep 2026).
#ifndef PAYLOAD_FAN_1
#define PAYLOAD_FAN_1 "1"
#endif
#ifndef PAYLOAD_FAN_2
#define PAYLOAD_FAN_2 "2"
#endif
#ifndef PAYLOAD_FAN_3
#define PAYLOAD_FAN_3 "3"
#endif
#ifndef PAYLOAD_FAN_4
#define PAYLOAD_FAN_4 "4"
#endif
```

- [ ] **Step 6: `main.cpp`** — add `#include "mhi_fan.h"` (alphabetically after `mhi_diag_frame.h`; Task 5 later inserts `mhi_error_text.h` between the two). After the `vanes_lr_names` block:

```cpp
// The texts on the Fan topic; set/Fan accepts these and 1..4 (fork #21 F6).
static const MhiFanNames fan_names = {{PAYLOAD_FAN_1, PAYLOAD_FAN_2, PAYLOAD_FAN_3, PAYLOAD_FAN_4}, PAYLOAD_FAN_AUTO};
```

Replace the `set/Fan` branch (lines 136-159):

```cpp
  else if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_FAN)) == 0) {
    static const byte kCoreFan[4] = {0, 1, 2, 6};  // the core's set_fan() values for levels 1..4
    const int fanlevel = mhi_fan_parse(&fan_names, payload_str);
    if (fanlevel == MHI_FAN_AUTO) {
      mhi_ac_ctrl_core.set_fan(7);
      publish_cmd_ok();
    }
    else if (fanlevel >= 1 && fanlevel <= 4) {
      mhi_ac_ctrl_core.set_fan(kCoreFan[fanlevel - 1]);
      publish_cmd_ok();
    }
    else
      publish_cmd_invalidparameter();
  }
```

Replace the `status_fan` case (lines 362-385):

```cpp
        case status_fan: {
          int fanlevel = MHI_FAN_NONE;
          switch (value) {
            case 0: fanlevel = 1; break;
            case 1: fanlevel = 2; break;
            case 2: fanlevel = 3; break;
            case 6: fanlevel = 4; break;
            case 7: fanlevel = MHI_FAN_AUTO; break;
          }
          if (fanlevel != MHI_FAN_NONE)
            output_P(status, TOPIC_FAN, mhi_fan_text(&fan_names, fanlevel));
          else { // invalid values
            itoa(value, strtmp, 10);
            strcat(strtmp, "?");
            output_P(status, TOPIC_FAN, strtmp);
          }
          break;
        }
```

(`TOPIC_FAN` stays unwrapped by `PSTR()` here, matching this case's existing style; harmless either way on the ESP8266.)

- [ ] **Step 7: Build** — `pio run -e d1_mini -e ci-custom-payloads -e ci-all-options` → all succeed; with the defaults `Fan`/`set/Fan` behave byte-for-byte as before.

- [ ] **Step 8: Commit**

```bash
git add lib/mhi_pure/mhi_fan.h lib/mhi_pure/mhi_fan.cpp test/test_mhi_fan/test_mhi_fan.cpp src/MHI-AC-Ctrl.h src/main.cpp
git commit -m "feat: named fan levels via PAYLOAD_FAN_1..4, defaulting to the numeric texts (#21)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KYBzXU6ciProQ7qkoN43Jk"
```

---

### Task 5: `mhi_error_text` — `ErrorText` and `OpData/PROTECTION-TEXT` (F2)

**Files:** create `lib/mhi_pure/mhi_error_text.h`/`.cpp`, `test/test_mhi_error_text/test_mhi_error_text.cpp`; modify `src/MHI-AC-Ctrl.h` (after line 86, after line 135), `src/main.cpp` (includes; `status_errorcode`/`erropdata_errorcode` at lines 434-438; `opdata_protection_no` at lines 529-532).

**Produces:** `size_t mhi_error_text(uint8_t code, char*, size_t)`, `size_t mhi_protection_text(uint8_t no, char*, size_t)`; `TOPIC_ERROR_TEXT`, `TOPIC_PROTECTION_TEXT`.

The protection table (0-17) is `SW-Configuration.md`'s existing table, reproduced verbatim. The error table's content is researched separately (spec §4.3): this task ships a **placeholder** row so the "in-table" path is real and tested; a later step replaces it with the researched table without touching anything else. A code in neither table falls back to the plain code (`"E<n>"`, or the bare number for protection) — deliberate, not a gap.

- [ ] **Step 1: Write the failing tests**

`test/test_mhi_error_text/test_mhi_error_text.cpp`:

```cpp
// Host tests for error/compressor-protection text (fork #21 F2; spec §4.3).
// The error table is a PLACEHOLDER (see mhi_error_text.cpp): these tests only
// prove the mechanism (code 0, one in-table code, one out-of-table code).
// test_error_text_in_table_code must be updated together with the table.

#include <unity.h>

#include "mhi_error_text.h"

void setUp(void) {}
void tearDown(void) {}

static void test_error_text(void) {
  char out[96];
  TEST_ASSERT_TRUE(mhi_error_text(0, out, sizeof(out)) > 0);
  TEST_ASSERT_EQUAL_STRING("OK", out);
  TEST_ASSERT_TRUE(mhi_error_text(1, out, sizeof(out)) > 0);  // the placeholder row
  TEST_ASSERT_EQUAL_STRING("E1: PLACEHOLDER: fill from the research result (orchestrator provides)", out);
  TEST_ASSERT_TRUE(mhi_error_text(250, out, sizeof(out)) > 0);  // out of table
  TEST_ASSERT_EQUAL_STRING("E250", out);
}

static void test_error_text_refuses_a_small_buffer(void) {
  char out[3] = "x";
  TEST_ASSERT_EQUAL_size_t(0, mhi_error_text(0, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
}

static void test_protection_text(void) {
  char out[64];
  TEST_ASSERT_TRUE(mhi_protection_text(0, out, sizeof(out)) > 0);
  TEST_ASSERT_EQUAL_STRING("Normal", out);
  TEST_ASSERT_TRUE(mhi_protection_text(11, out, sizeof(out)) > 0);
  TEST_ASSERT_EQUAL_STRING("Power transistor anomaly (Overheat)", out);
  TEST_ASSERT_TRUE(mhi_protection_text(13, out, sizeof(out)) > 0);  // the table's own placeholder dash
  TEST_ASSERT_EQUAL_STRING("-", out);
  TEST_ASSERT_TRUE(mhi_protection_text(20, out, sizeof(out)) > 0);  // out of table: the plain number
  TEST_ASSERT_EQUAL_STRING("20", out);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_error_text);
  RUN_TEST(test_error_text_refuses_a_small_buffer);
  RUN_TEST(test_protection_text);
  return UNITY_END();
}
```

- [ ] **Step 2: Run and see it fail** — `pio test -e native -f test_mhi_error_text` → `mhi_error_text.h: No such file or directory`.

- [ ] **Step 3: Implement**

`lib/mhi_pure/mhi_error_text.h`:

```cpp
// Error and compressor-protection text (fork #21 F2; spec §4.3). Pure logic,
// no Arduino.
#pragma once
#include <stddef.h>
#include <stdint.h>

// "OK" for 0, "E<n>: <meaning>" for a code the table knows, "E<n>" otherwise.
// Returns the length, 0 (and an empty string) when it does not fit out_len.
size_t mhi_error_text(uint8_t code, char* out, size_t out_len);

// "Normal" for 0, the compressor-protection text of SW-Configuration.md for
// 1..17, the plain number as text otherwise. Same return convention.
size_t mhi_protection_text(uint8_t no, char* out, size_t out_len);
```

`lib/mhi_pure/mhi_error_text.cpp`:

```cpp
#include "mhi_error_text.h"
#include <stdio.h>

// Same reasoning as mhi_discovery.cpp: on the ESP8266 a plain string literal
// lives in RAM, so the format strings and the table texts go to flash here.
#if defined(ARDUINO)
#include <pgmspace.h>
#define FMT(s) PSTR(s)
#define MHI_VSNPRINTF vsnprintf_P
#else
#define FMT(s) s
#define MHI_VSNPRINTF vsnprintf
#endif

struct MhiCodeText {
  uint8_t code;
  const char* text;
};

// PLACEHOLDER -- fill from the research result (orchestrator provides).
// MHI's service documentation for the residential RAC series (spec §4.3);
// upstream never verified the byte equals the printed E-number, so the docs
// must say where the real table came from once it lands. This one row only
// proves the "in-table" path; every other code prints as plain "E<n>".
// Replace this array (and the matching test) with the researched table.
static const MhiCodeText kErrorTable[] = {
  {1, FMT("PLACEHOLDER: fill from the research result (orchestrator provides)")},
};

// SW-Configuration.md, "MQTT operating data PROTECTION-NO topic".
static const MhiCodeText kProtectionTable[] = {
  {1, FMT("Discharge pipe temperature protection control")},
  {2, FMT("Discharge pipe temperature anomaly")},
  {3, FMT("Current safe control of inverter primary current")},
  {4, FMT("High pressure protection control")},
  {5, FMT("High pressure anomaly")},
  {6, FMT("Low pressure protection control")},
  {7, FMT("Low pressure anomaly")},
  {8, FMT("Anti-frost prevention control")},
  {9, FMT("Current cut")},
  {10, FMT("Power transistor protection control")},
  {11, FMT("Power transistor anomaly (Overheat)")},
  {12, FMT("Compression ratio control")},
  {13, FMT("-")},
  {14, FMT("Condensation prevention control")},
  {15, FMT("Current safe control of inverter secondary current")},
  {16, FMT("Stop by compressor rotor lock")},
  {17, FMT("Stop by compressor startup failure")},
};

static const char* find_text(const MhiCodeText* table, size_t count, uint8_t code) {
  for (size_t i = 0; i < count; i++)
    if (table[i].code == code) return table[i].text;
  return NULL;
}

static size_t finish(char* out, size_t out_len, int r) {
  if (r < 0 || (size_t)r >= out_len) {
    out[0] = '\0';
    return 0;
  }
  return (size_t)r;
}

size_t mhi_error_text(uint8_t code, char* out, size_t out_len) {
  if (!out || out_len == 0) return 0;
  if (code == 0) return finish(out, out_len, snprintf(out, out_len, "OK"));
  const char* meaning = find_text(kErrorTable, sizeof(kErrorTable) / sizeof(kErrorTable[0]), code);
  if (meaning) return finish(out, out_len, MHI_VSNPRINTF(out, out_len, FMT("E%u: %s"), (unsigned)code, meaning));
  return finish(out, out_len, snprintf(out, out_len, "E%u", (unsigned)code));
}

size_t mhi_protection_text(uint8_t no, char* out, size_t out_len) {
  if (!out || out_len == 0) return 0;
  if (no == 0) return finish(out, out_len, snprintf(out, out_len, "Normal"));
  const char* meaning = find_text(kProtectionTable, sizeof(kProtectionTable) / sizeof(kProtectionTable[0]), no);
  if (meaning) return finish(out, out_len, MHI_VSNPRINTF(out, out_len, FMT("%s"), meaning));
  return finish(out, out_len, snprintf(out, out_len, "%u", (unsigned)no));
}
```

- [ ] **Step 4: Run and see it pass** — `pio test -e native -f test_mhi_error_text` → `3 Tests 0 Failures 0 Ignored`.

- [ ] **Step 5: The two topics** — after `TOPIC_ERRORCODE`:

```cpp
#ifndef TOPIC_ERROR_TEXT
#define TOPIC_ERROR_TEXT "ErrorText"          // "OK" or "E<n>[: meaning]", next to Errorcode (fork #21 F2)
#endif
```

After `TOPIC_PROTECTION_NO`:

```cpp
#ifndef TOPIC_PROTECTION_TEXT
#define TOPIC_PROTECTION_TEXT "PROTECTION-TEXT"  // OpData/, next to PROTECTION-NO (fork #21 F2)
#endif
```

- [ ] **Step 6: `main.cpp`** — add `#include "mhi_error_text.h"` (after `mhi_diag_frame.h`, before `mhi_fan.h`). Replace the `status_errorcode`/`erropdata_errorcode` case (lines 434-438):

```cpp
        case status_errorcode:
        case erropdata_errorcode: {
          itoa(value, strtmp, 10);
          output_P(status, PSTR(TOPIC_ERRORCODE), strtmp);
          char errtext[64];
          mhi_error_text((uint8_t)value, errtext, sizeof(errtext));
          output_P(status, PSTR(TOPIC_ERROR_TEXT), errtext);
          break;
        }
```

(Reusing `status` routes `ErrorText` to the same prefix as `Errorcode`: base for `status_errorcode`, `ErrOpData/` for `erropdata_errorcode`.) Replace the `opdata_protection_no` case (lines 529-532):

```cpp
        case opdata_protection_no: {
          itoa(value, strtmp, 10);
          output_P(status, PSTR(TOPIC_PROTECTION_NO), strtmp);
          char ptext[64];
          mhi_protection_text((uint8_t)value, ptext, sizeof(ptext));
          output_P(status, PSTR(TOPIC_PROTECTION_TEXT), ptext);
          break;
        }
```

- [ ] **Step 7: Build** — `pio run -e d1_mini -e ci-all-options` → both succeed.

- [ ] **Step 8: Commit**

```bash
git add lib/mhi_pure/mhi_error_text.h lib/mhi_pure/mhi_error_text.cpp test/test_mhi_error_text/test_mhi_error_text.cpp src/MHI-AC-Ctrl.h src/main.cpp
git commit -m "feat: ErrorText and OpData/PROTECTION-TEXT, error table shipped as a placeholder pending research (#21)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KYBzXU6ciProQ7qkoN43Jk"
```

---

### Task 6: `mhi_discovery` — twelve new rows, `has_lr`/`has_outdoor`, both fixture sets (pure)

**Files:** modify `lib/mhi_pure/mhi_discovery.h` (enum, struct), `lib/mhi_pure/mhi_discovery.cpp` (`kComponent`/`kSuffix`, `head()`/`tail()`, the build switch, new `mhi_discovery_row_enabled()`), `test/test_mhi_discovery/test_mhi_discovery.cpp`; create `test/fixtures/discovery_all/` (22 files).

Home Assistant abbreviations verified today against `home-assistant/core`'s `homeassistant/components/mqtt/abbreviations.py` (`dev` branch):

```
"swing_h_mode_cmd_t": "swing_horizontal_mode_command_topic",
"swing_h_mode_stat_t": "swing_horizontal_mode_state_topic",
"swing_h_modes": "swing_horizontal_modes",
```

`via_device` has **no** abbreviation: `DEVICE_ABBREVIATIONS` lists only `cns, cu, ids, name, mf, mdl, mdl_id, hw, sw, sa, sn`; it is spelled out in full inside `dev`. Already-used ones re-confirmed: `dev_cla`→`device_class`, `ent_cat`→`entity_category`, `unit_of_meas`→`unit_of_measurement`, `stat_cla`→`state_class`, `ops`→`options`, `pl_on`/`pl_off`→`payload_on`/`payload_off`.

- [ ] **Step 1: Extend the header**

Replace the `MhiDiscoveryRow` enum:

```cpp
enum MhiDiscoveryRow : uint8_t {
  MHI_DISCOVERY_CLIMATE, MHI_DISCOVERY_VANES, MHI_DISCOVERY_SILENT, MHI_DISCOVERY_PROBLEM, MHI_DISCOVERY_WIRING,
  MHI_DISCOVERY_UPTIME, MHI_DISCOVERY_FREE_HEAP, MHI_DISCOVERY_RSSI, MHI_DISCOVERY_RESET_REASON, MHI_DISCOVERY_WIFI_PHY,
  MHI_DISCOVERY_VANES_LR,       // select   <id_prefix>_vanes_lr (fork #20)
  MHI_DISCOVERY_3DAUTO,         // switch   <id_prefix>_3d_auto (fork #20)
  MHI_DISCOVERY_FRAME_ERRORS,   // sensor   <id_prefix>_frame_errors (fork #21 F1)
  MHI_DISCOVERY_FRAME_TIMEOUTS, // sensor   <id_prefix>_frame_timeouts (fork #21 F1)
  MHI_DISCOVERY_ERROR_TEXT,     // sensor   <id_prefix>_error_text (fork #21 F2)
  // From here on, the outdoor device (fork #19): uniq_id <outdoor_id>_<suffix>.
  MHI_DISCOVERY_OU_OUTDOOR,     // sensor        <outdoor_id>_outdoor_temp
  MHI_DISCOVERY_OU_CT,          // sensor        <outdoor_id>_current
  MHI_DISCOVERY_OU_KWH,         // sensor        <outdoor_id>_energy
  MHI_DISCOVERY_OU_COMP,        // sensor        <outdoor_id>_comp_freq
  MHI_DISCOVERY_OU_DEFROST,     // binary_sensor <outdoor_id>_defrost
  MHI_DISCOVERY_OU_COMP_RUN,    // sensor        <outdoor_id>_comp_run
  MHI_DISCOVERY_OU_PROTECTION,  // sensor        <outdoor_id>_protection
  MHI_DISCOVERY_ROWS
};
```

Append to `struct MhiDiscoveryCtx` (after `const char* wiring_ok;`) — field order is binding, every initializer below follows it:

```cpp
  // Fork #20/#19/#21 (batch C). Only outdoor_entity_prefix may be NULL, like entity_prefix.
  bool has_lr;                        // USE_EXTENDED_FRAME_SIZE
  const char* t_vaneslr;              // TOPIC_VANESLR
  const char* t_3dauto;               // TOPIC_3DAUTO
  const char* vanes_lr[8];            // PAYLOAD_VANESLR_1..7, _SWING
  const char* threedauto_on;          // PAYLOAD_3DAUTO_ON
  const char* threedauto_off;         // PAYLOAD_3DAUTO_OFF
  bool has_outdoor;                   // HA_OUTDOOR_DEVICE
  const char* outdoor_id;             // HA_OUTDOOR_ID; dev.ids and uniq_id prefix of the OU_* rows
  const char* outdoor_name;           // HA_OUTDOOR_NAME; dev.name
  const char* outdoor_entity_prefix;  // HA_OUTDOOR_ENTITY_PREFIX; NULL: none
  const char* op_prefix;              // what MQTT_OP_PREFIX adds to MQTT_PREFIX, "OpData/"
  const char* t_op_outdoor, *t_op_ct, *t_op_kwh, *t_op_comp, *t_op_defrost, *t_op_total_comp_run, *t_op_protection_text;
  const char* defrost_on, *defrost_off;  // PAYLOAD_OP_DEFROST_ON/OFF
  const char* t_frame_errors, *t_frame_timeouts, *t_error_text;
  const char* fan[4];                 // PAYLOAD_FAN_1..4
```

Add, after `mhi_discovery_slug`'s declaration:

```cpp
// Whether a row is part of this build: false for MHI_DISCOVERY_VANES_LR and
// MHI_DISCOVERY_3DAUTO when !has_lr, false for every OU_* row when
// !has_outdoor, true otherwise. Callers must skip a disabled row entirely.
bool mhi_discovery_row_enabled(MhiDiscoveryRow row, const MhiDiscoveryCtx* ctx);
```

- [ ] **Step 2: Extend the tests**

In `test/test_mhi_discovery/test_mhi_discovery.cpp`, append to `kDefault` (after `.wiring_ok = "o.k.",`) — every value matches the repo defaults, so `kDefault`'s existing assertions are unaffected:

```cpp
  .has_lr = false,
  .t_vaneslr = "VanesLR", .t_3dauto = "3Dauto",
  .vanes_lr = {"Left", "LeftCenter", "Center", "CenterRight", "Right", "Wide", "Spot", "Swing"},
  .threedauto_on = "On", .threedauto_off = "Off",
  .has_outdoor = false,
  .outdoor_id = "MHI-AC-Ctrl_outdoor", .outdoor_name = "AC outdoor unit", .outdoor_entity_prefix = NULL,
  .op_prefix = "OpData/",
  .t_op_outdoor = "OUTDOOR", .t_op_ct = "CT", .t_op_kwh = "KWH", .t_op_comp = "COMP", .t_op_defrost = "DEFROST",
  .t_op_total_comp_run = "TOTAL-COMP-RUN", .t_op_protection_text = "PROTECTION-TEXT",
  .defrost_on = "On", .defrost_off = "Off",
  .t_frame_errors = "FrameErrors", .t_frame_timeouts = "FrameTimeouts", .t_error_text = "ErrorText",
  .fan = {"1", "2", "3", "4"},
```

Extend `kDefault.names` (was 10 entries) to 22, appending: `"VanesLR", "3D auto", "Frame errors", "Frame timeouts", "Error text", "Outdoor temperature", "Outdoor current", "Energy", "Compressor frequency", "Defrost", "Compressor run time", "Protection"`.

Give `kUitkijk` the same block but `.has_lr = true` (Uitkijk really has `USE_EXTENDED_FRAME_SIZE`, spec §1), `.has_outdoor = false` (outdoor is Slaapkamer-only, spec §3), `.outdoor_id = "ac_uitkijk_outdoor"`, and extend its `.names` the same 12 entries (reuse the English placeholders above; hass-config's Dutch names are the toolkit's business, not this fixture).

Add a third context — everything on, for the second fixture set and the outdoor-row assertions — derived from `kUitkijk` rather than listed in full:

```cpp
// Slaapkamer: has_lr and the outdoor device both on (spec §3).
static MhiDiscoveryCtx make_slaapkamer() {
  MhiDiscoveryCtx c = kUitkijk;
  c.base = "airco/slaapkamer";
  c.hostname = "airco-slaapkamer";
  c.device_name = "AC Slaapkamer";
  c.version = "batchc-fixture";
  c.climate_id = "AC_Slaapkamer";
  c.id_prefix = "ac_slaapkamer";
  c.entity_prefix = "ac_slaapkamer";
  c.has_outdoor = true;
  c.outdoor_id = "ac_slaapkamer_outdoor";
  c.outdoor_entity_prefix = "ac_buitenunit";
  return c;
}
static const MhiDiscoveryCtx kSlaapkamer = make_slaapkamer();
```

Extend `kFixtureName` (was 10 entries) to 22, appending: `"vanes_lr", "3dauto", "frame_errors", "frame_timeouts", "error_text", "ou_outdoor", "ou_ct", "ou_kwh", "ou_comp", "ou_defrost", "ou_comp_run", "ou_protection"`.

In `every_row_fits()` and `test_reference_fixtures_are_written()`, skip a disabled row: add `if (!mhi_discovery_row_enabled((MhiDiscoveryRow)r, ctx /* or &kDefault */)) continue;` as the first line of each loop body. Add `static void test_every_slaapkamer_row_fits(void) { every_row_fits(&kSlaapkamer, "slaapkamer"); }`. In the shared assertion inside `every_row_fits`, drop the fragment `"\"mdl\":\"MHI-AC-Ctrl\","` from the blanket `strstr` check (the outdoor device's `mdl` is `"outdoor unit"`); keep `"\"mf\":\"Mitsubishi Heavy Industries\","`.

Add these tests, registering all in `main()`:

```cpp
static void test_row_enabled_gates_vaneslr_3dauto_and_outdoor(void) {
  TEST_ASSERT_FALSE(mhi_discovery_row_enabled(MHI_DISCOVERY_VANES_LR, &kDefault));
  TEST_ASSERT_FALSE(mhi_discovery_row_enabled(MHI_DISCOVERY_3DAUTO, &kDefault));
  TEST_ASSERT_FALSE(mhi_discovery_row_enabled(MHI_DISCOVERY_OU_PROTECTION, &kUitkijk));  // has_lr, not has_outdoor
  TEST_ASSERT_TRUE(mhi_discovery_row_enabled(MHI_DISCOVERY_VANES_LR, &kUitkijk));
  TEST_ASSERT_TRUE(mhi_discovery_row_enabled(MHI_DISCOVERY_OU_PROTECTION, &kSlaapkamer));
  for (int r = 0; r < MHI_DISCOVERY_ROWS; r++) {
    if (r == MHI_DISCOVERY_VANES_LR || r == MHI_DISCOVERY_3DAUTO || r >= MHI_DISCOVERY_OU_OUTDOOR) continue;
    TEST_ASSERT_TRUE(mhi_discovery_row_enabled((MhiDiscoveryRow)r, &kDefault));
  }
}

static void test_the_climate_gains_swing_horizontal_with_has_lr(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_CLIMATE, &kDefault, out, sizeof(out)) > 0);
  TEST_ASSERT_NULL(strstr(out, "swing_h"));  // kDefault: has_lr false
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_CLIMATE, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"swing_h_mode_cmd_t\":\"~/set/VanesLR\",\"swing_h_mode_stat_t\":\"~/VanesLR\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"swing_h_modes\":[\"Left\",\"LeftCenter\",\"Center\",\"CenterRight\",\"Right\",\"Wide\",\"Spot\",\"Swing\"],"));
}

static void test_the_vaneslr_select_and_3dauto_switch(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_VANES_LR, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"uniq_id\":\"ac_uitkijk_vanes_lr\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/VanesLR\",\"cmd_t\":\"~/set/VanesLR\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ops\":[\"Left\",\"LeftCenter\",\"Center\",\"CenterRight\",\"Right\",\"Wide\",\"Spot\",\"Swing\"],"));
  TEST_ASSERT_NULL(strstr(out, "ent_cat"));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_3DAUTO, &kUitkijk, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"uniq_id\":\"ac_uitkijk_3d_auto\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/3Dauto\",\"cmd_t\":\"~/set/3Dauto\",\"pl_on\":\"On\",\"pl_off\":\"Off\","));
  TEST_ASSERT_NULL(strstr(out, "ent_cat"));
}

static void test_frame_counters_and_error_text_are_diagnostic(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_FRAME_ERRORS, &kDefault, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/FrameErrors\",\"stat_cla\":\"total_increasing\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_FRAME_TIMEOUTS, &kDefault, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/FrameTimeouts\",\"stat_cla\":\"total_increasing\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_ERROR_TEXT, &kDefault, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/ErrorText\",\"ent_cat\":\"diagnostic\","));
}

static void test_outdoor_rows_carry_their_own_device(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_OUTDOOR, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"uniq_id\":\"ac_slaapkamer_outdoor_outdoor_temp\","));
  TEST_ASSERT_NOT_NULL(strstr(
      out, "\"dev\":{\"ids\":[\"ac_slaapkamer_outdoor\"],\"name\":\"AC outdoor unit\",\"mf\":\"Mitsubishi Heavy Industries\",\"mdl\":\"outdoor unit\",\"via_device\":\"airco-slaapkamer\"}}"));
  TEST_ASSERT_NULL(strstr(out, "\"sw\":"));  // the outdoor device has no firmware version of its own
  TEST_ASSERT_NOT_NULL(strstr(out, "\"default_entity_id\":\"sensor.ac_buitenunit_outdoor_temperature\","));
}

static void test_outdoor_rows_read_the_units_own_opdata_topics(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_OUTDOOR, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/OpData/OUTDOOR\",\"dev_cla\":\"temperature\",\"unit_of_meas\":\"\xc2\xb0" "C\",\"stat_cla\":\"measurement\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_KWH, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/OpData/KWH\",\"dev_cla\":\"energy\",\"unit_of_meas\":\"kWh\",\"stat_cla\":\"total_increasing\","));
  TEST_ASSERT_NULL(strstr(out, "ent_cat"));  // OU_KWH is not diagnostic
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_COMP, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/OpData/COMP\",\"dev_cla\":\"frequency\",\"unit_of_meas\":\"Hz\",\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_DEFROST, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/OpData/DEFROST\",\"pl_on\":\"On\",\"pl_off\":\"Off\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_COMP_RUN, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/OpData/TOTAL-COMP-RUN\",\"dev_cla\":\"duration\",\"unit_of_meas\":\"h\",\"stat_cla\":\"total_increasing\",\"ent_cat\":\"diagnostic\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_PROTECTION, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/OpData/PROTECTION-TEXT\",\"ent_cat\":\"diagnostic\","));
}

static void test_second_fixture_set_is_written(void) {
  for (int r = 0; r < MHI_DISCOVERY_ROWS; r++) {
    if (!mhi_discovery_row_enabled((MhiDiscoveryRow)r, &kSlaapkamer)) continue;
    char path[96], topic[MHI_DISCOVERY_TOPIC_MAX], payload[MHI_DISCOVERY_BUF];
    snprintf(path, sizeof(path), "test/fixtures/discovery_all/%s.txt", kFixtureName[r]);
    TEST_ASSERT_TRUE(mhi_discovery_topic((MhiDiscoveryRow)r, &kSlaapkamer, topic, sizeof(topic)) > 0);
    TEST_ASSERT_TRUE(mhi_discovery_build((MhiDiscoveryRow)r, &kSlaapkamer, payload, sizeof(payload)) > 0);
    FILE* f = fopen(path, "w");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "cannot write test/fixtures/discovery_all/: run pio test from the project root");
    fprintf(f, "%s\n%s\n", topic, payload);
    fclose(f);
  }
}
```

Create the second fixture directory with a placeholder the test overwrites: `mkdir -p test/fixtures/discovery_all && touch test/fixtures/discovery_all/.gitkeep`.

- [ ] **Step 3: Run and see it fail** — `pio test -e native -f test_mhi_discovery` → build error (`mhi_discovery_row_enabled` undeclared; unknown designated initializers).

- [ ] **Step 4: Implement**

`lib/mhi_pure/mhi_discovery.cpp`: extend `kComponent`/`kSuffix` (line 20-23):

```cpp
static const char* const kComponent[MHI_DISCOVERY_ROWS] = {
  "climate", "select", "switch", "binary_sensor", "binary_sensor", "sensor", "sensor", "sensor", "sensor", "sensor",
  "select", "switch", "sensor", "sensor", "sensor",
  "sensor", "sensor", "sensor", "sensor", "binary_sensor", "sensor", "sensor"};
static const char* const kSuffix[MHI_DISCOVERY_ROWS] = {
  "", "vanes", "silent", "problem", "wiring", "uptime", "free_heap", "rssi", "reset_reason", "wifi_phy",
  "vanes_lr", "3d_auto", "frame_errors", "frame_timeouts", "error_text",
  "outdoor_temp", "current", "energy", "comp_freq", "defrost", "comp_run", "protection"};
```

Add above `head()`: `static bool is_outdoor_row(MhiDiscoveryRow row) { return row >= MHI_DISCOVERY_OU_OUTDOOR; }`

Replace `head()` (line 77-97) and `tail()` (line 99-105):

```cpp
static void head(Out* o, const MhiDiscoveryCtx* c, MhiDiscoveryRow row) {
  put(o, FMT("{\"~\":\"%s\","), c->base);
  if (row == MHI_DISCOVERY_CLIMATE) {
    put(o, FMT("\"name\":null,\"uniq_id\":\"%s\","), c->climate_id);
    if (c->entity_prefix) put(o, FMT("\"default_entity_id\":\"climate.%s\","), c->entity_prefix);
    return;
  }
  const char* id_prefix = is_outdoor_row(row) ? c->outdoor_id : c->id_prefix;
  const char* entity_prefix = is_outdoor_row(row) ? c->outdoor_entity_prefix : c->entity_prefix;
  put(o, FMT("\"name\":"));
  put_str(o, c->names[row]);
  put(o, FMT(",\"uniq_id\":\"%s_%s\","), id_prefix, kSuffix[row]);
  if (entity_prefix) {
    char slug[48];
    if (mhi_discovery_slug(c->names[row], slug, sizeof(slug)) == 0) { o->overflow = true; return; }
    put(o, FMT("\"default_entity_id\":\"%s.%s_%s\","), kComponent[row], entity_prefix, slug);
  }
}

static void tail(Out* o, const MhiDiscoveryCtx* c, MhiDiscoveryRow row, bool diagnostic) {
  if (diagnostic) put(o, FMT("\"ent_cat\":\"diagnostic\","));
  put(o, FMT("\"avty_t\":\"~/%s\",\"pl_avail\":\"%s\",\"pl_not_avail\":\"%s\",\"dev\":{\"ids\":[\""),
      c->t_connected, c->connected_on, c->connected_off);
  if (is_outdoor_row(row)) {
    put(o, FMT("%s\"],\"name\":"), c->outdoor_id);
    put_str(o, c->outdoor_name);
    put(o, FMT(",\"mf\":\"Mitsubishi Heavy Industries\",\"mdl\":\"outdoor unit\",\"via_device\":\"%s\"}}"), c->hostname);
  }
  else {
    put(o, FMT("%s\"],\"name\":"), c->hostname);
    put_str(o, c->device_name);
    put(o, FMT(",\"mf\":\"Mitsubishi Heavy Industries\",\"mdl\":\"MHI-AC-Ctrl\",\"sw\":\"%s\"}}"), c->version);
  }
}
```

Add, next to `state_topic()`: `static void op_state_topic(Out* o, const MhiDiscoveryCtx* c, const char* topic) { put(o, FMT("\"stat_t\":\"~/%s%s\","), c->op_prefix, topic); }`

Add, next to `mhi_discovery_modes_valid()`:

```cpp
bool mhi_discovery_row_enabled(MhiDiscoveryRow row, const MhiDiscoveryCtx* c) {
  if (row == MHI_DISCOVERY_VANES_LR || row == MHI_DISCOVERY_3DAUTO) return c->has_lr;
  if (is_outdoor_row(row)) return c->has_outdoor;
  return true;
}
```

In `mhi_discovery_build()`'s `MHI_DISCOVERY_CLIMATE` case, change `const char* fan_modes[5] = {"1", "2", "3", "4", c->fan_auto};` to `const char* fan_modes[5] = {c->fan[0], c->fan[1], c->fan[2], c->fan[3], c->fan_auto};`. After `put_list(&o, "swing_modes", c->vanes, 6);` and before the `min_temp` line, insert:

```cpp
      if (c->has_lr) {
        put(&o, FMT("\"swing_h_mode_cmd_t\":\"~/%s%s\",\"swing_h_mode_stat_t\":\"~/%s\","),
            c->set_prefix, c->t_vaneslr, c->t_vaneslr);
        put_list(&o, "swing_h_modes", c->vanes_lr, 8);
      }
```

Add the twelve new cases, after `MHI_DISCOVERY_WIFI_PHY`'s case, before `MHI_DISCOVERY_ROWS`:

```cpp
    case MHI_DISCOVERY_VANES_LR:
      diagnostic = false;
      put(&o, FMT("\"stat_t\":\"~/%s\",\"cmd_t\":\"~/%s%s\","), c->t_vaneslr, c->set_prefix, c->t_vaneslr);
      put_list(&o, "ops", c->vanes_lr, 8);
      break;
    case MHI_DISCOVERY_3DAUTO:
      diagnostic = false;
      put(&o, FMT("\"stat_t\":\"~/%s\",\"cmd_t\":\"~/%s%s\",\"pl_on\":\"%s\",\"pl_off\":\"%s\","),
          c->t_3dauto, c->set_prefix, c->t_3dauto, c->threedauto_on, c->threedauto_off);
      break;
    case MHI_DISCOVERY_FRAME_ERRORS:
      state_topic(&o, c->t_frame_errors);
      put(&o, FMT("\"stat_cla\":\"total_increasing\","));
      break;
    case MHI_DISCOVERY_FRAME_TIMEOUTS:
      state_topic(&o, c->t_frame_timeouts);
      put(&o, FMT("\"stat_cla\":\"total_increasing\","));
      break;
    case MHI_DISCOVERY_ERROR_TEXT:
      state_topic(&o, c->t_error_text);
      break;
    case MHI_DISCOVERY_OU_OUTDOOR:
      diagnostic = false;
      op_state_topic(&o, c, c->t_op_outdoor);
      put(&o, FMT("\"dev_cla\":\"temperature\",\"unit_of_meas\":\"\xc2\xb0" "C\",\"stat_cla\":\"measurement\","));
      break;
    case MHI_DISCOVERY_OU_CT:
      diagnostic = false;
      op_state_topic(&o, c, c->t_op_ct);
      put(&o, FMT("\"dev_cla\":\"current\",\"unit_of_meas\":\"A\",\"stat_cla\":\"measurement\","));
      break;
    case MHI_DISCOVERY_OU_KWH:
      diagnostic = false;
      op_state_topic(&o, c, c->t_op_kwh);
      put(&o, FMT("\"dev_cla\":\"energy\",\"unit_of_meas\":\"kWh\",\"stat_cla\":\"total_increasing\","));
      break;
    case MHI_DISCOVERY_OU_COMP:
      op_state_topic(&o, c, c->t_op_comp);
      put(&o, FMT("\"dev_cla\":\"frequency\",\"unit_of_meas\":\"Hz\",\"stat_cla\":\"measurement\","));
      break;
    case MHI_DISCOVERY_OU_DEFROST:
      op_state_topic(&o, c, c->t_op_defrost);
      put(&o, FMT("\"pl_on\":\"%s\",\"pl_off\":\"%s\","), c->defrost_on, c->defrost_off);
      break;
    case MHI_DISCOVERY_OU_COMP_RUN:
      op_state_topic(&o, c, c->t_op_total_comp_run);
      put(&o, FMT("\"dev_cla\":\"duration\",\"unit_of_meas\":\"h\",\"stat_cla\":\"total_increasing\","));
      break;
    case MHI_DISCOVERY_OU_PROTECTION:
      op_state_topic(&o, c, c->t_op_protection_text);
      break;
```

Change the final call site to `tail(&o, c, row, diagnostic);`. (`"\xc2\xb0" "C"` is UTF-8 `°C` as a real two-byte escape inside the string literal, adjacent-concatenated with `"C"` before `FMT()` wraps the whole thing — do not type the six characters `\xc2\xb0` literally as text elsewhere.)

- [ ] **Step 5: Run and see it pass** — `pio test -e native -f test_mhi_discovery -v` → every test passes; three `longest row` lines print (default, uitkijk, slaapkamer). **Record all three in the commit message.** If any exceeds 900 B, stop and flag it before continuing.

- [ ] **Step 6: Check the fixtures and commit**

```bash
git diff --stat -- test/fixtures/discovery   # only the 3 new files, nothing else changed
rm -f test/fixtures/discovery_all/.gitkeep
git add lib/mhi_pure/mhi_discovery.h lib/mhi_pure/mhi_discovery.cpp test/test_mhi_discovery/test_mhi_discovery.cpp test/fixtures/discovery/ test/fixtures/discovery_all/
git commit -m "feat: discovery rows for VanesLR, 3D auto, the frame counters, error text and the outdoor device (#20, #19, #21)

Longest rows: default <n> B, uitkijk <n> B, slaapkamer <n> B, of the 1024 B buffer.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KYBzXU6ciProQ7qkoN43Jk"
```

---

### Task 7: Discovery on the unit — options, `src/discovery.cpp`, CI

**Files:** modify `src/support.h` (after line 94), `src/discovery.cpp` (guards at line 11-28, `ctx` at line 33-64, `discovery_loop()` at line 83-106), `tools/discovery_payloads.cpp`, `platformio.ini`, `.github/workflows/ci.yml`.

**Produces:** `HA_OUTDOOR_DEVICE`, `HA_OUTDOOR_ID`, `HA_OUTDOOR_NAME`, `HA_OUTDOOR_ENTITY_PREFIX`, and `HA_NAME_VANES_LR`, `_3DAUTO`, `_FRAME_ERRORS`, `_FRAME_TIMEOUTS`, `_ERROR_TEXT`, `_OU_OUTDOOR`, `_OU_CT`, `_OU_KWH`, `_OU_COMP`, `_OU_DEFROST`, `_OU_COMP_RUN`, `_OU_PROTECTION`.

- [ ] **Step 1: Options in `support.h`** — after `HA_NAME_WIFI_PHY`, before `HA_RESET_REASON_TPL`:

```cpp
#ifndef HA_NAME_VANES_LR
#define HA_NAME_VANES_LR "VanesLR"
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
#ifndef HA_NAME_ERROR_TEXT
#define HA_NAME_ERROR_TEXT "Error text"
#endif
// The outdoor device (fork #19): off by default -- only one indoor unit of a
// multi-split should publish it (spec §3). Needs HA_DISCOVERY.
//#define HA_OUTDOOR_DEVICE true
#ifndef HA_OUTDOOR_ID
#define HA_OUTDOOR_ID HA_ID_PREFIX "_outdoor"
#endif
#ifndef HA_OUTDOOR_NAME
#define HA_OUTDOOR_NAME "AC outdoor unit"
#endif
//#define HA_OUTDOOR_ENTITY_PREFIX "ac_outdoor"       // same idea as HA_ENTITY_PREFIX, for the outdoor entities
#ifndef HA_NAME_OU_OUTDOOR
#define HA_NAME_OU_OUTDOOR "Outdoor temperature"
#endif
#ifndef HA_NAME_OU_CT
#define HA_NAME_OU_CT "Outdoor current"
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
#define HA_NAME_OU_PROTECTION "Protection"
#endif
```

- [ ] **Step 2: `src/discovery.cpp`** — before the `#ifdef HA_DISCOVERY` line (so it fires regardless), after the `POWERON_WHEN_CHANGING_MODE` guard:

```cpp
#if defined(HA_OUTDOOR_DEVICE) && !defined(HA_DISCOVERY)
#error "HA_OUTDOOR_DEVICE needs HA_DISCOVERY"
#endif
```

Next to the existing `static_assert`s: `static_assert(starts_with(MQTT_OP_PREFIX, MQTT_PREFIX), "HA_DISCOVERY needs MQTT_OP_PREFIX to start with MQTT_PREFIX");`

Extend the `ctx` initializer (after `.wiring_ok = MHI_WIRING_OK,`):

```cpp
#ifdef USE_EXTENDED_FRAME_SIZE
  .has_lr = true,
#else
  .has_lr = false,
#endif
  .t_vaneslr = TOPIC_VANESLR, .t_3dauto = TOPIC_3DAUTO,
  .vanes_lr = {PAYLOAD_VANESLR_1, PAYLOAD_VANESLR_2, PAYLOAD_VANESLR_3, PAYLOAD_VANESLR_4, PAYLOAD_VANESLR_5,
               PAYLOAD_VANESLR_6, PAYLOAD_VANESLR_7, PAYLOAD_VANESLR_SWING},
  .threedauto_on = PAYLOAD_3DAUTO_ON, .threedauto_off = PAYLOAD_3DAUTO_OFF,
#ifdef HA_OUTDOOR_DEVICE
  .has_outdoor = true,
#else
  .has_outdoor = false,
#endif
  .outdoor_id = HA_OUTDOOR_ID, .outdoor_name = HA_OUTDOOR_NAME,
#ifdef HA_OUTDOOR_ENTITY_PREFIX
  .outdoor_entity_prefix = HA_OUTDOOR_ENTITY_PREFIX,
#else
  .outdoor_entity_prefix = NULL,
#endif
  .op_prefix = MQTT_OP_PREFIX + (sizeof(MQTT_PREFIX) - 1),
  .t_op_outdoor = TOPIC_OUTDOOR, .t_op_ct = TOPIC_CT, .t_op_kwh = TOPIC_KWH, .t_op_comp = TOPIC_COMP,
  .t_op_defrost = TOPIC_DEFROST, .t_op_total_comp_run = TOPIC_TOTAL_COMP_RUN, .t_op_protection_text = TOPIC_PROTECTION_TEXT,
  .defrost_on = PAYLOAD_OP_DEFROST_ON, .defrost_off = PAYLOAD_OP_DEFROST_OFF,
  .t_frame_errors = TOPIC_FRAME_ERRORS, .t_frame_timeouts = TOPIC_FRAME_TIMEOUTS, .t_error_text = TOPIC_ERROR_TEXT,
  .fan = {PAYLOAD_FAN_1, PAYLOAD_FAN_2, PAYLOAD_FAN_3, PAYLOAD_FAN_4},
```

Extend `.names` to 22 entries, appending: `HA_NAME_VANES_LR, HA_NAME_3DAUTO, HA_NAME_FRAME_ERRORS, HA_NAME_FRAME_TIMEOUTS, HA_NAME_ERROR_TEXT, HA_NAME_OU_OUTDOOR, HA_NAME_OU_CT, HA_NAME_OU_KWH, HA_NAME_OU_COMP, HA_NAME_OU_DEFROST, HA_NAME_OU_COMP_RUN, HA_NAME_OU_PROTECTION`.

In `discovery_loop()`, add the enabled check right after the modes check:

```cpp
  else if (!mhi_discovery_row_enabled(row, &ctx)) {
    // skipped: has_lr or has_outdoor is off in this build
  }
  else if (mhi_discovery_topic(row, &ctx, topic, sizeof(topic)) == 0 ||
```

- [ ] **Step 3: `tools/discovery_payloads.cpp`** — extend the default `MhiDiscoveryCtx` and `.names` exactly as `kDefault` in Task 6 (both false, same default texts). Extend `kNameOption` to 22 entries, appending: `"--name-vanes-lr", "--name-3dauto", "--name-frame-errors", "--name-frame-timeouts", "--name-error-text", "--name-ou-outdoor", "--name-ou-ct", "--name-ou-kwh", "--name-ou-comp", "--name-ou-defrost", "--name-ou-comp-run", "--name-ou-protection"`. In the option parser, before the `else { known = false; ... }` fallback:

```cpp
    else if (strcmp(opt, "--lr") == 0) c.has_lr = strcmp(val, "1") == 0;
    else if (strcmp(opt, "--vanes-lr") == 0) known = split(val, c.vanes_lr, 8);
    else if (strcmp(opt, "--outdoor") == 0) c.has_outdoor = strcmp(val, "1") == 0;
    else if (strcmp(opt, "--outdoor-id") == 0) c.outdoor_id = val;
    else if (strcmp(opt, "--outdoor-name") == 0) c.outdoor_name = val;
    else if (strcmp(opt, "--outdoor-entity-prefix") == 0) c.outdoor_entity_prefix = val;
    else if (strcmp(opt, "--fan-1") == 0) c.fan[0] = val;
    else if (strcmp(opt, "--fan-2") == 0) c.fan[1] = val;
    else if (strcmp(opt, "--fan-3") == 0) c.fan[2] = val;
    else if (strcmp(opt, "--fan-4") == 0) c.fan[3] = val;
```

In the row loop, skip a disabled row: `if (!mhi_discovery_row_enabled((MhiDiscoveryRow)r, &c)) continue;` as the loop's first line.

- [ ] **Step 4: Build the tool and check it by hand**

```bash
g++ -std=gnu++17 -Wall -Wextra -Werror -I lib/mhi_pure lib/mhi_pure/mhi_discovery.cpp tools/discovery_payloads.cpp -o .pio/discovery_payloads
.pio/discovery_payloads --lr 1 --outdoor 1 --outdoor-id ac_test_outdoor | wc -l  # 22
```

- [ ] **Step 5: `platformio.ini`** — after `[env:ci-ha-discovery]`:

```ini
; Home Assistant discovery with the outdoor device and the extended frame:
; exercises has_lr and has_outdoor together.
[env:ci-ha-discovery-outdoor]
extends = ci
build_flags =
	${esp8266.build_flags}
	${ha_payloads.build_flags}
	-D USE_EXTENDED_FRAME_SIZE=true
	-D HA_DISCOVERY=true
	-D HA_OUTDOOR_DEVICE=true
	-D HA_ENTITY_PREFIX=\"mhi_ac_ctrl_ci\"
	-D HA_OUTDOOR_ENTITY_PREFIX=\"mhi_ac_ctrl_ci_outdoor\"
	-D HA_RESET_REASON_TPL=\"{{value}}\"
```

In `[env:ci-all-options]`, add `-D HA_OUTDOOR_DEVICE=true` next to `-D HA_DISCOVERY=true`.

- [ ] **Step 6: `.github/workflows/ci.yml`** — add `ci-ha-discovery-outdoor` to `matrix.environment` (after `ci-ha-discovery`). Replace the "Discovery fixtures match the code" step:

```yaml
      - name: Discovery fixtures match the code
        run: |
          git diff --exit-code -- test/fixtures/discovery test/fixtures/discovery_all
          test -z "$(git status --porcelain -- test/fixtures/discovery test/fixtures/discovery_all)"
```

- [ ] **Step 7: Build** — `pio run -e d1_mini -e ci-ha-discovery -e ci-ha-discovery-outdoor -e ci-all-options` → all succeed.

- [ ] **Step 8: Commit**

```bash
git add src/support.h src/discovery.cpp tools/discovery_payloads.cpp platformio.ini .github/workflows/ci.yml
git commit -m "feat: HA_OUTDOOR_DEVICE and the new HA_NAME_* options, discovery_payloads.cpp options, CI coverage for both on at once (#20, #19, #21)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KYBzXU6ciProQ7qkoN43Jk"
```

---

### Task 8: Docs

**Files:** modify `SW-Configuration.md`, `Version.md`.

- [ ] **Step 1: Topics table** — replace the `VanesLR` row (`VanesLR|r/w|1,2,3,4,5,6,7,"Swing"|Vanes left/right position <sup>4</sup>`) with:

```
VanesLR|r/w|"Left","LeftCenter","Center","CenterRight","Right","Wide","Spot","Swing"|Vanes left/right position, as seen on the unit: 1 leftmost .. 7 spot; writing 1..7 or 8 (="Swing") still works; define `PAYLOAD_VANESLR_1`..`PAYLOAD_VANESLR_7` as `"1"`..`"7"` to keep the numeric texts <sup>4</sup>
```

Update the `Fan` row to add: `; define PAYLOAD_FAN_1..PAYLOAD_FAN_4 for named levels (default "1".."4", unchanged on the wire)`. Add two rows after `3Dauto`:

```
FrameErrors|r|0 ..|invalid-signature/checksum frames since boot; saturates, never wraps <sup>7</sup>
FrameTimeouts|r|0 ..|SCK timeouts since boot; saturates, never wraps <sup>7</sup>
ErrorText|r|"OK" or "E&lt;n&gt;[: meaning]"|next to `Errorcode`; the meaning table is being compiled separately <sup>8</sup>
```

Add footnotes after footnote 6:

```
<sup>7</sup> Published with the periodic telemetry and at every MQTT connect, next to `Uptime`. What is normal for `FrameTimeouts` (boot, OTA, Wi-Fi scans) is unknown until a unit has run with it for a while.

<sup>8</sup> The error table comes from MHI's service documentation for the residential RAC series; upstream never verified the byte equals the printed E-number. Unlisted codes print as plain `E<n>`.
```

- [ ] **Step 2: PROTECTION-TEXT and OpData/ retention** — in the "MQTT operating data PROTECTION-NO topic" section, add after the table: `OpData/PROTECTION-TEXT` publishes the same table's text (`Normal` for 0) whenever `OpData/PROTECTION-NO` does. Add a subsection after it:

```markdown
## OpData/ topics and retention

Every `OpData/` topic, like every other status topic, is published retained (`output_P()`, `src/support.cpp`). A retained topic keeps its last value on the broker across a Home Assistant restart: the entity does not go to "unknown", it shows the value it last had until the AC's next report changes it.
```

- [ ] **Step 3: Home Assistant discovery section** — replace the entity-count sentence with:

```markdown
Per unit: a climate (mode, setpoint, room temperature, fan, vane position as swing mode, `Action`, and with `USE_EXTENDED_FRAME_SIZE` the left/right louvers as swing_horizontal mode), a select for the vane position, a switch for `Silent`, two problem binary sensors, seven diagnostic sensors (`Uptime`, `FreeHeap`, `RSSI`, `ResetReason`, `WIFI_PHY`, `FrameErrors`, `FrameTimeouts`) and a diagnostic `ErrorText` sensor, all under one device. With `USE_EXTENDED_FRAME_SIZE`, also a select for the left/right louvers and a switch for `3Dauto`. With `HA_OUTDOOR_DEVICE`, seven more entities for the shared outdoor unit's own device (temperature, current, energy, compressor frequency, defrost, compressor run time, protection text), linked with `via_device`, reading the publishing unit's own `OpData/` topics -- with two indoor units sharing one outdoor unit, only one of them should have `HA_OUTDOOR_DEVICE` on. 13 entities with neither option, up to 22 with both. Availability comes from `connected`.
```

After `HA_RESET_REASON_TPL` in the code block, add:

```cpp
//#define HA_OUTDOOR_DEVICE true            // also publish the outdoor unit's device; on for at most one of the units sharing it
#define HA_OUTDOOR_ID HA_ID_PREFIX "_outdoor"
#define HA_OUTDOOR_NAME "AC outdoor unit"
//#define HA_OUTDOOR_ENTITY_PREFIX "ac_outdoor"
#define HA_NAME_VANES_LR "VanesLR"  // entity names; likewise HA_NAME_3DAUTO, _FRAME_ERRORS, _FRAME_TIMEOUTS, _ERROR_TEXT, _OU_OUTDOOR, _OU_CT, _OU_KWH, _OU_COMP, _OU_DEFROST, _OU_COMP_RUN, _OU_PROTECTION
```

- [ ] **Step 4: `Version.md`** — add a bullet under "Adaptions since version 2.8":

```markdown
- left/right louvers, 3D auto, the outdoor device, frame counters, fan names and error/protection text (#20, #19, #21): `set_vanesLR()`/`set_3Dauto()` no longer step on each other's set flag; `VanesLR` publishes named positions (`Left`..`Spot`, `Swing`); with `HA_OUTDOOR_DEVICE` the shared outdoor unit gets its own Home Assistant device, linked by `via_device`, reading the publishing unit's own `OpData/` topics; new `FrameErrors`/`FrameTimeouts` counters; `Fan` accepts named levels via `PAYLOAD_FAN_1`..`_4` (default `"1"`..`"4"`, unchanged on the wire); new `ErrorText` and `OpData/PROTECTION-TEXT`, the error table shipped as a placeholder pending research
```

- [ ] **Step 5: Commit**

```bash
git add SW-Configuration.md Version.md
git commit -m "docs: batch C topics, options and OpData/ retention (#20, #19, #21)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01KYBzXU6ciProQ7qkoN43Jk"
```

---

### Task 9: Final verification

No code changes; no commit unless something needs fixing (fix forward following Tasks 1-8's own conventions).

- [ ] **Step 1** — `pio test -e native` → every suite passes, `0 Failures`; `git status --porcelain -- test/fixtures/discovery test/fixtures/discovery_all` is empty.
- [ ] **Step 2** — `pio run -e d1_mini -e ci-extended-frame -e ci-enhanced-resolution -e ci-ds18x20-sensor -e ci-ds18x20-troom -e ci-poweron-when-changing-mode -e ci-continue-without-mqtt -e ci-custom-payloads -e ci-ha-discovery -e ci-ha-discovery-outdoor -e ci-config-defaults -e ci-all-options` → every environment builds. Note the `d1_mini`/`ci-all-options` flash sizes against the 349104 B baseline; say plainly if either grew disproportionately.
- [ ] **Step 3** — confirm the ten pre-existing discovery fixtures are unchanged: `git diff 5b0ed4e -- test/fixtures/discovery/climate.txt test/fixtures/discovery/vanes.txt test/fixtures/discovery/silent.txt test/fixtures/discovery/problem.txt test/fixtures/discovery/wiring.txt test/fixtures/discovery/uptime.txt test/fixtures/discovery/free_heap.txt test/fixtures/discovery/rssi.txt test/fixtures/discovery/reset_reason.txt test/fixtures/discovery/wifi_phy.txt` must be empty.
- [ ] **Step 4** — report the flash sizes and the three discovery-buffer high-water marks (Task 6's commit) to the orchestrator.

The toolkit outside the repo (`~/.config/hass/tools/mhi/`) and flashing Uitkijk/Slaapkamer are **not** part of this plan; the orchestrator carries out spec §6-7 separately, after CI is green and Lucas has given his go per unit.

---

## Risks and things to watch on the hardware

- **The one open question from spec §2.1**: not known whether the AC itself drops 3D auto when a position is chosen, or what a position command does while 3D auto is on. Observe both after flashing and write the answer into `SW-Configuration.md`.
- Decoupling the commands is an intentional behaviour change: today a position command silently turns 3D auto off and vice versa; after this batch, neither does. Confirm on the bench (spec §7: "3D auto on leaves the swing on") before telling hass-config the firmware has landed.
- `FrameTimeouts`' normal rate is unknown until the soak runs; do not treat a non-zero count as a fault before it sets a baseline.
- The two real units keep `Fan`'s numeric texts (F6, decided 18 Sep): do not change the toolkit's fan names in the same step as this flash without also updating hass-config's automations, since Home Assistant rejects `climate.set_fan_mode` outside `fan_modes`.
- `HA_OUTDOOR_DEVICE` must be on for at most one of the two units once both share the outdoor unit; two publishers would collide on the same `uniq_id`s.
- Discovery buffer headroom: Task 6 records the longest row for all three contexts; if the real units' Dutch entity names run longer than these fixtures, re-check headroom against the 1024 B buffer before flashing.
- Flash budget: Task 9 reports the new sizes against the 349104 B baseline; investigate a disproportionate jump before shipping.
