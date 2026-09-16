# Phase 4 batch A: protocol discovery tooling — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make any IR-remote button a five-minute experiment on the unit: publish the bytes of the AC's status frame that change (`diag/frame`), the value bytes of operating data the firmware does not decode (`diag/opdata`), and let Home Assistant ask the AC once for any operating-data code (`set/OpDataRequest`).

**Architecture:** The SPI core (`MHI-AC-Ctrl-core.cpp`) gains one optional virtual on its callback interface, `cbiRawFunction(status, bytes, len)`, and calls it twice: with the whole MOSI frame after checksum validation and with `DB9..DB12` of an unknown operating-data answer; plus a one-shot request slot that takes the place of the next code in the normal request cycle. Everything else, the masked frame diff, the hex text, the request parsing, is pure logic in `lib/mhi_pure/mhi_diag_frame` with host tests, and `main.cpp` owns the MQTT topics, the 1 s rate limit and the `Diag` switch.

**Tech Stack:** PlatformIO, ESP8266 Arduino core 3.1.2, PubSubClient3 3.3.1, Unity host tests (`pio test -e native`), GitHub Actions CI (host tests + ten build environments + flash-size guard).

**Spec:** `docs/superpowers/specs/2026-09-16-phase-4-batches-design.md` §3 (batch A), with §5.5 of `docs/superpowers/specs/2026-07-25-mhi-ac-ctrl-improvement-design.md` as background. Fork issue #4 tracks the checklist.

## Global Constraints

- `lib/mhi_pure` compiles without Arduino: plain `stdint.h`/`stddef.h`/`stdio.h`/`string.h` only; every function there has a Unity test in `test/test_<module>/test_<module>.cpp` that failed before the implementation.
- `-Werror` applies to `src/` (`build_src_flags`), `-Wall -Wextra -Werror` to the native tests. No new warnings anywhere.
- Flash budget 460000 bytes, asserted by `scripts/check_flash_size.py`; d1_mini was 342336 bytes at `5f1d94e`.
- Every new topic and payload text is an `#ifndef`-guarded define in `src/MHI-AC-Ctrl.h`; every new option an `#ifndef`-guarded define in `src/support.h`, overridable from the gitignored `src/config_defaults.h`.
- Payload casing follows the repo's existing switches (`set/PassiveMode` uses `On`/`Off`): `set/Diag` and the `Diag` topic use `On`/`Off`.
- Nothing is flashed without Lucas's explicit go for that unit; Slaapkamer before Uitkijk; a rollback image of the running version must exist (`flash-unit.sh` refuses otherwise).
- Commits: conventional message referencing `#4`, ending with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`. Push over HTTPS: `git -c credential.helper='!gh auth git-credential' push https://github.com/lvschouwen/MHI-AC-Ctrl.git <branch>`.
- Branch `feat/4a-diag-tooling` off `master`; merge `--ff-only` after CI is green; delete the branch.

---

## File map

| File | Responsibility |
|---|---|
| `lib/mhi_pure/mhi_diag_frame.h` / `.cpp` (new) | the compare mask, the frame diff text, the opdata hex text, the `set/OpDataRequest` parser |
| `test/test_mhi_diag_frame/test_mhi_diag_frame.cpp` (new) | Unity tests for all of the above |
| `src/MHI-AC-Ctrl-core.h` | `raw_frame`/`raw_opdata` status ids, `cbiRawFunction` on the callback interface, `request_OpData()` and its pending state |
| `src/MHI-AC-Ctrl-core.cpp` | the two raw calls, the one-shot request slot |
| `src/MHI-AC-Ctrl.h` | topics `diag/frame`, `diag/opdata`, `Diag`, `set/Diag`, `set/OpDataRequest`; payloads `On`/`Off` |
| `src/support.h` | `DIAG_DEFAULT` |
| `src/main.cpp` | `cbiRawFunction` implementation, the two publishes, the rate limit, the `Diag` state, the two commands |
| `SW-Configuration.md`, `Troubleshooting.md`, `Version.md` | docs |
| `~/.config/hass/tools/mhi/remote-test.sh` (new), `post-flash-check.sh`, `health-check.sh`, `README.md` | toolkit (outside the repo) |

---

### Task 1: Branch, and the `set/OpDataRequest` parser (pure)

**Files:**
- Create: `lib/mhi_pure/mhi_diag_frame.h`, `lib/mhi_pure/mhi_diag_frame.cpp`, `test/test_mhi_diag_frame/test_mhi_diag_frame.cpp`

**Interfaces:**
- Produces: `bool mhi_opdata_request_parse(const char* payload, uint8_t* prefix, uint8_t* code);` — `"c021"` → `0xc0`, `0x21`; only prefixes `0x40`/`0xc0`; exactly four hex digits, either case; `false` leaves the outputs untouched.

- [ ] **Step 1: Create the branch**

```bash
cd /home/lucas/coding/MHI-AC-Ctrl && git checkout -b feat/4a-diag-tooling master
```

- [ ] **Step 2: Write the failing tests**

`test/test_mhi_diag_frame/test_mhi_diag_frame.cpp`:

```cpp
// Host tests for the protocol discovery tooling (fork issue #4, batch A;
// spec docs/superpowers/specs/2026-09-16-phase-4-batches-design.md §3).
//
// main.cpp publishes what these functions produce: the bytes of the AC's
// status frame that changed, the value bytes of unknown operating data, and
// a parsed set/OpDataRequest. The compare mask is what keeps diag/frame quiet
// while the operating-data bytes cycle.

#include <string.h>
#include <unity.h>

#include "mhi_diag_frame.h"
#include "mhi_frame.h"

void setUp(void) {}
void tearDown(void) {}

// --- set/OpDataRequest parsing ---------------------------------------------

static void test_request_parses_an_indoor_code(void) {
  uint8_t prefix = 0, code = 0;
  TEST_ASSERT_TRUE(mhi_opdata_request_parse("c021", &prefix, &code));
  TEST_ASSERT_EQUAL_HEX8(0xc0, prefix);
  TEST_ASSERT_EQUAL_HEX8(0x21, code);
}

static void test_request_parses_an_outdoor_code_in_upper_case(void) {
  uint8_t prefix = 0, code = 0;
  TEST_ASSERT_TRUE(mhi_opdata_request_parse("40DD", &prefix, &code));
  TEST_ASSERT_EQUAL_HEX8(0x40, prefix);
  TEST_ASSERT_EQUAL_HEX8(0xdd, code);
}

static void test_request_rejects_a_prefix_the_ac_never_sees(void) {
  // Only the two prefixes of the built-in request table are sent.
  uint8_t prefix = 1, code = 2;
  TEST_ASSERT_FALSE(mhi_opdata_request_parse("8021", &prefix, &code));
  TEST_ASSERT_FALSE(mhi_opdata_request_parse("0021", &prefix, &code));
  TEST_ASSERT_EQUAL_HEX8(1, prefix);  // untouched
  TEST_ASSERT_EQUAL_HEX8(2, code);
}

static void test_request_rejects_anything_but_four_hex_digits(void) {
  uint8_t prefix = 1, code = 2;
  TEST_ASSERT_FALSE(mhi_opdata_request_parse("c02", &prefix, &code));
  TEST_ASSERT_FALSE(mhi_opdata_request_parse("c0211", &prefix, &code));
  TEST_ASSERT_FALSE(mhi_opdata_request_parse("c0zz", &prefix, &code));
  TEST_ASSERT_FALSE(mhi_opdata_request_parse("", &prefix, &code));
  TEST_ASSERT_FALSE(mhi_opdata_request_parse(NULL, &prefix, &code));
  TEST_ASSERT_EQUAL_HEX8(1, prefix);
  TEST_ASSERT_EQUAL_HEX8(2, code);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_request_parses_an_indoor_code);
  RUN_TEST(test_request_parses_an_outdoor_code_in_upper_case);
  RUN_TEST(test_request_rejects_a_prefix_the_ac_never_sees);
  RUN_TEST(test_request_rejects_anything_but_four_hex_digits);
  return UNITY_END();
}
```

`lib/mhi_pure/mhi_diag_frame.h` (the whole header now; later tasks fill in the implementations):

```cpp
// Protocol discovery tooling: turning a raw MOSI frame and unknown operating
// data into text a human can read on an MQTT topic (fork issue #4, batch A,
// spec docs/superpowers/specs/2026-09-16-phase-4-batches-design.md §3).
//
// Pure logic, no Arduino. main.cpp owns the MQTT client, the Diag switch and
// the rate limit; this file only compares, formats and parses.

#pragma once

#include <stddef.h>
#include <stdint.h>

// A standard frame is 20 bytes, the WF-RAC extended frame 33.
#define MHI_DIAG_FRAME_MAX 33
// At most this many changed bytes are named; "+" says there were more.
#define MHI_DIAG_CHANGES_MAX 6
// Room for "DB26 ff>ff " x 6, "+ ", "|" and 33 bytes as " xx", plus the NUL.
#define MHI_DIAG_TEXT_MAX 200

// The compare mask: which bits of each MOSI byte take part. The header, the
// operating-data bytes DB9-DB12, the checksum bytes and the frame toggle in
// DB14 bit 2 are ignored; DB6 keeps only its low six bits, because bits 0xc0
// echo the operating-data request prefix (0x40 / 0xc0). A byte that proves
// noisy on hardware is masked here, with a test.
void mhi_diag_mask_default(uint8_t* mask, size_t len);

struct MhiDiagFrame {
  uint8_t last[MHI_DIAG_FRAME_MAX];  // the last published frame
  bool have_last;                    // false until the first publish, and after a reconnect
};

// Compares frame (len bytes, 20 or 33) with the last published frame under
// mask. When a masked bit differs, or nothing was published yet, writes
// "DB5 00>10 DB13 05>07 | 6c 80 04 ..." ("first | ..." the first time) into
// out, remembers frame as published and returns the text length. Returns 0
// and leaves out and the state alone when nothing changed. out_len must be
// at least MHI_DIAG_TEXT_MAX.
size_t mhi_diag_frame_changes(MhiDiagFrame* d, const uint8_t* frame, size_t len, const uint8_t* mask, char* out, size_t out_len);

// "dd 80 01 00": the four operating-data bytes DB9..DB12. Returns the length,
// 0 when out is too small.
size_t mhi_diag_opdata_text(const uint8_t* db9, char* out, size_t out_len);

// Parses a set/OpDataRequest payload such as "c021" into the DB6 request
// prefix (only 0x40 and 0xc0 are accepted, the two the request table uses)
// and the DB9 code. Exactly four hex digits, either case. False leaves the
// outputs untouched.
bool mhi_opdata_request_parse(const char* payload, uint8_t* prefix, uint8_t* code);
```

`lib/mhi_pure/mhi_diag_frame.cpp` (stub so the tests link and fail on assertions):

```cpp
#include "mhi_diag_frame.h"

#include <stdio.h>
#include <string.h>

#include "mhi_frame.h"

void mhi_diag_mask_default(uint8_t* mask, size_t len) {
  (void)mask;
  (void)len;
}

size_t mhi_diag_frame_changes(MhiDiagFrame* d, const uint8_t* frame, size_t len, const uint8_t* mask, char* out, size_t out_len) {
  (void)d; (void)frame; (void)len; (void)mask; (void)out; (void)out_len;
  return 0;
}

size_t mhi_diag_opdata_text(const uint8_t* db9, char* out, size_t out_len) {
  (void)db9; (void)out; (void)out_len;
  return 0;
}

bool mhi_opdata_request_parse(const char* payload, uint8_t* prefix, uint8_t* code) {
  (void)payload; (void)prefix; (void)code;
  return false;
}
```

- [ ] **Step 3: Run the tests, see them fail on assertions**

```bash
pio test -e native -f test_mhi_diag_frame 2>&1 | grep -E 'test_mhi_diag_frame.cpp|PASSED|FAILED|ERRORED|error'
```
Expected: the two `parses` tests FAIL ("Expected TRUE Was FALSE"), the two `rejects` tests PASS (the stub returns false), no compile errors.

- [ ] **Step 4: Implement the parser**

Replace the `mhi_opdata_request_parse` stub in `lib/mhi_pure/mhi_diag_frame.cpp`:

```cpp
static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool mhi_opdata_request_parse(const char* payload, uint8_t* prefix, uint8_t* code) {
  if (payload == NULL || strlen(payload) != 4) return false;
  int v[4];
  for (int i = 0; i < 4; i++) {
    v[i] = hex_nibble(payload[i]);
    if (v[i] < 0) return false;
  }
  const uint8_t p = (uint8_t)(v[0] << 4 | v[1]);
  if (p != 0x40 && p != 0xc0) return false;  // the request table's two prefixes
  *prefix = p;
  *code = (uint8_t)(v[2] << 4 | v[3]);
  return true;
}
```

- [ ] **Step 5: Run the tests, all four pass**

```bash
pio test -e native -f test_mhi_diag_frame 2>&1 | grep -E 'PASSED|FAILED|ERRORED'
```
Expected: `native  test_mhi_diag_frame  PASSED`.

- [ ] **Step 6: Commit**

```bash
git add lib/mhi_pure/mhi_diag_frame.h lib/mhi_pure/mhi_diag_frame.cpp test/test_mhi_diag_frame/test_mhi_diag_frame.cpp
git commit -m "feat: parse set/OpDataRequest payloads for the one-shot operating-data probe (#4)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: The opdata hex text (pure)

**Files:**
- Modify: `lib/mhi_pure/mhi_diag_frame.cpp`, `test/test_mhi_diag_frame/test_mhi_diag_frame.cpp`

**Interfaces:**
- Produces: `size_t mhi_diag_opdata_text(const uint8_t* db9, char* out, size_t out_len);` — `{0xdd,0x80,0x01,0x00}` → `"dd 80 01 00"` (11 characters).

- [ ] **Step 1: Add the failing tests** (before `int main`, and the two `RUN_TEST` lines inside `main`)

```cpp
// --- unknown operating data with its value bytes ---------------------------

static void test_opdata_text_shows_the_four_bytes_in_hex(void) {
  const uint8_t db9[4] = {0xdd, 0x80, 0x01, 0x00};  // Silent, as seen on 16 Sep
  char out[16];
  TEST_ASSERT_EQUAL_size_t(11, mhi_diag_opdata_text(db9, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("dd 80 01 00", out);
}

static void test_opdata_text_refuses_a_buffer_that_cannot_hold_it(void) {
  const uint8_t db9[4] = {0x21, 0x10, 0x00, 0x00};
  char out[11];  // one short of the NUL
  TEST_ASSERT_EQUAL_size_t(0, mhi_diag_opdata_text(db9, out, sizeof(out)));
}
```

```cpp
  RUN_TEST(test_opdata_text_shows_the_four_bytes_in_hex);
  RUN_TEST(test_opdata_text_refuses_a_buffer_that_cannot_hold_it);
```

- [ ] **Step 2: Run, see the first new test fail**

```bash
pio test -e native -f test_mhi_diag_frame 2>&1 | grep -E 'test_mhi_diag_frame.cpp|PASSED|FAILED'
```
Expected: `test_opdata_text_shows_the_four_bytes_in_hex ... Expected 11 Was 0 [FAILED]`.

- [ ] **Step 3: Implement**

Replace the `mhi_diag_opdata_text` stub:

```cpp
size_t mhi_diag_opdata_text(const uint8_t* db9, char* out, size_t out_len) {
  if (out_len < 12) return 0;  // "xx xx xx xx" and the NUL
  const int n = snprintf(out, out_len, "%02x %02x %02x %02x", db9[0], db9[1], db9[2], db9[3]);
  return n > 0 ? (size_t)n : 0;
}
```

- [ ] **Step 4: Run, all pass**

```bash
pio test -e native -f test_mhi_diag_frame 2>&1 | grep -E 'PASSED|FAILED|ERRORED'
```
Expected: PASSED.

- [ ] **Step 5: Commit**

```bash
git add lib/mhi_pure/mhi_diag_frame.cpp test/test_mhi_diag_frame/test_mhi_diag_frame.cpp
git commit -m "feat: format the value bytes of unknown operating data for diag/opdata (#4)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: The compare mask and the frame diff text (pure)

**Files:**
- Modify: `lib/mhi_pure/mhi_diag_frame.cpp`, `test/test_mhi_diag_frame/test_mhi_diag_frame.cpp`

**Interfaces:**
- Produces: `void mhi_diag_mask_default(uint8_t* mask, size_t len);` and `size_t mhi_diag_frame_changes(MhiDiagFrame*, const uint8_t* frame, size_t len, const uint8_t* mask, char* out, size_t out_len);` exactly as declared in Task 1's header. Byte names in the text follow `mhi_frame.h`: `SB0`-`SB2`, `DB0`-`DB14`, `CBH`, `CBL`, `DB15`-`DB26`, `CB2`.

- [ ] **Step 1: Add the failing tests**

```cpp
// --- the compare mask ------------------------------------------------------

static void test_default_mask_ignores_what_changes_on_its_own(void) {
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 20);
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[SB0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[SB1]);
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[SB2]);
  TEST_ASSERT_EQUAL_HEX8(0x3f, mask[DB6]);   // request-prefix bits 0xc0 cycle
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[DB9]);
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[DB10]);
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[DB11]);
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[DB12]);
  TEST_ASSERT_EQUAL_HEX8(0xfb, mask[DB14]);  // bit 2 toggles every frame
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[CBH]);
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[CBL]);
}

static void test_default_mask_compares_the_status_bytes_in_full(void) {
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 33);
  TEST_ASSERT_EQUAL_HEX8(0xff, mask[DB0]);
  TEST_ASSERT_EQUAL_HEX8(0xff, mask[DB5]);   // undocumented per SPI.md, the point of the tool
  TEST_ASSERT_EQUAL_HEX8(0xff, mask[DB13]);
  TEST_ASSERT_EQUAL_HEX8(0xff, mask[DB15]);
  TEST_ASSERT_EQUAL_HEX8(0xff, mask[DB26]);
  TEST_ASSERT_EQUAL_HEX8(0x00, mask[CBL2]);
}

// --- the frame diff ----------------------------------------------------------

// A plausible 20-byte status frame: header 6c 80 04, then DB0..DB14, checksum.
static const uint8_t kFrame[20] = {0x6c, 0x80, 0x04, 0x08, 0x3b, 0x2e, 0x4c, 0x22, 0x00, 0x00, 0x00, 0x00,
                                   0x02, 0x10, 0x3b, 0x00, 0x05, 0x00, 0x02, 0x1d};

static void test_the_first_frame_is_published_whole(void) {
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 20);
  char out[MHI_DIAG_TEXT_MAX];
  const size_t n = mhi_diag_frame_changes(&d, kFrame, 20, mask, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("first | 6c 80 04 08 3b 2e 4c 22 00 00 00 00 02 10 3b 00 05 00 02 1d", out);
  TEST_ASSERT_EQUAL_size_t(strlen(out), n);
  TEST_ASSERT_TRUE(d.have_last);
}

static void test_an_unchanged_frame_publishes_nothing(void) {
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 20);
  char out[MHI_DIAG_TEXT_MAX] = "untouched";
  mhi_diag_frame_changes(&d, kFrame, 20, mask, out, sizeof(out));
  TEST_ASSERT_EQUAL_size_t(0, mhi_diag_frame_changes(&d, kFrame, 20, mask, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("first | 6c 80 04 08 3b 2e 4c 22 00 00 00 00 02 10 3b 00 05 00 02 1d", out);  // left alone
}

static void test_a_changed_status_byte_is_named_with_old_and_new(void) {
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 20);
  char out[MHI_DIAG_TEXT_MAX];
  mhi_diag_frame_changes(&d, kFrame, 20, mask, out, sizeof(out));
  uint8_t next[20];
  memcpy(next, kFrame, 20);
  next[DB5] = 0x10;  // a HI/ECO press flipping an undocumented bit
  TEST_ASSERT_GREATER_THAN_size_t(0, mhi_diag_frame_changes(&d, next, 20, mask, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("DB5 00>10 | 6c 80 04 08 3b 2e 4c 22 10 00 00 00 02 10 3b 00 05 00 02 1d", out);
}

static void test_the_compare_is_against_the_last_published_frame(void) {
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 20);
  char out[MHI_DIAG_TEXT_MAX];
  mhi_diag_frame_changes(&d, kFrame, 20, mask, out, sizeof(out));
  uint8_t next[20];
  memcpy(next, kFrame, 20);
  next[DB5] = 0x10;
  mhi_diag_frame_changes(&d, next, 20, mask, out, sizeof(out));
  next[DB5] = 0x11;
  mhi_diag_frame_changes(&d, next, 20, mask, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING_LEN("DB5 10>11 |", out, 11);
}

static void test_masked_bytes_never_trigger_a_publish(void) {
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 20);
  char out[MHI_DIAG_TEXT_MAX];
  mhi_diag_frame_changes(&d, kFrame, 20, mask, out, sizeof(out));
  uint8_t next[20];
  memcpy(next, kFrame, 20);
  next[DB9] = 0x80;   // the operating-data cycle
  next[DB10] = 0x10;
  next[DB11] = 0x33;
  next[DB12] = 0x01;
  next[DB6] ^= 0xc0;  // the echoed request prefix
  next[DB14] ^= 0x04; // the frame toggle
  next[CBH] = 0xaa;   // checksum follows the rest
  next[CBL] = 0xbb;
  TEST_ASSERT_EQUAL_size_t(0, mhi_diag_frame_changes(&d, next, 20, mask, out, sizeof(out)));
}

static void test_the_low_bits_of_db6_and_the_rest_of_db14_still_count(void) {
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 20);
  char out[MHI_DIAG_TEXT_MAX];
  mhi_diag_frame_changes(&d, kFrame, 20, mask, out, sizeof(out));
  uint8_t next[20];
  memcpy(next, kFrame, 20);
  next[DB6] |= 0x01;
  next[DB14] |= 0x08;
  TEST_ASSERT_GREATER_THAN_size_t(0, mhi_diag_frame_changes(&d, next, 20, mask, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING_LEN("DB6 00>01 DB14 00>08 |", out, 22);
}

static void test_more_than_six_changes_are_summarised_with_a_plus(void) {
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 20);
  char out[MHI_DIAG_TEXT_MAX];
  mhi_diag_frame_changes(&d, kFrame, 20, mask, out, sizeof(out));
  uint8_t next[20];
  memcpy(next, kFrame, 20);
  for (size_t i = DB0; i <= DB5; i++) next[i] ^= 0x01;  // six
  next[DB7] ^= 0x01;                                    // the seventh
  TEST_ASSERT_GREATER_THAN_size_t(0, mhi_diag_frame_changes(&d, next, 20, mask, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING_LEN("DB0 08>09 DB1 3b>3a DB2 2e>2f DB3 4c>4d DB4 22>23 DB5 00>01 + |", out, 62);
  TEST_ASSERT_LESS_THAN_size_t(MHI_DIAG_TEXT_MAX, strlen(out));
}

static void test_an_extended_frame_names_its_extra_bytes(void) {
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 33);
  char out[MHI_DIAG_TEXT_MAX];
  uint8_t frame[33] = {0};
  memcpy(frame, kFrame, 20);
  mhi_diag_frame_changes(&d, frame, 33, mask, out, sizeof(out));
  frame[DB15] = 0x04;  // 3D auto
  TEST_ASSERT_GREATER_THAN_size_t(0, mhi_diag_frame_changes(&d, frame, 33, mask, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING_LEN("DB15 00>04 |", out, 12);
  TEST_ASSERT_LESS_THAN_size_t(MHI_DIAG_TEXT_MAX, strlen(out));
}

static void test_a_frame_longer_than_the_maximum_is_refused(void) {
  MhiDiagFrame d = {{0}, false};
  uint8_t mask[MHI_DIAG_FRAME_MAX];
  mhi_diag_mask_default(mask, 33);
  uint8_t frame[40] = {0};
  char out[MHI_DIAG_TEXT_MAX];
  TEST_ASSERT_EQUAL_size_t(0, mhi_diag_frame_changes(&d, frame, 40, mask, out, sizeof(out)));
  TEST_ASSERT_FALSE(d.have_last);
}
```

And in `main`:

```cpp
  RUN_TEST(test_default_mask_ignores_what_changes_on_its_own);
  RUN_TEST(test_default_mask_compares_the_status_bytes_in_full);
  RUN_TEST(test_the_first_frame_is_published_whole);
  RUN_TEST(test_an_unchanged_frame_publishes_nothing);
  RUN_TEST(test_a_changed_status_byte_is_named_with_old_and_new);
  RUN_TEST(test_the_compare_is_against_the_last_published_frame);
  RUN_TEST(test_masked_bytes_never_trigger_a_publish);
  RUN_TEST(test_the_low_bits_of_db6_and_the_rest_of_db14_still_count);
  RUN_TEST(test_more_than_six_changes_are_summarised_with_a_plus);
  RUN_TEST(test_an_extended_frame_names_its_extra_bytes);
  RUN_TEST(test_a_frame_longer_than_the_maximum_is_refused);
```

- [ ] **Step 2: Run, see the mask and diff tests fail**

```bash
pio test -e native -f test_mhi_diag_frame 2>&1 | grep -E 'FAILED|PASSED' | head -20
```
Expected: the mask tests fail (`Expected 0x00 Was ...` on uninitialised memory or similar), `first_frame`, `changed_status_byte`, `last_published`, `low_bits`, `more_than_six`, `extended` FAIL; `unchanged`, `masked_bytes`, `longer_than_maximum` may PASS against the stub, which is fine: they pin behaviour the implementation must keep.

- [ ] **Step 3: Implement the mask and the diff**

Replace the two stubs in `lib/mhi_pure/mhi_diag_frame.cpp` and add the helpers above them:

```cpp
#include <stdarg.h>

void mhi_diag_mask_default(uint8_t* mask, size_t len) {
  memset(mask, 0xff, len);
  for (size_t i = SB0; i <= SB2 && i < len; i++) mask[i] = 0x00;
  if (len > DB6) mask[DB6] = 0x3f;   // bits 0xc0 echo the request prefix
  for (size_t i = DB9; i <= DB12 && i < len; i++) mask[i] = 0x00;
  if (len > DB14) mask[DB14] = (uint8_t)~0x04;  // the frame toggle
  if (len > CBH) mask[CBH] = 0x00;
  if (len > CBL) mask[CBL] = 0x00;
  if (len > CBL2) mask[CBL2] = 0x00;
}

// Byte names as in mhi_frame.h. out holds 5 characters ("DB26" and the NUL).
static void byte_name(size_t i, char* out) {
  if (i <= SB2) snprintf(out, 5, "SB%u", (unsigned)i);
  else if (i <= DB14) snprintf(out, 5, "DB%u", (unsigned)(i - DB0));
  else if (i == CBH) snprintf(out, 5, "CBH");
  else if (i == CBL) snprintf(out, 5, "CBL");
  else if (i <= DB26) snprintf(out, 5, "DB%u", (unsigned)(i - DB15 + 15));
  else snprintf(out, 5, "CB2");
}

// Appends to out without ever writing past out_len; n stays below out_len.
static void append(char* out, size_t out_len, size_t* n, const char* fmt, ...) {
  if (*n >= out_len) return;
  va_list ap;
  va_start(ap, fmt);
  const int w = vsnprintf(out + *n, out_len - *n, fmt, ap);
  va_end(ap);
  if (w < 0) return;
  *n = ((size_t)w < out_len - *n) ? *n + (size_t)w : out_len - 1;
}

size_t mhi_diag_frame_changes(MhiDiagFrame* d, const uint8_t* frame, size_t len, const uint8_t* mask, char* out, size_t out_len) {
  if (len > MHI_DIAG_FRAME_MAX || out_len < MHI_DIAG_TEXT_MAX) return 0;
  size_t n = 0;
  if (d->have_last) {
    size_t changed = 0;
    for (size_t i = 0; i < len; i++) {
      if (((frame[i] ^ d->last[i]) & mask[i]) == 0) continue;
      changed++;
      if (changed > MHI_DIAG_CHANGES_MAX) continue;
      char name[5];
      byte_name(i, name);
      append(out, out_len, &n, "%s %02x>%02x ", name, d->last[i], frame[i]);
    }
    if (changed == 0) return 0;
    if (changed > MHI_DIAG_CHANGES_MAX) append(out, out_len, &n, "+ ");
  } else {
    append(out, out_len, &n, "first ");
  }
  append(out, out_len, &n, "|");
  for (size_t i = 0; i < len; i++) append(out, out_len, &n, " %02x", frame[i]);
  memcpy(d->last, frame, len);
  d->have_last = true;
  return n;
}
```

- [ ] **Step 4: Run the whole suite, all pass, then every host suite**

```bash
pio test -e native -f test_mhi_diag_frame 2>&1 | grep -E 'FAILED|PASSED|ERRORED'
pio test -e native 2>&1 | grep -E '^native|FAILED|ERRORED'
```
Expected: 17 tests in `test_mhi_diag_frame` PASSED; all other suites PASSED (107 + 17 = 124 host tests).

- [ ] **Step 5: Commit**

```bash
git add lib/mhi_pure/mhi_diag_frame.cpp test/test_mhi_diag_frame/test_mhi_diag_frame.cpp
git commit -m "feat: masked status-frame diff text for diag/frame (#4)

Compares a MOSI frame with the last published one under a mask that leaves
out what changes on its own (header, the cycling operating-data bytes and
the request-prefix bits of DB6, checksum, the DB14 frame toggle), and names
the changed bytes old>new ahead of the whole frame in hex.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: Raw bytes leave the core

**Files:**
- Modify: `src/MHI-AC-Ctrl-core.h:50-55` (enum `ACStatus`), `:83-85` (`CallbackInterface_Status`)
- Modify: `src/MHI-AC-Ctrl-core.cpp:264-276` (after checksum validation), `:594-598` (`default:` branch)

**Interfaces:**
- Produces: enum values `raw_frame`, `raw_opdata` (in the `type_status` group, never passed to `output_P`); `virtual void cbiRawFunction(ACStatus status, const uint8_t* bytes, size_t len)` with an empty default body on `CallbackInterface_Status`. The core calls it with `(raw_frame, MOSI_frame, frameSize)` once per valid frame and `(raw_opdata, &MOSI_frame[DB9], 4)` per unknown operating-data answer.

There is no host test for the core (it is Arduino-bound, the project's convention); the build under `-Werror` and the bench check in Task 9 verify it.

- [ ] **Step 1: Add the status ids and the virtual**

In `src/MHI-AC-Ctrl-core.h`, change the first line of `enum ACStatus`:

```cpp
enum ACStatus { // Status enum
  status_power = type_status, status_mode, status_fan, status_vanes, status_vanesLR, status_3Dauto, status_troom, status_tsetpoint, status_errorcode, status_action,
  raw_frame, raw_opdata,  // cbiRawFunction only: the whole MOSI frame, and DB9..DB12 of unknown operating data. Never published through output_P
```

and the callback interface:

```cpp
class CallbackInterface_Status {
  public:
    virtual void cbiStatusFunction(ACStatus status, int value) = 0;
    // Raw bytes for the protocol discovery tooling (fork #4): raw_frame with
    // the whole MOSI frame after checksum validation, raw_opdata with
    // DB9..DB12 of an operating-data answer the decoder does not know.
    // Optional: the default does nothing.
    virtual void cbiRawFunction(ACStatus status, const uint8_t* bytes, size_t len) { (void)status; (void)bytes; (void)len; }
};
```

- [ ] **Step 2: Call it from the core**

In `src/MHI-AC-Ctrl-core.cpp`, directly after the extended-frame checksum check and before `if (new_datapacket_received) {` (currently lines 270-276):

```cpp
  // Every valid frame goes to the discovery tooling; main.cpp decides whether
  // anything in it is worth publishing.
  m_cbiStatus->cbiRawFunction(raw_frame, MOSI_frame, frameSize);
```

In the operating-data switch's `default:` branch (currently line 594), before the existing `cbiStatusFunction(opdata_unknown, ...)` call:

```cpp
      default:    // unknown operating data
        m_cbiStatus->cbiRawFunction(raw_opdata, &MOSI_frame[DB9], 4);  // the value bytes, which the number below drops
        m_cbiStatus->cbiStatusFunction(opdata_unknown, MOSI_frame[DB10] << 8 | MOSI_frame[DB9]);
```

- [ ] **Step 3: Build every firmware environment**

```bash
envs=$(grep -o '^\[env:[^]]*\]' platformio.ini | sed 's/\[env:\(.*\)\]/\1/' | grep -v '^native$' | sed 's/^/-e /' | tr '\n' ' ') && pio run $envs 2>&1 | grep -E 'SUCCESS|FAILED|error:|warning:' | tail -12
```
Expected: ten `SUCCESS` lines, no `error:`/`warning:` lines from `src/`.

- [ ] **Step 4: Commit**

```bash
git add src/MHI-AC-Ctrl-core.h src/MHI-AC-Ctrl-core.cpp
git commit -m "feat: hand raw frames and unknown operating data to the status callback (#4)

A second, optional virtual on the callback interface carries bytes, so
nothing is packed into the int the status callback uses.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 5: The one-shot operating-data request slot in the core

**Files:**
- Modify: `src/MHI-AC-Ctrl-core.h:131-133` (private members next to `request_erropData`), `:150-165` (public methods next to `request_ErrOpData`)
- Modify: `src/MHI-AC-Ctrl-core.cpp:100-102` (next to `request_ErrOpData()`), `:162-168` (the request window)

**Interfaces:**
- Produces: `void MHI_AC_Ctrl_Core::request_OpData(byte prefix, byte code);` — stores one pending request; the next time the normal cycle would write a request code into `MISO_frame[DB6]`/`[DB9]`, the pending one is written instead, once, without advancing `opdataNo`.

- [ ] **Step 1: Add the state and the method**

`src/MHI-AC-Ctrl-core.h`, private members, after `bool request_erropData = false;`:

```cpp
    // One-shot operating-data request from set/OpDataRequest (fork #4). Sent
    // in place of the next code of the normal cycle, once, see loop().
    bool request_opdata_pending = false;
    byte request_opdata_prefix = 0;
    byte request_opdata_code = 0;
```

public, after `void request_ErrOpData();`:

```cpp
    void request_OpData(byte prefix, byte code);  // ask the AC once for one operating-data code (prefix 0x40/0xc0 as in the request table)
```

`src/MHI-AC-Ctrl-core.cpp`, after `request_ErrOpData()`:

```cpp
void MHI_AC_Ctrl_Core::request_OpData(byte prefix, byte code) {
  request_opdata_prefix = prefix;
  request_opdata_code = code;
  request_opdata_pending = true;  // a second command before it is sent replaces it
}
```

- [ ] **Step 2: Take the slot**

Replace the inner block of the request window (currently lines 163-167):

```cpp
      if (erropdataCnt == 0) {
        if (request_opdata_pending) {
          // The probe takes this slot instead of the next code; opdataNo is
          // not advanced, so the cycle resumes with the code it would have sent.
          MISO_frame[DB6] = request_opdata_prefix;
          MISO_frame[DB9] = request_opdata_code;
          request_opdata_pending = false;
        }
        else {
          MISO_frame[DB6] = pgm_read_word(opdata + opdataNo);
          MISO_frame[DB9] = pgm_read_word(opdata + opdataNo) >> 8;
          opdataNo = (opdataNo + 1) % opdataCnt;
        }
      }
```

Nothing changes for the `erropdataCnt > 0` path (lines 183-186 keep overwriting `DB6`/`DB9` with the reset values): the pending request simply survives until the next window, as the spec §3.3 requires.

- [ ] **Step 3: Build every firmware environment** (same command as Task 4 step 3). Expected: ten `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add src/MHI-AC-Ctrl-core.h src/MHI-AC-Ctrl-core.cpp
git commit -m "feat: one-shot operating-data request in place of the next cycle code (#4)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 6: Topics, the `Diag` switch, the two publishes and the two commands

**Files:**
- Modify: `src/MHI-AC-Ctrl.h:155-157` (after `TOPIC_REQUEST_PASSIVEMODE`), `:254-256` (after `PAYLOAD_REQUEST_PASSIVEMODE_OFF`)
- Modify: `src/support.h:43-45` (after `WiFI_SEARCH_FOR_STRONGER_AP_INTERVALL`)
- Modify: `src/main.cpp:10-18` (includes), `:27-28` (globals), `:214-220` (end of the command parser), `:223-226` (`StatusHandler`), `:504-527` (`setup()`), `:553-556` (`loop()`, the reconnect branch)

**Interfaces:**
- Consumes: Task 1-3's `mhi_diag_frame.h`, Task 4's `cbiRawFunction`/`raw_frame`/`raw_opdata`, Task 5's `request_OpData()`, `MhiRetryPacer`/`mhi_retry_due`/`mhi_retry_reset` from `mhi_link.h`, `output_P()` and `MQTTclient` from `support.h`.
- Produces: topics `diag/frame`, `diag/opdata` (not retained), `Diag` (retained `On`/`Off`), commands `set/Diag` and `set/OpDataRequest`.

- [ ] **Step 1: Defines**

`src/MHI-AC-Ctrl.h`, after the `TOPIC_REQUEST_PASSIVEMODE` block:

```cpp
// Protocol discovery tooling (fork #4): what the AC's frame changed, unknown
// operating data with its value bytes, and a one-shot request.
#ifndef TOPIC_DIAG_FRAME
#define TOPIC_DIAG_FRAME "diag/frame"
#endif
#ifndef TOPIC_DIAG_OPDATA
#define TOPIC_DIAG_OPDATA "diag/opdata"
#endif
#ifndef TOPIC_DIAG
#define TOPIC_DIAG "Diag"
#endif
#ifndef TOPIC_REQUEST_DIAG
#define TOPIC_REQUEST_DIAG "Diag"
#endif
#ifndef TOPIC_REQUEST_OPDATA
#define TOPIC_REQUEST_OPDATA "OpDataRequest"
#endif
```

after the `PAYLOAD_REQUEST_PASSIVEMODE_OFF` block:

```cpp
#ifndef PAYLOAD_DIAG_ON
#define PAYLOAD_DIAG_ON "On"
#endif
#ifndef PAYLOAD_DIAG_OFF
#define PAYLOAD_DIAG_OFF "Off"
#endif
```

`src/support.h`, after the `WiFI_SEARCH_FOR_STRONGER_AP_INTERVALL` block:

```cpp
#ifndef DIAG_DEFAULT
#define DIAG_DEFAULT true                           // whether diag/frame (the status-frame change topic) is on after boot; set/Diag switches it at runtime
#endif
```

- [ ] **Step 2: main.cpp — includes, state, the raw callback**

Add to the includes (alphabetical, after `mhi_action.h`):

```cpp
#include "mhi_diag_frame.h"
#include "mhi_link.h"
```

After `MhiTroomFilter troom_filter = {0, false};`:

```cpp
// Protocol discovery tooling (#4 batch A). diag/frame compares each valid
// frame with the last *published* one, at most once a second, while Diag is
// on; a reconnect starts with a whole frame ("first | ...").
static MhiDiagFrame diag_frame = {{0}, false};
static uint8_t diag_mask[MHI_DIAG_FRAME_MAX];
static MhiRetryPacer diag_pacer = {0, false};
static bool diag_on = DIAG_DEFAULT;

static void publish_diag_state() {
  if (diag_on)
    output_P((ACStatus)type_status, PSTR(TOPIC_DIAG), PSTR(PAYLOAD_DIAG_ON));
  else
    output_P((ACStatus)type_status, PSTR(TOPIC_DIAG), PSTR(PAYLOAD_DIAG_OFF));
}
```

In `class StatusHandler`, after the closing brace of `cbiStatusFunction` (before the class's closing `};`):

```cpp
    void cbiRawFunction(ACStatus status, const uint8_t* bytes, size_t len) override {
      char text[MHI_DIAG_TEXT_MAX];
      if (status == raw_opdata) {
        // An event, not state: not retained, published every time it is seen.
        if (mhi_diag_opdata_text(bytes, text, sizeof(text)) > 0)
          MQTTclient.publish(MQTT_PREFIX TOPIC_DIAG_OPDATA, text, false);
      }
      else if (status == raw_frame && diag_on && mhi_retry_due(&diag_pacer, millis(), 1000)) {
        if (mhi_diag_frame_changes(&diag_frame, bytes, len, diag_mask, text, sizeof(text)) > 0)
          MQTTclient.publish(MQTT_PREFIX TOPIC_DIAG_FRAME, text, false);
      }
    }
```

- [ ] **Step 3: main.cpp — the two commands**

In `MQTT_subscribe_callback`, before the final `else publish_cmd_unknown();`, i.e. after the `set/PassiveMode` block's closing brace:

```cpp
  else if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_REQUEST_DIAG)) == 0) {
    if (strcmp_P(payload_str, PSTR(PAYLOAD_DIAG_ON)) == 0) {
      diag_on = true;
      publish_diag_state();
      publish_cmd_ok();
    }
    else if (strcmp_P(payload_str, PSTR(PAYLOAD_DIAG_OFF)) == 0) {
      diag_on = false;
      publish_diag_state();
      publish_cmd_ok();
    }
    else
      publish_cmd_invalidparameter();
  }
  else if (strcmp_P(topic, PSTR(MQTT_SET_PREFIX TOPIC_REQUEST_OPDATA)) == 0) {
    uint8_t prefix, code;
    if (mhi_opdata_request_parse(payload_str, &prefix, &code)) {
      mhi_ac_ctrl_core.request_OpData(prefix, code);
      publish_cmd_ok();
    }
    else
      publish_cmd_invalidparameter();
  }
```

- [ ] **Step 4: main.cpp — boot and reconnect**

In `setup()`, before `mhi_ac_ctrl_core.init(drive_miso);`:

```cpp
  mhi_diag_mask_default(diag_mask, sizeof(diag_mask));
```

In `loop()`, inside `if (MQTTStatus == MQTT_RECONNECTED) {` after `mhi_troom_filter_reset(&troom_filter);`:

```cpp
      publish_diag_state();
      diag_frame.have_last = false;   // the next diag/frame is a whole frame
      mhi_retry_reset(&diag_pacer);
```

- [ ] **Step 5: Build every firmware environment, check the size**

```bash
envs=$(grep -o '^\[env:[^]]*\]' platformio.ini | sed 's/\[env:\(.*\)\]/\1/' | grep -v '^native$' | sed 's/^/-e /' | tr '\n' ' ') && pio run $envs 2>&1 | grep -E 'SUCCESS|FAILED|error:|warning:' | tail -12
pio run -e d1_mini 2>&1 | grep 'Flash budget'
```
Expected: ten `SUCCESS`; d1_mini a few KB above 342336, well under 460000. If a `warning:` from `src/` appears the build fails (`-Werror`): fix it, do not silence it.

- [ ] **Step 6: Run the host tests once more**

```bash
pio test -e native 2>&1 | grep -E '^native|FAILED|ERRORED'
```
Expected: all suites PASSED.

- [ ] **Step 7: Commit**

```bash
git add src/MHI-AC-Ctrl.h src/support.h src/main.cpp
git commit -m "feat: diag/frame, diag/opdata, set/Diag and set/OpDataRequest topics (#4)

diag/frame publishes the status bytes that changed since the last publish,
old>new, then the whole frame, at most once a second while Diag is On.
diag/opdata publishes DB9..DB12 of every operating-data answer the decoder
does not know. set/OpDataRequest asks the AC once for any code.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 7: Docs

**Files:**
- Modify: `SW-Configuration.md` (a new section before `# Advanced settings`, currently line 263; the option under `## WiFi` is not touched), `Troubleshooting.md:22-27` (Known limitations), `Version.md:23` (after the #18 bullet)

- [ ] **Step 1: SW-Configuration.md** — insert before the line `# Advanced settings`:

````markdown
## Finding out what a remote button does

Some remote functions are not decoded (ECO, HI POWER, night setback), and which byte of the AC's status carries them differs per model. Three diagnostic topics turn that into a five-minute test: press the button, watch the topic. The state of the tool and the boot default:

```cpp
#define DIAG_DEFAULT true    // whether diag/frame is on after boot; set/Diag switches it at runtime
```

topic | r/w | value | comment
---|---|---|---
`diag/frame` | r | `DB5 00>10 \| 6c 80 04 …` | the bytes of the AC's status frame that changed since the last publish, old>new, then the whole frame in hex. At most once a second while `Diag` is `On`; `first \| …` after every MQTT connect. The bytes that change on their own are left out of the compare: the header, the operating-data bytes DB9-DB12 and the request-prefix bits of DB6, the checksum, the frame toggle in DB14. Not retained
`diag/opdata` | r | `dd 80 01 00` | an operating-data answer the firmware does not decode, as DB9 DB10 DB11 DB12. `OpData/unknown` still publishes the same answer as a number. Not retained
`Diag` | r | `On`, `Off` | whether `diag/frame` is published
`set/Diag` | w | `On`, `Off` | switch `diag/frame` at runtime; answers on `cmd_received`
`set/OpDataRequest` | w | four hex digits, e.g. `c021` | ask the AC once for operating-data code `0x21` with request prefix `c0` (indoor) or `40` (outdoor), the same request the built-in codes use, in place of the next code of the normal cycle. The answer arrives on `diag/opdata`, or on the code's own topic if it is a known one. `cmd_received` answers `o.k.`, or `invalid parameter` for any other prefix or length

Worked example (16 Sep 2026, `airco/uitkijk/#` captured while pressing the remote): SILENT on and off each produced `OpData/unknown 32989` (`0x80DD`), so Silent is reported as `DB9 = 0xDD`, `DB10 = 0x80`, with the on/off state in `DB11`, which `diag/opdata` now shows. HI/ECO produced no operating data at all; its only trace was `Fan` and the internal setpoint changing. With `diag/frame` running, a press that flips a bit anywhere in the status frame shows up as one line naming the byte.

````

- [ ] **Step 2: Troubleshooting.md** — after the line `- ECO, Silent and Night set back mode` in Known limitations, add:

```markdown

To find out which bytes a remote function changes on your unit, see [Finding out what a remote button does](SW-Configuration.md#finding-out-what-a-remote-button-does): `diag/frame` names the status bytes that change, `diag/opdata` shows unknown operating data with its value, and `set/OpDataRequest` asks the AC for any operating-data code.
```

- [ ] **Step 3: Version.md** — after the periodic-telemetry (#18) bullet:

```markdown
- protocol discovery tooling (#4, batch A): `diag/frame` publishes the bytes of the AC's status frame that changed (old>new, then the whole frame), at most once a second while `Diag` is `On`; `diag/opdata` publishes unknown operating data with its value bytes (`OpData/unknown` dropped them); `set/OpDataRequest` asks the AC once for any operating-data code. Together they turn "what does this remote button do" into a five-minute test on the unit
```

- [ ] **Step 4: Commit**

```bash
git add SW-Configuration.md Troubleshooting.md Version.md
git commit -m "docs: how to find out what a remote button does with the diag topics (#4)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 8: CI, merge, toolkit

**Files:**
- Create: `~/.config/hass/tools/mhi/remote-test.sh`
- Modify: `~/.config/hass/tools/mhi/post-flash-check.sh` (the topic list at line 28 and a gated check after the telemetry block), `health-check.sh:44`, `README.md` (the script table)

- [ ] **Step 1: Push and wait for CI**

```bash
git -c credential.helper='!gh auth git-credential' push -u https://github.com/lvschouwen/MHI-AC-Ctrl.git feat/4a-diag-tooling
sha=$(git rev-parse --short=7 HEAD)
for i in $(seq 1 6); do id=$(gh run list -R lvschouwen/MHI-AC-Ctrl --branch feat/4a-diag-tooling --limit 1 --json databaseId,headSha -q ".[] | select(.headSha | startswith(\"$sha\")) | .databaseId"); [ -n "$id" ] && break; sleep 10; done
gh run watch "$id" -R lvschouwen/MHI-AC-Ctrl --exit-status --interval 20 >/dev/null; gh run view "$id" -R lvschouwen/MHI-AC-Ctrl --json conclusion,jobs -q '.conclusion, (.jobs[] | "\(.name): \(.conclusion)")'
```
Expected: `success`, `Host tests: success`, ten `Build …: success`.

- [ ] **Step 2: Merge fast-forward, push, delete the branch**

```bash
git checkout master && git merge --ff-only feat/4a-diag-tooling
git -c credential.helper='!gh auth git-credential' push https://github.com/lvschouwen/MHI-AC-Ctrl.git master
git -c credential.helper='!gh auth git-credential' push https://github.com/lvschouwen/MHI-AC-Ctrl.git --delete feat/4a-diag-tooling
git branch -d feat/4a-diag-tooling
git fetch -q https://github.com/lvschouwen/MHI-AC-Ctrl.git master:refs/remotes/origin/master
git log --oneline -1
```
Note the merged commit hash: it is `DIAG_COMMIT` below.

- [ ] **Step 3: `remote-test.sh`** — create `~/.config/hass/tools/mhi/remote-test.sh`, `chmod +x`:

```bash
#!/usr/bin/env bash
# Remote-button test: show what a unit publishes while someone presses the IR
# remote. Captures airco/<unit>/# and prints, as they arrive, the lines that
# matter for a button test: diag/frame, diag/opdata, Diag, Fan, Mode, Power,
# Tsetpoint, Vanes, OpData/unknown, OpData/Tsetpoint and the two fan speeds.
# Read-only. Usage: remote-test.sh slaapkamer|uitkijk [seconds, default 600]
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$HERE/units.sh"
unit_vars "${1:-}" || { echo "usage: $0 slaapkamer|uitkijk [seconds]" >&2; exit 2; }
SECS=${2:-600}
LOG=$(mktemp "${TMPDIR:-/tmp}/remote-test-$UNIT.XXXXXX")
echo "Capturing $PREFIX/# for $SECS s (Ctrl-C stops early). Full log: $LOG"
echo "Press the remote now; hold each state ~40 s. Times are local."
(cd "$HASS" && timeout "$SECS" node tools/ha-mqtt-capture.mjs "$PREFIX/#" "$LOG" >/dev/null 2>&1) &
CAP=$!
tail -n0 -F "$LOG" 2>/dev/null \
  | grep --line-buffered -E "$PREFIX/(diag/frame|diag/opdata|Diag|Fan|Mode|Power|Tsetpoint|Vanes|OpData/unknown|OpData/Tsetpoint|OpData/IU-FANSPEED|OpData/OU-FANSPEED) \"" \
  | grep --line-buffered -v '(retained)$' \
  | while IFS= read -r line; do
      ts=${line%%Z *}; rest=${line#*Z }
      printf '%s %s\n' "$(date -d "${ts}Z" '+%H:%M:%S')" "${rest#"$PREFIX/"}"
    done &
TAIL=$!
trap 'kill "$CAP" "$TAIL" 2>/dev/null' INT TERM
wait "$CAP"
kill "$TAIL" 2>/dev/null
echo "Capture ended. Full log: $LOG"
```

- [ ] **Step 4: post-flash-check.sh and health-check.sh**

In `post-flash-check.sh`: add `DIAG_COMMIT=<hash from step 2>   # fork issue #4 batch A: first firmware with the Diag topic` under `TELEMETRY_COMMIT`; add `Diag` to the topic list on line 28 after `FreeHeap`; and after the telemetry `fi` block insert:

```bash
diag=$(last Diag "$W/a.log")
if git -C "$REPO" merge-base --is-ancestor "$DIAG_COMMIT" "$EXPECT" 2>/dev/null; then
  [ "$diag" = On ] && ok "Diag On" || bad "Diag is '$diag', expected On"
else
  [ -z "$diag" ] && ok "no retained Diag ($EXPECT predates it)" || bad "stale retained Diag '$diag'"
fi
```

In `health-check.sh` line 44, add `Diag` after `FreeHeap`. In `README.md`, add a row: `| remote-test.sh <unit> [seconds] | shows what the unit publishes while the IR remote is pressed (diag/frame, diag/opdata, fan, setpoint) | Claude or Lucas |` and mention `Diag` in the post-flash-check row.

```bash
bash -n ~/.config/hass/tools/mhi/remote-test.sh && bash -n ~/.config/hass/tools/mhi/post-flash-check.sh && bash -n ~/.config/hass/tools/mhi/health-check.sh && echo syntax-ok
```
Expected: `syntax-ok`.

- [ ] **Step 5: Update issue #4 and memory**

Tick the batch A build items in #4's body (all but the flash line) with `gh api repos/lvschouwen/MHI-AC-Ctrl/issues/4 -X PATCH -F body=@file`, post a comment with the merged hash, CI run, test count and image size, and note in memory (`ac-units.md`: master hash, not flashed; `phase-4-design-state.md`: batch A merged) that nothing is flashed.

---

### Task 9: Hardware — flash Slaapkamer and run the remote test (needs Lucas)

**Files:** none in the repo. Outcome recorded on #4.

- [ ] **Step 1: Lucas flashes Slaapkamer** with his own go: `! ~/.config/hass/tools/mhi/flash-unit.sh slaapkamer`. Then:

```bash
~/.config/hass/tools/mhi/post-flash-check.sh slaapkamer
```
Expected: ALL CHECKS PASSED, including `Diag On`, `ResetReason Software/System restart`, and `FreeHeap` within a few hundred bytes of the previous value (44648 at the last flash).

- [ ] **Step 2: Watch `diag/frame` at rest for two minutes**

```bash
~/.config/hass/tools/mhi/remote-test.sh slaapkamer 120
```
Expected: one `first | …` line right after the run starts is NOT expected (the connect happened earlier); at most an occasional `DB3`/`DB13` line as the room temperature or outdoor state changes. If a byte flips every second, that byte needs the mask: publish `set/Diag Off`, note the byte on #4, and it becomes a one-line mask change plus a test before Uitkijk is flashed.

- [ ] **Step 3: The remote test**, with Lucas at the Slaapkamer remote, `remote-test.sh slaapkamer 600` running, one press per message, ~40 s apart: HI/ECO (→ HI), HI/ECO (→ ECO), HI/ECO (→ normal), SILENT on, SILENT off. Record for each press every `diag/frame` and `diag/opdata` line.

Expected for Silent: two `diag/opdata` lines `dd 80 xx 00` with different `xx` for on and off. Expected for HI/ECO: either `diag/frame` lines naming a byte (then that byte and its values are the decode for batch B), or nothing (then HI/ECO is not on the bus as state, and batch B drops it with this evidence).

- [ ] **Step 4: The probes**

```bash
# through Home Assistant's API, as post-flash-check.sh does; PREFIX from units.sh
T=$(cat ~/.config/hass/token); H=http://192.168.15.4:8123
for p in c021 4021 c0dd 40dd; do
  curl -s -m 10 -o /dev/null -w "$p -> HTTP %{http_code}\n" -H "Authorization: Bearer $T" -H 'Content-Type: application/json' \
    -X POST "$H/api/services/mqtt/publish" -d "{\"topic\":\"airco/slaapkamer/set/OpDataRequest\",\"payload\":\"$p\"}"
  sleep 25
done
```
with `remote-test.sh slaapkamer 150` running alongside. Expected: each request answers `cmd_received o.k.`; within ~2 s a `diag/opdata` line if the AC answers the code, or nothing if it ignores it. Also send `set/OpDataRequest banana` once and expect `invalid parameter`.

- [ ] **Step 5: Record and decide.** Post the table of presses and lines on #4, tick the flash line for Slaapkamer, and write the batch B decode (Silent values; HI/ECO byte or "not on the bus") into the addendum §4.3 as a short "Result of the batch A test" paragraph, committed as `docs:`.

- [ ] **Step 6: Uitkijk**, only with its own go: `! ~/.config/hass/tools/mhi/flash-unit.sh uitkijk`, then `post-flash-check.sh uitkijk`; tick the flash line, update `ac-units.md` (both units on the new hash, kept images) and message the hass-config session that a flash happened (#304's reboot counter check) and that `Diag`, `diag/frame`, `diag/opdata` exist.

---

## Self-review

**Spec coverage (§3):** §3.0 raw callback → Task 4; §3.1 `diag/opdata` → Tasks 2, 4, 6; §3.2 `diag/frame`, mask, 1 s rate limit, `set/Diag`, `DIAG_DEFAULT`, size under the 256 B buffer (`MHI_DIAG_TEXT_MAX` 200 + topic ≤ 40) → Tasks 3, 6; §3.3 `set/OpDataRequest` parsing, prefixes, one-shot slot semantics → Tasks 1, 5, 6; §3.4 files → file map; §3.5 verification → Task 9. Two things the spec says that the plan changes deliberately: payload casing `On`/`Off` (repo convention, see Global Constraints) and the reconnect behaviour (`first | …` after every connect, which §3.2 implies with "compared with the last published copy").

**Placeholder scan:** none; every step has its code or command.

**Type consistency:** `mhi_diag_frame_changes(MhiDiagFrame*, const uint8_t*, size_t, const uint8_t*, char*, size_t)` is identical in the header (Task 1), the implementation (Task 3) and the caller (Task 6); `cbiRawFunction(ACStatus, const uint8_t*, size_t)` identical in Task 4 and Task 6; `request_OpData(byte, byte)` in Task 5 and Task 6 (`uint8_t` is `byte`); `raw_frame`/`raw_opdata` names identical in Tasks 4 and 6.
