# Outdoor election, crash-loop safe mode and the ride-alongs (fork #22, #23, #24) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One build, one flash per unit, with three things in it:
- **#22:** the indoor units of one outdoor unit elect, over retained MQTT records, the one unit that writes the outdoor unit's 11 values under a shared group root and sends its Home Assistant device. The others stay members and take over when the publisher drops out.
- **#23:** a unit that crashes three times in a row shortly after boot starts in a safe mode with only Wi-Fi and OTA, so a bad build can always be replaced over the air.
- **#24:** four small additions: a Restart button in Home Assistant, a `Discovery` status that tells when a config was skipped, the batch C cleanup, and fan names overridden in CI.

**Architecture:** Two new pure modules in `lib/mhi_pure`, both host-tested:
- `mhi_group` holds the member record, the configuration rules, and the election as a state machine. The configuration rules are `constexpr`, so `support.h` uses them for its compile-time checks and the host tests pin the same functions. The state machine takes `now` as an argument and is tested with a simulated clock through the 17 scenarios of the #22 spec §9 and 13 more that pin what the plan review found untested.
- `mhi_safe_mode` makes the boot decision from the reset reason and a three-word RTC record, in which the core's crash hook sets a crashed bit.

Two new glue files carry them out on the unit:
- `src/group.{h,cpp}` feeds the election the MQTT messages and runs its actions: the record, `Group`, `reset_system_values()`, the outdoor discovery cursor and the peer subscriptions.
- `src/safe_mode.{h,cpp}` reads and writes the RTC words and holds the deliberate crash and the core's crash hook.

Elsewhere:
- `main.cpp` branches into safe mode first thing in `setup()` and `loop()`.
- `mhi_discovery` gets the Group role row and the Restart button, retires the energy row, and points the outdoor rows at the group root with absolute availability.
- `output_P()` gates the 11 system values.
- The core publishes those 11 again after every reset.

**Tech Stack:** PlatformIO: espressif8266@4.2.1, Arduino core 3.1.2 (framework package 3.30102.0), xtensa GCC 10.3, `-std=gnu++17`. PubSubClient3 3.3.1. Unity host tests (`pio test -e native`), GitHub Actions CI. The toolkit is bash and Python 3, checked with pytest and shellcheck.

**Specs:**
- `docs/superpowers/specs/2026-09-19-outdoor-election-design.md` (#22), with the owner's answers to this plan's first questions (commit `04fbecb`);
- `docs/superpowers/specs/2026-09-19-safe-mode-and-ride-alongs-design.md` (#23, #24), with RTC user block 32 (commit `a6f16e3`) and the crash hook the plan review asked for (commit `725dd49`).

Lucas approved both. The plan implements them. Where this plan chooses something a spec leaves open, the step says `Decision:`. Where the plan finds a spec contradicting the code, it follows the spec and lists the point under "Questions for the spec owner" at the end.

**Branch:** `feat/22-outdoor-election`, clean at `725dd49` (spec commits only since `8a2c82d`). Do not switch branches. This plan does not push, open a PR or merge.

**How this plan was checked before it was written:** a script applied every code block below, in task order, to fresh `git archive` copies of `725dd49` in a scratch directory. The same script renders the blocks into this file. There:
- every task's end state passed `pio test -e native` on its own copy, and the firmware tasks built `d1_mini`, `ci-ha-discovery-outdoor`, `ci-all-options` and `ci-custom-payloads`;
- at the end: 248 test cases passed and all 12 CI environments built;
- the six compile-time refusals of Task 5 fired;
- the disassembly of the deliberate crash showed a real store to address 0, and the linked crash hook was this plan's;
- 21 deliberately broken safe-mode rules each failed at least one test, and so did 24 of 25 broken election rules; the 25th cannot change what the election does (Task 2 Step 6);
- the toolkit tests passed and `shellcheck -S warning` was clean.

The numbers quoted in the steps (test counts, row sizes, flash and RAM) come from that run.

## Global Constraints

- Implement the two specs exactly: nothing more, nothing less. A `Decision:` line marks each choice a spec left open.
- `lib/mhi_pure` compiles without Arduino. The only Arduino-dependent code allowed is the existing `#if defined(ARDUINO)` PROGMEM pair (`FMT`/`MHI_VSNPRINTF`) in `mhi_discovery.cpp`.
  - `mhi_group.{h,cpp}` include only `stddef.h`, `stdint.h`, `stdio.h`, `string.h` and `mhi_discovery.h`.
  - `mhi_safe_mode.{h,cpp}` include only `stdint.h`.
- The native tests build with `-std=gnu++17 -Wall -Wextra -Werror`, and `src/` with `-Werror` (`build_src_flags`). Every new pure function has a Unity test that failed first.
- **Never open, read, cat, grep or print `src/config_defaults.h`.** It holds real Wi-Fi, MQTT and OTA passwords. An in-repo `pio run` reads it through `__has_include` in `src/mhi_config.h`, so every firmware build in this plan runs in the clean worktree `.pio/ci-tree` (procedure below). Like CI, that worktree has no such file.
- No flashing, no `pio run -t upload`, no espota, and no network calls to the AC units or the broker. Never send `set/reset crash` to anything: the safe-mode proof is rollout, not this plan.
- Of the toolkit, run only these local checks: `python3 -m pytest`, `shellcheck`, `bash -n` and `discovery-payloads.sh`.
  - Never run `flash-unit.sh` or `rollback-unit.sh`: they read secrets.
  - Never run `post-flash-check.sh`, `health-check.sh`, `remote-test.sh` or `control-test.sh`: they talk to Home Assistant and the broker.
- `MhiDiscoveryRow` is append-only:
  - `MHI_DISCOVERY_OU_KWH` stays in the enum, disabled in every build and refused by the builder;
  - `MHI_DISCOVERY_GROUP_ROLE` and then `MHI_DISCOVERY_RESTART` are appended last;
  - `mhi_discovery_is_outdoor_row()` is the closed range `OU_OUTDOOR..OU_PROTECTION`.
- Never publish an empty payload on a discovery topic. Every caller publishes only when both `mhi_discovery_topic()` and `mhi_discovery_build()` return more than 0.
- No `setBufferSize()`: PubSubClient3's receive buffer stays `MQTT_MAX_PACKET_SIZE`, 256 bytes. The discovery buffer stays a `static char[1024]`.
- Values copied from the #22 spec:
  - group protocol `1`, `MHI_GROUP_MAX_PEERS` 6;
  - timing: grace 5 s; settle 5 s; `connected` 0 → gone after 30 s; stale after 3 × the peer's own period; outdoor configs 30 s after a claim or publisher start; conflict re-send **35 s** after the losing role-1 record;
  - sizes: record ≤ 140 bytes, the longest valid one 139; `GROUP_ROOT` 1..64 characters, ends in `/`, no `+ # ;`; hostname 1..32 characters without `/ + # ; " \`;
  - `outdoor_id` and an explicit `HA_OUTDOOR_ID`: 1..40 characters without `; / + # " \`, space or control character;
  - a record's prefix and `MQTT_PREFIX`: 1..64 characters, ends in `/`, without `; + # " \`, space or control character;
  - `TELEMETRY_PERIOD` 1..86400;
  - `Group`: `0` member, `1` publisher, `2` outdoor ID mismatch, `3` protocol version mismatch.
- Values copied from the #23/#24 spec:
  - the RTC record is three words at RTC user block 32, right after eboot's OTA command in blocks 0-31: magic `0x4D484953`, data (the count in bits 0-7, the entries in bits 8-15, the crashed bit in bit 16), check `magic ^ data ^ 0xFFFFFFFF`;
  - reasons 1, 2 and 3 are crashes, and so is a boot that finds the crashed bit, which the core's `custom_crash_callback()` sets and every boot clears; safe mode starts at a count of 3; the count saturates at 255;
  - the count is cleared after 120 s of normal uptime, and safe mode restarts after 10 min;
  - `TOPIC_SAFE_MODE "SafeMode"`, a bare integer at every connect;
  - `PAYLOAD_DISCOVERY_SKIPPED "skipped"`;
  - the Restart button: `button`, `<id_prefix>_restart`, `HA_NAME_RESTART "Restart"`, `cmd_t ~/<set_prefix><TOPIC_REQUEST_RESET>`, `pl_prs PAYLOAD_REQUEST_RESET`, `dev_cla restart`, `ent_cat config`;
  - 17 entities per unit with the 33-byte frame.
- The 11 system values are a fixed list: OUTDOOR, CT, COMP, DEFROST, TOTAL-COMP-RUN, PROTECTION-NO, TD, TDSH, THO-R1, THI-R2, OU-FANSPEED. Only their `opdata_*` statuses are routed.
- Nothing in the safe-mode decision path may block, loop or touch the network: RTC word reads, a pure function, one RTC write. The crash hook does the same and nothing else: no Serial, no allocation.
- Every new `TOPIC_*` and `PAYLOAD_*` goes into `src/MHI-AC-Ctrl.h` with the others.
- Flash budget: `custom_max_firmware_bytes 460000` (`scripts/check_flash_size.py`). Measured on the trial run, at `725dd49` → the end of Task 10:
  - `d1_mini` 343872 → 350144 B, `ci-all-options` 355984 → 362672 B;
  - static RAM of `d1_mini` 30672 → 32176 B (+1504 B, of which `MhiGroup` is 1252 B);
  - FreeHeap on the units, about 41.5 KB today, is expected to be about 40 KB at boot.
- Everything is English. The new Home Assistant names are `HA_NAME_GROUP_ROLE` "Group role" and `HA_NAME_RESTART` "Restart".
- Commit subjects are conventional (`feat`/`fix`/`test`/`docs`/`refactor`/`chore`/`ci`) with the issue number: `(#22)`, `(#23)`, `(#24)`, or `(#22, #23, #24)` for the docs. Every commit ends with exactly these two lines. Do not paraphrase them or put your own model name in:
  ```
  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_013aKH8NAdtagPe21HRXJdt5
  ```
- Never weaken or delete a test to make a change pass.
  - Task 3 changes three existing assertions and names, for each, the spec section that retires what it asserted.
  - Task 10 replaces the weak fixture tests and the dead `mdl` assertion, as #24 spec §2.2 asks.
- Do not touch the units, hass-config or the broker.

### The clean build (Tasks 3-6, 8-10 and 13)

```bash
cd /home/lucas/coding/MHI-AC-Ctrl
# Once: a worktree of this repo inside the ignored .pio/, without src/config_defaults.h.
[ -d .pio/ci-tree ] || git worktree add --detach .pio/ci-tree HEAD
mkdir -p .pio/ci-tree/.pio
[ -d .pio/ci-tree/.pio/libdeps ] || cp -r .pio/libdeps .pio/ci-tree/.pio/   # saves the library download
# Every build: stage the task's files (a new file must be staged), snapshot, check the snapshot out there, build.
git add <the task's files>
SNAP=$(git stash create); SNAP=${SNAP:-HEAD}
git -C .pio/ci-tree checkout -q --detach "$SNAP"
(cd .pio/ci-tree && pio run -e <env> -e <env> ...)
```

`git stash create` writes a commit object holding the index and the tracked working tree, without changing either. `.pio/` is ignored, so the worktree never shows in `git status`. Task 13 removes it again.
Decision: firmware builds run in this worktree, not in the repo. The repo's builds read the gitignored `src/config_defaults.h`, and the hard rule is that no build may depend on it.

The 12 environments of `.github/workflows/ci.yml`, used by "all 12" below:

```
d1_mini ci-extended-frame ci-enhanced-resolution ci-ds18x20-sensor ci-ds18x20-troom ci-poweron-when-changing-mode ci-continue-without-mqtt ci-custom-payloads ci-ha-discovery ci-ha-discovery-outdoor ci-config-defaults ci-all-options
```

`ci.yml` itself does not change. The environment names stay the same, its fixture gate already covers both fixture directories, and the new `test/test_mhi_safe_mode` is picked up by `pio test -e native`.

---

## Task split, and where it differs from the suggestion

| # | Task | Deliverable |
|---|---|---|
| 1 | #22 `mhi_group`: record, rules, default outdoor ID | pure, 10 host tests |
| 2 | #22 `mhi_group`: peer table, §5.3 definitions, election | pure, 36 more host tests, among them the 17 scenarios, and a mutation check |
| 3 | #22 discovery: Group role row, group root, retired energy row, renderer, context fill | pure + firmware context, fixtures, 6 more host tests |
| 4 | #22 core: `reset_system_values()` and the widened fields | compile-verified |
| 5 | #22 configuration: checks, `#error`, derived outdoor ID, the two discovery cursors, CI envs | compile-verified, refusals shown |
| 6 | #22 group glue: `src/group.{h,cpp}`, the gate, the subscription, the `main.cpp` hooks | compile-verified |
| 7 | #23 `mhi_safe_mode`: the boot decision and the crashed bit | pure, 16 host tests, and a mutation check |
| 8 | #23 safe mode on the unit: RTC record, crash hook, `setup()`/`loop()`, `SafeMode`, `set/reset crash` | compile-verified, disassembly and the linked hook shown |
| 9 | #24 Restart button and the `Discovery` `skipped` value | pure + firmware, fixtures, 2 more host tests |
| 10 | #24 batch C cleanup and the CI fan names | no behaviour change |
| 11 | Docs for #22, #23, #24: README, `SW-Configuration.md`, `Troubleshooting.md`, `Version.md` | text |
| 12 | Toolkit outside the repo, `rollback-unit.sh` included | pytest, shellcheck |
| 13 | Final verification | report |

Decision: the suggested Task 5 of #22 is split into Task 5 (configuration, checks, cursors, CI envs) and Task 6 (the election glue). A reviewer can reject either one and approve the other, and each ends in a build of all 12 environments.

Decision: the `GROUP_ROOT`/`GROUP_OP_PREFIX`/`TOPIC_GROUP`/`HA_NAME_GROUP_ROLE` defines and the `src/discovery.cpp` context fill move into Task 3. `MhiDiscoveryCtx` and its three initialisers are one interface. A commit that changed the struct without them would build clean while the firmware published a NULL name for the Group role row (batch C's lesson, see its plan, Task 6). The Restart button's fields and defines are in Task 9 for the same reason.

Decision: #23 is two tasks, the pure decision (7) and the glue (8), like #22's module and glue. #24 is two tasks: the discovery changes (9), and the cleanup with the CI env (10), which change no behaviour. The docs of all three issues are one task (11), after the code, so the text describes what was built.

## File map

| File | Responsibility | Task |
|---|---|---|
| `lib/mhi_pure/mhi_group.h`/`.cpp` (new) | record format and parse, configuration rules (constexpr), default outdoor ID; peer table, §5.3 definitions, election tick and action set | 1, 2 |
| `test/test_mhi_group/test_mhi_group.cpp` (new) | Unity tests: record, rules, ID, scenarios 1-30, plumbing | 1, 2 |
| `lib/mhi_pure/mhi_discovery.h`/`.cpp` | `GROUP_ROLE` row, retired `OU_KWH`, `group_base`/`avty_topic`/`t_group`, public closed-range `mhi_discovery_is_outdoor_row()` (3); `RESTART` row, `t_request_reset`/`request_reset` (9) | 3, 9 |
| `test/test_mhi_discovery/test_mhi_discovery.cpp`, `test/fixtures/discovery*/` | new tests; fixtures: +2 `group_role.txt`, 6 `ou_*` changed, `ou_kwh.txt` deleted (3); +2 `restart.txt` (9); read-back fixture tests, no dead `mdl` assertion (10) | 3, 9, 10 |
| `tools/discovery_payloads.cpp` | `--group-base`, `--name-group-role`, derived `avty_topic` and outdoor ID, the `HA_OUTDOOR_DEVICE` comment (3); `--name-restart` (9) | 3, 9 |
| `lib/mhi_pure/mhi_safe_mode.h`/`.cpp`, `test/test_mhi_safe_mode/test_mhi_safe_mode.cpp` (new) | the boot decision, the 120 s clear, the crashed bit | 7 |
| `src/safe_mode.h`/`.cpp` (new) | RTC read and write, reason mapping, `SafeMode` value, the deliberate crash, `custom_crash_callback()` | 8 |
| `src/MHI-AC-Ctrl.h` | `TOPIC_GROUP` (3); `TOPIC_SAFE_MODE`, `PAYLOAD_REQUEST_RESET_CRASH` (8); `PAYLOAD_DISCOVERY_SKIPPED` (9) | 3, 8, 9 |
| `src/support.h` | group defines (3); `#error`, `TELEMETRY_PERIOD`, compile-time checks, `outdoor_id()` (5); `uptime_seconds()` (6); `HA_NAME_RESTART` (9) | 3, 5, 6, 9 |
| `src/support.cpp` | `outdoor_id()`, telemetry condition (5); gate, `members/+` subscription, `uptime_seconds()` (6); `SafeMode` at connect (8) | 5, 6, 8 |
| `src/discovery.h`/`.cpp` | context fill (3); two cursors, `has_outdoor` on, derived ID (5); Restart fields, `skipped` below `modes` (9) | 3, 5, 9 |
| `src/MHI-AC-Ctrl-core.h`/`.cpp` | `reset_system_values()`, 16-bit fields for the 11 (4); include order (10) | 4, 10 |
| `src/group.h`/`.cpp` (new) | the glue that owns `MhiGroup` and carries out its actions | 6 |
| `src/main.cpp` | callback routing, setup, loop hooks (6); safe-mode branch, 10-min restart, 120 s clear, `set/reset crash` (8) | 6, 8 |
| `lib/mhi_pure/mhi_vanes_lr.h` | the louver comment (spread modes) | 10 |
| `platformio.ini` | `ci-ha-discovery-outdoor` and `ci-all-options` without `HA_OUTDOOR_DEVICE`, with `GROUP_ROOT` (5); `ci-custom-payloads` fan names (10) | 5, 10 |
| `README.md`, `SW-Configuration.md`, `Troubleshooting.md`, `Version.md` | docs | 11 |
| `~/.config/hass/tools/mhi/*` (not in git) | toolkit, `rollback-unit.sh` included | 12 |

---

### Task 1: #22 `mhi_group`: the member record, the configuration rules and the default outdoor ID (pure)

**Files:**
- Create: `lib/mhi_pure/mhi_group.h`
- Create: `lib/mhi_pure/mhi_group.cpp`
- Test: `test/test_mhi_group/test_mhi_group.cpp`

**Interfaces:**
- Consumes: `size_t mhi_discovery_slug(const char* name, char* out, size_t out_len)` from `lib/mhi_pure/mhi_discovery.h`. It returns 0 for a name that slugs to nothing or does not fit.
- Produces (every later task relies on these names):
  - `#define MHI_GROUP_PROTO 1`, `MHI_GROUP_HOST_MAX 32`, `MHI_GROUP_ID_MAX 40`, `MHI_GROUP_ROOT_MAX 64`, `MHI_GROUP_RECORD_MAX 140`, `MHI_GROUP_PERIOD_MAX 86400`
  - `constexpr size_t mhi_group_len(const char* s)`
  - `constexpr bool mhi_group_text_ok(const char* s, size_t min, size_t max, const char* forbidden, bool strict)`
  - `constexpr bool mhi_group_starts_with(const char* s, const char* prefix)`
  - `constexpr bool mhi_group_host_valid(const char* s)`
  - `constexpr bool mhi_group_id_valid(const char* s)`: the record's `outdoor_id` rule, and the rule for an explicit `HA_OUTDOOR_ID`
  - `constexpr bool mhi_group_prefix_valid(const char* s)`: the record's prefix rule, and the rule for `MQTT_PREFIX`
  - `constexpr bool mhi_group_root_valid(const char* s)`
  - `constexpr size_t mhi_group_record_packet_max(size_t root_len)`, which gives 251 for 64
  - `constexpr size_t mhi_group_slug_len(const char* s)`, `constexpr size_t mhi_group_default_outdoor_id_len(const char* root)`
  - `struct MhiGroupRecord { uint8_t proto; uint8_t role; uint32_t term; uint32_t uptime; uint32_t period; char outdoor_id[41]; char prefix[65]; }`
  - `enum MhiGroupParse : uint8_t { MHI_GROUP_PARSE_MEMBER, MHI_GROUP_PARSE_FOREIGN, MHI_GROUP_PARSE_EMPTY, MHI_GROUP_PARSE_INVALID }`
  - `MhiGroupParse mhi_group_parse_record(const char* payload, size_t len, MhiGroupRecord* out)`
  - `size_t mhi_group_format_record(const MhiGroupRecord* rec, char* out, size_t out_len)`
  - `size_t mhi_group_default_outdoor_id(const char* group_root, char* out, size_t out_len)`

Decision: the §2 configuration rules are `constexpr` functions in this pure header, so `support.h`'s `static_assert`s (Task 5) and the host tests use one implementation, and a bad rule fails a host test instead of only a firmware build.
Decision: the record parser checks the `outdoor_id` and prefix fields with the same two functions, `mhi_group_id_valid()` and `mhi_group_prefix_valid()`, that `support.h` applies to this unit's own `HA_OUTDOOR_ID` and `MQTT_PREFIX` (answer 2), so a unit can never build a record that its peers reject.
Decision: a root that slugs to nothing gives the outdoor ID `outdoor`. The slug itself never has an underscore at its ends, and the spec's test asks for this case without naming the result.
Decision: `mhi_group_default_outdoor_id()` returns 0 for a root longer than 64 characters or a result longer than `out_len` allows. Task 5 makes the build refuse the case where the result is longer than 40 characters (the owner's answer 1).
Decision: "the longest valid record is 139 bytes" is pinned with a proto of `255`: the field table allows 1..255, and such a record parses as foreign. The longest record this firmware writes, with proto `1`, is pinned too, at 137 bytes.

- [ ] **Step 1: Write the failing test**

`test/test_mhi_group/test_mhi_group.cpp`:

```cpp
// Host tests for the outdoor election (fork #22; spec
// docs/superpowers/specs/2026-09-19-outdoor-election-design.md §9): the member
// record, the configuration rules support.h applies at compile time, the
// default outdoor ID, and the election scenarios with a simulated clock.

#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "mhi_group.h"

void setUp(void) {}
void tearDown(void) {}

// The configuration rules are constexpr: the same calls support.h makes.
static_assert(mhi_group_root_valid("airco/outdoor/"), "Lucas's GROUP_ROOT");
static_assert(!mhi_group_root_valid("airco/outdoor"), "a root must end in /");
static_assert(mhi_group_host_valid("airco-slaapkamer"), "Lucas's HOSTNAME");
static_assert(mhi_group_record_packet_max(64) == 251, "spec §2: the worst record packet with a 64-character root");
static_assert(mhi_group_default_outdoor_id_len("airco/outdoor/") == 21, "airco_outdoor_outdoor");
static_assert(mhi_group_id_valid("ac_outdoor") && !mhi_group_id_valid("ac outdoor"), "Lucas's HA_OUTDOOR_ID; no space");
static_assert(mhi_group_prefix_valid("airco/slaapkamer/") && !mhi_group_prefix_valid("airco/slaapkamer"), "Lucas's MQTT_PREFIX; ends in /");

// --- the record -------------------------------------------------------------

static MhiGroupParse parse(const char* s, MhiGroupRecord* r) { return mhi_group_parse_record(s, strlen(s), r); }

static void test_a_record_round_trips(void) {
  MhiGroupRecord r;
  TEST_ASSERT_EQUAL(MHI_GROUP_PARSE_MEMBER, parse("1;1;1;41382;60;ac_outdoor;airco/slaapkamer/", &r));
  TEST_ASSERT_EQUAL_UINT8(1, r.proto);
  TEST_ASSERT_EQUAL_UINT8(1, r.role);
  TEST_ASSERT_EQUAL_UINT32(1, r.term);
  TEST_ASSERT_EQUAL_UINT32(41382, r.uptime);
  TEST_ASSERT_EQUAL_UINT32(60, r.period);
  TEST_ASSERT_EQUAL_STRING("ac_outdoor", r.outdoor_id);
  TEST_ASSERT_EQUAL_STRING("airco/slaapkamer/", r.prefix);
  char out[MHI_GROUP_RECORD_MAX + 1];
  TEST_ASSERT_EQUAL_size_t(43, mhi_group_format_record(&r, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("1;1;1;41382;60;ac_outdoor;airco/slaapkamer/", out);
}

static void test_the_longest_valid_record_is_139_bytes(void) {
  MhiGroupRecord r;
  memset(&r, 0, sizeof(r));
  r.proto = 255;  // the field table allows 1..255: three digits
  r.role = 1;
  r.term = 4294967295u;
  r.uptime = 4294967295u;
  r.period = 86400;
  memset(r.outdoor_id, 'i', MHI_GROUP_ID_MAX);
  memset(r.prefix, 'p', MHI_GROUP_ROOT_MAX - 1);
  r.prefix[MHI_GROUP_ROOT_MAX - 1] = '/';
  char out[MHI_GROUP_RECORD_MAX + 1];
  TEST_ASSERT_EQUAL_size_t(139, mhi_group_format_record(&r, out, sizeof(out)));
  MhiGroupRecord back;
  TEST_ASSERT_EQUAL(MHI_GROUP_PARSE_FOREIGN, parse(out, &back));  // valid, and not this protocol
  TEST_ASSERT_EQUAL_UINT8(255, back.proto);
  // This firmware's longest: proto 1, so two bytes shorter, and it round-trips.
  r.proto = 1;
  TEST_ASSERT_EQUAL_size_t(137, mhi_group_format_record(&r, out, sizeof(out)));
  TEST_ASSERT_EQUAL(MHI_GROUP_PARSE_MEMBER, parse(out, &back));
  TEST_ASSERT_EQUAL_UINT32(4294967295u, back.term);
  TEST_ASSERT_EQUAL_STRING(r.outdoor_id, back.outdoor_id);
  TEST_ASSERT_EQUAL_STRING(r.prefix, back.prefix);
  TEST_ASSERT_EQUAL_size_t(0, mhi_group_format_record(&r, out, 137));  // no room for the NUL
  TEST_ASSERT_EQUAL_STRING("", out);
}

static void test_every_invalid_form_is_rejected(void) {
  static const char* const kBad[] = {
    "1;0;0;5;60;ac_outdoor",                        // six fields
    "1;0;0;5;60;ac_outdoor;airco/a/;x",             // eight fields
    "1;0;-1;5;60;ac_outdoor;airco/a/",              // a sign
    "1;0;+1;5;60;ac_outdoor;airco/a/",              // a leading +
    "1;0;;5;60;ac_outdoor;airco/a/",                // an empty field
    "1;0;0;5;60;;airco/a/",                         // an empty ID
    "1;2;1;5;60;ac_outdoor;airco/a/",               // role 2
    "1;0;4294967296;5;60;ac_outdoor;airco/a/",      // term past 32 bits
    "1;0;0;4294967296;60;ac_outdoor;airco/a/",      // uptime past 32 bits
    "1;0;0;5;0;ac_outdoor;airco/a/",                // period 0
    "1;0;0;5;86401;ac_outdoor;airco/a/",            // period past a day
    "1;1;0;5;60;ac_outdoor;airco/a/",               // role 1 with term 0
    "1;0;0;5;60;ac/outdoor;airco/a/",               // / in the ID
    "1;0;0;5;60;ac outdoor;airco/a/",               // a space in the ID
    "1;0;0;5;60;ac\"outdoor;airco/a/",              // " in the ID
    "1;0;0;5;60;ac\\outdoor;airco/a/",              // \ in the ID
    "1;0;0;5;60;ac+outdoor;airco/a/",               // + in the ID
    "1;0;0;5;60;ac#outdoor;airco/a/",               // # in the ID
    "1;0;0;5;60;ac\toutdoor;airco/a/",              // a control character in the ID
    "1;0;0;5;60;ac_outdoor;airco/a",                // a prefix without /
    "1;0;0;5;60;ac_outdoor;airco/+/",               // + in the prefix
    "1;0;0;5;60;ac_outdoor;airco a/",               // a space in the prefix
    "x;0;0;5;60;ac_outdoor;airco/a/",               // a non-digit proto
    "v1;0;0;5;60;ac_outdoor;airco/a/",              // a non-digit proto
    "0;0;0;5;60;ac_outdoor;airco/a/",               // proto 0
    "256;0;0;5;60;ac_outdoor;airco/a/",             // proto past 255
    ";0;0;5;60;ac_outdoor;airco/a/",                // an empty proto
  };
  MhiGroupRecord r;
  for (size_t i = 0; i < sizeof(kBad) / sizeof(kBad[0]); i++)
    TEST_ASSERT_EQUAL_MESSAGE(MHI_GROUP_PARSE_INVALID, parse(kBad[i], &r), kBad[i]);
}

static void test_too_long_is_rejected(void) {
  char s[200];
  MhiGroupRecord r;
  // An ID of 41 characters.
  snprintf(s, sizeof(s), "1;0;0;5;60;%s;airco/a/", "iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii");
  TEST_ASSERT_EQUAL(MHI_GROUP_PARSE_INVALID, parse(s, &r));
  // A prefix of 65 characters.
  snprintf(s, sizeof(s), "1;0;0;5;60;ac_outdoor;%s/", "pppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppp");
  TEST_ASSERT_EQUAL(MHI_GROUP_PARSE_INVALID, parse(s, &r));
  // 141 bytes: over the limit whatever the fields say.
  memset(s, '1', 141);
  s[141] = '\0';
  TEST_ASSERT_EQUAL(MHI_GROUP_PARSE_INVALID, parse(s, &r));
}

static void test_a_foreign_record_is_told_by_its_first_field(void) {
  MhiGroupRecord r;
  TEST_ASSERT_EQUAL(MHI_GROUP_PARSE_FOREIGN, parse("2;whatever;a later version;sends", &r));
  TEST_ASSERT_EQUAL_UINT8(2, r.proto);
  TEST_ASSERT_EQUAL(MHI_GROUP_PARSE_FOREIGN, parse("255", &r));
  TEST_ASSERT_EQUAL_UINT8(255, r.proto);
}

static void test_an_empty_payload_is_a_deleted_record(void) {
  MhiGroupRecord r;
  TEST_ASSERT_EQUAL(MHI_GROUP_PARSE_EMPTY, mhi_group_parse_record("", 0, &r));
}

static void test_hostnames(void) {
  TEST_ASSERT_TRUE(mhi_group_host_valid("airco-uitkijk"));
  TEST_ASSERT_TRUE(mhi_group_host_valid("MHI-AC-Ctrl"));
  TEST_ASSERT_TRUE(mhi_group_host_valid("12345678901234567890123456789012"));    // 32
  TEST_ASSERT_FALSE(mhi_group_host_valid("123456789012345678901234567890123"));  // 33
  TEST_ASSERT_FALSE(mhi_group_host_valid(""));
  TEST_ASSERT_FALSE(mhi_group_host_valid(NULL));
  TEST_ASSERT_FALSE(mhi_group_host_valid("a/b"));
  TEST_ASSERT_FALSE(mhi_group_host_valid("a+b"));
  TEST_ASSERT_FALSE(mhi_group_host_valid("a#b"));
  TEST_ASSERT_FALSE(mhi_group_host_valid("a;b"));
  TEST_ASSERT_FALSE(mhi_group_host_valid("a\"b"));
  TEST_ASSERT_FALSE(mhi_group_host_valid("a\\b"));
}

static void test_the_compile_time_rules(void) {
  TEST_ASSERT_TRUE(mhi_group_root_valid("MHI-AC-Ctrl/"));
  TEST_ASSERT_FALSE(mhi_group_root_valid(""));
  TEST_ASSERT_FALSE(mhi_group_root_valid("airco/+/"));
  TEST_ASSERT_FALSE(mhi_group_root_valid("airco/#/"));
  TEST_ASSERT_FALSE(mhi_group_root_valid("airco;/"));
  TEST_ASSERT_TRUE(mhi_group_root_valid("123456789012345678901234567890123456789012345678901234567890123/"));    // 64
  TEST_ASSERT_FALSE(mhi_group_root_valid("1234567890123456789012345678901234567890123456789012345678901234/"));  // 65
  TEST_ASSERT_TRUE(mhi_group_starts_with("airco/slaapkamer/set/", "airco/slaapkamer/"));
  TEST_ASSERT_FALSE(mhi_group_starts_with("airco/outdoor/members/", "airco/slaapkamer/set/"));
  // The record needs 250 bytes of packet at most, well inside PubSubClient3's 256.
  TEST_ASSERT_EQUAL_size_t(5 + 2 + 14 + 8 + 32 + 140, mhi_group_record_packet_max(14));
}

// The record's rules for outdoor_id and prefix (spec §5.1), which support.h
// also applies to this unit's own HA_OUTDOOR_ID and MQTT_PREFIX (§2).
static void test_the_record_rules_for_the_id_and_the_prefix(void) {
  TEST_ASSERT_TRUE(mhi_group_id_valid("ac_outdoor"));
  TEST_ASSERT_TRUE(mhi_group_id_valid("1234567890123456789012345678901234567890"));    // 40
  TEST_ASSERT_FALSE(mhi_group_id_valid("12345678901234567890123456789012345678901"));  // 41
  TEST_ASSERT_FALSE(mhi_group_id_valid(""));
  TEST_ASSERT_FALSE(mhi_group_id_valid(NULL));
  static const char* const kBadId[] = {"a;b", "a/b", "a+b", "a#b", "a\"b", "a\\b", "a b", "a\tb", "a\x7f" "b"};
  for (size_t i = 0; i < sizeof(kBadId) / sizeof(kBadId[0]); i++) TEST_ASSERT_FALSE_MESSAGE(mhi_group_id_valid(kBadId[i]), kBadId[i]);
  TEST_ASSERT_TRUE(mhi_group_prefix_valid("airco/slaapkamer/"));
  TEST_ASSERT_TRUE(mhi_group_prefix_valid("/"));
  TEST_ASSERT_TRUE(mhi_group_prefix_valid("123456789012345678901234567890123456789012345678901234567890123/"));    // 64
  TEST_ASSERT_FALSE(mhi_group_prefix_valid("1234567890123456789012345678901234567890123456789012345678901234/"));  // 65
  TEST_ASSERT_FALSE(mhi_group_prefix_valid("airco/slaapkamer"));  // no trailing /
  TEST_ASSERT_FALSE(mhi_group_prefix_valid(""));
  TEST_ASSERT_FALSE(mhi_group_prefix_valid(NULL));
  static const char* const kBadPrefix[] = {"a;b/", "a+b/", "a#b/", "a\"b/", "a\\b/", "a b/", "a\tb/"};
  for (size_t i = 0; i < sizeof(kBadPrefix) / sizeof(kBadPrefix[0]); i++)
    TEST_ASSERT_FALSE_MESSAGE(mhi_group_prefix_valid(kBadPrefix[i]), kBadPrefix[i]);
  // The parser applies the same two rules.
  MhiGroupRecord r;
  TEST_ASSERT_EQUAL(MHI_GROUP_PARSE_INVALID, parse("1;0;0;5;60;a b;airco/a/", &r));
  TEST_ASSERT_EQUAL(MHI_GROUP_PARSE_INVALID, parse("1;0;0;5;60;ac_outdoor;a b/", &r));
  TEST_ASSERT_EQUAL(MHI_GROUP_PARSE_MEMBER, parse("1;0;0;5;60;ac_outdoor;/", &r));
}

static void test_the_default_outdoor_id(void) {
  char id[MHI_GROUP_ID_MAX + 1];
  TEST_ASSERT_EQUAL_size_t(21, mhi_group_default_outdoor_id("airco/outdoor/", id, sizeof(id)));
  TEST_ASSERT_EQUAL_STRING("airco_outdoor_outdoor", id);
  TEST_ASSERT_EQUAL_size_t(19, mhi_group_default_outdoor_id("MHI-AC-Ctrl/", id, sizeof(id)));
  TEST_ASSERT_EQUAL_STRING("mhi_ac_ctrl_outdoor", id);
  // A root that slugs to nothing: no underscore at the ends, as in the slug.
  TEST_ASSERT_EQUAL_size_t(7, mhi_group_default_outdoor_id("--/", id, sizeof(id)));
  TEST_ASSERT_EQUAL_STRING("outdoor", id);
  // The group base (no trailing slash) derives the same ID, which the renderer relies on.
  TEST_ASSERT_TRUE(mhi_group_default_outdoor_id("airco/outdoor", id, sizeof(id)) > 0);
  TEST_ASSERT_EQUAL_STRING("airco_outdoor_outdoor", id);
  // Longer than 40 characters: refused, and support.h refuses the build.
  TEST_ASSERT_EQUAL_size_t(0, mhi_group_default_outdoor_id("a-very-long-group-root-for-outdoor-units/", id, sizeof(id)));
  TEST_ASSERT_EQUAL_STRING("", id);
  // The compile-time length agrees with the run-time ID.
  static const char* const kRoots[] = {"airco/outdoor/", "MHI-AC-Ctrl/", "--/", "a//b/", "x/"};
  for (size_t i = 0; i < sizeof(kRoots) / sizeof(kRoots[0]); i++) {
    TEST_ASSERT_TRUE(mhi_group_default_outdoor_id(kRoots[i], id, sizeof(id)) > 0);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(strlen(id), mhi_group_default_outdoor_id_len(kRoots[i]), kRoots[i]);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_record_round_trips);
  RUN_TEST(test_the_longest_valid_record_is_139_bytes);
  RUN_TEST(test_every_invalid_form_is_rejected);
  RUN_TEST(test_too_long_is_rejected);
  RUN_TEST(test_a_foreign_record_is_told_by_its_first_field);
  RUN_TEST(test_an_empty_payload_is_a_deleted_record);
  RUN_TEST(test_hostnames);
  RUN_TEST(test_the_compile_time_rules);
  RUN_TEST(test_the_record_rules_for_the_id_and_the_prefix);
  RUN_TEST(test_the_default_outdoor_id);
  return UNITY_END();
}
```

- [ ] **Step 2: Run it and see it fail**

Run: `pio test -e native -f test_mhi_group`
Expected: build error, `fatal error: mhi_group.h: No such file or directory`.

- [ ] **Step 3: Implement**

`lib/mhi_pure/mhi_group.h`:

```cpp
// The outdoor election (fork #22; spec
// docs/superpowers/specs/2026-09-19-outdoor-election-design.md). The indoor
// units that share one outdoor unit keep a retained record each under
// <GROUP_ROOT>members/<HOSTNAME> and elect, from those records and their
// connected topics, the one unit that publishes the outdoor unit's values and
// its Home Assistant device.
//
// Pure logic, no Arduino and no clock of its own: every entry point takes now,
// a millis() value, and every time is an unsigned difference, so the 49.7-day
// wrap is harmless.

#pragma once

#include <stddef.h>
#include <stdint.h>

#define MHI_GROUP_PROTO 1           // the group protocol major of this firmware
#define MHI_GROUP_HOST_MAX 32       // a hostname: the topic level after members/
#define MHI_GROUP_ID_MAX 40         // an outdoor ID
#define MHI_GROUP_ROOT_MAX 64       // GROUP_ROOT, and a record's prefix field
#define MHI_GROUP_RECORD_MAX 140    // a record payload; the longest valid one is 139
#define MHI_GROUP_PERIOD_MAX 86400  // TELEMETRY_PERIOD's upper bound, s: 3 periods in ms stay inside 31 bits

// --- configuration rules, usable at compile time ------------------------------

constexpr size_t mhi_group_len(const char* s) {
  size_t n = 0;
  while (s[n] != '\0') n++;
  return n;
}

// min..max characters, none of them in forbidden; with strict, no space and no
// control character either. constexpr, so support.h checks the unit's own
// configuration with the same rules the units apply to each other's records.
constexpr bool mhi_group_text_ok(const char* s, size_t min, size_t max, const char* forbidden, bool strict) {
  size_t n = 0;
  for (; s[n] != '\0'; n++) {
    if (n >= max) return false;
    const unsigned char c = (unsigned char)s[n];
    if (strict && (c <= ' ' || c == 0x7f)) return false;
    for (const char* f = forbidden; *f != '\0'; f++)
      if (s[n] == *f) return false;
  }
  return n >= min;
}

constexpr bool mhi_group_starts_with(const char* s, const char* prefix) {
  return *prefix == '\0' || (*s == *prefix && mhi_group_starts_with(s + 1, prefix + 1));
}

// A hostname, as the topic level after members/ and as HOSTNAME (spec §2, §5.1):
// 1..32 characters without / + # ; " \.
constexpr bool mhi_group_host_valid(const char* s) {
  return s != nullptr && mhi_group_text_ok(s, 1, MHI_GROUP_HOST_MAX, "/+#;\"\\", false);
}

// A record's outdoor_id (spec §5.1), and an explicit HA_OUTDOOR_ID (§2): 1..40
// characters without ; / + # " \, space or control character.
constexpr bool mhi_group_id_valid(const char* s) {
  return s != nullptr && mhi_group_text_ok(s, 1, MHI_GROUP_ID_MAX, ";/+#\"\\", true);
}

// A record's prefix (spec §5.1), and MQTT_PREFIX (§2): 1..64 characters, ends
// in /, no ; + # " \, space or control character.
constexpr bool mhi_group_prefix_valid(const char* s) {
  return s != nullptr && mhi_group_text_ok(s, 1, MHI_GROUP_ROOT_MAX, ";+#\"\\", true) && s[mhi_group_len(s) - 1] == '/';
}

// GROUP_ROOT (spec §2): 1..64 characters, ends in /, no + # ;.
constexpr bool mhi_group_root_valid(const char* s) {
  return s != nullptr && mhi_group_text_ok(s, 1, MHI_GROUP_ROOT_MAX, "+#;", false) && s[mhi_group_len(s) - 1] == '/';
}

// The worst record packet a unit receives (spec §2): 5 bytes of fixed header,
// 2 of topic length, <GROUP_ROOT>members/<a 32-character hostname>, and a
// 140-byte record. PubSubClient3 drops a larger packet whole.
constexpr size_t mhi_group_record_packet_max(size_t root_len) {
  return 5 + 2 + root_len + (sizeof("members/") - 1) + MHI_GROUP_HOST_MAX + MHI_GROUP_RECORD_MAX;
}

// The length of mhi_discovery_slug(s), at compile time: runs of characters
// outside a-z A-Z 0-9 become one "_", none at the ends.
constexpr size_t mhi_group_slug_len(const char* s) {
  size_t n = 0;
  bool pending = false;
  for (; *s != '\0'; s++) {
    const bool alnum = (*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z');
    if (!alnum) {
      if (n > 0) pending = true;
      continue;
    }
    if (pending) n++;
    pending = false;
    n++;
  }
  return n;
}

// The length of mhi_group_default_outdoor_id(root), at compile time.
constexpr size_t mhi_group_default_outdoor_id_len(const char* root) {
  return mhi_group_slug_len(root) > 0 ? mhi_group_slug_len(root) + (sizeof("_outdoor") - 1) : sizeof("outdoor") - 1;
}

// --- the member record (spec §5.1) --------------------------------------------

// <proto>;<role>;<term>;<uptime>;<period>;<outdoor_id>;<prefix>
struct MhiGroupRecord {
  uint8_t proto;                        // 1..255; a foreign record fills only this
  uint8_t role;                         // 0 member, 1 publisher
  uint32_t term;                        // publisher generation; >= 1 with role 1
  uint32_t uptime;                      // the unit's uptime counter, s
  uint32_t period;                      // the unit's TELEMETRY_PERIOD, 1..86400 s
  char outdoor_id[MHI_GROUP_ID_MAX + 1];
  char prefix[MHI_GROUP_ROOT_MAX + 1];  // the unit's MQTT_PREFIX
};

enum MhiGroupParse : uint8_t {
  MHI_GROUP_PARSE_MEMBER,   // a valid proto-1 record; every field is filled
  MHI_GROUP_PARSE_FOREIGN,  // the first field is a valid number other than 1; only proto is filled
  MHI_GROUP_PARSE_EMPTY,    // an empty payload: the unit's record was deleted
  MHI_GROUP_PARSE_INVALID,  // anything else
};

// Parses len bytes of payload. *out is written only for MEMBER and FOREIGN.
MhiGroupParse mhi_group_parse_record(const char* payload, size_t len, MhiGroupRecord* out);

// Writes the record as text. Returns its length, 0 (and "") when it does not fit.
size_t mhi_group_format_record(const MhiGroupRecord* rec, char* out, size_t out_len);

// The default outdoor ID (spec §2): the slug of group_root + "_outdoor", or
// "outdoor" for a root that slugs to nothing. Returns the length, 0 (and "")
// for a root longer than 64 characters or a result that does not fit out_len.
size_t mhi_group_default_outdoor_id(const char* group_root, char* out, size_t out_len);
```

`lib/mhi_pure/mhi_group.cpp`:

```cpp
#include "mhi_group.h"

#include <stdio.h>
#include <string.h>

#include "mhi_discovery.h"

// --- the member record ----------------------------------------------------------

// 1..10 digits, nothing else (no sign, no leading +), at most max.
static bool parse_u32(const char* s, size_t n, uint32_t max, uint32_t* out) {
  if (n == 0 || n > 10) return false;
  uint64_t v = 0;
  for (size_t i = 0; i < n; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + (uint64_t)(s[i] - '0');
  }
  if (v > max) return false;
  *out = (uint32_t)v;
  return true;
}

// A field as a C string, for the rules in mhi_group.h; false when it does not
// fit or holds a NUL.
static bool copy_field(const char* s, size_t n, char* out, size_t out_size) {
  if (n >= out_size || memchr(s, '\0', n) != NULL) return false;
  memcpy(out, s, n);
  out[n] = '\0';
  return true;
}

MhiGroupParse mhi_group_parse_record(const char* p, size_t len, MhiGroupRecord* out) {
  if (len == 0) return MHI_GROUP_PARSE_EMPTY;
  if (p == NULL || len > MHI_GROUP_RECORD_MAX) return MHI_GROUP_PARSE_INVALID;
  // The first field alone decides between this protocol and a foreign one, so
  // a later version may change everything after it.
  size_t first = 0;
  while (first < len && p[first] != ';') first++;
  uint32_t proto = 0;
  if (!parse_u32(p, first, 255, &proto) || proto == 0) return MHI_GROUP_PARSE_INVALID;
  MhiGroupRecord r;
  memset(&r, 0, sizeof(r));
  r.proto = (uint8_t)proto;
  if (proto != MHI_GROUP_PROTO) {
    *out = r;
    return MHI_GROUP_PARSE_FOREIGN;
  }
  const char* field[7] = {};
  size_t field_len[7] = {};
  size_t count = 0, start = 0;
  for (size_t i = 0; i <= len; i++) {
    if (i < len && p[i] != ';') continue;
    if (count == 7) return MHI_GROUP_PARSE_INVALID;  // an eighth field
    field[count] = p + start;
    field_len[count] = i - start;
    count++;
    start = i + 1;
  }
  if (count != 7) return MHI_GROUP_PARSE_INVALID;
  uint32_t role = 0, period = 0;
  if (!parse_u32(field[1], field_len[1], 1, &role) || !parse_u32(field[2], field_len[2], UINT32_MAX, &r.term) ||
      !parse_u32(field[3], field_len[3], UINT32_MAX, &r.uptime) ||
      !parse_u32(field[4], field_len[4], MHI_GROUP_PERIOD_MAX, &period) || period == 0)
    return MHI_GROUP_PARSE_INVALID;
  if (role == 1 && r.term == 0) return MHI_GROUP_PARSE_INVALID;
  // The same rules support.h applies to this unit's own HA_OUTDOOR_ID and MQTT_PREFIX.
  if (!copy_field(field[5], field_len[5], r.outdoor_id, sizeof(r.outdoor_id)) || !mhi_group_id_valid(r.outdoor_id))
    return MHI_GROUP_PARSE_INVALID;
  if (!copy_field(field[6], field_len[6], r.prefix, sizeof(r.prefix)) || !mhi_group_prefix_valid(r.prefix))
    return MHI_GROUP_PARSE_INVALID;
  r.role = (uint8_t)role;
  r.period = period;
  *out = r;
  return MHI_GROUP_PARSE_MEMBER;
}

size_t mhi_group_format_record(const MhiGroupRecord* r, char* out, size_t out_len) {
  if (out == NULL || out_len == 0) return 0;
  const int n = snprintf(out, out_len, "%u;%u;%lu;%lu;%lu;%s;%s", (unsigned)r->proto, (unsigned)r->role,
                         (unsigned long)r->term, (unsigned long)r->uptime, (unsigned long)r->period, r->outdoor_id,
                         r->prefix);
  if (n < 0 || (size_t)n >= out_len) {
    out[0] = '\0';
    return 0;
  }
  return (size_t)n;
}

size_t mhi_group_default_outdoor_id(const char* root, char* out, size_t out_len) {
  if (out == NULL || out_len == 0) return 0;
  out[0] = '\0';
  if (root == NULL || strlen(root) > MHI_GROUP_ROOT_MAX) return 0;
  char slug[MHI_GROUP_ROOT_MAX + 1];
  // No underscore at the ends, as in the slug itself: a root that slugs to
  // nothing gives "outdoor", not "_outdoor".
  const int n = mhi_discovery_slug(root, slug, sizeof(slug)) > 0 ? snprintf(out, out_len, "%s_outdoor", slug)
                                                                  : snprintf(out, out_len, "outdoor");
  if (n < 0 || (size_t)n >= out_len) {
    out[0] = '\0';
    return 0;
  }
  return (size_t)n;
}
```

- [ ] **Step 4: Run it and see it pass**

Run: `pio test -e native -f test_mhi_group`
Expected: `10 Tests 0 Failures 0 Ignored` and `OK`.

- [ ] **Step 5: Commit**

```bash
git add lib/mhi_pure/mhi_group.h lib/mhi_pure/mhi_group.cpp test/test_mhi_group/test_mhi_group.cpp
git commit -m "feat: the group member record, its configuration rules and the default outdoor ID, host-tested (#22)

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013aKH8NAdtagPe21HRXJdt5"
```

---

### Task 2: #22 `mhi_group`: the peer table, the definitions of §5.3 and the election (pure)

**Files:**
- Modify: `lib/mhi_pure/mhi_group.h`: append after the `mhi_group_default_outdoor_id` declaration, which ends the file: line 133
- Modify: `lib/mhi_pure/mhi_group.cpp`: append at the end
- Test: `test/test_mhi_group/test_mhi_group.cpp`: new tests above `main()`, and a new `main()`: lines 212-225

**Interfaces:**
- Consumes: Task 1's record, parse, format and rules.
- Produces (Task 6 uses these):
  - `#define MHI_GROUP_MAX_PEERS 6`, `MHI_GROUP_GRACE_MS 5000`, `MHI_GROUP_SETTLE_MS 5000`, `MHI_GROUP_DOWN_GONE_MS 30000`, `MHI_GROUP_CONFIGS_MS 30000`, `MHI_GROUP_RESEND_MS 35000`
  - `enum MhiGroupKind : uint8_t { MHI_GROUP_KIND_MEMBER, MHI_GROUP_KIND_FOREIGN }`
  - `enum MhiGroupLink : uint8_t { MHI_GROUP_LINK_UNKNOWN, MHI_GROUP_LINK_UP, MHI_GROUP_LINK_DOWN }`
  - `struct MhiGroupPeer`, `struct MhiGroup` (fields in the header below; tests read `g.state`, `g.peers[i].host`, `g.peers[i].used`, and set `g.role`/`g.term`/`g.max_term_seen` for scenarios 8, 26 and 30)
  - the action flags `MHI_GROUP_ACT_RECORD 0x01`, `MHI_GROUP_ACT_STATE 0x02`, `MHI_GROUP_ACT_START 0x04`, `MHI_GROUP_ACT_DEMOTE 0x08`, `MHI_GROUP_ACT_CONFIGS 0x10`
  - `struct MhiGroupActions { uint8_t flags; uint8_t state; char subscribe[65]; }` (no unsubscribe, answer 7)
  - `enum MhiGroupResult : uint8_t { MHI_GROUP_REC_OK, MHI_GROUP_REC_INVALID, MHI_GROUP_REC_FULL }`
  - `void mhi_group_init(MhiGroup* g, const char* host, const char* outdoor_id, const char* prefix, uint32_t period_s)`
  - `void mhi_group_connect(MhiGroup* g, uint32_t now)`
  - `MhiGroupResult mhi_group_on_record(MhiGroup* g, const char* host, const char* payload, size_t len, uint32_t now)`
  - `void mhi_group_on_connected(MhiGroup* g, const char* host, bool up, uint32_t now)`
  - `const char* mhi_group_host_of_connected_topic(const MhiGroup* g, const char* topic, const char* t_connected)`
  - `void mhi_group_tick(MhiGroup* g, uint32_t now, MhiGroupActions* act)`
  - `bool mhi_group_may_publish_system(const MhiGroup* g)`
  - `size_t mhi_group_own_record(const MhiGroup* g, uint32_t uptime_s, char* out, size_t out_len)`

Decision: `mhi_group_on_record()` also takes the payload length the broker sent. The glue's 141-byte copy truncates a longer payload, so only the original length can tell a 150-byte payload from a valid 140-byte one.
Decision: a refresh is detected by a 32-bit FNV-1a hash of the whole payload, not by comparing parsed fields. A foreign record's fields after the first are never read, yet its uptime still changes every period.
Decision: `mhi_group_host_of_connected_topic()` maps a topic to its peer. The spec's `mhi_group_on_connected(host, up, now)` needs a host, and the glue only has the topic.
Decision: a claim when `max_term_seen` is 4294967295 keeps that term. Adding 1 would wrap to 0, which a role-1 record may not carry.
Decision: `Group` is published whenever the computed state differs from the last one published since the connect, the way back from 2 or 3 included. Otherwise a stale `2` would stay retained.
Decision: the settle clock runs only while the role is 0, the state is 0 and there is no incumbent; anything else stops it. Rule 4 applies only in that state, so a unit leaving state 2 settles its full 5 s again.
Decision: rule 3 arms once per new or changed record (a retained copy delivered again does not), only after the grace period, and a second such record restarts the 35 s. In the grace period it is not needed: the publisher start's configs, 30 s after the grace period, already come after the loser's (scenario 30). That is how the plan reads "once per such record".
Decision: one subscription per tick. The glue must subscribe from `loop()` (§6.1), and PubSubClient hands over at most one message per `loop()` pass. Each peer entry has one `subscribed` flag: set when its connected topic is subscribed, cleared on a new entry, a prefix change or a change from foreign to member. Nothing is ever unsubscribed (spec §6.1 step 2 since `04fbecb`).
Decision: a `connected` payload other than `PAYLOAD_CONNECTED_TRUE`/`_FALSE`, an empty retained deletion included, leaves the peer's link state as it was. The same applies to a `connected` message for a peer that is not in the table.
Decision: the gate reads the state cached by the last tick. The tick runs on every `loop()` pass, and the gate's caller, `output_P()`, has no clock.
Decision: `mhi_group_init()` copies without validating. `support.h` checks `HOSTNAME`, the outdoor ID and `GROUP_ROOT` at compile time (Task 5).
Decision: every tick latches what has gone, per entry: `stale` once the record is not refreshed for 3 periods, until the payload changes; `down_gone` once `connected` has read 0 for 30 s, until `connected` 1 or another prefix. `now - refreshed_ms` wraps after 49.7 days, and without the latch a record never refreshed since would look fresh again for 3 periods (scenarios 28 and 29).
Decision: `copy_bounded()` copies with `memcpy` and an explicit length from a bounded scan, not with `strncpy`: at most `size - 1` characters, never a read past the source's NUL.

- [ ] **Step 1: Write the failing tests**

In `test/test_mhi_group/test_mhi_group.cpp`, replace the whole `main()` with the new tests followed by the new `main()`:

Replace (`test/test_mhi_group/test_mhi_group.cpp`, lines 212-225):

```cpp
int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_record_round_trips);
  RUN_TEST(test_the_longest_valid_record_is_139_bytes);
  RUN_TEST(test_every_invalid_form_is_rejected);
  RUN_TEST(test_too_long_is_rejected);
  RUN_TEST(test_a_foreign_record_is_told_by_its_first_field);
  RUN_TEST(test_an_empty_payload_is_a_deleted_record);
  RUN_TEST(test_hostnames);
  RUN_TEST(test_the_compile_time_rules);
  RUN_TEST(test_the_record_rules_for_the_id_and_the_prefix);
  RUN_TEST(test_the_default_outdoor_id);
  return UNITY_END();
}
```

with:

```cpp
// --- the election: a simulated unit -----------------------------------------

static MhiGroup g;
static MhiGroupActions act;
// Every flag seen since the last reset, and when each first appeared.
static uint8_t seen;
static uint32_t first_ms[8];

static void reset_seen(void) {
  seen = 0;
  for (int b = 0; b < 8; b++) first_ms[b] = UINT32_MAX;
}

static uint32_t when(uint8_t flag) {
  for (int b = 0; b < 8; b++)
    if (flag == (1u << b)) return first_ms[b];
  return UINT32_MAX;
}

static void tick(uint32_t now) {
  mhi_group_tick(&g, now, &act);
  for (int b = 0; b < 8; b++)
    if ((act.flags & (1u << b)) && !(seen & (1u << b))) first_ms[b] = now;
  seen |= act.flags;
}

// Ticks every 100 ms from `from` for `ms`, around the wrap too.
static void run(uint32_t from, uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 100) tick(from + t);
}

static void boot(const char* host, uint32_t now) {
  mhi_group_init(&g, host, "ac_outdoor", "airco/me/", 60);
  mhi_group_connect(&g, now);
  reset_seen();
}

static void record(const char* host, const char* payload, uint32_t now) {
  TEST_ASSERT_EQUAL(MHI_GROUP_REC_OK, mhi_group_on_record(&g, host, payload, strlen(payload), now));
}

static void own_record(char* out) {
  TEST_ASSERT_TRUE(mhi_group_own_record(&g, 7, out, MHI_GROUP_RECORD_MAX + 1) > 0);
}

// 1. A lone unit, cold start: grace, record role 0, claim term 1 at 10 s, configs at 40 s.
static void test_scenario_1_lone_unit_cold_start(void) {
  boot("airco-slaapkamer", 0);
  run(100, 4900);
  TEST_ASSERT_EQUAL_UINT8(0, seen);  // 16. nothing in the grace period
  tick(5000);
  TEST_ASSERT_EQUAL_UINT8(MHI_GROUP_ACT_RECORD | MHI_GROUP_ACT_STATE, act.flags);
  TEST_ASSERT_EQUAL_UINT8(0, act.state);
  char rec[MHI_GROUP_RECORD_MAX + 1];
  own_record(rec);
  TEST_ASSERT_EQUAL_STRING("1;0;0;7;60;ac_outdoor;airco/me/", rec);
  reset_seen();
  run(5100, 4900);
  TEST_ASSERT_EQUAL_UINT8(0, seen);
  tick(10000);  // the settle clock started at 5 s
  TEST_ASSERT_EQUAL_UINT8(MHI_GROUP_ACT_RECORD | MHI_GROUP_ACT_STATE | MHI_GROUP_ACT_START, act.flags);
  TEST_ASSERT_EQUAL_UINT8(1, act.state);
  own_record(rec);
  TEST_ASSERT_EQUAL_STRING("1;1;1;7;60;ac_outdoor;airco/me/", rec);
  TEST_ASSERT_TRUE(mhi_group_may_publish_system(&g));
  reset_seen();
  run(10100, 30000);
  TEST_ASSERT_EQUAL_UINT32(40000, when(MHI_GROUP_ACT_CONFIGS));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_START));
}

// 2. A rebooted publisher resumes from its own record without a new term.
static void test_scenario_2_rebooted_publisher_resumes(void) {
  boot("airco-slaapkamer", 0);  // a reboot: role 0, term 0
  record("airco-slaapkamer", "1;1;3;900;60;ac_outdoor;airco/me/", 0);
  TEST_ASSERT_FALSE(mhi_group_may_publish_system(&g));  // still in the grace period
  run(100, 4900);
  TEST_ASSERT_EQUAL_UINT8(0, seen);
  tick(5000);
  TEST_ASSERT_EQUAL_UINT8(MHI_GROUP_ACT_RECORD | MHI_GROUP_ACT_STATE | MHI_GROUP_ACT_START, act.flags);
  TEST_ASSERT_EQUAL_UINT8(1, act.state);
  char rec[MHI_GROUP_RECORD_MAX + 1];
  own_record(rec);
  TEST_ASSERT_EQUAL_STRING("1;1;3;7;60;ac_outdoor;airco/me/", rec);
  reset_seen();
  run(5100, 30000);
  TEST_ASSERT_EQUAL_UINT32(35000, when(MHI_GROUP_ACT_CONFIGS));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_START));  // no claim: the term stays 3
  own_record(rec);
  TEST_ASSERT_EQUAL_STRING("1;1;3;7;60;ac_outdoor;airco/me/", rec);
}

// A member (Uitkijk) with a live publisher (Slaapkamer, term 1), both
// delivered right after the connect at t0; ticks up to t0 + 9.9 s.
static void member_with_publisher(uint32_t t0) {
  boot("airco-uitkijk", t0);
  record("airco-slaapkamer", "1;1;1;500;60;ac_outdoor;airco/slaapkamer/", t0);
  mhi_group_on_connected(&g, "airco-slaapkamer", true, t0);
  run(t0 + 100, 9900);
  TEST_ASSERT_EQUAL_UINT8(MHI_GROUP_ACT_RECORD | MHI_GROUP_ACT_STATE, seen);  // the record at 5 s, no claim
  TEST_ASSERT_EQUAL_UINT8(0, g.state);
  reset_seen();
}

// 3. A member waits 30 s after connected 0, settles 5 s and claims max+1; no claim at 34.9 s.
static void test_scenario_3_takeover_after_30_s_and_5_s(void) {
  member_with_publisher(0);
  run(10000, 10000);
  mhi_group_on_connected(&g, "airco-slaapkamer", false, 20000);
  run(20000, 35000);  // up to 54.9 s, 34.9 s after connected 0
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_START));
  tick(55000);
  TEST_ASSERT_EQUAL_UINT8(MHI_GROUP_ACT_RECORD | MHI_GROUP_ACT_STATE | MHI_GROUP_ACT_START, act.flags);
  char rec[MHI_GROUP_RECORD_MAX + 1];
  own_record(rec);
  TEST_ASSERT_EQUAL_STRING("1;1;2;7;60;ac_outdoor;airco/me/", rec);
}

// 4. connected 0 then 1 within 30 s: no takeover.
static void test_scenario_4_a_quick_reconnect_is_no_takeover(void) {
  member_with_publisher(0);
  run(10000, 10000);
  mhi_group_on_connected(&g, "airco-slaapkamer", false, 20000);
  run(20000, 10000);
  mhi_group_on_connected(&g, "airco-slaapkamer", true, 30000);
  run(30000, 100000);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_START));
}

// 5. A stale record (connected stays 1, no refresh for 3 periods) counts as gone.
static void test_scenario_5_a_stale_record_is_gone(void) {
  member_with_publisher(0);  // refreshed at 0: stale from 180 s, claim at 185 s
  run(10000, 175000);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_START));
  run(185000, 1000);
  TEST_ASSERT_EQUAL_UINT32(185000, when(MHI_GROUP_ACT_START));
}

// 6. The peer's own period, not this unit's, decides staleness.
static void test_scenario_6_the_peers_own_period_decides(void) {
  boot("airco-uitkijk", 0);                                                      // this unit: 60 s
  record("airco-slaapkamer", "1;1;1;500;300;ac_outdoor;airco/slaapkamer/", 0);  // the peer: 300 s
  mhi_group_on_connected(&g, "airco-slaapkamer", true, 0);
  run(100, 904900);  // fresh until 900 s, so no claim before 905 s
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_START));
  run(905000, 1000);
  TEST_ASSERT_EQUAL_UINT32(905000, when(MHI_GROUP_ACT_START));
}

// 7a. Simultaneous claims, equal terms: the higher hostname demotes and cancels its configs.
static void test_scenario_7_the_higher_hostname_yields(void) {
  boot("airco-uitkijk", 0);
  run(100, 10000);
  TEST_ASSERT_EQUAL_UINT32(10000, when(MHI_GROUP_ACT_START));  // claimed term 1
  record("airco-slaapkamer", "1;1;1;500;60;ac_outdoor;airco/slaapkamer/", 10050);
  reset_seen();
  tick(10100);
  TEST_ASSERT_EQUAL_UINT8(MHI_GROUP_ACT_RECORD | MHI_GROUP_ACT_DEMOTE | MHI_GROUP_ACT_STATE, act.flags);
  TEST_ASSERT_EQUAL_UINT8(0, act.state);
  TEST_ASSERT_FALSE(mhi_group_may_publish_system(&g));
  char rec[MHI_GROUP_RECORD_MAX + 1];
  own_record(rec);
  TEST_ASSERT_EQUAL_STRING("1;0;1;7;60;ac_outdoor;airco/me/", rec);
  run(10200, 60000);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_CONFIGS));  // the 40 s configs were cancelled
}

// 7b. ... and the lower hostname re-sends its configs 35 s after the loser's
// record: after its own configs (30 s after its claim) and after the loser's.
static void test_scenario_7_the_lower_hostname_resends(void) {
  boot("airco-slaapkamer", 0);
  run(100, 10000);
  TEST_ASSERT_EQUAL_UINT32(10000, when(MHI_GROUP_ACT_START));
  record("airco-uitkijk", "1;1;1;500;60;ac_outdoor;airco/uitkijk/", 10050);
  reset_seen();
  run(10100, 30000);  // up to 40.0 s
  TEST_ASSERT_EQUAL_UINT32(40000, when(MHI_GROUP_ACT_CONFIGS));  // the claim's own configs
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_DEMOTE));
  reset_seen();
  run(40100, 4900);  // up to 44.9 s
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_CONFIGS));
  run(45000, 1000);  // due at 45.05 s: the first tick after it
  TEST_ASSERT_EQUAL_UINT32(45100, when(MHI_GROUP_ACT_CONFIGS));
  // The loser's record delivered again, unchanged: no second re-send.
  record("airco-uitkijk", "1;1;1;500;60;ac_outdoor;airco/uitkijk/", 46000);
  reset_seen();
  run(46000, 60000);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_CONFIGS));
}

// 8. The returning ex-publisher becomes a member and never gets a start or config action.
static void test_scenario_8_the_returning_ex_publisher_stays_a_member(void) {
  mhi_group_init(&g, "airco-slaapkamer", "ac_outdoor", "airco/me/", 60);
  g.role = 1;  // it still holds term 1: no reboot
  g.term = 1;
  g.max_term_seen = 1;
  mhi_group_connect(&g, 0);
  reset_seen();
  record("airco-slaapkamer", "1;1;1;500;60;ac_outdoor;airco/me/", 0);
  record("airco-uitkijk", "1;1;2;800;60;ac_outdoor;airco/uitkijk/", 0);
  mhi_group_on_connected(&g, "airco-uitkijk", true, 0);
  run(100, 120000);
  TEST_ASSERT_EQUAL_UINT32(5000, when(MHI_GROUP_ACT_RECORD));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_START));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_CONFIGS));
  TEST_ASSERT_EQUAL_UINT8(0, g.state);
  char rec[MHI_GROUP_RECORD_MAX + 1];
  own_record(rec);
  TEST_ASSERT_EQUAL_STRING("1;0;1;7;60;ac_outdoor;airco/me/", rec);
}

// 9. Three units: only the lowest alive candidate claims.
static void test_scenario_9_only_the_lowest_candidate_claims(void) {
  boot("airco-b", 0);
  record("airco-a", "1;0;0;500;60;ac_outdoor;airco/a/", 0);
  record("airco-c", "1;0;0;500;60;ac_outdoor;airco/c/", 0);
  mhi_group_on_connected(&g, "airco-a", true, 0);
  mhi_group_on_connected(&g, "airco-c", true, 0);
  run(100, 59900);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_START));  // a is lower
  record("airco-a", "1;1;1;510;60;ac_outdoor;airco/a/", 60000);     // a claims: an incumbent now
  run(60000, 60000);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_START));
  // The same group from a's side: it claims at 10 s.
  boot("airco-a", 0);
  record("airco-b", "1;0;0;500;60;ac_outdoor;airco/b/", 0);
  record("airco-c", "1;0;0;500;60;ac_outdoor;airco/c/", 0);
  mhi_group_on_connected(&g, "airco-b", true, 0);
  mhi_group_on_connected(&g, "airco-c", true, 0);
  run(100, 10900);
  TEST_ASSERT_EQUAL_UINT32(10000, when(MHI_GROUP_ACT_START));
}

// 10. An outdoor ID mismatch gives Group 2, never a claim, and demotes a publisher.
static void test_scenario_10_an_outdoor_id_mismatch(void) {
  boot("airco-b", 0);
  record("airco-a", "1;0;0;500;60;other_outdoor;airco/a/", 0);  // the leader: its ID is the group's
  mhi_group_on_connected(&g, "airco-a", true, 0);
  run(100, 60000);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_START));
  TEST_ASSERT_EQUAL_UINT8(2, g.state);
  // A publisher meets the mismatch after its grace period.
  boot("airco-b", 0);
  run(100, 10000);
  TEST_ASSERT_EQUAL_UINT32(10000, when(MHI_GROUP_ACT_START));
  record("airco-a", "1;0;0;500;60;other_outdoor;airco/a/", 20000);
  mhi_group_on_connected(&g, "airco-a", true, 20000);
  reset_seen();
  tick(20000);
  TEST_ASSERT_EQUAL_UINT8(MHI_GROUP_ACT_RECORD | MHI_GROUP_ACT_DEMOTE | MHI_GROUP_ACT_STATE, act.flags);
  TEST_ASSERT_EQUAL_UINT8(2, act.state);
  TEST_ASSERT_FALSE(mhi_group_may_publish_system(&g));
  run(20100, 60000);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_CONFIGS));
}

// 11. A foreign record from a lower hostname gives Group 3; from a higher one it has no effect.
static void test_scenario_11_a_foreign_record(void) {
  boot("airco-b", 0);
  record("airco-a", "2;anything", 0);
  run(100, 60000);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_START));
  TEST_ASSERT_EQUAL_UINT8(3, g.state);
  boot("airco-b", 0);
  record("airco-c", "2;anything", 0);
  run(100, 10900);
  TEST_ASSERT_EQUAL_UINT32(10000, when(MHI_GROUP_ACT_START));
  TEST_ASSERT_EQUAL_UINT8(1, g.state);
}

static const char kP7[] = "1;0;0;5;60;ac_outdoor;airco/p7/";

// This unit, airco-z, and six connected members airco-p1 .. airco-p6 in entries 0-5.
static void six_peers(void) {
  boot("airco-z", 0);
  char host[16], payload[64];
  for (int i = 1; i <= 6; i++) {
    snprintf(host, sizeof(host), "airco-p%d", i);
    snprintf(payload, sizeof(payload), "1;0;0;500;60;ac_outdoor;airco/p%d/", i);
    record(host, payload, 0);
    mhi_group_on_connected(&g, host, true, 0);
  }
  run(100, 900);  // subscribes the six connected topics
}

// 12. A full table replaces the entry gone the longest, otherwise ignores the new unit.
static void test_scenario_12_a_full_table(void) {
  six_peers();
  TEST_ASSERT_EQUAL(MHI_GROUP_REC_FULL, mhi_group_on_record(&g, "airco-p7", kP7, strlen(kP7), 1000));
  mhi_group_on_connected(&g, "airco-p3", false, 1000);  // gone from 31 s: the longest
  run(1000, 1000);
  mhi_group_on_connected(&g, "airco-p5", false, 2000);  // gone from 32 s
  run(2000, 38000);
  record("airco-p7", kP7, 40000);
  TEST_ASSERT_EQUAL_STRING("airco-p7", g.peers[2].host);  // p3's entry
  TEST_ASSERT_EQUAL_STRING("airco-p5", g.peers[4].host);
  tick(40000);  // p7's connected topic is subscribed; p3's stays until the next connect
  TEST_ASSERT_EQUAL_STRING("airco/p7/", act.subscribe);
}

// 13. A retained record delivered again is not a refresh.
static void test_scenario_13_a_retained_copy_is_no_refresh(void) {
  member_with_publisher(0);
  run(10000, 90000);
  record("airco-slaapkamer", "1;1;1;500;60;ac_outdoor;airco/slaapkamer/", 100000);  // the same payload
  run(100000, 85100);
  TEST_ASSERT_EQUAL_UINT32(185000, when(MHI_GROUP_ACT_START));  // stale 180 s after the first delivery
}

// 14. The table is cleared at connect, so a long own outage does not make peers stale.
static void test_scenario_14_the_table_is_cleared_at_connect(void) {
  member_with_publisher(0);
  // An hour offline, then the broker hands the same retained record over again.
  mhi_group_connect(&g, 3600000);
  reset_seen();
  record("airco-slaapkamer", "1;1;1;500;60;ac_outdoor;airco/slaapkamer/", 3600000);
  mhi_group_on_connected(&g, "airco-slaapkamer", true, 3600000);
  run(3600100, 120000);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_START));
}

// 15. Times around the millis() wrap: scenario 3 with the wrap between connected 0 and the claim.
static void test_scenario_15_the_millis_wrap(void) {
  const uint32_t t0 = 0xFFFFFFFFu - 25000u + 1u;  // t0 + 25 s wraps to 0
  member_with_publisher(t0);
  run(t0 + 10000, 10000);
  mhi_group_on_connected(&g, "airco-slaapkamer", false, t0 + 20000);
  run(t0 + 20000, 35000);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_START));
  tick(t0 + 55000);
  TEST_ASSERT_TRUE(act.flags & MHI_GROUP_ACT_START);
}

// 16. Nothing is published during the grace period, whatever arrives.
static void test_scenario_16_nothing_in_the_grace_period(void) {
  boot("airco-a", 0);
  record("airco-a", "1;1;4;900;60;ac_outdoor;airco/me/", 0);
  record("airco-b", "1;1;9;900;60;ac_outdoor;airco/b/", 0);
  mhi_group_on_connected(&g, "airco-b", true, 0);
  run(100, 4900);
  TEST_ASSERT_EQUAL_UINT8(0, seen);
  TEST_ASSERT_FALSE(mhi_group_may_publish_system(&g));
}

// 17. max_term_seen never goes down.
static void test_scenario_17_max_term_seen_never_goes_down(void) {
  boot("airco-a", 0);
  record("airco-b", "1;0;7;500;60;ac_outdoor;airco/b/", 0);
  record("airco-b", "1;0;2;510;60;ac_outdoor;airco/b/", 0);
  mhi_group_connect(&g, 1000);  // the table goes, the term stays
  reset_seen();
  run(1100, 10000);
  TEST_ASSERT_EQUAL_UINT32(11000, when(MHI_GROUP_ACT_START));
  char rec[MHI_GROUP_RECORD_MAX + 1];
  own_record(rec);
  TEST_ASSERT_EQUAL_STRING("1;1;8;7;60;ac_outdoor;airco/me/", rec);
}

// 18. Rule 5: the record goes out once per period of this unit, not less often.
static void test_scenario_18_the_record_goes_out_every_period(void) {
  boot("airco-slaapkamer", 0);
  run(100, 10000);  // the claim at 10 s sends the record
  TEST_ASSERT_EQUAL_UINT32(10000, when(MHI_GROUP_ACT_START));
  for (uint32_t due = 70000; due <= 190000; due += 60000) {
    reset_seen();
    run(due - 59900, 59900);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_RECORD));
    tick(due);
    TEST_ASSERT_TRUE(act.flags & MHI_GROUP_ACT_RECORD);
  }
}

// 19. The publisher comes back while a member settles, then leaves again: the
// settle clock starts over, and the claim comes 5 s after the second time it is gone.
static void test_scenario_19_an_incumbent_back_during_the_settle(void) {
  member_with_publisher(0);
  run(10000, 10000);
  mhi_group_on_connected(&g, "airco-slaapkamer", false, 20000);  // gone from 50 s: settling from 50 s
  run(20000, 32000);                                             // up to 51.9 s
  mhi_group_on_connected(&g, "airco-slaapkamer", true, 52000);   // back: an incumbent again
  run(52000, 1000);
  mhi_group_on_connected(&g, "airco-slaapkamer", false, 53000);  // gone again from 83 s
  run(53000, 35000);                                             // up to 87.9 s
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_START));
  tick(88000);
  TEST_ASSERT_TRUE(act.flags & MHI_GROUP_ACT_START);
}

// 20. The entry gone the longest is replaced wherever it is in the table.
static void test_scenario_20_the_longest_gone_entry_after_a_shorter_one(void) {
  six_peers();
  mhi_group_on_connected(&g, "airco-p5", false, 1000);  // gone from 31 s: the longest
  run(1000, 1000);
  mhi_group_on_connected(&g, "airco-p3", false, 2000);  // gone from 32 s, and in an earlier entry
  run(2000, 38000);
  record("airco-p7", kP7, 40000);
  TEST_ASSERT_EQUAL_STRING("airco-p3", g.peers[2].host);
  TEST_ASSERT_EQUAL_STRING("airco-p7", g.peers[4].host);  // p5's entry
}

// 21. A peer whose period is shorter than this unit's is stale after 3 of its own.
static void test_scenario_21_a_shorter_peer_period(void) {
  boot("airco-uitkijk", 0);                                                     // this unit: 60 s
  record("airco-slaapkamer", "1;1;1;500;20;ac_outdoor;airco/slaapkamer/", 0);  // the peer: 20 s
  mhi_group_on_connected(&g, "airco-slaapkamer", true, 0);
  run(100, 64900);  // stale from 60 s, so the claim comes at 65 s
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_START));
  tick(65000);
  TEST_ASSERT_TRUE(act.flags & MHI_GROUP_ACT_START);
}

// 22. A third unit with a higher term arrives while a re-send is pending: the
// demote cancels the re-send, so no configs.
static void test_scenario_22_a_demote_cancels_the_resend(void) {
  boot("airco-slaapkamer", 0);
  run(100, 10000);                                                           // claims term 1 at 10 s
  record("airco-uitkijk", "1;1;1;500;60;ac_outdoor;airco/uitkijk/", 10050);  // loses: the re-send is due at 45.05 s
  run(10100, 30000);                                                         // the claim's own configs at 40 s
  record("airco-zolder", "1;1;2;900;60;ac_outdoor;airco/zolder/", 41000);    // term 2 beats this unit
  reset_seen();
  tick(41000);
  TEST_ASSERT_TRUE(act.flags & MHI_GROUP_ACT_DEMOTE);
  run(41100, 60000);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_CONFIGS));
}

// 23. A reconnect while a re-send is pending: nothing pending from before the
// connect goes out; the configs come 30 s after the grace period.
static void test_scenario_23_a_reconnect_drops_what_was_pending(void) {
  boot("airco-slaapkamer", 0);
  run(100, 10000);                                                           // claims at 10 s: configs due at 40 s
  record("airco-uitkijk", "1;1;1;500;60;ac_outdoor;airco/uitkijk/", 10050);  // the re-send is due at 45.05 s
  run(10100, 9900);
  mhi_group_connect(&g, 20000);  // the broker connection dropped and came back
  reset_seen();
  record("airco-uitkijk", "1;1;1;500;60;ac_outdoor;airco/uitkijk/", 20000);  // retained, delivered again
  run(20000, 35000);  // up to 54.9 s
  TEST_ASSERT_EQUAL_UINT32(25000, when(MHI_GROUP_ACT_START));  // the publisher start after the grace period
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_CONFIGS));
  tick(55000);
  TEST_ASSERT_TRUE(act.flags & MHI_GROUP_ACT_CONFIGS);
}

// 24. A second connected 0 does not restart the 30 s: the takeover is 35 s after the first.
static void test_scenario_24_a_repeated_connected_0(void) {
  member_with_publisher(0);
  run(10000, 10000);
  mhi_group_on_connected(&g, "airco-slaapkamer", false, 20000);
  run(20000, 10000);
  mhi_group_on_connected(&g, "airco-slaapkamer", false, 30000);  // the retained 0 delivered again
  run(30000, 25000);                                             // up to 54.9 s
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_START));
  tick(55000);
  TEST_ASSERT_TRUE(act.flags & MHI_GROUP_ACT_START);
}

// 25. After a reboot, the unit's own record says role 0, term 5 (it was
// publisher 5, then a member): its next claim takes term 6.
static void test_scenario_25_the_own_records_term_counts_for_a_claim(void) {
  boot("airco-slaapkamer", 0);
  record("airco-slaapkamer", "1;0;5;900;60;ac_outdoor;airco/me/", 0);
  run(100, 10000);
  TEST_ASSERT_EQUAL_UINT32(10000, when(MHI_GROUP_ACT_START));
  char rec[MHI_GROUP_RECORD_MAX + 1];
  own_record(rec);
  TEST_ASSERT_EQUAL_STRING("1;1;6;7;60;ac_outdoor;airco/me/", rec);
}

// 26. At an equal term the unit keeps what it holds: demoted from term 3 while
// the broker was away, its retained record still says publisher 3.
static void test_scenario_26_an_own_record_at_the_same_term_is_not_adopted(void) {
  mhi_group_init(&g, "airco-slaapkamer", "ac_outdoor", "airco/me/", 60);
  g.role = 0;  // no reboot: role 0, term 3 in RAM
  g.term = 3;
  g.max_term_seen = 3;
  mhi_group_connect(&g, 0);
  reset_seen();
  record("airco-slaapkamer", "1;1;3;900;60;ac_outdoor;airco/me/", 0);
  run(100, 10000);
  TEST_ASSERT_EQUAL_UINT32(10000, when(MHI_GROUP_ACT_START));  // a new claim, not a publisher start at 5 s
  char rec[MHI_GROUP_RECORD_MAX + 1];
  own_record(rec);
  TEST_ASSERT_EQUAL_STRING("1;1;4;7;60;ac_outdoor;airco/me/", rec);
}

// 27. The leader is the lowest alive unit: a fresh record whose connected reads
// 0 does not lead, so its other outdoor ID does not stop this unit.
static void test_scenario_27_only_an_alive_peer_leads(void) {
  boot("airco-b", 0);
  record("airco-a", "1;0;0;500;60;other_outdoor;airco/a/", 0);
  mhi_group_on_connected(&g, "airco-a", false, 0);  // its will: a fresh record, not alive
  run(100, 10000);
  TEST_ASSERT_EQUAL_UINT32(10000, when(MHI_GROUP_ACT_START));
  TEST_ASSERT_EQUAL_UINT8(1, g.state);
}

// 28. A record that is never refreshed stays gone past the 49.7-day millis() wrap.
static void test_scenario_28_a_stale_entry_stays_gone_across_the_wrap(void) {
  boot("airco-b", 0);
  record("airco-a", "1;0;0;500;60;other_outdoor;airco/a/", 0);  // leads while alive: Group 2
  mhi_group_on_connected(&g, "airco-a", true, 0);
  run(100, 5000);
  TEST_ASSERT_EQUAL_UINT8(2, g.state);
  run(5100, 180000);  // stale from 180 s: this unit leads, and claims at 185 s
  TEST_ASSERT_EQUAL_UINT32(185000, when(MHI_GROUP_ACT_START));
  reset_seen();
  // Once a minute to 2^32 ms + 10 min. Without the latch, a's record would look
  // fresh for 180 s from 2^32 ms, lead with its other outdoor ID and demote this
  // unit; three of these ticks fall in those 180 s.
  uint64_t t = 186000;
  for (; t <= 0x100000000ull + 600000u; t += 60000) tick((uint32_t)t);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_DEMOTE));
  TEST_ASSERT_EQUAL_UINT8(1, g.state);
  // A changed record is a sign of life: a is fresh again, leads, and this unit steps down.
  record("airco-a", "1;0;0;501;60;other_outdoor;airco/a/", (uint32_t)t);
  tick((uint32_t)t);
  TEST_ASSERT_TRUE(act.flags & MHI_GROUP_ACT_DEMOTE);
  TEST_ASSERT_EQUAL_UINT8(2, g.state);
}

// 29. A peer whose connected has read 0 for 30 s stays gone past the wrap too,
// even while its record keeps changing.
static void test_scenario_29_a_down_entry_stays_gone_across_the_wrap(void) {
  boot("airco-b", 0);
  record("airco-a", "1;1;7;500;60;ac_outdoor;airco/a/", 0);  // a publisher, term 7
  mhi_group_on_connected(&g, "airco-a", false, 0);           // its will: gone from 30 s
  run(100, 35000);                                           // this unit claims term 8 at 35 s
  TEST_ASSERT_EQUAL_UINT32(35000, when(MHI_GROUP_ACT_START));
  reset_seen();
  // a's record says term 9 and changes every minute; its connected stays 0.
  // Ticks every 10 s, so some fall in the 30 s after the wrap.
  char payload[64];
  unsigned long uptime = 600;
  for (uint64_t t = 36000; t <= 0x100000000ull + 600000u; t += 10000) {
    if ((t - 36000) % 60000 == 0) {
      snprintf(payload, sizeof(payload), "1;1;9;%lu;60;ac_outdoor;airco/a/", uptime++);
      record("airco-a", payload, (uint32_t)t);
    }
    tick((uint32_t)t);
  }
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_DEMOTE));
}

// 30. A losing claim seen during the grace period arms no re-send: the
// publisher start's configs, 30 s after the grace period, come after it anyway.
static void test_scenario_30_no_resend_armed_in_the_grace_period(void) {
  mhi_group_init(&g, "airco-slaapkamer", "ac_outdoor", "airco/me/", 60);
  g.role = 1;  // a publisher that reconnects without a reboot
  g.term = 1;
  g.max_term_seen = 1;
  mhi_group_connect(&g, 0);
  reset_seen();
  run(100, 1900);
  record("airco-uitkijk", "1;1;1;500;60;ac_outdoor;airco/uitkijk/", 2000);  // loses to this unit
  run(2000, 33100);  // up to 35.0 s
  TEST_ASSERT_EQUAL_UINT32(5000, when(MHI_GROUP_ACT_START));
  TEST_ASSERT_EQUAL_UINT32(35000, when(MHI_GROUP_ACT_CONFIGS));
  reset_seen();
  run(35100, 60000);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_CONFIGS));  // none at 37 s
}

// --- the plumbing the glue relies on ------------------------------------------

static void test_connected_topics_are_subscribed_from_the_tick_even_in_the_grace_period(void) {
  boot("airco-uitkijk", 0);
  record("airco-slaapkamer", "1;0;1;500;60;ac_outdoor;airco/slaapkamer/", 0);
  record("airco-x", "2;foreign", 0);
  tick(100);
  TEST_ASSERT_EQUAL_STRING("airco/slaapkamer/", act.subscribe);
  tick(200);
  TEST_ASSERT_EQUAL_STRING("", act.subscribe);  // once, and never for the foreign record
  // A new prefix: the new topic is subscribed, the old one is left to the next connect.
  record("airco-slaapkamer", "1;0;1;510;60;ac_outdoor;airco/bedroom/", 300);
  tick(300);
  TEST_ASSERT_EQUAL_STRING("airco/bedroom/", act.subscribe);
  // A message on the old topic now matches no peer: the glue ignores it.
  TEST_ASSERT_NULL(mhi_group_host_of_connected_topic(&g, "airco/slaapkamer/connected", "connected"));
  // A deleted record: nothing to subscribe.
  record("airco-slaapkamer", "", 400);
  tick(400);
  TEST_ASSERT_EQUAL_STRING("", act.subscribe);
  TEST_ASSERT_FALSE(g.peers[0].used);
}

static void test_a_connected_topic_maps_to_its_peer(void) {
  boot("airco-uitkijk", 0);
  record("airco-slaapkamer", "1;0;1;500;60;ac_outdoor;airco/slaapkamer/", 0);
  record("airco-x", "2;foreign", 0);
  TEST_ASSERT_EQUAL_STRING("airco-slaapkamer", mhi_group_host_of_connected_topic(&g, "airco/slaapkamer/connected", "connected"));
  TEST_ASSERT_NULL(mhi_group_host_of_connected_topic(&g, "airco/slaapkamer/connectedx", "connected"));
  TEST_ASSERT_NULL(mhi_group_host_of_connected_topic(&g, "airco/other/connected", "connected"));
}

static void test_own_record_after_the_grace_period_is_ignored(void) {
  boot("airco-a", 0);
  run(100, 5000);
  record("airco-a", "1;1;9;900;60;ac_outdoor;airco/me/", 6000);  // the unit's own echo, or a stranger with its name
  char rec[MHI_GROUP_RECORD_MAX + 1];
  own_record(rec);
  TEST_ASSERT_EQUAL_STRING("1;0;0;7;60;ac_outdoor;airco/me/", rec);
}

static void test_invalid_records_and_hosts_are_reported(void) {
  boot("airco-a", 0);
  TEST_ASSERT_EQUAL(MHI_GROUP_REC_INVALID, mhi_group_on_record(&g, "airco-b", "1;0", 3, 100));
  TEST_ASSERT_EQUAL(MHI_GROUP_REC_INVALID, mhi_group_on_record(&g, "a+b", "1;0;0;5;60;ac_outdoor;airco/b/", 30, 100));
  // The glue's 141-byte copy of a longer payload: the length says it was cut.
  TEST_ASSERT_EQUAL(MHI_GROUP_REC_INVALID, mhi_group_on_record(&g, "airco-b", "1;0;0;5;60;ac_outdoor;airco/b/", 150, 100));
}

static void test_the_group_state_fits_in_modest_ram(void) {
  printf("  sizeof(MhiGroup) = %u bytes, sizeof(MhiGroupActions) = %u bytes\n", (unsigned)sizeof(MhiGroup),
         (unsigned)sizeof(MhiGroupActions));
  TEST_ASSERT_LESS_OR_EQUAL_size_t(1300, sizeof(MhiGroup));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_record_round_trips);
  RUN_TEST(test_the_longest_valid_record_is_139_bytes);
  RUN_TEST(test_every_invalid_form_is_rejected);
  RUN_TEST(test_too_long_is_rejected);
  RUN_TEST(test_a_foreign_record_is_told_by_its_first_field);
  RUN_TEST(test_an_empty_payload_is_a_deleted_record);
  RUN_TEST(test_hostnames);
  RUN_TEST(test_the_compile_time_rules);
  RUN_TEST(test_the_record_rules_for_the_id_and_the_prefix);
  RUN_TEST(test_the_default_outdoor_id);
  RUN_TEST(test_scenario_1_lone_unit_cold_start);
  RUN_TEST(test_scenario_2_rebooted_publisher_resumes);
  RUN_TEST(test_scenario_3_takeover_after_30_s_and_5_s);
  RUN_TEST(test_scenario_4_a_quick_reconnect_is_no_takeover);
  RUN_TEST(test_scenario_5_a_stale_record_is_gone);
  RUN_TEST(test_scenario_6_the_peers_own_period_decides);
  RUN_TEST(test_scenario_7_the_higher_hostname_yields);
  RUN_TEST(test_scenario_7_the_lower_hostname_resends);
  RUN_TEST(test_scenario_8_the_returning_ex_publisher_stays_a_member);
  RUN_TEST(test_scenario_9_only_the_lowest_candidate_claims);
  RUN_TEST(test_scenario_10_an_outdoor_id_mismatch);
  RUN_TEST(test_scenario_11_a_foreign_record);
  RUN_TEST(test_scenario_12_a_full_table);
  RUN_TEST(test_scenario_13_a_retained_copy_is_no_refresh);
  RUN_TEST(test_scenario_14_the_table_is_cleared_at_connect);
  RUN_TEST(test_scenario_15_the_millis_wrap);
  RUN_TEST(test_scenario_16_nothing_in_the_grace_period);
  RUN_TEST(test_scenario_17_max_term_seen_never_goes_down);
  RUN_TEST(test_scenario_18_the_record_goes_out_every_period);
  RUN_TEST(test_scenario_19_an_incumbent_back_during_the_settle);
  RUN_TEST(test_scenario_20_the_longest_gone_entry_after_a_shorter_one);
  RUN_TEST(test_scenario_21_a_shorter_peer_period);
  RUN_TEST(test_scenario_22_a_demote_cancels_the_resend);
  RUN_TEST(test_scenario_23_a_reconnect_drops_what_was_pending);
  RUN_TEST(test_scenario_24_a_repeated_connected_0);
  RUN_TEST(test_scenario_25_the_own_records_term_counts_for_a_claim);
  RUN_TEST(test_scenario_26_an_own_record_at_the_same_term_is_not_adopted);
  RUN_TEST(test_scenario_27_only_an_alive_peer_leads);
  RUN_TEST(test_scenario_28_a_stale_entry_stays_gone_across_the_wrap);
  RUN_TEST(test_scenario_29_a_down_entry_stays_gone_across_the_wrap);
  RUN_TEST(test_scenario_30_no_resend_armed_in_the_grace_period);
  RUN_TEST(test_connected_topics_are_subscribed_from_the_tick_even_in_the_grace_period);
  RUN_TEST(test_a_connected_topic_maps_to_its_peer);
  RUN_TEST(test_own_record_after_the_grace_period_is_ignored);
  RUN_TEST(test_invalid_records_and_hosts_are_reported);
  RUN_TEST(test_the_group_state_fits_in_modest_ram);
  return UNITY_END();
}
```

- [ ] **Step 2: Run them and see them fail**

Run: `pio test -e native -f test_mhi_group`
Expected: build errors: `'MhiGroup' does not name a type`, `'MhiGroupActions' does not name a type`, and `'mhi_group_tick' was not declared in this scope`.

- [ ] **Step 3: Implement**

In `lib/mhi_pure/mhi_group.h`, append after the last declaration:

Replace (`lib/mhi_pure/mhi_group.h`, line 133):

```cpp
size_t mhi_group_default_outdoor_id(const char* group_root, char* out, size_t out_len);
```

with:

```cpp
size_t mhi_group_default_outdoor_id(const char* group_root, char* out, size_t out_len);

// --- the election (spec §5.2-§6.3) ---------------------------------------------

#define MHI_GROUP_MAX_PEERS 6
#define MHI_GROUP_GRACE_MS 5000        // after a connect: only collect (§6.1)
#define MHI_GROUP_SETTLE_MS 5000       // no incumbent for this long before a claim (§6.2 rule 4)
#define MHI_GROUP_DOWN_GONE_MS 30000   // connected 0 for this long: gone (§5.3)
#define MHI_GROUP_CONFIGS_MS 30000     // claim or publisher start -> outdoor configs (§6.3)
#define MHI_GROUP_RESEND_MS 35000      // a beaten claim -> the configs again, after the loser's (§6.2 rule 3)

enum MhiGroupKind : uint8_t { MHI_GROUP_KIND_MEMBER, MHI_GROUP_KIND_FOREIGN };
enum MhiGroupLink : uint8_t { MHI_GROUP_LINK_UNKNOWN, MHI_GROUP_LINK_UP, MHI_GROUP_LINK_DOWN };

struct MhiGroupPeer {
  bool used;
  bool subscribed;           // the glue subscribed <rec.prefix><TOPIC_CONNECTED>
  MhiGroupKind kind;
  MhiGroupLink connected;    // the peer's <prefix><TOPIC_CONNECTED>
  bool stale;                // latched by the tick: no refresh for 3 periods, until the payload changes
  bool down_gone;            // latched by the tick: connected 0 for 30 s, until connected 1 or another prefix
  char host[MHI_GROUP_HOST_MAX + 1];
  MhiGroupRecord rec;        // foreign: rec.proto only
  uint32_t hash;             // of the whole payload: a change is a refresh
  uint32_t refreshed_ms;     // first seen, or when the payload last changed
  uint32_t down_since_ms;    // while connected is MHI_GROUP_LINK_DOWN
};

struct MhiGroup {
  // This unit, from mhi_group_init().
  char host[MHI_GROUP_HOST_MAX + 1];
  char outdoor_id[MHI_GROUP_ID_MAX + 1];
  char prefix[MHI_GROUP_ROOT_MAX + 1];
  uint32_t period;          // TELEMETRY_PERIOD, s
  // Kept across connects.
  uint8_t role;             // 0 member, 1 publisher; 0 at boot
  uint32_t term;            // 0 at boot
  uint32_t max_term_seen;   // never decreases (§5.3)
  bool connected;           // mhi_group_connect() has run at least once
  // Reset at every connect.
  bool in_grace;
  uint32_t connect_ms;
  uint8_t state;            // the Group number as of the last tick
  uint8_t published_state;  // the Group number last asked for; 0xff: none since the connect
  uint32_t record_ms;       // when the record was last asked for
  bool settling;
  uint32_t settle_ms;
  bool configs_pending;
  uint32_t configs_ms;      // when the 30 s before the configs started
  bool resend_pending;
  uint32_t resend_ms;       // when the 5 s before the re-send started
  MhiGroupPeer peers[MHI_GROUP_MAX_PEERS];
};

enum : uint8_t {
  MHI_GROUP_ACT_RECORD = 0x01,   // publish this unit's record, retained, on <GROUP_ROOT>members/<HOSTNAME>
  MHI_GROUP_ACT_STATE = 0x02,    // publish MhiGroupActions.state, retained, on <MQTT_PREFIX><TOPIC_GROUP>
  MHI_GROUP_ACT_START = 0x04,    // call reset_system_values(): a claim or a publisher start
  MHI_GROUP_ACT_DEMOTE = 0x08,   // call discovery_cancel_outdoor()
  MHI_GROUP_ACT_CONFIGS = 0x10,  // call discovery_start_outdoor()
};

// What one tick asks the glue to do, in this order: subscribe, then the flags
// in the order they are listed above, DEMOTE first. There is no unsubscribe:
// an old peer topic goes at the next connect (clean session), and a message on
// it matches no peer (spec §6.1 step 2).
struct MhiGroupActions {
  uint8_t flags;
  uint8_t state;                            // with MHI_GROUP_ACT_STATE: 0 member, 1 publisher, 2 ID mismatch, 3 version mismatch
  char subscribe[MHI_GROUP_ROOT_MAX + 1];   // "" or a prefix: subscribe <prefix><TOPIC_CONNECTED>
};

enum MhiGroupResult : uint8_t {
  MHI_GROUP_REC_OK,       // taken, or deliberately ignored (this unit's own record after the grace period)
  MHI_GROUP_REC_INVALID,  // ignored: say so with one Serial line
  MHI_GROUP_REC_FULL,     // a new unit and no gone entry to replace: say so with one Serial line
};

// At boot: role 0, term 0. The texts are this unit's HOSTNAME, outdoor ID and
// MQTT_PREFIX; support.h checks them at compile time.
void mhi_group_init(MhiGroup* g, const char* host, const char* outdoor_id, const char* prefix, uint32_t period_s);

// After every MQTT connect: clears the peer table and starts the grace period.
void mhi_group_connect(MhiGroup* g, uint32_t now);

// A message on <GROUP_ROOT>members/<host>. payload is NUL-terminated; len is
// the length the broker sent, so a payload the glue's copy truncated is refused.
MhiGroupResult mhi_group_on_record(MhiGroup* g, const char* host, const char* payload, size_t len, uint32_t now);

// A peer's connected topic said 1 (up) or 0 (down).
void mhi_group_on_connected(MhiGroup* g, const char* host, bool up, uint32_t now);

// The member peer whose <prefix><t_connected> is topic, or NULL.
const char* mhi_group_host_of_connected_topic(const MhiGroup* g, const char* topic, const char* t_connected);

// Every loop() pass while connected: the rules of §6.1-§6.3.
void mhi_group_tick(MhiGroup* g, uint32_t now, MhiGroupActions* act);

// The system-value gate (§6.4): past the grace period, role 1 and state 1.
bool mhi_group_may_publish_system(const MhiGroup* g);

// This unit's record with the given uptime. Returns the length, 0 when it does not fit.
size_t mhi_group_own_record(const MhiGroup* g, uint32_t uptime_s, char* out, size_t out_len);
```

Append to the end of `lib/mhi_pure/mhi_group.cpp`, after a blank line:

```cpp
// --- the peer table and the definitions of §5.3 --------------------------------

// At most size - 1 characters and a NUL, never a read past src's NUL.
static void copy_bounded(char* dst, size_t size, const char* src) {
  size_t n = 0;
  while (n + 1 < size && src[n] != '\0') n++;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

// FNV-1a: tells a changed payload from a retained copy delivered again.
static uint32_t payload_hash(const char* p, size_t len) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < len; i++) {
    h ^= (uint8_t)p[i];
    h *= 16777619u;
  }
  return h;
}

static uint32_t stale_ms(const MhiGroup* g, const MhiGroupPeer* p) {
  // The peer's own period; a foreign record's is unknown, so this unit's.
  const uint32_t period = p->kind == MHI_GROUP_KIND_MEMBER ? p->rec.period : g->period;
  return 3u * period * 1000u;
}

// The latch keeps an entry that was never refreshed from turning fresh again
// when now - refreshed_ms wraps after 49.7 days; likewise down_gone.
static bool fresh(const MhiGroup* g, const MhiGroupPeer* p, uint32_t now) {
  return !p->stale && now - p->refreshed_ms < stale_ms(g, p);
}

static bool alive(const MhiGroup* g, const MhiGroupPeer* p, uint32_t now) {
  if (!fresh(g, p, now)) return false;
  return p->kind == MHI_GROUP_KIND_FOREIGN || p->connected == MHI_GROUP_LINK_UP;
}

static bool down_gone(const MhiGroupPeer* p, uint32_t now) {
  return p->down_gone || (p->connected == MHI_GROUP_LINK_DOWN && now - p->down_since_ms >= MHI_GROUP_DOWN_GONE_MS);
}

static bool gone(const MhiGroup* g, const MhiGroupPeer* p, uint32_t now) {
  return !fresh(g, p, now) || down_gone(p, now);
}

// How long a gone peer has been gone: since its record went stale or since its
// connected 0 turned into gone, whichever came first. Past the 49.7-day wrap a
// latched entry's age is arbitrary; it is still gone, so it may be replaced.
static uint32_t gone_for(const MhiGroup* g, const MhiGroupPeer* p, uint32_t now) {
  uint32_t age = 0;
  if (!fresh(g, p, now)) age = now - p->refreshed_ms - stale_ms(g, p);
  if (down_gone(p, now)) {
    const uint32_t down = now - p->down_since_ms - MHI_GROUP_DOWN_GONE_MS;
    if (down > age) age = down;
  }
  return age;
}

static int find_peer(const MhiGroup* g, const char* host) {
  for (int i = 0; i < MHI_GROUP_MAX_PEERS; i++)
    if (g->peers[i].used && strcmp(g->peers[i].host, host) == 0) return i;
  return -1;
}

// A free entry, else the one gone the longest, else -1 (spec §5.2).
static int slot_for_new_peer(const MhiGroup* g, uint32_t now) {
  int best = -1;
  uint32_t best_age = 0;
  for (int i = 0; i < MHI_GROUP_MAX_PEERS; i++) {
    const MhiGroupPeer* p = &g->peers[i];
    if (!p->used) return i;
    if (!gone(g, p, now)) continue;
    const uint32_t age = gone_for(g, p, now);
    if (best < 0 || age > best_age) {
      best = i;
      best_age = age;
    }
  }
  return best;
}

// The lowest hostname among this unit and every alive peer; NULL is this unit.
static const MhiGroupPeer* leader(const MhiGroup* g, uint32_t now) {
  const MhiGroupPeer* best = NULL;
  const char* best_host = g->host;
  for (int i = 0; i < MHI_GROUP_MAX_PEERS; i++) {
    const MhiGroupPeer* p = &g->peers[i];
    if (p->used && alive(g, p, now) && strcmp(p->host, best_host) < 0) {
      best = p;
      best_host = p->host;
    }
  }
  return best;
}

// The group's outdoor ID: the leader's. NULL when the leader is foreign, whose
// record says nothing but its version.
static const char* group_outdoor_id(const MhiGroup* g, uint32_t now) {
  const MhiGroupPeer* l = leader(g, now);
  if (l == NULL) return g->outdoor_id;
  return l->kind == MHI_GROUP_KIND_MEMBER ? l->rec.outdoor_id : NULL;
}

static uint8_t compute_state(const MhiGroup* g, uint32_t now) {
  const MhiGroupPeer* l = leader(g, now);
  if (l != NULL && l->kind == MHI_GROUP_KIND_FOREIGN) return 3;
  if (l != NULL && strcmp(l->rec.outdoor_id, g->outdoor_id) != 0) return 2;
  return g->role;
}

static bool compatible(const MhiGroup* g, const MhiGroupPeer* p, uint32_t now) {
  const char* id = group_outdoor_id(g, now);
  return p->used && p->kind == MHI_GROUP_KIND_MEMBER && id != NULL && strcmp(p->rec.outdoor_id, id) == 0;
}

// a beats b: a higher term, or the same term and a lower hostname.
static bool beats(uint32_t a_term, const char* a_host, uint32_t b_term, const char* b_host) {
  return a_term > b_term || (a_term == b_term && strcmp(a_host, b_host) < 0);
}

static bool peer_incumbent(const MhiGroup* g, const MhiGroupPeer* p, uint32_t now) {
  return compatible(g, p, now) && p->rec.role == 1 && !gone(g, p, now);
}

static bool any_incumbent(const MhiGroup* g, uint32_t now, uint8_t state) {
  if (g->role == 1 && state != 2 && state != 3) return true;
  for (int i = 0; i < MHI_GROUP_MAX_PEERS; i++)
    if (peer_incumbent(g, &g->peers[i], now)) return true;
  return false;
}

static bool an_incumbent_beats_this_unit(const MhiGroup* g, uint32_t now) {
  for (int i = 0; i < MHI_GROUP_MAX_PEERS; i++) {
    const MhiGroupPeer* p = &g->peers[i];
    if (peer_incumbent(g, p, now) && beats(p->rec.term, p->host, g->term, g->host)) return true;
  }
  return false;
}

// This unit has the lowest hostname among the candidates (it is one: rule 4
// runs only at state 0).
static bool lowest_candidate(const MhiGroup* g, uint32_t now) {
  for (int i = 0; i < MHI_GROUP_MAX_PEERS; i++) {
    const MhiGroupPeer* p = &g->peers[i];
    if (compatible(g, p, now) && alive(g, p, now) && strcmp(p->host, g->host) < 0) return false;
  }
  return true;
}

static void note_term(MhiGroup* g, uint32_t term) {
  if (term > g->max_term_seen) g->max_term_seen = term;
}

// --- entry points ---------------------------------------------------------------

void mhi_group_init(MhiGroup* g, const char* host, const char* outdoor_id, const char* prefix, uint32_t period_s) {
  memset(g, 0, sizeof(*g));
  copy_bounded(g->host, sizeof(g->host), host);
  copy_bounded(g->outdoor_id, sizeof(g->outdoor_id), outdoor_id);
  copy_bounded(g->prefix, sizeof(g->prefix), prefix);
  g->period = period_s;
  g->published_state = 0xff;
}

void mhi_group_connect(MhiGroup* g, uint32_t now) {
  // The broker hands every retained record and connected topic over again
  // within the grace period, and every staleness clock starts now (§5.2). A
  // clean session also dropped every peer subscription.
  memset(g->peers, 0, sizeof(g->peers));
  g->connected = true;
  g->in_grace = true;
  g->connect_ms = now;
  g->state = 0;
  g->published_state = 0xff;
  g->record_ms = now;
  g->settling = false;
  g->configs_pending = false;
  g->resend_pending = false;
}

MhiGroupResult mhi_group_on_record(MhiGroup* g, const char* host, const char* payload, size_t len, uint32_t now) {
  if (!mhi_group_host_valid(host) || payload == NULL || strlen(payload) != len) return MHI_GROUP_REC_INVALID;
  MhiGroupRecord rec = {};
  const MhiGroupParse parsed = mhi_group_parse_record(payload, len, &rec);

  if (strcmp(host, g->host) == 0) {
    // This unit's own record, read only in the grace period (§6.1 step 3):
    // after a reboot it is what lets a publisher resume without a handover.
    if (!g->in_grace) return MHI_GROUP_REC_OK;
    if (parsed == MHI_GROUP_PARSE_INVALID) return MHI_GROUP_REC_INVALID;
    if (parsed != MHI_GROUP_PARSE_MEMBER) return MHI_GROUP_REC_OK;
    note_term(g, rec.term);
    if (rec.term > g->term) {
      g->role = rec.role;
      g->term = rec.term;
    }
    return MHI_GROUP_REC_OK;
  }

  int i = find_peer(g, host);
  if (parsed == MHI_GROUP_PARSE_EMPTY) {
    if (i >= 0) g->peers[i].used = false;
    return MHI_GROUP_REC_OK;
  }
  if (parsed == MHI_GROUP_PARSE_INVALID) return MHI_GROUP_REC_INVALID;
  if (parsed == MHI_GROUP_PARSE_MEMBER) note_term(g, rec.term);

  const MhiGroupKind kind = parsed == MHI_GROUP_PARSE_MEMBER ? MHI_GROUP_KIND_MEMBER : MHI_GROUP_KIND_FOREIGN;
  const uint32_t hash = payload_hash(payload, len);
  bool changed;
  if (i < 0) {
    i = slot_for_new_peer(g, now);
    if (i < 0) return MHI_GROUP_REC_FULL;
    MhiGroupPeer* p = &g->peers[i];
    p->used = true;
    p->subscribed = false;
    copy_bounded(p->host, sizeof(p->host), host);
    p->connected = MHI_GROUP_LINK_UNKNOWN;
    p->down_gone = false;
    changed = true;
  }
  else {
    const MhiGroupPeer* p = &g->peers[i];
    changed = p->hash != hash;
    // Another connected topic, or none: what was known about the old one no
    // longer holds, and the new one is subscribed at the next tick.
    if (p->kind != kind || (kind == MHI_GROUP_KIND_MEMBER && strcmp(p->rec.prefix, rec.prefix) != 0)) {
      g->peers[i].connected = MHI_GROUP_LINK_UNKNOWN;
      g->peers[i].subscribed = false;
      g->peers[i].down_gone = false;
    }
  }
  MhiGroupPeer* p = &g->peers[i];
  p->kind = kind;
  p->rec = rec;
  if (changed) {  // a retained copy delivered again is not a refresh (§5.2)
    p->hash = hash;
    p->refreshed_ms = now;
    p->stale = false;
  }

  // §6.2 rule 3: a compatible peer claims the role this unit holds and wins.
  if (changed && !g->in_grace && g->role == 1 && kind == MHI_GROUP_KIND_MEMBER && rec.role == 1 &&
      compatible(g, p, now) && beats(g->term, g->host, rec.term, p->host)) {
    g->resend_pending = true;
    g->resend_ms = now;
  }
  return MHI_GROUP_REC_OK;
}

void mhi_group_on_connected(MhiGroup* g, const char* host, bool up, uint32_t now) {
  const int i = find_peer(g, host);
  if (i < 0) return;
  MhiGroupPeer* p = &g->peers[i];
  if (up) {
    p->connected = MHI_GROUP_LINK_UP;
    p->down_gone = false;
  }
  else if (p->connected != MHI_GROUP_LINK_DOWN) {  // a repeated 0 does not restart the clock
    p->connected = MHI_GROUP_LINK_DOWN;
    p->down_since_ms = now;
  }
}

const char* mhi_group_host_of_connected_topic(const MhiGroup* g, const char* topic, const char* t_connected) {
  for (int i = 0; i < MHI_GROUP_MAX_PEERS; i++) {
    const MhiGroupPeer* p = &g->peers[i];
    if (!p->used || p->kind != MHI_GROUP_KIND_MEMBER) continue;
    const size_t n = strlen(p->rec.prefix);
    if (strncmp(topic, p->rec.prefix, n) == 0 && strcmp(topic + n, t_connected) == 0) return p->host;
  }
  return NULL;
}

// One subscription per tick: the glue subscribes from loop(), never inside the
// MQTT callback (§6.1 step 2). An old prefix is never unsubscribed: that
// subscription goes at the next connect, and a message on it matches no peer.
static void next_subscription(MhiGroup* g, MhiGroupActions* act) {
  for (int i = 0; i < MHI_GROUP_MAX_PEERS; i++) {
    MhiGroupPeer* p = &g->peers[i];
    if (!p->used || p->kind != MHI_GROUP_KIND_MEMBER || p->subscribed) continue;
    copy_bounded(act->subscribe, sizeof(act->subscribe), p->rec.prefix);
    p->subscribed = true;
    return;
  }
}

static void demote(MhiGroup* g, uint32_t now, MhiGroupActions* act) {
  g->role = 0;
  g->configs_pending = false;
  g->resend_pending = false;
  g->record_ms = now;
  act->flags |= MHI_GROUP_ACT_RECORD | MHI_GROUP_ACT_DEMOTE;
}

static void claim(MhiGroup* g, uint32_t now, MhiGroupActions* act) {
  g->role = 1;
  // Saturates rather than wrapping to the invalid term 0.
  g->term = g->max_term_seen == UINT32_MAX ? UINT32_MAX : g->max_term_seen + 1;
  note_term(g, g->term);
  g->settling = false;
  g->record_ms = now;
  g->configs_pending = true;
  g->configs_ms = now;
  act->flags |= MHI_GROUP_ACT_RECORD | MHI_GROUP_ACT_START;
}

// §6.2 rules 1, 2 and 4. Rule 3 is in mhi_group_on_record, rule 5 in the tick.
static void apply_rules(MhiGroup* g, uint32_t now, MhiGroupActions* act) {
  uint8_t state = compute_state(g, now);
  if (g->role == 1 && (state == 2 || state == 3 || an_incumbent_beats_this_unit(g, now))) {
    demote(g, now, act);
    state = compute_state(g, now);
  }
  if (g->role != 0 || state != 0 || any_incumbent(g, now, state)) {
    g->settling = false;  // nothing ever preempts a live incumbent
    return;
  }
  if (!g->settling) {
    g->settling = true;
    g->settle_ms = now;
  }
  else if (now - g->settle_ms >= MHI_GROUP_SETTLE_MS && lowest_candidate(g, now)) {
    claim(g, now, act);
  }
}

// Every tick, so well within 49.7 days: what has gone stays gone until the
// peer shows life again (a changed record, or connected 1).
static void latch_gone(MhiGroup* g, uint32_t now) {
  for (int i = 0; i < MHI_GROUP_MAX_PEERS; i++) {
    MhiGroupPeer* p = &g->peers[i];
    if (!p->used) continue;
    if (!fresh(g, p, now)) p->stale = true;
    if (down_gone(p, now)) p->down_gone = true;
  }
}

void mhi_group_tick(MhiGroup* g, uint32_t now, MhiGroupActions* act) {
  act->flags = 0;
  act->state = 0;
  act->subscribe[0] = '\0';
  if (!g->connected) return;
  latch_gone(g, now);
  next_subscription(g, act);
  if (g->in_grace) {
    if (now - g->connect_ms < MHI_GROUP_GRACE_MS) return;  // only collect (§6.1 step 4)
    // §6.1 step 5: the rules once, then the record and the Group, then the
    // publisher start when the role survived.
    g->in_grace = false;
    apply_rules(g, now, act);
    act->flags |= MHI_GROUP_ACT_RECORD;
    g->record_ms = now;
    if (g->role == 1) {
      act->flags |= MHI_GROUP_ACT_START;
      g->configs_pending = true;
      g->configs_ms = now;
    }
  }
  else {
    apply_rules(g, now, act);
    if (!(act->flags & MHI_GROUP_ACT_RECORD) && now - g->record_ms >= g->period * 1000u) {  // rule 5
      act->flags |= MHI_GROUP_ACT_RECORD;
      g->record_ms = now;
    }
  }
  if (g->configs_pending && now - g->configs_ms >= MHI_GROUP_CONFIGS_MS) {
    g->configs_pending = false;
    act->flags |= MHI_GROUP_ACT_CONFIGS;
  }
  if (g->resend_pending && now - g->resend_ms >= MHI_GROUP_RESEND_MS) {
    g->resend_pending = false;
    act->flags |= MHI_GROUP_ACT_CONFIGS;
  }
  g->state = compute_state(g, now);
  if (g->state != g->published_state) {
    g->published_state = g->state;
    act->flags |= MHI_GROUP_ACT_STATE;
  }
  if (act->flags & MHI_GROUP_ACT_STATE) act->state = g->state;
}

bool mhi_group_may_publish_system(const MhiGroup* g) {
  return g->connected && !g->in_grace && g->role == 1 && g->state == 1;
}

size_t mhi_group_own_record(const MhiGroup* g, uint32_t uptime_s, char* out, size_t out_len) {
  MhiGroupRecord r;
  memset(&r, 0, sizeof(r));
  r.proto = MHI_GROUP_PROTO;
  r.role = g->role;
  r.term = g->term;
  r.uptime = uptime_s;
  r.period = g->period;
  copy_bounded(r.outdoor_id, sizeof(r.outdoor_id), g->outdoor_id);
  copy_bounded(r.prefix, sizeof(r.prefix), g->prefix);
  return mhi_group_format_record(&r, out, out_len);
}
```

- [ ] **Step 4: Run them and see them pass**

Run: `pio test -e native -f test_mhi_group -v`
Expected: `46 Tests 0 Failures 0 Ignored` and `OK`, and the line `  sizeof(MhiGroup) = 1252 bytes, sizeof(MhiGroupActions) = 67 bytes`. Those are the host's sizes; the ESP8266's differ by a few bytes at most.

- [ ] **Step 5: Run the whole suite**

Run: `pio test -e native`
Expected: `224 test cases: 224 succeeded` (178 before, +46), and `git status --porcelain -- test/fixtures` empty.

- [ ] **Step 6: Check that the tests catch a broken rule**

For each mutation, change the named line of `lib/mhi_pure/mhi_group.cpp` in a scratch copy of the whole tree and run `pio test -e native -f test_mhi_group`. Each one must fail the test named, or more. The trial run:

| mutation in `mhi_group.cpp` | fails |
|---|---|
| a full table replaces the first gone entry, not the one gone the longest | scenario 20 |
| staleness from the longer of the two periods | scenario 21 |
| rule 3 also arms during the grace period | scenario 30 |
| an incumbent does not stop the settle clock | scenario 19 |
| a repeated `connected` 0 restarts the 30 s | scenario 24 |
| the own record's term not noted in `max_term_seen` | scenario 25 |
| the own record adopted at an equal term | scenario 26 |
| `connected` 0 gone after more than 30 s, not at 30 s | 5 tests, among them scenarios 3, 15 and 19 |
| no periodic record (rule 5) | scenario 18 |
| the periodic record every 3 periods | scenario 18 |
| rule 2 counts only incumbents whose `connected` reads 1 | scenarios 7a and 22 |
| the leader chosen among fresh peers, not alive ones | scenario 27 |
| an incumbent must be alive, not merely not gone | 8 tests, among them scenarios 3, 4 and 7a |
| a retained copy counts as a refresh | scenarios 7b and 13 |
| a foreign record's period taken as 1 s | scenario 11 |
| a demote keeps a pending re-send | scenario 22 |
| a connect keeps the pending configs and re-send | scenario 23 |
| an empty payload leaves the peer's entry | `test_connected_topics_are_subscribed_from_the_tick_even_in_the_grace_period` |
| a prefix change keeps the `subscribed` flag | the same test |
| a unit in state 2 still settles and claims | scenario 28 |
| no `stale` latch | scenario 28 |
| no `down_gone` latch | scenario 29 |
| a changed record does not clear `stale` | scenario 28 |
| `connected` 1 does not clear `down_gone` | scenario 19 |

One more mutation survives, and no test can catch it: `lowest_candidate()` counting every alive peer below this unit, compatible or not. Rule 4 runs only in state 0, where the leader is alive and compatible, so any alive peer below this unit means the leader is below it too, and the answer is the same.

Do not commit a mutation.

- [ ] **Step 7: Commit**

```bash
git add lib/mhi_pure/mhi_group.h lib/mhi_pure/mhi_group.cpp test/test_mhi_group/test_mhi_group.cpp
git commit -m "feat: the outdoor election as a pure state machine, with the 17 scenarios of spec §9 and 13 more (#22)

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013aKH8NAdtagPe21HRXJdt5"
```

---

### Task 3: #22 discovery: the Group role row, the group root, the retired energy row, the renderer and the context fill

**Files:**
- Modify: `lib/mhi_pure/mhi_discovery.h` (enum, struct, declarations): lines 37-46, 101, 105, 109-110, 133-137, 146-147 (each counted after the edits listed before it)
- Modify: `lib/mhi_pure/mhi_discovery.cpp` (tables, `mhi_discovery_is_outdoor_row`, `head`, `tail`, `op_state_topic`'s comment, `mhi_discovery_row_enabled`, `mhi_discovery_topic`, the build switch): lines 23-27, 83-90, 99-100, 109-110, 126-129, 152, 191-193, 205, 315-319, 330-332 (each counted after the edits listed before it)
- Test: `test/test_mhi_discovery/test_mhi_discovery.cpp`: lines 30-32, 49-56, 74-76, 93-100, 103-104, 116-117, 128, 208, 219-220, 371, 427-433, 444, 613-614 (each counted after the edits listed before it)
- Fixtures:
  - create `test/fixtures/discovery/group_role.txt` and `test/fixtures/discovery_all/group_role.txt`;
  - modify the six `test/fixtures/discovery_all/ou_{outdoor,ct,comp,defrost,comp_run,protection}.txt`;
  - delete `test/fixtures/discovery_all/ou_kwh.txt`.
- Modify: `tools/discovery_payloads.cpp`: lines 7, 13, 18-22, 34, 42, 78, 95-97, 104-106, 113, 152-163 (each counted after the edits listed before it)
- Modify: `src/support.h` (two blocks): lines 140-142, 177-179 (each counted after the edits listed before it)
- Modify: `src/MHI-AC-Ctrl.h` (`TOPIC_GROUP`): lines 185-187
- Modify: `src/discovery.cpp` (assert, buffer, context, setup): lines 31-33, 38-39, 61, 97, 102-103, 111-116 (each counted after the edits listed before it)

**Interfaces:**
- Consumes: `size_t mhi_group_default_outdoor_id(const char*, char*, size_t)` and `MHI_GROUP_ID_MAX` (Task 1).
- Produces:
  - `MHI_DISCOVERY_GROUP_ROLE`, the last row, so `MHI_DISCOVERY_ROWS` is 23;
  - `MhiDiscoveryCtx` gains `const char* group_base`, `const char* avty_topic` and `const char* t_group`, appended after `fan[4]`;
  - `bool mhi_discovery_is_outdoor_row(MhiDiscoveryRow row)`, used by Task 5;
  - in `src/support.h`: `GROUP_ROOT`, `GROUP_OP_PREFIX` and `HA_NAME_GROUP_ROLE`; in `src/MHI-AC-Ctrl.h`: `TOPIC_GROUP` (answer 5); used by Tasks 5 and 6;
  - in the renderer: `--group-base <GROUP_ROOT without its slash>` and `--name-group-role <text>`.

Decision: `is_outdoor_row()` becomes the public `mhi_discovery_is_outdoor_row()`. `src/discovery.cpp`'s two cursors (Task 5) need the same classification, and this keeps one definition.
Decision: `mhi_discovery_build()` returns 0 (and `""`) for the retired `MHI_DISCOVERY_OU_KWH`. Every caller publishes only a length above 0, so even a caller that forgot the enabled check cannot publish that config. Its flash string goes too.
Decision: the tests' `kSlaapkamer` keeps `outdoor_id "ac_slaapkamer_outdoor"` and only gains `group_base "airco/outdoor"` and `avty_topic "airco/slaapkamer/connected"`. The `discovery_all` fixture diff then shows exactly the two changes spec §8.2 promises.
Decision: `kDefault.outdoor_id` becomes `mhi_ac_ctrl_outdoor`, the new derived default. This changes no fixture: `has_outdoor` is false there.
Decision: the old `static_assert(starts_with(MQTT_OP_PREFIX, MQTT_PREFIX))` gives way to `starts_with(GROUP_OP_PREFIX, GROUP_ROOT)`. The outdoor rows no longer read `MQTT_OP_PREFIX`, and with the defaults both asserts check the same thing.

- [ ] **Step 1: Extend the header**

In `lib/mhi_pure/mhi_discovery.h`:

Replace (`lib/mhi_pure/mhi_discovery.h`, lines 37-46):

```cpp
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

with:

```cpp
  // OU_OUTDOOR..OU_PROTECTION: the outdoor device (fork #19): uniq_id <outdoor_id>_<suffix>,
  // read from the group root and sent only by the group's publisher (fork #22).
  MHI_DISCOVERY_OU_OUTDOOR,     // sensor        <outdoor_id>_outdoor_temp
  MHI_DISCOVERY_OU_CT,          // sensor        <outdoor_id>_current
  MHI_DISCOVERY_OU_KWH,         // retired (fork #22): KWH is per unit; never built, never published
  MHI_DISCOVERY_OU_COMP,        // sensor        <outdoor_id>_comp_freq
  MHI_DISCOVERY_OU_DEFROST,     // binary_sensor <outdoor_id>_defrost
  MHI_DISCOVERY_OU_COMP_RUN,    // sensor        <outdoor_id>_comp_run
  MHI_DISCOVERY_OU_PROTECTION,  // sensor        <outdoor_id>_protection
  MHI_DISCOVERY_GROUP_ROLE,     // sensor   <id_prefix>_group_role, on the Group topic (fork #22): a unit row
  MHI_DISCOVERY_ROWS
};
```

Replace (`lib/mhi_pure/mhi_discovery.h`, line 101):

```cpp
  bool has_outdoor;                   // HA_OUTDOOR_DEVICE
```

with:

```cpp
  bool has_outdoor;                   // the outdoor rows are wanted (fork #22: every unit; the group decides when they go out)
```

Replace (`lib/mhi_pure/mhi_discovery.h`, line 105):

```cpp
  const char* op_prefix;              // what MQTT_OP_PREFIX adds to MQTT_PREFIX, "OpData/"
```

with:

```cpp
  const char* op_prefix;              // what GROUP_OP_PREFIX adds to GROUP_ROOT, "OpData/"
```

Replace (`lib/mhi_pure/mhi_discovery.h`, lines 109-110):

```cpp
  const char* fan[4];                 // PAYLOAD_FAN_1..4
};
```

with:

```cpp
  const char* fan[4];                 // PAYLOAD_FAN_1..4
  // Fork #22 (the outdoor election).
  const char* group_base;             // GROUP_ROOT without its trailing slash: the outdoor rows' "~"
  const char* avty_topic;             // MQTT_PREFIX TOPIC_CONNECTED in full: the outdoor rows' availability
  const char* t_group;                // TOPIC_GROUP, relative to base
};
```

Replace (`lib/mhi_pure/mhi_discovery.h`, lines 133-137):

```cpp
// Whether a row is part of this build: false for MHI_DISCOVERY_VANES_LR and
// MHI_DISCOVERY_3DAUTO when !has_lr, false for every OU_* row when
// !has_outdoor, false for a row that is not in the table, true otherwise.
// Callers must skip a disabled row entirely.
bool mhi_discovery_row_enabled(MhiDiscoveryRow row, const MhiDiscoveryCtx* ctx);
```

with:

```cpp
// Whether a row is part of this build: false for MHI_DISCOVERY_VANES_LR and
// MHI_DISCOVERY_3DAUTO when !has_lr, false for every outdoor row when
// !has_outdoor, false for the retired MHI_DISCOVERY_OU_KWH in every build,
// false for a row that is not in the table, true otherwise. Callers must skip
// a disabled row entirely.
bool mhi_discovery_row_enabled(MhiDiscoveryRow row, const MhiDiscoveryCtx* ctx);

// The outdoor device's rows, MHI_DISCOVERY_OU_OUTDOOR..MHI_DISCOVERY_OU_PROTECTION:
// their own dev block, uniq_id and topic keyed by outdoor_id, "~" the group
// base, absolute availability. A closed range: a row appended later is a unit
// row unless it is added here.
bool mhi_discovery_is_outdoor_row(MhiDiscoveryRow row);
```

Replace (`lib/mhi_pure/mhi_discovery.h`, lines 146-147):

```cpp
// One row's JSON. Returns the length, 0 (and an empty string) when it does
// not fit out_len. out_len should be MHI_DISCOVERY_BUF.
```

with:

```cpp
// One row's JSON. Returns the length, 0 (and an empty string) when it does
// not fit out_len or the row is retired (MHI_DISCOVERY_OU_KWH): a caller
// publishes only a length above 0, so no discovery topic ever gets an empty
// payload, which would delete the Home Assistant entity. out_len should be
// MHI_DISCOVERY_BUF.
```

- [ ] **Step 2: Extend the tests**

In `test/test_mhi_discovery/test_mhi_discovery.cpp`. Three existing assertions change, each because the spec retires what it asserted:
- (a) `every_row_fits` asserts `~/connected` for unit rows and the absolute `avty_topic` for outdoor rows (spec §8.2);
- (b) the loop in `test_row_enabled_gates_vaneslr_3dauto_and_outdoor` uses the closed range (spec §8.1);
- (c) `test_outdoor_rows_read_the_units_own_opdata_topics` loses its energy assertions (spec §4.2 retires the row) and becomes `test_outdoor_rows_read_the_group_roots_opdata_topics`, with a CT assertion in their place. The retired row is pinned by the new `test_the_retired_energy_row_is_never_built`.

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, lines 30-32):

```cpp
  .names = {NULL, "Vanes", "Silent", "Problem", "Wiring", "Uptime", "Free heap", "Wi-Fi signal", "Reset reason", "Wi-Fi PHY",
            "Vanes left/right", "3D auto", "Frame errors", "Frame timeouts", "Error code",
            "Temperature", "Current", "Energy", "Compressor frequency", "Defrost", "Compressor run time", "Protection state"},
```

with:

```cpp
  .names = {NULL, "Vanes", "Silent", "Problem", "Wiring", "Uptime", "Free heap", "Wi-Fi signal", "Reset reason", "Wi-Fi PHY",
            "Vanes left/right", "3D auto", "Frame errors", "Frame timeouts", "Error code",
            "Temperature", "Current", "Energy", "Compressor frequency", "Defrost", "Compressor run time", "Protection state",
            "Group role"},
```

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, lines 49-56):

```cpp
  .outdoor_id = "MHI-AC-Ctrl_outdoor", .outdoor_name = "AC outdoor unit", .outdoor_entity_prefix = NULL,
  .op_prefix = "OpData/",
  .t_op_outdoor = "OUTDOOR", .t_op_ct = "CT", .t_op_kwh = "KWH", .t_op_comp = "COMP", .t_op_defrost = "DEFROST",
  .t_op_total_comp_run = "TOTAL-COMP-RUN", .t_op_protection_no = "PROTECTION-NO",
  .defrost_on = "On", .defrost_off = "Off",
  .t_frame_errors = "FrameErrors", .t_frame_timeouts = "FrameTimeouts",
  .fan = {"1", "2", "3", "4"},
};
```

with:

```cpp
  .outdoor_id = "mhi_ac_ctrl_outdoor", .outdoor_name = "AC outdoor unit", .outdoor_entity_prefix = NULL,  // derived from GROUP_ROOT "MHI-AC-Ctrl/" (fork #22)
  .op_prefix = "OpData/",
  .t_op_outdoor = "OUTDOOR", .t_op_ct = "CT", .t_op_kwh = "KWH", .t_op_comp = "COMP", .t_op_defrost = "DEFROST",
  .t_op_total_comp_run = "TOTAL-COMP-RUN", .t_op_protection_no = "PROTECTION-NO",
  .defrost_on = "On", .defrost_off = "Off",
  .t_frame_errors = "FrameErrors", .t_frame_timeouts = "FrameTimeouts",
  .fan = {"1", "2", "3", "4"},
  .group_base = "MHI-AC-Ctrl", .avty_topic = "MHI-AC-Ctrl/connected", .t_group = "Group",  // a single split: GROUP_ROOT = MQTT_PREFIX
};
```

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, lines 74-76):

```cpp
  .names = {NULL, "louvers", "quiet mode", "fault", "wiring fault", "time since boot", "heap free", "wifi-signal", "restart reason", "wifi-standard",
            "Vanes left/right", "3D auto", "Frame errors", "Frame timeouts", "Error code",
            "Temperature", "Current", "Energy", "Compressor frequency", "Defrost", "Compressor run time", "Protection state"},
```

with:

```cpp
  .names = {NULL, "louvers", "quiet mode", "fault", "wiring fault", "time since boot", "heap free", "wifi-signal", "restart reason", "wifi-standard",
            "Vanes left/right", "3D auto", "Frame errors", "Frame timeouts", "Error code",
            "Temperature", "Current", "Energy", "Compressor frequency", "Defrost", "Compressor run time", "Protection state",
            "Group role"},
```

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, lines 93-100):

```cpp
  .outdoor_id = "ac_uitkijk_outdoor", .outdoor_name = "AC outdoor unit", .outdoor_entity_prefix = NULL,
  .op_prefix = "OpData/",
  .t_op_outdoor = "OUTDOOR", .t_op_ct = "CT", .t_op_kwh = "KWH", .t_op_comp = "COMP", .t_op_defrost = "DEFROST",
  .t_op_total_comp_run = "TOTAL-COMP-RUN", .t_op_protection_no = "PROTECTION-NO",
  .defrost_on = "On", .defrost_off = "Off",
  .t_frame_errors = "FrameErrors", .t_frame_timeouts = "FrameTimeouts",
  .fan = {"1", "2", "3", "4"},
};
```

with:

```cpp
  .outdoor_id = "ac_uitkijk_outdoor", .outdoor_name = "AC outdoor unit", .outdoor_entity_prefix = NULL,
  .op_prefix = "OpData/",
  .t_op_outdoor = "OUTDOOR", .t_op_ct = "CT", .t_op_kwh = "KWH", .t_op_comp = "COMP", .t_op_defrost = "DEFROST",
  .t_op_total_comp_run = "TOTAL-COMP-RUN", .t_op_protection_no = "PROTECTION-NO",
  .defrost_on = "On", .defrost_off = "Off",
  .t_frame_errors = "FrameErrors", .t_frame_timeouts = "FrameTimeouts",
  .fan = {"1", "2", "3", "4"},
  .group_base = "airco/uitkijk", .avty_topic = "airco/uitkijk/connected", .t_group = "Group",
};
```

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, lines 103-104):

```cpp
// Slaapkamer: has_lr and the outdoor device both on (spec §3).
static MhiDiscoveryCtx make_slaapkamer() {
```

with:

```cpp
// Slaapkamer: has_lr and the outdoor device both on (batch C spec §3), as the
// group's publisher (fork #22).
static MhiDiscoveryCtx make_slaapkamer() {
```

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, lines 116-117):

```cpp
  c.outdoor_entity_prefix = "ac_outdoor";
  return c;
```

with:

```cpp
  c.outdoor_entity_prefix = "ac_outdoor";
  // Lucas's group (fork #22): the outdoor rows read airco/outdoor/ and are
  // available while Slaapkamer, their publisher, is.
  c.group_base = "airco/outdoor";
  c.avty_topic = "airco/slaapkamer/connected";
  return c;
```

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, line 128):

```cpp
  "ou_outdoor", "ou_ct", "ou_kwh", "ou_comp", "ou_defrost", "ou_comp_run", "ou_protection"};
```

with:

```cpp
  "ou_outdoor", "ou_ct", "ou_kwh", "ou_comp", "ou_defrost", "ou_comp_run", "ou_protection",
  "group_role"};
```

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, line 208):

```cpp
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"avty_t\":\"~/connected\",\"pl_avail\":\"1\",\"pl_not_avail\":\"0\""), msg);
```

with:

```cpp
    char avty[128];
    if (mhi_discovery_is_outdoor_row((MhiDiscoveryRow)r))  // the publisher's connected topic in full (fork #22)
      snprintf(avty, sizeof(avty), "\"avty_t\":\"%s\",\"pl_avail\":\"1\",\"pl_not_avail\":\"0\"", ctx->avty_topic);
    else
      snprintf(avty, sizeof(avty), "\"avty_t\":\"~/connected\",\"pl_avail\":\"1\",\"pl_not_avail\":\"0\"");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, avty), msg);
```

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, lines 219-220):

```cpp
    if (r >= MHI_DISCOVERY_OU_OUTDOOR)
      TEST_ASSERT_NOT_NULL_MESSAGE
```

with:

```cpp
    if (mhi_discovery_is_outdoor_row((MhiDiscoveryRow)r))
      TEST_ASSERT_NOT_NULL_MESSAGE
```

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, line 371):

```cpp
    if (r == MHI_DISCOVERY_VANES_LR || r == MHI_DISCOVERY_3DAUTO || r >= MHI_DISCOVERY_OU_OUTDOOR) continue;
```

with:

```cpp
    if (r == MHI_DISCOVERY_VANES_LR || r == MHI_DISCOVERY_3DAUTO || mhi_discovery_is_outdoor_row((MhiDiscoveryRow)r)) continue;
```

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, lines 427-433):

```cpp
static void test_outdoor_rows_read_the_units_own_opdata_topics(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_OUTDOOR, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/OpData/OUTDOOR\",\"dev_cla\":\"temperature\",\"unit_of_meas\":\"\xc2\xb0" "C\",\"stat_cla\":\"measurement\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_KWH, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/OpData/KWH\",\"dev_cla\":\"energy\",\"unit_of_meas\":\"kWh\",\"stat_cla\":\"total_increasing\","));
  TEST_ASSERT_NULL(strstr(out, "ent_cat"));  // OU_KWH is not diagnostic
```

with:

```cpp
static void test_outdoor_rows_read_the_group_roots_opdata_topics(void) {
  char out[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_OUTDOOR, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/OpData/OUTDOOR\",\"dev_cla\":\"temperature\",\"unit_of_meas\":\"\xc2\xb0" "C\",\"stat_cla\":\"measurement\","));
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_CT, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/OpData/CT\",\"dev_cla\":\"current\",\"unit_of_meas\":\"A\",\"stat_cla\":\"measurement\","));
  TEST_ASSERT_NULL(strstr(out, "ent_cat"));  // OU_CT is not diagnostic
```

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, line 444):

```cpp
// --- the committed reference payloads ---------------------------------------
```

with:

```cpp
// --- fork #22: the group role, the group root, the retired energy row ----------

static void test_the_group_role_row_is_a_unit_row(void) {
  char out[MHI_DISCOVERY_BUF], topic[MHI_DISCOVERY_TOPIC_MAX];
  TEST_ASSERT_FALSE(mhi_discovery_is_outdoor_row(MHI_DISCOVERY_GROUP_ROLE));  // a closed range, not ">= OU_OUTDOOR"
  TEST_ASSERT_TRUE(mhi_discovery_row_enabled(MHI_DISCOVERY_GROUP_ROLE, &kDefault));
  TEST_ASSERT_TRUE(mhi_discovery_topic(MHI_DISCOVERY_GROUP_ROLE, &kSlaapkamer, topic, sizeof(topic)) > 0);
  TEST_ASSERT_EQUAL_STRING("homeassistant/sensor/ac_slaapkamer_group_role/config", topic);
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_GROUP_ROLE, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "{\"~\":\"airco/slaapkamer\",\"name\":\"Group role\",\"uniq_id\":\"ac_slaapkamer_group_role\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"default_entity_id\":\"sensor.ac_slaapkamer_group_role\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"stat_t\":\"~/Group\",\"ent_cat\":\"diagnostic\",\"avty_t\":\"~/connected\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"dev\":{\"ids\":[\"airco-slaapkamer\"],\"name\":\"AC Slaapkamer\","));
  TEST_ASSERT_NULL(strstr(out, "dev_cla"));
  TEST_ASSERT_NULL(strstr(out, "unit_of_meas"));
  TEST_ASSERT_NULL(strstr(out, "stat_cla"));
  // Without an entity prefix, no default_entity_id.
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_GROUP_ROLE, &kDefault, out, sizeof(out)) > 0);
  TEST_ASSERT_NULL(strstr(out, "default_entity_id"));
}

static void test_is_outdoor_row_is_the_closed_outdoor_block(void) {
  for (int r = 0; r < MHI_DISCOVERY_ROWS; r++)
    TEST_ASSERT_EQUAL(r >= MHI_DISCOVERY_OU_OUTDOOR && r <= MHI_DISCOVERY_OU_PROTECTION,
                      mhi_discovery_is_outdoor_row((MhiDiscoveryRow)r));
  TEST_ASSERT_FALSE(mhi_discovery_is_outdoor_row(MHI_DISCOVERY_ROWS));
}

static void test_outdoor_rows_use_the_group_base_and_absolute_availability(void) {
  char out[MHI_DISCOVERY_BUF], topic[MHI_DISCOVERY_TOPIC_MAX];
  for (int r = MHI_DISCOVERY_OU_OUTDOOR; r <= MHI_DISCOVERY_OU_PROTECTION; r++) {
    if (!mhi_discovery_row_enabled((MhiDiscoveryRow)r, &kSlaapkamer)) continue;
    TEST_ASSERT_TRUE(mhi_discovery_build((MhiDiscoveryRow)r, &kSlaapkamer, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING_LEN("{\"~\":\"airco/outdoor\",", out, strlen("{\"~\":\"airco/outdoor\","));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"avty_t\":\"airco/slaapkamer/connected\",\"pl_avail\":\"1\",\"pl_not_avail\":\"0\","));
    TEST_ASSERT_NULL(strstr(out, "\"avty_t\":\"~/"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"via_device\":\"airco-slaapkamer\"}}"));
  }
  // The topic and the uniq_id stay keyed by the outdoor ID: the same for every publisher.
  TEST_ASSERT_TRUE(mhi_discovery_topic(MHI_DISCOVERY_OU_CT, &kSlaapkamer, topic, sizeof(topic)) > 0);
  TEST_ASSERT_EQUAL_STRING("homeassistant/sensor/ac_slaapkamer_outdoor_current/config", topic);
  // Another publisher changes only avty_t and via_device (spec §8.2).
  MhiDiscoveryCtx uitkijk = kSlaapkamer;
  uitkijk.base = "airco/uitkijk";
  uitkijk.hostname = "airco-uitkijk";
  uitkijk.avty_topic = "airco/uitkijk/connected";
  char other[MHI_DISCOVERY_BUF];
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_CT, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_OU_CT, &uitkijk, other, sizeof(other)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(other, "\"avty_t\":\"airco/uitkijk/connected\","));
  TEST_ASSERT_NOT_NULL(strstr(other, "\"via_device\":\"airco-uitkijk\"}}"));
  const char* cut_out = strstr(out, "\"avty_t\"");
  const char* cut_other = strstr(other, "\"avty_t\"");
  TEST_ASSERT_EQUAL_size_t((size_t)(cut_out - out), (size_t)(cut_other - other));
  TEST_ASSERT_EQUAL_STRING_LEN(out, other, (size_t)(cut_out - out));  // everything before avty_t is identical
}

static void test_the_retired_energy_row_is_never_built(void) {
  const MhiDiscoveryCtx* all[] = {&kDefault, &kUitkijk, &kSlaapkamer};
  char out[MHI_DISCOVERY_BUF];
  for (size_t i = 0; i < 3; i++) {
    TEST_ASSERT_FALSE(mhi_discovery_row_enabled(MHI_DISCOVERY_OU_KWH, all[i]));
    strcpy(out, "stale");
    TEST_ASSERT_EQUAL_size_t(0, mhi_discovery_build(MHI_DISCOVERY_OU_KWH, all[i], out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
  }
}

// HA expands "~" only where a value starts with it; "~" never ends in "/", so
// no expanded topic holds "//" (spec §8.2).
static void test_no_expanded_topic_contains_a_double_slash(void) {
  const MhiDiscoveryCtx* all[] = {&kDefault, &kUitkijk, &kSlaapkamer};
  for (size_t i = 0; i < 3; i++) {
    for (int r = 0; r < MHI_DISCOVERY_ROWS; r++) {
      if (!mhi_discovery_row_enabled((MhiDiscoveryRow)r, all[i])) continue;
      char out[MHI_DISCOVERY_BUF], expanded[2 * MHI_DISCOVERY_BUF], tilde[80], msg[48];
      snprintf(msg, sizeof(msg), "context %u row %d", (unsigned)i, r);
      TEST_ASSERT_TRUE_MESSAGE(mhi_discovery_build((MhiDiscoveryRow)r, all[i], out, sizeof(out)) > 0, msg);
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, strncmp(out, "{\"~\":\"", 6), msg);
      const char* end = strchr(out + 6, '"');
      TEST_ASSERT_NOT_NULL_MESSAGE(end, msg);
      snprintf(tilde, sizeof(tilde), "%.*s", (int)(end - (out + 6)), out + 6);
      size_t n = 0;
      for (const char* p = out; *p; p++) {
        if (p[0] == '"' && p[1] == '~' && p[2] == '/') {
          n += (size_t)snprintf(expanded + n, sizeof(expanded) - n, "\"%s", tilde);
          p++;  // the "~"; the "/" is copied next
          continue;
        }
        expanded[n++] = *p;
      }
      expanded[n] = '\0';
      TEST_ASSERT_NULL_MESSAGE(strstr(expanded, "//"), msg);
    }
  }
}

static void test_no_row_builds_an_empty_payload(void) {
  const MhiDiscoveryCtx* all[] = {&kDefault, &kUitkijk, &kSlaapkamer};
  for (size_t i = 0; i < 3; i++) {
    for (int r = 0; r < MHI_DISCOVERY_ROWS; r++) {
      char out[MHI_DISCOVERY_BUF], msg[48];
      snprintf(msg, sizeof(msg), "context %u row %d", (unsigned)i, r);
      const size_t n = mhi_discovery_build((MhiDiscoveryRow)r, all[i], out, sizeof(out));
      TEST_ASSERT_EQUAL_size_t_MESSAGE(strlen(out), n, msg);
      if (mhi_discovery_row_enabled((MhiDiscoveryRow)r, all[i])) {
        TEST_ASSERT_TRUE_MESSAGE(n > 2, msg);
        TEST_ASSERT_EQUAL_CHAR_MESSAGE('{', out[0], msg);
        TEST_ASSERT_EQUAL_CHAR_MESSAGE('}', out[n - 1], msg);
      }
    }
  }
}

// --- the committed reference payloads ---------------------------------------
```

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, lines 613-614):

```cpp
  RUN_TEST(test_outdoor_rows_read_the_units_own_opdata_topics);
  RUN_TEST(test_reference_fixtures_are_written);
```

with:

```cpp
  RUN_TEST(test_outdoor_rows_read_the_group_roots_opdata_topics);
  RUN_TEST(test_the_group_role_row_is_a_unit_row);
  RUN_TEST(test_is_outdoor_row_is_the_closed_outdoor_block);
  RUN_TEST(test_outdoor_rows_use_the_group_base_and_absolute_availability);
  RUN_TEST(test_the_retired_energy_row_is_never_built);
  RUN_TEST(test_no_expanded_topic_contains_a_double_slash);
  RUN_TEST(test_no_row_builds_an_empty_payload);
  RUN_TEST(test_reference_fixtures_are_written);
```

- [ ] **Step 3: Run the tests and see them fail**

Run: `pio test -e native -f test_mhi_discovery`
Expected: build errors in `lib/mhi_pure/mhi_discovery.cpp`: `static assertion failed: a row appended after the outdoor rows must be classified in is_outdoor_row() first`, and `enumeration value 'MHI_DISCOVERY_GROUP_ROLE' not handled in switch [-Werror=switch]`. This is the tripwire batch C left for exactly this.

- [ ] **Step 4: Implement the builder**

In `lib/mhi_pure/mhi_discovery.cpp`:

Replace (`lib/mhi_pure/mhi_discovery.cpp`, lines 23-27):

```cpp
  "sensor", "sensor", "sensor", "sensor", "binary_sensor", "sensor", "sensor"};
static const char* const kSuffix[MHI_DISCOVERY_ROWS] = {
  "", "vanes", "silent", "problem", "wiring", "uptime", "free_heap", "rssi", "reset_reason", "wifi_phy",
  "vanes_lr", "3d_auto", "frame_errors", "frame_timeouts", "error_code",
  "outdoor_temp", "current", "energy", "comp_freq", "defrost", "comp_run", "protection"};
```

with:

```cpp
  "sensor", "sensor", "sensor", "sensor", "binary_sensor", "sensor", "sensor",
  "sensor"};
static const char* const kSuffix[MHI_DISCOVERY_ROWS] = {
  "", "vanes", "silent", "problem", "wiring", "uptime", "free_heap", "rssi", "reset_reason", "wifi_phy",
  "vanes_lr", "3d_auto", "frame_errors", "frame_timeouts", "error_code",
  "outdoor_temp", "current", "energy", "comp_freq", "defrost", "comp_run", "protection",
  "group_role"};
```

Replace (`lib/mhi_pure/mhi_discovery.cpp`, lines 83-90):

```cpp
// The rows of the outdoor device (fork #19): their own dev block, and both the
// uniq_id and the config topic keyed by outdoor_id instead of id_prefix.
static bool is_outdoor_row(MhiDiscoveryRow row) { return row >= MHI_DISCOVERY_OU_OUTDOOR; }

// The test above is open-ended, and MhiDiscoveryRow is append-only: a row added
// after the outdoor block would become an outdoor row without anyone saying so.
static_assert(MHI_DISCOVERY_OU_PROTECTION + 1 == MHI_DISCOVERY_ROWS,
              "a row appended after the outdoor rows must be classified in is_outdoor_row() first");
```

with:

```cpp
// The rows of the outdoor device (fork #19): their own dev block, and both the
// uniq_id and the config topic keyed by outdoor_id instead of id_prefix.
// A closed range (fork #22): MHI_DISCOVERY_GROUP_ROLE, appended after the
// block, is a unit row.
bool mhi_discovery_is_outdoor_row(MhiDiscoveryRow row) {
  return row >= MHI_DISCOVERY_OU_OUTDOOR && row <= MHI_DISCOVERY_OU_PROTECTION;
}

// The outdoor block is the seven rows of fork #19, and the table ends with the
// Group role row. MhiDiscoveryRow is append-only: whoever appends a row decides
// in mhi_discovery_is_outdoor_row() whether it is an outdoor row, then moves
// this line.
static_assert(MHI_DISCOVERY_OU_PROTECTION - MHI_DISCOVERY_OU_OUTDOOR == 6, "the outdoor block is OU_OUTDOOR..OU_PROTECTION");
static_assert(MHI_DISCOVERY_GROUP_ROLE + 1 == MHI_DISCOVERY_ROWS,
              "a row appended after MHI_DISCOVERY_GROUP_ROLE: classify it in mhi_discovery_is_outdoor_row() first");
```

Replace (`lib/mhi_pure/mhi_discovery.cpp`, lines 99-100):

```cpp
static void head(Out* o, const MhiDiscoveryCtx* c, MhiDiscoveryRow row) {
  put(o, FMT("{\"~\":\"%s\","), c->base);
```

with:

```cpp
static void head(Out* o, const MhiDiscoveryCtx* c, MhiDiscoveryRow row) {
  // The outdoor rows read the group root (fork #22); it never ends in "/", so
  // no expanded topic holds "//".
  put(o, FMT("{\"~\":\"%s\","), mhi_discovery_is_outdoor_row(row) ? c->group_base : c->base);
```

Replace (`lib/mhi_pure/mhi_discovery.cpp`, lines 109-110):

```cpp
  const char* id_prefix = is_outdoor_row(row) ? c->outdoor_id : c->id_prefix;
  const char* entity_prefix = is_outdoor_row(row) ? c->outdoor_entity_prefix : c->entity_prefix;
```

with:

```cpp
  const char* id_prefix = mhi_discovery_is_outdoor_row(row) ? c->outdoor_id : c->id_prefix;
  const char* entity_prefix = mhi_discovery_is_outdoor_row(row) ? c->outdoor_entity_prefix : c->entity_prefix;
```

Replace (`lib/mhi_pure/mhi_discovery.cpp`, lines 126-129):

```cpp
  if (diagnostic) put(o, FMT("\"ent_cat\":\"diagnostic\","));
  put(o, FMT("\"avty_t\":\"~/%s\",\"pl_avail\":\"%s\",\"pl_not_avail\":\"%s\",\"dev\":{\"ids\":[\""),
      c->t_connected, c->connected_on, c->connected_off);
  if (is_outdoor_row(row)) {
```

with:

```cpp
  if (diagnostic) put(o, FMT("\"ent_cat\":\"diagnostic\","));
  // The outdoor rows are available while the unit that publishes them is: its
  // connected topic in full, since their "~" is the group root (fork #22).
  if (mhi_discovery_is_outdoor_row(row))
    put(o, FMT("\"avty_t\":\"%s\","), c->avty_topic);
  else
    put(o, FMT("\"avty_t\":\"~/%s\","), c->t_connected);
  put(o, FMT("\"pl_avail\":\"%s\",\"pl_not_avail\":\"%s\",\"dev\":{\"ids\":[\""), c->connected_on, c->connected_off);
  if (mhi_discovery_is_outdoor_row(row)) {
```

Replace (`lib/mhi_pure/mhi_discovery.cpp`, line 152):

```cpp
// An operating-data topic: the unit's own MQTT_OP_PREFIX, below the same "~".
```

with:

```cpp
// An operating-data topic of the outdoor device: GROUP_OP_PREFIX, below the group root's "~".
```

Replace (`lib/mhi_pure/mhi_discovery.cpp`, lines 191-193):

```cpp
  if (row >= MHI_DISCOVERY_ROWS) return false;  // not a row of the table, like mhi_discovery_topic()
  if (row == MHI_DISCOVERY_VANES_LR || row == MHI_DISCOVERY_3DAUTO) return c->has_lr;
  if (is_outdoor_row(row)) return c->has_outdoor;
```

with:

```cpp
  if (row >= MHI_DISCOVERY_ROWS) return false;  // not a row of the table, like mhi_discovery_topic()
  if (row == MHI_DISCOVERY_OU_KWH) return false;  // retired (fork #22 spec §4.2): KWH is per unit
  if (row == MHI_DISCOVERY_VANES_LR || row == MHI_DISCOVERY_3DAUTO) return c->has_lr;
  if (mhi_discovery_is_outdoor_row(row)) return c->has_outdoor;
```

Replace (`lib/mhi_pure/mhi_discovery.cpp`, line 205):

```cpp
                 is_outdoor_row(row) ? c->outdoor_id : c->id_prefix, kSuffix[row]);
```

with:

```cpp
                 mhi_discovery_is_outdoor_row(row) ? c->outdoor_id : c->id_prefix, kSuffix[row]);
```

Delete (`lib/mhi_pure/mhi_discovery.cpp`, lines 315-319):

```cpp
    case MHI_DISCOVERY_OU_KWH:
      diagnostic = false;
      op_state_topic(&o, c, c->t_op_kwh);
      put(&o, FMT("\"dev_cla\":\"energy\",\"unit_of_meas\":\"kWh\",\"stat_cla\":\"total_increasing\","));
      break;
```

Replace (`lib/mhi_pure/mhi_discovery.cpp`, lines 330-332):

```cpp
    case MHI_DISCOVERY_ROWS:  // excluded above; keeps -Wswitch exhaustive
      out[0] = '\0';
      return 0;
```

with:

```cpp
    case MHI_DISCOVERY_GROUP_ROLE:
      // 0 member, 1 publisher, 2 outdoor ID mismatch, 3 version mismatch; the
      // wording is Home Assistant's (fork #22 spec §3).
      state_topic(&o, c->t_group);
      break;
    case MHI_DISCOVERY_OU_KWH:  // retired (fork #22): never built, so never published, and never empty
    case MHI_DISCOVERY_ROWS:    // excluded above; keeps -Wswitch exhaustive
      out[0] = '\0';
      return 0;
```

- [ ] **Step 5: Run the tests and see them pass**

Run: `pio test -e native -f test_mhi_discovery -v`
Expected: `32 Tests 0 Failures 0 Ignored`, and the three high-water marks, unchanged from batch C:
```
  default: longest row 0, 701 of 1024 bytes
  uitkijk: longest row 0, 910 of 1024 bytes
  slaapkamer: longest row 0, 928 of 1024 bytes
```

- [ ] **Step 6: The fixtures**

The run rewrote them. The deleted row is not rewritten, and CI's gate would catch the stale file, so remove it:

```bash
git rm -q test/fixtures/discovery_all/ou_kwh.txt
git status --porcelain -- test/fixtures
```
Expected, and nothing else:
```
 M test/fixtures/discovery_all/ou_comp.txt
 M test/fixtures/discovery_all/ou_comp_run.txt
 M test/fixtures/discovery_all/ou_ct.txt
 M test/fixtures/discovery_all/ou_defrost.txt
 M test/fixtures/discovery_all/ou_outdoor.txt
 M test/fixtures/discovery_all/ou_protection.txt
D  test/fixtures/discovery_all/ou_kwh.txt
?? test/fixtures/discovery/group_role.txt
?? test/fixtures/discovery_all/group_role.txt
```
The 13 existing files in `test/fixtures/discovery/` do not change by one byte. Check spec §8.2 by eye: `git diff --word-diff=plain -- test/fixtures/discovery_all/ou_ct.txt` shows exactly two changes, `"~":"airco/slaapkamer"` → `"~":"airco/outdoor"` and `"avty_t":"~/connected"` → `"avty_t":"airco/slaapkamer/connected"`. The new `test/fixtures/discovery_all/group_role.txt` reads:
```
homeassistant/sensor/ac_slaapkamer_group_role/config
{"~":"airco/slaapkamer","name":"Group role","uniq_id":"ac_slaapkamer_group_role","default_entity_id":"sensor.ac_slaapkamer_group_role","stat_t":"~/Group","ent_cat":"diagnostic","avty_t":"~/connected","pl_avail":"1","pl_not_avail":"0","dev":{"ids":["airco-slaapkamer"],"name":"AC Slaapkamer","mf":"Mitsubishi Heavy Industries","mdl":"MHI-AC-Ctrl","sw":"batchc-fixture"}}
```

- [ ] **Step 7: The renderer**

In `tools/discovery_payloads.cpp`:

Replace (`tools/discovery_payloads.cpp`, line 7):

```cpp
//   g++ -std=gnu++17 -Wall -Wextra -Werror -I lib/mhi_pure lib/mhi_pure/mhi_discovery.cpp tools/discovery_payloads.cpp -o .pio/discovery_payloads
```

with:

```cpp
//   g++ -std=gnu++17 -Wall -Wextra -Werror -I lib/mhi_pure lib/mhi_pure/mhi_discovery.cpp lib/mhi_pure/mhi_group.cpp tools/discovery_payloads.cpp -o .pio/discovery_payloads
```

Replace (`tools/discovery_payloads.cpp`, line 13):

```cpp
//   --vanes-lr (eight comma-separated names); --outdoor 0|1 (HA_OUTDOOR_DEVICE)
```

with:

```cpp
//   --vanes-lr (eight comma-separated names); --outdoor 0|1 (HA_OUTDOOR_DEVICE, gone since fork #22: see below)
```

Replace (`tools/discovery_payloads.cpp`, lines 18-22):

```cpp
//   --name-ou-defrost, --name-ou-comp-run, --name-ou-protection.
// Every option has the repo default; --modes and --vanes take exactly six
// comma-separated items and --vanes-lr exactly eight; --lr and --outdoor take
// exactly 0 or 1; --outdoor-id defaults to <id_prefix>_outdoor, as the
// firmware's HA_OUTDOOR_ID does; an option given twice takes its last value.
```

with:

```cpp
//   --name-ou-defrost, --name-ou-comp-run, --name-ou-protection.
// The outdoor election (fork #22) adds: --group-base (GROUP_ROOT without its
//   trailing slash, default --base: a single split), which the six outdoor
//   rows read, and --name-group-role. --outdoor 1 adds those six rows as this
//   unit publishes them when it is the group's publisher: availability is its
//   own <--base>/connected. The retired energy row is never rendered.
// Every option has the repo default; --modes and --vanes take exactly six
// comma-separated items and --vanes-lr exactly eight; --lr and --outdoor take
// exactly 0 or 1; --outdoor-id defaults to the slug of --group-base plus
// "_outdoor", as the firmware derives HA_OUTDOOR_ID from GROUP_ROOT; an option
// given twice takes its last value.
```

Replace (`tools/discovery_payloads.cpp`, line 34):

```cpp
#include "mhi_discovery.h"
```

with:

```cpp
#include "mhi_discovery.h"
#include "mhi_group.h"
```

Replace (`tools/discovery_payloads.cpp`, line 42):

```cpp
  "--name-ou-comp-run", "--name-ou-protection"};
```

with:

```cpp
  "--name-ou-comp-run", "--name-ou-protection", "--name-group-role"};
```

Replace (`tools/discovery_payloads.cpp`, line 78):

```cpp
              "Temperature", "Current", "Energy", "Compressor frequency", "Defrost", "Compressor run time", "Protection state"},
```

with:

```cpp
              "Temperature", "Current", "Energy", "Compressor frequency", "Defrost", "Compressor run time", "Protection state",
              "Group role"},
```

Replace (`tools/discovery_payloads.cpp`, lines 95-97):

```cpp
    // outdoor_id is re-derived from id_prefix after the option loop unless
    // --outdoor-id is given; this literal is the same value for the defaults.
    .outdoor_id = "MHI-AC-Ctrl_outdoor", .outdoor_name = "AC outdoor unit", .outdoor_entity_prefix = NULL,
```

with:

```cpp
    // outdoor_id, group_base and avty_topic are derived after the option loop
    // (from --group-base and --base) unless given; these literals are the
    // same values for the defaults.
    .outdoor_id = "mhi_ac_ctrl_outdoor", .outdoor_name = "AC outdoor unit", .outdoor_entity_prefix = NULL,
```

Replace (`tools/discovery_payloads.cpp`, lines 104-106):

```cpp
    .fan = {"1", "2", "3", "4"},
  };
  bool outdoor_id_given = false;
```

with:

```cpp
    .fan = {"1", "2", "3", "4"},
    .group_base = "MHI-AC-Ctrl", .avty_topic = "MHI-AC-Ctrl/connected", .t_group = "Group",
  };
  bool outdoor_id_given = false, group_base_given = false;
```

Replace (`tools/discovery_payloads.cpp`, line 113):

```cpp
    else if (strcmp(opt, "--base") == 0) c.base = val;
```

with:

```cpp
    else if (strcmp(opt, "--base") == 0) c.base = val;
    else if (strcmp(opt, "--group-base") == 0) { c.group_base = val; group_base_given = true; }
```

Replace (`tools/discovery_payloads.cpp`, lines 152-163):

```cpp
  // The firmware's HA_OUTDOOR_ID is HA_ID_PREFIX "_outdoor" (src/support.h), so
  // the default has to follow --id-prefix; it can only be derived once the
  // option loop is done.
  char derived_outdoor_id[MHI_DISCOVERY_TOPIC_MAX];
  if (!outdoor_id_given) {
    const int n = snprintf(derived_outdoor_id, sizeof(derived_outdoor_id), "%s_outdoor", c.id_prefix);
    if (n < 0 || (size_t)n >= sizeof(derived_outdoor_id)) {
      fprintf(stderr, "discovery_payloads: --id-prefix is too long for the derived outdoor id\n");
      return 1;
    }
    c.outdoor_id = derived_outdoor_id;
  }
```

with:

```cpp
  // What the firmware derives, once the option loop is done: GROUP_ROOT
  // defaults to MQTT_PREFIX, the outdoor rows' availability is the unit's own
  // <MQTT_PREFIX>connected, and HA_OUTDOOR_ID defaults to the slug of
  // GROUP_ROOT plus "_outdoor" (src/support.cpp, mhi_group_default_outdoor_id).
  if (!group_base_given) c.group_base = c.base;
  char avty_topic[MHI_DISCOVERY_TOPIC_MAX];
  const int an = snprintf(avty_topic, sizeof(avty_topic), "%s/%s", c.base, c.t_connected);
  if (an < 0 || (size_t)an >= sizeof(avty_topic)) {
    fprintf(stderr, "discovery_payloads: --base is too long for the availability topic\n");
    return 1;
  }
  c.avty_topic = avty_topic;
  char derived_outdoor_id[MHI_GROUP_ID_MAX + 1];
  if (!outdoor_id_given) {
    // The slug ignores the trailing slash, so the group base derives what GROUP_ROOT does.
    if (mhi_group_default_outdoor_id(c.group_base, derived_outdoor_id, sizeof(derived_outdoor_id)) == 0) {
      fprintf(stderr, "discovery_payloads: --group-base is too long for the derived outdoor id; give --outdoor-id\n");
      return 1;
    }
    c.outdoor_id = derived_outdoor_id;
  }
```

Build and check it:
```bash
g++ -std=gnu++17 -Wall -Wextra -Werror -I lib/mhi_pure lib/mhi_pure/mhi_discovery.cpp lib/mhi_pure/mhi_group.cpp tools/discovery_payloads.cpp -o .pio/discovery_payloads
.pio/discovery_payloads | wc -l                 # 14: the 13 unit rows of batch C and the Group role
.pio/discovery_payloads --outdoor 1 | wc -l     # 20: plus six outdoor rows, no energy row
.pio/discovery_payloads --base airco/slaapkamer --hostname airco-slaapkamer --device-name "AC Slaapkamer" \
  --climate-id AC_Slaapkamer --id-prefix ac_slaapkamer --entity-prefix ac_slaapkamer --version test --lr 1 \
  --group-base airco/outdoor --outdoor-id ac_outdoor --outdoor-entity-prefix ac_outdoor --outdoor 1 > .pio/lucas.tsv
wc -l < .pio/lucas.tsv                          # 22: 16 unit rows and 6 outdoor rows
grep -c '"~":"airco/outdoor".*"avty_t":"airco/slaapkamer/connected"' .pio/lucas.tsv   # 6
.pio/discovery_payloads --group-base airco/outdoor --outdoor 1 | grep -c 'airco_outdoor_outdoor_outdoor_temp'   # 1: the derived ID
```

- [ ] **Step 8: The firmware options and the context fill**

In `src/support.h`, after the `HA_NAME_OU_PROTECTION` block:

Replace (`src/support.h`, lines 140-142):

```cpp
#ifndef HA_NAME_OU_PROTECTION
#define HA_NAME_OU_PROTECTION "Protection state"
#endif
```

with:

```cpp
#ifndef HA_NAME_OU_PROTECTION
#define HA_NAME_OU_PROTECTION "Protection state"
#endif
#ifndef HA_NAME_GROUP_ROLE
#define HA_NAME_GROUP_ROLE "Group role"             // the diagnostic sensor on the Group topic (fork #22)
#endif
```

and after the `MQTT_ERR_OP_PREFIX` block:

Replace (`src/support.h`, lines 177-179):

```cpp
#ifndef MQTT_ERR_OP_PREFIX
#define MQTT_ERR_OP_PREFIX MQTT_PREFIX "ErrOpData/" // prefix for publishing operating data from last error, must end with a "/"
#endif
```

with:

```cpp
#ifndef MQTT_ERR_OP_PREFIX
#define MQTT_ERR_OP_PREFIX MQTT_PREFIX "ErrOpData/" // prefix for publishing operating data from last error, must end with a "/"
#endif

// The outdoor election (fork #22, SW-Configuration.md "Several indoor units
// on one outdoor unit"): the indoor units of one outdoor unit share GROUP_ROOT
// and elect the one that writes the outdoor unit's values under
// GROUP_OP_PREFIX. A single split needs nothing: its root is its own
// MQTT_PREFIX, so its topics stay where they are.
#ifndef GROUP_ROOT
#define GROUP_ROOT MQTT_PREFIX                      // topic root shared by the units of one outdoor unit, ends in "/", at most 64 characters
#ifndef GROUP_OP_PREFIX
#define GROUP_OP_PREFIX MQTT_OP_PREFIX              // without a GROUP_ROOT a custom MQTT_OP_PREFIX keeps its topics
#endif
#endif
#ifndef GROUP_OP_PREFIX
#define GROUP_OP_PREFIX GROUP_ROOT "OpData/"        // where the publisher writes the outdoor unit's values
#endif
```

In `src/MHI-AC-Ctrl.h`, beside the other topics:

Replace (`src/MHI-AC-Ctrl.h`, lines 185-187):

```cpp
#ifndef TOPIC_DISCOVERY
#define TOPIC_DISCOVERY "Discovery"           // retained, after the discovery configs went out: ok, or modes when the climate row was skipped (HA_DISCOVERY)
#endif
```

with:

```cpp
#ifndef TOPIC_DISCOVERY
#define TOPIC_DISCOVERY "Discovery"           // retained, after the discovery configs went out: ok, or modes when the climate row was skipped (HA_DISCOVERY)
#endif
#ifndef TOPIC_GROUP
#define TOPIC_GROUP "Group"                   // retained: 0 member, 1 publisher, 2 outdoor ID mismatch, 3 version mismatch (fork #22)
#endif
```

In `src/discovery.cpp`:

Replace (`src/discovery.cpp`, lines 31-33):

```cpp
// The outdoor rows read "~/<op_prefix><topic>", so the operating-data prefix
// must sit under the status prefix too.
static_assert(starts_with(MQTT_OP_PREFIX, MQTT_PREFIX), "HA_DISCOVERY needs MQTT_OP_PREFIX to start with MQTT_PREFIX");
```

with:

```cpp
// The outdoor rows read "~/<op_prefix><topic>" with the group root as "~"
// (fork #22), so the publisher's prefix must sit under the group root.
static_assert(starts_with(GROUP_OP_PREFIX, GROUP_ROOT), "HA_DISCOVERY needs GROUP_OP_PREFIX to start with GROUP_ROOT");
```

Replace (`src/discovery.cpp`, lines 38-39):

```cpp
// MQTT_PREFIX without its trailing slash: the payload's "~".
static char base_topic[sizeof(MQTT_PREFIX)];
```

with:

```cpp
// MQTT_PREFIX without its trailing slash: the payload's "~".
static char base_topic[sizeof(MQTT_PREFIX)];
// GROUP_ROOT without its trailing slash: the outdoor rows' "~" (fork #22).
static char group_base_topic[sizeof(GROUP_ROOT)];
```

Replace (`src/discovery.cpp`, line 61):

```cpp
            HA_NAME_OU_COMP_RUN, HA_NAME_OU_PROTECTION},
```

with:

```cpp
            HA_NAME_OU_COMP_RUN, HA_NAME_OU_PROTECTION, HA_NAME_GROUP_ROLE},
```

Replace (`src/discovery.cpp`, line 97):

```cpp
  .op_prefix = MQTT_OP_PREFIX + (sizeof(MQTT_PREFIX) - 1),
```

with:

```cpp
  .op_prefix = GROUP_OP_PREFIX + (sizeof(GROUP_ROOT) - 1),
```

Replace (`src/discovery.cpp`, lines 102-103):

```cpp
  .fan = {PAYLOAD_FAN_1, PAYLOAD_FAN_2, PAYLOAD_FAN_3, PAYLOAD_FAN_4},
};
```

with:

```cpp
  .fan = {PAYLOAD_FAN_1, PAYLOAD_FAN_2, PAYLOAD_FAN_3, PAYLOAD_FAN_4},
  .group_base = group_base_topic,
  .avty_topic = MQTT_PREFIX TOPIC_CONNECTED,
  .t_group = TOPIC_GROUP,
};
```

Replace (`src/discovery.cpp`, lines 111-116):

```cpp
void discovery_setup() {
  strncpy(base_topic, MQTT_PREFIX, sizeof(base_topic));
  base_topic[sizeof(base_topic) - 1] = '\0';
  const size_t n = strlen(base_topic);
  if (n > 0 && base_topic[n - 1] == '/') base_topic[n - 1] = '\0';
  modes_ok
```

with:

```cpp
// A prefix without its trailing slash, for a payload's "~".
static void strip_slash(char* dst, size_t size, const char* prefix) {
  strncpy(dst, prefix, size);
  dst[size - 1] = '\0';
  const size_t n = strlen(dst);
  if (n > 0 && dst[n - 1] == '/') dst[n - 1] = '\0';
}

void discovery_setup() {
  strip_slash(base_topic, sizeof(base_topic), MQTT_PREFIX);
  strip_slash(group_base_topic, sizeof(group_base_topic), GROUP_ROOT);
  modes_ok
```

- [ ] **Step 9: Build (clean build procedure)**

Stage: `git add lib/mhi_pure/mhi_discovery.h lib/mhi_pure/mhi_discovery.cpp test/test_mhi_discovery/test_mhi_discovery.cpp test/fixtures/discovery test/fixtures/discovery_all tools/discovery_payloads.cpp src/support.h src/MHI-AC-Ctrl.h src/discovery.cpp`, then build `d1_mini`, `ci-ha-discovery`, `ci-ha-discovery-outdoor` and `ci-all-options` in `.pio/ci-tree`.
Expected: all four report `SUCCESS`, with no warning from `src/`.

- [ ] **Step 10: Run the whole suite, then commit**

Run: `pio test -e native` → `230 test cases: 230 succeeded`, and `git status --porcelain -- test/fixtures` shows only the staged changes of Step 6.

```bash
git add lib/mhi_pure/mhi_discovery.h lib/mhi_pure/mhi_discovery.cpp test/test_mhi_discovery/test_mhi_discovery.cpp test/fixtures/discovery test/fixtures/discovery_all tools/discovery_payloads.cpp src/support.h src/MHI-AC-Ctrl.h src/discovery.cpp
git commit -m "feat: the Group role row, the outdoor rows on the group root, and the energy row retired (#22)

Longest rows unchanged: default 701 B, uitkijk 910 B, slaapkamer 928 B of the 1024 B buffer.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013aKH8NAdtagPe21HRXJdt5"
```

---

### Task 4: #22 core: `reset_system_values()` and the widened fields

**Files:**
- Modify: `src/MHI-AC-Ctrl-core.h` (the old operating-data fields; the `reset_old_values()` declaration): lines 111-131, 173 (each counted after the edits listed before it)
- Modify: `src/MHI-AC-Ctrl-core.cpp` (`reset_old_values()`): lines 26-47

**Interfaces:**
- Produces: `void MHI_AC_Ctrl_Core::reset_system_values()`, public; Task 6 calls it on `MHI_GROUP_ACT_START`. `reset_old_values()` calls it, so after a connect too, each of the 11 goes out at its first reading.

There is no host test: the core includes `Arduino.h`. Spec §9 leaves the sentinels to the units (§11). The decode compares a one-byte reading (`MOSI_frame[DB11]` or `[DB10]`) with the old value, both promoted to `int`, so `0xFFFF` equals no reading of the ten widened fields, and nothing else in the decode changes. COMP compares its 16-bit pair and already used `0xffff` before this change, as spec §4.3 notes.

- [ ] **Step 1: The fields and the declaration**

In `src/MHI-AC-Ctrl-core.h`:

Replace (`src/MHI-AC-Ctrl-core.h`, lines 111-131):

```cpp
    // old operating data
    uint16_t op_kwh_old;
    byte op_mode_old;
    byte op_settemp_old;
    byte op_return_air_old;
    byte op_iu_fanspeed_old;
    byte op_thi_r1_old;
    byte op_thi_r2_old;
    byte op_thi_r3_old;
    byte op_total_iu_run_old;
    byte op_outdoor_old;
    byte op_tho_r1_old;
    byte op_total_comp_run_old;
    byte op_ct_old;
    byte op_tdsh_old;
    byte op_protection_no_old;
    byte op_ou_fanspeed_old;
    byte op_defrost_old;
    uint16_t op_comp_old;
    byte op_td_old;
    uint16_t op_ou_eev1_old;
```

with:

```cpp
    // old operating data
    uint16_t op_kwh_old;
    byte op_mode_old;
    byte op_settemp_old;
    byte op_return_air_old;
    byte op_iu_fanspeed_old;
    byte op_thi_r1_old;
    byte op_thi_r3_old;
    byte op_total_iu_run_old;
    uint16_t op_ou_eev1_old;
    // The 11 system values (fork #22 spec §4.1): 16 bits wide, so the
    // sentinel 0xFFFF equals no one-byte reading (reset_system_values()).
    uint16_t op_outdoor_old;
    uint16_t op_ct_old;
    uint16_t op_comp_old;
    uint16_t op_defrost_old;
    uint16_t op_total_comp_run_old;
    uint16_t op_protection_no_old;
    uint16_t op_td_old;
    uint16_t op_tdsh_old;
    uint16_t op_tho_r1_old;
    uint16_t op_thi_r2_old;
    uint16_t op_ou_fanspeed_old;
```

Replace (`src/MHI-AC-Ctrl-core.h`, line 173):

```cpp
    void reset_old_values();              // resets the 'old' variables ensuring that all status information are resend
```

with:

```cpp
    void reset_old_values();              // resets the 'old' variables ensuring that all status information are resend
    void reset_system_values();           // the same for the 11 system values only: each is published at its next reading (fork #22)
```

- [ ] **Step 2: The reset**

In `src/MHI-AC-Ctrl-core.cpp`:

Replace (`src/MHI-AC-Ctrl-core.cpp`, lines 26-47):

```cpp
  // old operating data
  op_kwh_old = 0xffff;
  op_mode_old = 0xff;
  op_settemp_old = 0xff;
  op_return_air_old = 0xff;
  op_iu_fanspeed_old = 0xff;
  op_thi_r1_old = 0x00;
  op_thi_r2_old = 0x00;
  op_thi_r3_old = 0x00;
  op_total_iu_run_old = 0;
  op_outdoor_old = 0xff;
  op_tho_r1_old = 0x00;
  op_total_comp_run_old = 0;
  op_ct_old = 0xff;
  op_tdsh_old = 0xff;
  op_protection_no_old = 0xff;
  op_ou_fanspeed_old = 0xff;
  op_defrost_old = 0x00;
  op_comp_old = 0xffff;
  op_td_old  = 0x00;
  op_ou_eev1_old = 0xffff;
}
```

with:

```cpp
  // old operating data
  op_kwh_old = 0xffff;
  op_mode_old = 0xff;
  op_settemp_old = 0xff;
  op_return_air_old = 0xff;
  op_iu_fanspeed_old = 0xff;
  op_thi_r1_old = 0x00;
  op_thi_r3_old = 0x00;
  op_total_iu_run_old = 0;
  op_ou_eev1_old = 0xffff;
  reset_system_values();
}

// The group root starts empty and a new publisher must fill it in its first
// operating-data cycle (fork #22 spec §4.3). The old sentinels were valid
// readings for several of these (DEFROST "Off" was 0x00, OUTDOOR 0xff is
// 40.25 degC), so those values were never republished after a connect.
void MHI_AC_Ctrl_Core::reset_system_values() {
  op_outdoor_old = 0xffff;
  op_ct_old = 0xffff;
  op_comp_old = 0xffff;
  op_defrost_old = 0xffff;
  op_total_comp_run_old = 0xffff;
  op_protection_no_old = 0xffff;
  op_td_old = 0xffff;
  op_tdsh_old = 0xffff;
  op_tho_r1_old = 0xffff;
  op_thi_r2_old = 0xffff;
  op_ou_fanspeed_old = 0xffff;
}
```

- [ ] **Step 3: Build (clean build procedure)**

Stage `src/MHI-AC-Ctrl-core.h src/MHI-AC-Ctrl-core.cpp`, then build `d1_mini`, `ci-extended-frame` and `ci-all-options`.
Expected: `SUCCESS` for all three, with no warning from `src/`.

- [ ] **Step 4: Commit**

```bash
git add src/MHI-AC-Ctrl-core.h src/MHI-AC-Ctrl-core.cpp
git commit -m "feat: reset_system_values(): each of the 11 system values goes out at its next reading (#22)

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013aKH8NAdtagPe21HRXJdt5"
```

---

### Task 5: #22 configuration: the checks, `HA_OUTDOOR_DEVICE` removed, the derived outdoor ID, the two discovery cursors, the CI environments

**Files:**
- Modify: `src/support.h`: the `HA_OUTDOOR_DEVICE`/`HA_OUTDOOR_ID` block, `TELEMETRY_PERIOD`, after the `ROOM_TEMP_DS18X20` error, the declarations: lines 110-115, 145-150, 247-251, 298 (each counted after the edits listed before it)
- Modify: `src/support.cpp`: includes, `publishTelemetry()`, after `note_frame_result()`: lines 5, 240, 231-233 (each counted after the edits listed before it)
- Modify: `src/discovery.h`: line 9
- Modify: `src/discovery.cpp`: lines 11-14, 38, 82-87, 100-101, 114-115, 121-151, 177-179 (each counted after the edits listed before it)
- Modify: `platformio.ini`: `[env:ci-ha-discovery-outdoor]`, `[env:ci-all-options]`: lines 124-134, 158-159 (each counted after the edits listed before it)

**Interfaces:**
- Consumes: the Task 1 rules (`mhi_group_root_valid`, `mhi_group_record_packet_max`, `mhi_group_starts_with`, `mhi_group_host_valid`, `mhi_group_prefix_valid`, `mhi_group_id_valid`, `mhi_group_default_outdoor_id_len`, `mhi_group_default_outdoor_id`, `MHI_GROUP_ID_MAX`); `GROUP_ROOT` and `mhi_discovery_is_outdoor_row()` from Task 3.
- Produces:
  - `const char* outdoor_id()` in `support.h`/`support.cpp`: `HA_OUTDOOR_ID`, or the ID derived from `GROUP_ROOT`;
  - `void discovery_start_outdoor()` and `void discovery_cancel_outdoor()` in `discovery.h`;
  - the `#error` for `HA_OUTDOOR_DEVICE`, and `TELEMETRY_PERIOD` 1..86400.

Decision: the compile-time checks live in `src/support.h`, after `#include <PubSubClient.h>` (they need `MQTT_MAX_PACKET_SIZE`). They call the `constexpr` rules of `mhi_group.h`, which the host tests pin.
Answer 1 and 2 of the owner (spec §2 since `04fbecb`): a derived outdoor ID longer than 40 characters is refused ("define HA_OUTDOOR_ID"); an explicit `HA_OUTDOOR_ID` must pass the record's full `outdoor_id` rule, and `MQTT_PREFIX` the record's prefix rule, space and control characters included.
Decision: `outdoor_id()` derives the ID once, on first use. Discovery and, in Task 6, the group both use it, so there is one derivation and no ordering between `discovery_setup()` and `group_setup()`.
Decision: `src/discovery.cpp`'s `ctx` loses its `const`, and `discovery_setup()` fills in `ctx.outdoor_id`. That costs no RAM: a `const` struct of pointers sits in DRAM on the ESP8266 too.
Decision: the outdoor cursor waits until the unit cursor is done, then sends one row per pass. That is how the plan reads "after the unit rows" (§8.3).
Decision: `publishTelemetry()` drops its `TELEMETRY_PERIOD > 0 &&`, because the build now refuses 0.
Decision: `ci-ha-discovery-outdoor` also sets an explicit `HA_OUTDOOR_ID`, while `ci-all-options` derives it. Both branches of `#ifdef HA_OUTDOOR_ID` then compile, which is what the matrix is for. Both environments set the shared root `mhi-ci/outdoor/`.
Decision: the `#error "HA_OUTDOOR_DEVICE needs HA_DISCOVERY"` in `src/discovery.cpp` goes, because `support.h` now refuses the flag in every build.

- [ ] **Step 1: `src/support.h`**

Replace (`src/support.h`, lines 110-115):

```cpp
// The outdoor device (fork #19): off by default -- only one indoor unit of a
// multi-split should publish it (spec §3). Needs HA_DISCOVERY.
//#define HA_OUTDOOR_DEVICE true
#ifndef HA_OUTDOOR_ID
#define HA_OUTDOOR_ID HA_ID_PREFIX "_outdoor"
#endif
```

with:

```cpp
// The outdoor device (fork #19). Every unit is a candidate publisher since
// fork #22: the group's publisher sends it (GROUP_ROOT below).
//#define HA_OUTDOOR_ID "ac_outdoor"                // default: derived at boot from GROUP_ROOT, <slug of GROUP_ROOT>_outdoor, so every member of the group derives the same
```

Replace (`src/support.h`, lines 145-150):

```cpp
#ifndef TELEMETRY_PERIOD
#define TELEMETRY_PERIOD 300                        // seconds between publishes of RSSI, Uptime and FreeHeap while MQTT is connected; 0 publishes them at MQTT connect only
#endif
#if TELEMETRY_PERIOD * 1000UL > 0xFFFFFFFFUL
#error "TELEMETRY_PERIOD must be below 4294967 seconds (49.7 days): the interval is kept in 32-bit milliseconds"
#endif
```

with:

```cpp
#ifndef TELEMETRY_PERIOD
#define TELEMETRY_PERIOD 300                        // seconds between publishes of RSSI, Uptime, FreeHeap and the group record while MQTT is connected, 1..86400
#endif
#if TELEMETRY_PERIOD < 1 || TELEMETRY_PERIOD > 86400
#error "TELEMETRY_PERIOD must be 1..86400 seconds: the group record is re-sent every period and a unit counts as gone after 3 of them, in 32-bit milliseconds (fork #22)"
#endif
```

Replace (`src/support.h`, lines 247-251):

```cpp
#ifdef ROOM_TEMP_DS18X20
#if (TEMP_MEASURE_PERIOD == 0)
#error "You have to use a value>0 for TEMP_MEASURE_PERIOD when you want to use DS18x20 as an external temperature sensor"
#endif
#endif
```

with:

```cpp
#ifdef ROOM_TEMP_DS18X20
#if (TEMP_MEASURE_PERIOD == 0)
#error "You have to use a value>0 for TEMP_MEASURE_PERIOD when you want to use DS18x20 as an external temperature sensor"
#endif
#endif

#ifdef HA_OUTDOOR_DEVICE
#error "HA_OUTDOOR_DEVICE was removed (fork #22): every unit is a candidate publisher now; give the units of one outdoor unit the same GROUP_ROOT"
#endif

// The group's configuration (fork #22 spec §2), checked with the rules the
// units apply to each other's records.
#include "mhi_group.h"
static_assert(mhi_group_root_valid(GROUP_ROOT), "GROUP_ROOT must be 1..64 characters, end in / and contain no + # ;");
// PubSubClient3 drops a received packet larger than its buffer whole, and the
// firmware never enlarges it: a longer root would make every unit drop the
// others' records, and two publishers would never see each other.
static_assert(mhi_group_record_packet_max(sizeof(GROUP_ROOT) - 1) <= MQTT_MAX_PACKET_SIZE,
              "the largest group record packet does not fit PubSubClient's receive buffer: shorten GROUP_ROOT");
static_assert(!mhi_group_starts_with(GROUP_ROOT "members/", MQTT_SET_PREFIX),
              "<GROUP_ROOT>members/ must not start with MQTT_SET_PREFIX: the records would be read as commands");
static_assert(mhi_group_host_valid(HOSTNAME), "HOSTNAME must be 1..32 characters without / + # ; \" \\");
// Every record carries MQTT_PREFIX and the outdoor ID, and a peer rejects a
// record whose fields break the record's rules (spec §5.1): check them here.
static_assert(mhi_group_prefix_valid(MQTT_PREFIX),
              "MQTT_PREFIX must be 1..64 characters, end in / and contain no ; + # \" \\, space or control character");
#ifdef HA_OUTDOOR_ID
static_assert(mhi_group_id_valid(HA_OUTDOOR_ID),
              "HA_OUTDOOR_ID must be 1..40 characters without ; / + # \" \\, space or control character");
#else
static_assert(mhi_group_default_outdoor_id_len(GROUP_ROOT) <= MHI_GROUP_ID_MAX,
              "the outdoor ID derived from GROUP_ROOT is longer than 40 characters: define HA_OUTDOOR_ID");
#endif
```

Replace (`src/support.h`, line 298):

```cpp
void note_frame_result(int ret);  // count mhi_ac_ctrl_core.loop()'s return towards FrameErrors/FrameTimeouts (fork #21)
```

with:

```cpp
void note_frame_result(int ret);  // count mhi_ac_ctrl_core.loop()'s return towards FrameErrors/FrameTimeouts (fork #21)
const char* outdoor_id();                                     // HA_OUTDOOR_ID, or the one derived from GROUP_ROOT (fork #22)
```

- [ ] **Step 2: `src/support.cpp`**

Replace (`src/support.cpp`, line 5):

```cpp
#include "mhi_frame_stats.h"
```

with:

```cpp
#include "mhi_frame_stats.h"
#include "mhi_group.h"
```

Replace (`src/support.cpp`, line 240):

```cpp
  if (TELEMETRY_PERIOD > 0 && MQTTclient.connected() && mhi_retry_due(&telemetry_pacer, now, TELEMETRY_PERIOD * 1000UL))
```

with:

```cpp
  if (MQTTclient.connected() && mhi_retry_due(&telemetry_pacer, now, TELEMETRY_PERIOD * 1000UL))
```

Replace (`src/support.cpp`, lines 231-233):

```cpp
void note_frame_result(int ret) {
  mhi_frame_stats_count(&frame_stats, ret);
}
```

with:

```cpp
void note_frame_result(int ret) {
  mhi_frame_stats_count(&frame_stats, ret);
}

// Derived once, on first use: every member of a group derives the same ID from
// the same GROUP_ROOT (fork #22 spec §2). support.h checks both at compile time.
const char* outdoor_id() {
  static char id[MHI_GROUP_ID_MAX + 1];
  if (id[0] == '\0') {
#ifdef HA_OUTDOOR_ID
    strncpy(id, HA_OUTDOOR_ID, sizeof(id) - 1);
#else
    mhi_group_default_outdoor_id(GROUP_ROOT, id, sizeof(id));
#endif
  }
  return id;
}
```

- [ ] **Step 3: `src/discovery.h` and `src/discovery.cpp`**

Replace (`src/discovery.h`, line 9):

```cpp
void discovery_loop();     // every loop() pass: publishes the next row while connected, then the Discovery topic
```

with:

```cpp
void discovery_loop();     // every loop() pass: publishes the next unit row while connected, then the Discovery topic; then the next outdoor row
// The outdoor device's rows have their own cursor, and only the group moves it (fork #22 spec §8.3).
void discovery_start_outdoor();   // the six outdoor rows go out, one per loop() pass, after the unit rows
void discovery_cancel_outdoor();  // outdoor rows not yet sent are not sent
```

Delete (`src/discovery.cpp`, lines 11-14, and the blank line after it):

```cpp
// Outside the #ifdef below: inside it this could never fire.
#if defined(HA_OUTDOOR_DEVICE) && !defined(HA_DISCOVERY)
#error "HA_OUTDOOR_DEVICE needs HA_DISCOVERY"
#endif
```

Replace (`src/discovery.cpp`, line 38):

```cpp
static const MhiDiscoveryCtx ctx = {
```

with:

```cpp
// Not const: discovery_setup() fills in outdoor_id, which is derived at boot.
static MhiDiscoveryCtx ctx = {
```

Replace (`src/discovery.cpp`, lines 82-87):

```cpp
#ifdef HA_OUTDOOR_DEVICE
  .has_outdoor = true,
#else
  .has_outdoor = false,
#endif
  .outdoor_id = HA_OUTDOOR_ID, .outdoor_name = HA_OUTDOOR_NAME,
```

with:

```cpp
  .has_outdoor = true,  // every unit is a candidate publisher; the group starts the outdoor cursor (fork #22)
  .outdoor_id = NULL, .outdoor_name = HA_OUTDOOR_NAME,  // outdoor_id: discovery_setup()
```

Replace (`src/discovery.cpp`, lines 100-101):

```cpp
static bool modes_ok = false;
static uint8_t next_row = MHI_DISCOVERY_ROWS;  // nothing to publish until a connect
```

with:

```cpp
static bool modes_ok = false;
static uint8_t next_row = MHI_DISCOVERY_ROWS;          // the unit rows; nothing to publish until a connect
static uint8_t next_outdoor_row = MHI_DISCOVERY_ROWS;  // the outdoor rows; only the group starts it
```

Replace (`src/discovery.cpp`, lines 114-115):

```cpp
  strip_slash(group_base_topic, sizeof(group_base_topic), GROUP_ROOT);
  modes_ok
```

with:

```cpp
  strip_slash(group_base_topic, sizeof(group_base_topic), GROUP_ROOT);
  ctx.outdoor_id = outdoor_id();
  modes_ok
```

Replace (`src/discovery.cpp`, lines 121-151):

```cpp
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
  else if (!mhi_discovery_row_enabled(row, &ctx)) {
    // skipped: has_lr or has_outdoor is off in this build
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
```

with:

```cpp
void discovery_restart() {
  next_row = 0;
}

void discovery_start_outdoor() {
  next_outdoor_row = MHI_DISCOVERY_OU_OUTDOOR;
}

void discovery_cancel_outdoor() {
  next_outdoor_row = MHI_DISCOVERY_ROWS;
}

// One row, retained. A row that is not part of this build is skipped, one that
// does not fit is refused, so a discovery topic never gets an empty payload.
static void publish_row(MhiDiscoveryRow row) {
  // Static, not on the stack: loop() runs on the ESP8266's 4 KB cont stack and
  // the publish path runs below this frame.
  static char payload[MHI_DISCOVERY_BUF];
  char topic[MHI_DISCOVERY_TOPIC_MAX];
  if (!mhi_discovery_row_enabled(row, &ctx)) return;  // has_lr off, or the retired energy row
  if (mhi_discovery_topic(row, &ctx, topic, sizeof(topic)) == 0 ||
      mhi_discovery_build(row, &ctx, payload, sizeof(payload)) == 0) {
    Serial.printf_P(PSTR("HA_DISCOVERY: row %u does not fit, not published\n"), (unsigned)row);
    return;
  }
  MQTTclient.publish(topic, payload, true);
}

void discovery_loop() {
  if (!MQTTclient.connected()) return;
  if (next_row < MHI_DISCOVERY_ROWS) {
    const MhiDiscoveryRow row = (MhiDiscoveryRow)next_row++;
    if (row == MHI_DISCOVERY_CLIMATE && !modes_ok) {
      // skipped: said so at boot, and the Discovery topic says "modes"
    }
    else if (!mhi_discovery_is_outdoor_row(row)) {  // the outdoor rows are the group's
      publish_row(row);
    }
    if (next_row == MHI_DISCOVERY_ROWS) {
      if (modes_ok)
        output_P((ACStatus)type_status, PSTR(TOPIC_DISCOVERY), PSTR(PAYLOAD_DISCOVERY_OK));
      else
        output_P((ACStatus)type_status, PSTR(TOPIC_DISCOVERY), PSTR(PAYLOAD_DISCOVERY_MODES));
    }
  }
  else if (next_outdoor_row < MHI_DISCOVERY_ROWS) {  // after the unit rows, one per pass
    const MhiDiscoveryRow row = (MhiDiscoveryRow)next_outdoor_row++;
    if (mhi_discovery_is_outdoor_row(row))
      publish_row(row);
    else
      next_outdoor_row = MHI_DISCOVERY_ROWS;  // past the outdoor block: done
  }
}
```

Replace (`src/discovery.cpp`, lines 177-179):

```cpp
void discovery_setup() {}
void discovery_restart() {}
void discovery_loop() {}
```

with:

```cpp
void discovery_setup() {}
void discovery_restart() {}
void discovery_loop() {}
void discovery_start_outdoor() {}
void discovery_cancel_outdoor() {}
```

- [ ] **Step 4: `platformio.ini`**

Replace (`platformio.ini`, lines 124-134):

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
```

with:

```ini
; Home Assistant discovery with the extended frame, as a member of a group
; with a shared root (fork #22) and an explicit outdoor ID; ci-all-options
; derives the ID instead, so both branches of HA_OUTDOOR_ID compile.
[env:ci-ha-discovery-outdoor]
extends = ci
build_flags =
	${esp8266.build_flags}
	${ha_payloads.build_flags}
	-D USE_EXTENDED_FRAME_SIZE=true
	-D HA_DISCOVERY=true
	-D GROUP_ROOT=\"mhi-ci/outdoor/\"
	-D HA_OUTDOOR_ID=\"mhi_ac_ctrl_ci_outdoor\"
	-D HA_ENTITY_PREFIX=\"mhi_ac_ctrl_ci\"
```

Replace (`platformio.ini`, lines 158-159):

```ini
	-D HA_DISCOVERY=true
	-D HA_OUTDOOR_DEVICE=true
```

with:

```ini
	-D HA_DISCOVERY=true
	-D GROUP_ROOT=\"mhi-ci/outdoor/\"
```

- [ ] **Step 5: Host tests unchanged**

Run: `pio test -e native` → `230 test cases: 230 succeeded`, fixtures unchanged.

- [ ] **Step 6: Build all 12 (clean build procedure)**

Stage `src/support.h src/support.cpp src/discovery.h src/discovery.cpp platformio.ini` and build all 12 environments.
Expected: 12 × `SUCCESS`, with no warning from `src/`.

- [ ] **Step 7: The refusals fire**

In the worktree, still at the snapshot, run each of these (quoting exactly as written):

```bash
cd .pio/ci-tree
for f in '-D HA_OUTDOOR_DEVICE=true' '-D TELEMETRY_PERIOD=0' '-D GROUP_ROOT=\"airco/outdoor\"' \
         '-D GROUP_ROOT=\"a-very-long-group-root-for-the-outdoor-units-of-this-house/\"' \
         '-D HA_OUTDOOR_ID="\"ac outdoor\""' \
         '-D GROUP_ROOT=\"g/\" -D MQTT_PREFIX=\"1234567890123456789012345678901234567890123456789012345678901234/\"'; do
  echo "== $f"; PLATFORMIO_BUILD_FLAGS="$f" pio run -e ci-ha-discovery 2>&1 | grep -E "error:|FAILED" | sort -u | head -3
done
pio run -e ci-ha-discovery | tail -1   # back to a normal build: SUCCESS
cd ../..
```
Expected, in this order, each followed by `FAILED`:
- `#error "HA_OUTDOOR_DEVICE was removed (fork #22): every unit is a candidate publisher now; give the units of one outdoor unit the same GROUP_ROOT"`;
- `#error "TELEMETRY_PERIOD must be 1..86400 seconds: ...`;
- `static assertion failed: GROUP_ROOT must be 1..64 characters, end in / and contain no + # ;`;
- `static assertion failed: the outdoor ID derived from GROUP_ROOT is longer than 40 characters: define HA_OUTDOOR_ID`;
- `static assertion failed: HA_OUTDOOR_ID must be 1..40 characters without ; / + # " \, space or control character`;
- `static assertion failed: MQTT_PREFIX must be 1..64 characters, end in / and contain no ; + # " \, space or control character`.

Each case prints only its own message:
- The 60-character root passes the 64-character bound and the packet check (247 ≤ 256), and trips only the derived-ID length.
- The 65-character `MQTT_PREFIX` comes with a valid `GROUP_ROOT`, so only the prefix rule trips.

- [ ] **Step 8: Commit**

```bash
git add src/support.h src/support.cpp src/discovery.h src/discovery.cpp platformio.ini
git commit -m "feat: GROUP_ROOT checks, HA_OUTDOOR_DEVICE removed, derived outdoor ID, a separate outdoor discovery cursor (#22)

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013aKH8NAdtagPe21HRXJdt5"
```

---

### Task 6: #22 group glue: `src/group.{h,cpp}`, the system-value gate, the subscription, the `main.cpp` hooks

**Files:**
- Create: `src/group.h`
- Create: `src/group.cpp`
- Modify: `src/support.h` (declaration): line 299
- Modify: `src/support.cpp` (include, `uptime_seconds()`, subscription, classification and gate in `output_P()`): lines 4, 250-251, 324-325, 352, 385-386 (each counted after the edits listed before it)
- Modify: `src/main.cpp` (include, callback, `setup()`, `loop()`): lines 12, 76, 588, 627-630 (each counted after the edits listed before it)

**Interfaces:**
- Consumes: all of Task 2; `reset_system_values()` (Task 4); `outdoor_id()`, `discovery_start_outdoor()` and `discovery_cancel_outdoor()` (Task 5); `GROUP_ROOT`, `GROUP_OP_PREFIX`, `TOPIC_GROUP` (Task 3); `output_P`, `MQTTclient`, `mhi_copy_payload`.
- Produces:
  - `void group_setup()`, `void group_connected()`, `void group_loop()`;
  - `bool group_handle_message(const char* topic, const uint8_t* payload, unsigned int length)`;
  - `bool group_may_publish_system()`;
  - `uint32_t uptime_seconds()` in `support.h`/`support.cpp`.

Decision: `group_handle_message()` treats every topic outside `MQTT_SET_PREFIX` as the group's. The only other subscriptions are the group's, so a message on a peer's old connected topic (its prefix changed; answer 7 keeps that subscription until the next connect) is swallowed and never answers `cmd_received` "unknown command".
Decision: `group_connected()` also cancels the outdoor cursor, so nothing of an earlier connection goes out in the grace period.
Decision: the tick's actions are carried out in this order: subscribe, cancel the outdoor configs (demote), record, `Group`, `reset_system_values()`, start the outdoor configs. The record goes before `Group`, as spec §6.1 step 5 and §6.3 list them.
Decision: a peer's `connected` payload is compared as raw bytes with its length (`payload_is()`), so no copy is needed and a longer payload cannot match after truncation.
Decision: `group_setup()` runs right after `discovery_setup()` in `setup()`. `outdoor_id()` derives lazily, so the order does not matter.

- [ ] **Step 1: `src/group.h`**

```cpp
// The outdoor election on the unit (fork #22; spec
// docs/superpowers/specs/2026-09-19-outdoor-election-design.md §7): owns the
// election state, feeds it the group's MQTT messages and carries out what its
// tick asks for. The rules themselves are lib/mhi_pure/mhi_group, host-tested.

#pragma once

#include <stdint.h>

void group_setup();      // once at boot
void group_connected();  // after every MQTT connect: clears the peer table, starts the 5 s grace period
void group_loop();       // every loop() pass: the election, and what it asks for, while connected

// Offered every MQTT message first. True when the topic was the group's (a
// record, a peer's connected topic, or a message on the old connected topic of
// a peer whose prefix changed); such a message never answers on cmd_received.
bool group_handle_message(const char* topic, const uint8_t* payload, unsigned int length);

// The system-value gate of output_P() (spec §6.4): past the grace period, role 1 and state 1.
bool group_may_publish_system();
```

- [ ] **Step 2: `src/group.cpp`**

```cpp
#include "group.h"

#include <Arduino.h>

#include "MHI-AC-Ctrl-core.h"
#include "discovery.h"
#include "mhi_group.h"
#include "mhi_mqtt.h"
#include "support.h"

extern MHI_AC_Ctrl_Core mhi_ac_ctrl_core;  // main.cpp

static MhiGroup group;
// Static, not on the stack: loop() runs on the ESP8266's 4 KB cont stack.
static MhiGroupActions actions;

static const char kMembers[] PROGMEM = GROUP_ROOT "members/";

void group_setup() {
  mhi_group_init(&group, HOSTNAME, outdoor_id(), MQTT_PREFIX, TELEMETRY_PERIOD);
}

void group_connected() {
  mhi_group_connect(&group, millis());
  discovery_cancel_outdoor();  // nothing of an earlier connection goes out in the grace period
}

static void publish_record() {
  char record[MHI_GROUP_RECORD_MAX + 1];
  if (mhi_group_own_record(&group, uptime_seconds(), record, sizeof(record)) == 0) {
    Serial.println(F("Group: this unit's record does not fit, not published"));
    return;
  }
  MQTTclient.publish(GROUP_ROOT "members/" HOSTNAME, record, true);
}

static void publish_state(uint8_t state) {
  char text[4];
  itoa(state, text, 10);
  output_P((ACStatus)type_status, PSTR(TOPIC_GROUP), text);
}

// A peer's <prefix><TOPIC_CONNECTED>, from loop(): subscribing inside the MQTT
// callback would overwrite the buffer the callback's topic and payload live in.
// Never unsubscribed: a peer's old topic goes at the next connect (spec §6.1).
static void subscribe_connected(const char* prefix) {
  char topic[MHI_GROUP_ROOT_MAX + sizeof(TOPIC_CONNECTED)];
  snprintf(topic, sizeof(topic), "%s%s", prefix, TOPIC_CONNECTED);
  if (!MQTTclient.subscribe(topic))
    Serial.printf_P(PSTR("Group: subscribe %s failed\n"), topic);
}

void group_loop() {
  if (!MQTTclient.connected()) return;
  mhi_group_tick(&group, millis(), &actions);
  if (actions.subscribe[0] != '\0') subscribe_connected(actions.subscribe);
  if (actions.flags & MHI_GROUP_ACT_DEMOTE) discovery_cancel_outdoor();
  if (actions.flags & MHI_GROUP_ACT_RECORD) publish_record();
  if (actions.flags & MHI_GROUP_ACT_STATE) publish_state(actions.state);
  if (actions.flags & MHI_GROUP_ACT_START) mhi_ac_ctrl_core.reset_system_values();
  if (actions.flags & MHI_GROUP_ACT_CONFIGS) discovery_start_outdoor();
}

static bool payload_is(const uint8_t* payload, unsigned int length, const char* text) {
  return length == strlen(text) && memcmp(payload, text, length) == 0;
}

bool group_handle_message(const char* topic, const uint8_t* payload, unsigned int length) {
  if (strncmp_P(topic, kMembers, sizeof(kMembers) - 1) == 0) {
    const char* host = topic + sizeof(kMembers) - 1;
    // Its own copy: the command path's is 32 bytes, a record up to 140.
    char record[MHI_GROUP_RECORD_MAX + 1];
    mhi_copy_payload(record, sizeof(record), payload, length);
    switch (mhi_group_on_record(&group, host, record, length, millis())) {
      case MHI_GROUP_REC_OK:
        break;
      case MHI_GROUP_REC_INVALID:
        Serial.printf_P(PSTR("Group: the record of %s is not valid, ignored\n"), host);
        break;
      case MHI_GROUP_REC_FULL:
        Serial.printf_P(PSTR("Group: the table is full and no unit in it is gone, %s ignored\n"), host);
        break;
    }
    return true;
  }
  const char* host = mhi_group_host_of_connected_topic(&group, topic, TOPIC_CONNECTED);
  if (host != NULL) {
    if (payload_is(payload, length, PAYLOAD_CONNECTED_TRUE))
      mhi_group_on_connected(&group, host, true, millis());
    else if (payload_is(payload, length, PAYLOAD_CONNECTED_FALSE))
      mhi_group_on_connected(&group, host, false, millis());
    return true;
  }
  // Every subscription outside MQTT_SET_PREFIX is the group's.
  return strncmp_P(topic, PSTR(MQTT_SET_PREFIX), sizeof(MQTT_SET_PREFIX) - 1) != 0;
}

bool group_may_publish_system() {
  return mhi_group_may_publish_system(&group);
}
```

- [ ] **Step 3: `src/support.h` and `src/support.cpp`**

Replace (`src/support.h`, line 299):

```cpp
const char* outdoor_id();                                     // HA_OUTDOOR_ID, or the one derived from GROUP_ROOT (fork #22)
```

with:

```cpp
const char* outdoor_id();                                     // HA_OUTDOOR_ID, or the one derived from GROUP_ROOT (fork #22)
uint32_t uptime_seconds();                                    // the Uptime counter, advanced to now; the group record carries it (fork #22)
```

Replace (`src/support.cpp`, line 4):

```cpp
#include "mhi_diag.h"
```

with:

```cpp
#include "group.h"
#include "mhi_diag.h"
```

Replace (`src/support.cpp`, lines 250-251):

```cpp
// Called on every loop() pass, connected or not: an outage longer than the
// millis() wrap must not cost the counter a wrap.
```

with:

```cpp
uint32_t uptime_seconds() {
  return mhi_uptime_advance(&uptime_counter, millis());
}

// Called on every loop() pass, connected or not: an outage longer than the
// millis() wrap must not cost the counter a wrap.
```

Replace (`src/support.cpp`, lines 324-325):

```cpp
      MQTTclient.subscribe(MQTT_SET_PREFIX "#");
      return MQTT_RECONNECTED;
```

with:

```cpp
      MQTTclient.subscribe(MQTT_SET_PREFIX "#");
      MQTTclient.subscribe(GROUP_ROOT "members/+");  // every unit's record, this one's included (fork #22)
      return MQTT_RECONNECTED;
```

Replace (`src/support.cpp`, line 352):

```cpp
void output_P(const ACStatus status, PGM_P topic, PGM_P payload) {
```

with:

```cpp
// The outdoor unit's values (fork #22 spec §4.1): the same on every indoor
// unit, so only the group's publisher writes them, under the group root. Only
// their opdata_* statuses: the erropdata_* ones sharing their case labels in
// main.cpp are this unit's own error snapshot and stay under
// MQTT_ERR_OP_PREFIX.
static bool is_system_value(ACStatus status) {
  switch (status) {
    case opdata_outdoor:
    case opdata_ct:
    case opdata_comp:
    case opdata_defrost:
    case opdata_total_comp_run:
    case opdata_protection_no:
    case opdata_td:
    case opdata_tdsh:
    case opdata_tho_r1:
    case opdata_thi_r2:
    case opdata_ou_fanspeed:
      return true;
    default:  // every other status, KWH and OU-EEV1 included: this unit's own
      return false;
  }
}

void output_P(const ACStatus status, PGM_P topic, PGM_P payload) {
```

Replace (`src/support.cpp`, lines 385-386):

```cpp
  else if ((status & 0xc0) == type_opdata)
    prefix = PSTR(MQTT_OP_PREFIX);
```

with:

```cpp
  else if ((status & 0xc0) == type_opdata && is_system_value(status)) {
    if (!group_may_publish_system())
      return;  // not the publisher, or still in the grace period: dropped (spec §6.4)
    prefix = PSTR(GROUP_OP_PREFIX);
  }
  else if ((status & 0xc0) == type_opdata)
    prefix = PSTR(MQTT_OP_PREFIX);
```

- [ ] **Step 4: `src/main.cpp`**

Replace (`src/main.cpp`, line 12):

```cpp
#include "discovery.h"
```

with:

```cpp
#include "discovery.h"
#include "group.h"
```

Replace (`src/main.cpp`, line 76):

```cpp
void MQTT_subscribe_callback(const char* topic, byte* payload, unsigned int length) {
```

with:

```cpp
void MQTT_subscribe_callback(const char* topic, byte* payload, unsigned int length) {
  if (group_handle_message(topic, payload, length))
    return;  // a record or a peer's connected topic: not a command (fork #22)
```

Replace (`src/main.cpp`, line 588):

```cpp
  discovery_setup();
```

with:

```cpp
  discovery_setup();
  group_setup();
```

Replace (`src/main.cpp`, lines 627-630):

```cpp
      discovery_restart();
    }
    ArduinoOTA.handle();
    discovery_loop();
```

with:

```cpp
      discovery_restart();
      group_connected();
    }
    ArduinoOTA.handle();
    discovery_loop();
    group_loop();
```

- [ ] **Step 5: Build all 12 (clean build procedure)**

Stage `src/group.h src/group.cpp src/support.h src/support.cpp src/main.cpp` and build all 12 environments.
Expected: 12 × `SUCCESS`, no warning from `src/`. Note the `RAM:` line of `d1_mini` against 30672 bytes at `04fbecb`; Task 13 reports the final figures.

- [ ] **Step 6: Commit**

```bash
git add src/group.h src/group.cpp src/support.h src/support.cpp src/main.cpp
git commit -m "feat: the outdoor election on the unit: records, Group, the system-value gate and the outdoor configs (#22)

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013aKH8NAdtagPe21HRXJdt5"
```

---

### Task 7: #23 `mhi_safe_mode`: the boot decision and the crashed bit (pure)

**Files:**
- Create: `lib/mhi_pure/mhi_safe_mode.h`
- Create: `lib/mhi_pure/mhi_safe_mode.cpp`
- Test: `test/test_mhi_safe_mode/test_mhi_safe_mode.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces (Task 8 uses these):
  - `#define MHI_SAFE_MAGIC 0x4D484953u`, `MHI_SAFE_THRESHOLD 3`, `MHI_SAFE_CLEAR_MS 120000u`, `MHI_SAFE_RESTART_MS 600000u`, `MHI_SAFE_CRASHED_BIT 0x10000u` (data bit 16)
  - `MHI_RESET_POWER_ON 0` .. `MHI_RESET_EXTERNAL 6`: the SDK's reasons, mirrored
  - `struct MhiSafeBoot { bool safe_mode; uint8_t count; uint8_t entries; }`
  - `MhiSafeBoot mhi_safe_boot(uint32_t reset_reason, const uint32_t rec_in[3], uint32_t rec_out[3])`, as #23 spec §1.4 names it
  - `void mhi_safe_record_clear_count(uint32_t rec[3])`
  - `void mhi_safe_record_mark_crashed(uint32_t rec[3])`: the pure helper of spec §1.4 that sets the crashed bit (`725dd49`)

Decision: a power-on (reason 0) also sets the entries to 0, even when the record happens to be valid: the spec counts entries "since power-on". The usual power-on leaves an invalid record, which counts as 0 anyway.
Decision: the entries saturate at 255, like the count, so the byte never wraps back to 0.
Decision: a power-on ignores the crashed bit: count 0, entries 0, the bit cleared. RTC does not survive a power-on, so a valid marked record then is a leftover, and spec §1.2 already says a power-on sets the count to 0.
Decision: the crashed bit counts with every other reason too (4, 5, 6 and one out of range), because the spec counts "a boot that finds the crashed bit set". With a crash reason (1, 2, 3) it counts once: an exception and a software watchdog run the hook as well.
Decision: the bit counts only in a valid record; in an invalid one it is noise like the rest.
Decision: marking an invalid record makes it valid with count 0, entries 0 and the bit, so a crash before the boot decision ever wrote the record (a global constructor on the first boot of this build) still counts.
Decision: `rec_in` and `rec_out` may be the same array: the function reads before it writes. The glue passes separate arrays, but the tests run the whole boot sequence in place.
Decision: the SDK's reset reasons are mirrored as `MHI_RESET_*`, because `lib/mhi_pure` cannot include `user_interface.h`. Task 8 ties the two together with a `static_assert`, as `support.cpp` does for `mhi_frame_stats`.

- [ ] **Step 1: Write the failing test**

`test/test_mhi_safe_mode/test_mhi_safe_mode.cpp`, covering every item of spec §1.5:
- every reset reason 0-6, and 7 and 255 out of range;
- an invalid record for each of magic, check and garbage;
- count 2 → 3 enters safe mode;
- the cap at 255;
- a crash inside safe mode stays in safe mode;
- a soft restart after safe mode goes back to normal;
- the entries counter;
- the 120 s clear keeps the entries;
- the crashed bit (`725dd49`): with reason 4 it counts as a crash, and with 5, 6 and out of range too; with a crash reason it counts once; every boot clears it; a power-on ignores it; in an invalid record it counts nothing; marking keeps the count and the entries and makes an invalid record valid; and three `abort()` boots in a row enter safe mode.

```cpp
// Host tests for the crash-loop safe mode (fork #23; spec
// docs/superpowers/specs/2026-09-19-safe-mode-and-ride-alongs-design.md §1.5).

#include <string.h>
#include <unity.h>

#include "mhi_safe_mode.h"

void setUp(void) {}
void tearDown(void) {}

// A valid record holding a count and an entries number, as this module writes it.
static void make(uint32_t rec[3], uint8_t count, uint8_t entries) {
  const uint32_t data = (uint32_t)count | ((uint32_t)entries << 8);
  rec[0] = MHI_SAFE_MAGIC;
  rec[1] = data;
  rec[2] = MHI_SAFE_MAGIC ^ data ^ 0xFFFFFFFFu;
}

// The same, with the crashed bit the core's crash hook sets.
static void make_marked(uint32_t rec[3], uint8_t count, uint8_t entries) {
  make(rec, count, entries);
  rec[1] |= MHI_SAFE_CRASHED_BIT;
  rec[2] = MHI_SAFE_MAGIC ^ rec[1] ^ 0xFFFFFFFFu;
}

// A valid record without the crashed bit.
static void assert_valid(const uint32_t rec[3], uint8_t count, uint8_t entries) {
  uint32_t want[3];
  make(want, count, entries);
  TEST_ASSERT_EQUAL_HEX32_ARRAY(want, rec, 3);
}

static void test_the_crash_reasons_count_up(void) {
  static const uint32_t kCrash[] = {MHI_RESET_HW_WDT, MHI_RESET_EXCEPTION, MHI_RESET_SOFT_WDT};
  for (size_t i = 0; i < 3; i++) {
    uint32_t rec[3], out[3];
    make(rec, 1, 2);
    const MhiSafeBoot b = mhi_safe_boot(kCrash[i], rec, out);
    TEST_ASSERT_FALSE(b.safe_mode);
    TEST_ASSERT_EQUAL_UINT8(2, b.count);
    TEST_ASSERT_EQUAL_UINT8(2, b.entries);
    assert_valid(out, 2, 2);
  }
}

static void test_every_other_reason_sets_the_count_to_0(void) {
  // Soft restart (ESP.restart(), OTA, set/reset), deep-sleep wake, external
  // reset, and two reasons out of range: the count goes, the entries stay.
  static const uint32_t kOther[] = {MHI_RESET_SOFT_RESTART, MHI_RESET_DEEP_SLEEP, MHI_RESET_EXTERNAL, 7, 255};
  for (size_t i = 0; i < 5; i++) {
    uint32_t rec[3], out[3];
    make(rec, 2, 3);
    const MhiSafeBoot b = mhi_safe_boot(kOther[i], rec, out);
    TEST_ASSERT_FALSE(b.safe_mode);
    TEST_ASSERT_EQUAL_UINT8(0, b.count);
    TEST_ASSERT_EQUAL_UINT8(3, b.entries);
    assert_valid(out, 0, 3);
  }
}

static void test_power_on_clears_the_entries_too(void) {
  uint32_t rec[3], out[3];
  make(rec, 2, 3);  // a record that happened to survive
  const MhiSafeBoot b = mhi_safe_boot(MHI_RESET_POWER_ON, rec, out);
  TEST_ASSERT_FALSE(b.safe_mode);
  TEST_ASSERT_EQUAL_UINT8(0, b.count);
  TEST_ASSERT_EQUAL_UINT8(0, b.entries);  // "since power-on"
  assert_valid(out, 0, 0);
}

static void test_an_invalid_record_counts_as_0(void) {
  uint32_t bad_magic[3], bad_check[3], garbage[3] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
  make(bad_magic, 2, 5);
  bad_magic[0] ^= 1;
  make(bad_check, 2, 5);
  bad_check[2] ^= 0x100;
  const uint32_t* const kBad[] = {bad_magic, bad_check, garbage};
  for (size_t i = 0; i < 3; i++) {
    uint32_t out[3];
    const MhiSafeBoot b = mhi_safe_boot(MHI_RESET_EXCEPTION, kBad[i], out);
    TEST_ASSERT_FALSE(b.safe_mode);
    TEST_ASSERT_EQUAL_UINT8(1, b.count);  // this crash, counted from 0
    TEST_ASSERT_EQUAL_UINT8(0, b.entries);
    assert_valid(out, 1, 0);
  }
}

static void test_the_third_crash_in_a_row_enters_safe_mode(void) {
  uint32_t rec[3], out[3];
  make(rec, 2, 0);
  const MhiSafeBoot b = mhi_safe_boot(MHI_RESET_EXCEPTION, rec, out);
  TEST_ASSERT_TRUE(b.safe_mode);
  TEST_ASSERT_EQUAL_UINT8(3, b.count);
  TEST_ASSERT_EQUAL_UINT8(1, b.entries);
  assert_valid(out, 3, 1);
}

static void test_the_count_saturates_at_255(void) {
  uint32_t rec[3], out[3];
  make(rec, 255, 7);
  const MhiSafeBoot b = mhi_safe_boot(MHI_RESET_SOFT_WDT, rec, out);
  TEST_ASSERT_TRUE(b.safe_mode);
  TEST_ASSERT_EQUAL_UINT8(255, b.count);
  assert_valid(out, 255, 8);
}

static void test_a_crash_inside_safe_mode_stays_in_safe_mode(void) {
  uint32_t rec[3], out[3];
  make(rec, 3, 1);  // this unit was in safe mode, and crashed there
  const MhiSafeBoot b = mhi_safe_boot(MHI_RESET_HW_WDT, rec, out);
  TEST_ASSERT_TRUE(b.safe_mode);
  TEST_ASSERT_EQUAL_UINT8(4, b.count);
  TEST_ASSERT_EQUAL_UINT8(2, b.entries);  // a boot into safe mode is an entry
}

static void test_the_restart_after_safe_mode_boots_normally(void) {
  uint32_t rec[3], out[3];
  make(rec, 3, 1);  // safe mode ends with ESP.restart(): reason 4
  const MhiSafeBoot b = mhi_safe_boot(MHI_RESET_SOFT_RESTART, rec, out);
  TEST_ASSERT_FALSE(b.safe_mode);
  TEST_ASSERT_EQUAL_UINT8(0, b.count);
  TEST_ASSERT_EQUAL_UINT8(1, b.entries);  // SafeMode 1 at the next connect
}

static void test_the_entries_count_every_boot_into_safe_mode(void) {
  // Power-on (garbage in RTC), three crashes, safe mode, the restart, three
  // crashes again: two entries. The record is read and written in place, as
  // the glue does.
  uint32_t rec[3] = {0x12345678u, 0x9abcdef0u, 0x0badf00du};
  static const uint32_t kBoots[] = {MHI_RESET_POWER_ON, MHI_RESET_EXCEPTION, MHI_RESET_EXCEPTION, MHI_RESET_EXCEPTION,
                                    MHI_RESET_SOFT_RESTART, MHI_RESET_EXCEPTION, MHI_RESET_SOFT_WDT, MHI_RESET_HW_WDT};
  static const bool kSafe[] = {false, false, false, true, false, false, false, true};
  MhiSafeBoot b = {false, 0, 0};
  for (size_t i = 0; i < sizeof(kBoots) / sizeof(kBoots[0]); i++) {
    b = mhi_safe_boot(kBoots[i], rec, rec);
    TEST_ASSERT_EQUAL_MESSAGE(kSafe[i], b.safe_mode, "boot");
  }
  TEST_ASSERT_EQUAL_UINT8(2, b.entries);
  // The entries saturate too.
  make(rec, 2, 255);
  b = mhi_safe_boot(MHI_RESET_EXCEPTION, rec, rec);
  TEST_ASSERT_TRUE(b.safe_mode);
  TEST_ASSERT_EQUAL_UINT8(255, b.entries);
}

static void test_the_120_s_clear_keeps_the_entries(void) {
  uint32_t rec[3];
  make(rec, 2, 3);
  mhi_safe_record_clear_count(rec);
  assert_valid(rec, 0, 3);
  // So the next crash starts a new count instead of entering safe mode.
  const MhiSafeBoot b = mhi_safe_boot(MHI_RESET_EXCEPTION, rec, rec);
  TEST_ASSERT_FALSE(b.safe_mode);
  TEST_ASSERT_EQUAL_UINT8(1, b.count);
  TEST_ASSERT_EQUAL_UINT8(3, b.entries);
  // An invalid record comes out valid, with nothing counted.
  uint32_t garbage[3] = {1, 2, 3};
  mhi_safe_record_clear_count(garbage);
  assert_valid(garbage, 0, 0);
}

// --- the crashed bit: abort(), panic(), assert, new, stack overflow (spec §1.2) ---

static void test_the_crashed_bit_counts_a_restart_as_a_crash(void) {
  // The SDK reports these crashes as reason 4; the hook's bit counts them. The
  // other non-crash reasons (and two out of range) count with the bit too.
  static const uint32_t kReason[] = {MHI_RESET_SOFT_RESTART, MHI_RESET_DEEP_SLEEP, MHI_RESET_EXTERNAL, 7, 255};
  for (size_t i = 0; i < 5; i++) {
    uint32_t rec[3], out[3];
    make_marked(rec, 1, 2);
    const MhiSafeBoot b = mhi_safe_boot(kReason[i], rec, out);
    TEST_ASSERT_FALSE(b.safe_mode);
    TEST_ASSERT_EQUAL_UINT8(2, b.count);
    TEST_ASSERT_EQUAL_UINT8(2, b.entries);
    assert_valid(out, 2, 2);  // and the bit is gone
  }
  // The bit in an invalid record is noise, like the rest of it: nothing counted.
  uint32_t bad[3], out[3];
  make_marked(bad, 1, 2);
  bad[2] ^= 1;
  const MhiSafeBoot b = mhi_safe_boot(MHI_RESET_SOFT_RESTART, bad, out);
  TEST_ASSERT_EQUAL_UINT8(0, b.count);
  assert_valid(out, 0, 0);
}

static void test_the_crashed_bit_and_a_crash_reason_count_once(void) {
  // An exception or a software watchdog runs the hook too: one crash, not two.
  static const uint32_t kCrash[] = {MHI_RESET_HW_WDT, MHI_RESET_EXCEPTION, MHI_RESET_SOFT_WDT};
  for (size_t i = 0; i < 3; i++) {
    uint32_t rec[3], out[3];
    make_marked(rec, 1, 0);
    const MhiSafeBoot b = mhi_safe_boot(kCrash[i], rec, out);
    TEST_ASSERT_FALSE(b.safe_mode);
    TEST_ASSERT_EQUAL_UINT8(2, b.count);
    assert_valid(out, 2, 0);
  }
}

static void test_every_boot_clears_the_crashed_bit(void) {
  for (uint32_t reason = 0; reason <= MHI_RESET_EXTERNAL; reason++) {
    uint32_t rec[3], out[3];
    make_marked(rec, 0, 1);
    mhi_safe_boot(reason, rec, out);
    TEST_ASSERT_EQUAL_HEX32(0, out[1] & MHI_SAFE_CRASHED_BIT);
    TEST_ASSERT_EQUAL_HEX32(MHI_SAFE_MAGIC ^ out[1] ^ 0xFFFFFFFFu, out[2]);
  }
}

static void test_a_power_on_ignores_the_crashed_bit(void) {
  // RTC does not survive a power-on, so a marked record then is a leftover:
  // the power-on resets everything, as it does without the bit.
  uint32_t rec[3], out[3];
  make_marked(rec, 2, 3);
  const MhiSafeBoot b = mhi_safe_boot(MHI_RESET_POWER_ON, rec, out);
  TEST_ASSERT_FALSE(b.safe_mode);
  TEST_ASSERT_EQUAL_UINT8(0, b.count);
  TEST_ASSERT_EQUAL_UINT8(0, b.entries);
  assert_valid(out, 0, 0);
}

static void test_marking_sets_the_bit_and_keeps_the_record(void) {
  uint32_t rec[3], want[3];
  make(rec, 2, 3);
  mhi_safe_record_mark_crashed(rec);
  make_marked(want, 2, 3);
  TEST_ASSERT_EQUAL_HEX32_ARRAY(want, rec, 3);
  mhi_safe_record_mark_crashed(rec);  // twice is the same
  TEST_ASSERT_EQUAL_HEX32_ARRAY(want, rec, 3);
  // An invalid record, as before the first boot of this build: valid, count 0, marked.
  uint32_t garbage[3] = {1, 2, 3};
  mhi_safe_record_mark_crashed(garbage);
  make_marked(want, 0, 0);
  TEST_ASSERT_EQUAL_HEX32_ARRAY(want, garbage, 3);
}

static void test_three_aborts_in_a_row_enter_safe_mode(void) {
  // Power-on, then three boots after an abort(): the hook marks the record, the
  // SDK says reason 4. The record is read and written in place, as on the unit.
  uint32_t rec[3] = {0, 0, 0};
  MhiSafeBoot b = mhi_safe_boot(MHI_RESET_POWER_ON, rec, rec);
  TEST_ASSERT_FALSE(b.safe_mode);
  for (int i = 1; i <= 3; i++) {
    mhi_safe_record_mark_crashed(rec);
    b = mhi_safe_boot(MHI_RESET_SOFT_RESTART, rec, rec);
    TEST_ASSERT_EQUAL_UINT8(i, b.count);
  }
  TEST_ASSERT_TRUE(b.safe_mode);
  TEST_ASSERT_EQUAL_UINT8(1, b.entries);
  // The restart at the end of safe mode is a plain reason 4, without the bit.
  b = mhi_safe_boot(MHI_RESET_SOFT_RESTART, rec, rec);
  TEST_ASSERT_FALSE(b.safe_mode);
  TEST_ASSERT_EQUAL_UINT8(0, b.count);
  TEST_ASSERT_EQUAL_UINT8(1, b.entries);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_crash_reasons_count_up);
  RUN_TEST(test_every_other_reason_sets_the_count_to_0);
  RUN_TEST(test_power_on_clears_the_entries_too);
  RUN_TEST(test_an_invalid_record_counts_as_0);
  RUN_TEST(test_the_third_crash_in_a_row_enters_safe_mode);
  RUN_TEST(test_the_count_saturates_at_255);
  RUN_TEST(test_a_crash_inside_safe_mode_stays_in_safe_mode);
  RUN_TEST(test_the_restart_after_safe_mode_boots_normally);
  RUN_TEST(test_the_entries_count_every_boot_into_safe_mode);
  RUN_TEST(test_the_120_s_clear_keeps_the_entries);
  RUN_TEST(test_the_crashed_bit_counts_a_restart_as_a_crash);
  RUN_TEST(test_the_crashed_bit_and_a_crash_reason_count_once);
  RUN_TEST(test_every_boot_clears_the_crashed_bit);
  RUN_TEST(test_a_power_on_ignores_the_crashed_bit);
  RUN_TEST(test_marking_sets_the_bit_and_keeps_the_record);
  RUN_TEST(test_three_aborts_in_a_row_enter_safe_mode);
  return UNITY_END();
}
```

- [ ] **Step 2: Run it and see it fail**

Run: `pio test -e native -f test_mhi_safe_mode`
Expected: build error, `fatal error: mhi_safe_mode.h: No such file or directory`.

- [ ] **Step 3: Implement**

`lib/mhi_pure/mhi_safe_mode.h`:

```cpp
// Crash-loop safe mode (fork #23; spec
// docs/superpowers/specs/2026-09-19-safe-mode-and-ride-alongs-design.md §1).
// A unit that crashes three times in a row, each time within 120 s of its
// boot, starts in safe mode: Wi-Fi and OTA only, for 10 minutes. The count is
// kept in three words of RTC user memory, which survive a reset but not a
// power loss.
//
// Pure logic, no Arduino and no RTC access: the glue (src/safe_mode.cpp)
// reads the words, calls this, and writes them back.

#pragma once

#include <stdint.h>

#define MHI_SAFE_MAGIC 0x4D484953u   // "MHIS"
#define MHI_SAFE_THRESHOLD 3         // crashes in a row that start safe mode
#define MHI_SAFE_CLEAR_MS 120000u    // up this long in normal mode: the count goes back to 0, once per boot
#define MHI_SAFE_RESTART_MS 600000u  // in safe mode: ESP.restart() after this long
#define MHI_SAFE_CRASHED_BIT 0x10000u  // data bit 16: the core's crash hook ran before this boot

// The SDK's reset reasons (rst_info.reason), mirrored so this file needs no
// user_interface.h; src/safe_mode.cpp checks that they match.
#define MHI_RESET_POWER_ON 0
#define MHI_RESET_HW_WDT 1
#define MHI_RESET_EXCEPTION 2
#define MHI_RESET_SOFT_WDT 3
#define MHI_RESET_SOFT_RESTART 4  // ESP.restart(): also how an OTA update and set/reset end
#define MHI_RESET_DEEP_SLEEP 5
#define MHI_RESET_EXTERNAL 6

struct MhiSafeBoot {
  bool safe_mode;
  uint8_t count;    // crashes in a row, this boot's included; saturates at 255
  uint8_t entries;  // boots into safe mode since power-on, this one's included; saturates at 255
};

// The decision at boot (spec §1.2). rec_in is the record as read from RTC
// (<magic, data, check>; anything after a power-on); rec_out gets the record
// to write back, always valid and without the crashed bit. They may be the
// same array. A crash reason (1, 2, 3) or the crashed bit counts one crash,
// except at a power-on.
MhiSafeBoot mhi_safe_boot(uint32_t reset_reason, const uint32_t rec_in[3], uint32_t rec_out[3]);

// The crash hook's part (src/safe_mode.cpp, custom_crash_callback): sets the
// crashed bit and recomputes the check word. A valid record keeps its count
// and entries; an invalid one becomes count 0, entries 0, so the crash is
// still counted.
void mhi_safe_record_mark_crashed(uint32_t rec[3]);

// The 120 s clear: the count goes to 0, the entries stay. The record is valid afterwards.
void mhi_safe_record_clear_count(uint32_t rec[3]);
```

`lib/mhi_pure/mhi_safe_mode.cpp`:

```cpp
#include "mhi_safe_mode.h"

// <magic> <data: count in bits 0-7, entries in bits 8-15, the crashed bit 16> <check>
static uint32_t check_of(uint32_t data) {
  return MHI_SAFE_MAGIC ^ data ^ 0xFFFFFFFFu;
}

static bool valid(const uint32_t rec[3]) {
  return rec[0] == MHI_SAFE_MAGIC && rec[2] == check_of(rec[1]);
}

static void store(uint32_t rec[3], uint8_t count, uint8_t entries, bool crashed) {
  const uint32_t data = (uint32_t)count | ((uint32_t)entries << 8) | (crashed ? MHI_SAFE_CRASHED_BIT : 0u);
  rec[0] = MHI_SAFE_MAGIC;
  rec[1] = data;
  rec[2] = check_of(data);
}

// Hardware watchdog, exception, software watchdog.
static bool is_crash(uint32_t reason) {
  return reason == MHI_RESET_HW_WDT || reason == MHI_RESET_EXCEPTION || reason == MHI_RESET_SOFT_WDT;
}

MhiSafeBoot mhi_safe_boot(uint32_t reset_reason, const uint32_t rec_in[3], uint32_t rec_out[3]) {
  const bool ok = valid(rec_in);  // an invalid record, as after a power-on, counts as 0
  uint8_t count = ok ? (uint8_t)(rec_in[1] & 0xFFu) : 0;
  uint8_t entries = ok ? (uint8_t)((rec_in[1] >> 8) & 0xFFu) : 0;
  // abort(), panic(), a failed assert or new, a stack overflow: reason 4, but the hook marked the record.
  const bool marked = ok && (rec_in[1] & MHI_SAFE_CRASHED_BIT) != 0;
  if (reset_reason == MHI_RESET_POWER_ON) entries = 0;  // the entries count since power-on
  if (is_crash(reset_reason) || (marked && reset_reason != MHI_RESET_POWER_ON)) {  // once, even with both
    if (count < 255) count++;
  }
  else {
    count = 0;  // every other reason, and one out of range
  }
  const bool safe = count >= MHI_SAFE_THRESHOLD;
  if (safe && entries < 255) entries++;
  store(rec_out, count, entries, false);  // every boot clears the crashed bit
  const MhiSafeBoot boot = {safe, count, entries};
  return boot;
}

void mhi_safe_record_clear_count(uint32_t rec[3]) {
  const uint8_t entries = valid(rec) ? (uint8_t)((rec[1] >> 8) & 0xFFu) : 0;
  store(rec, 0, entries, false);
}

void mhi_safe_record_mark_crashed(uint32_t rec[3]) {
  const bool ok = valid(rec);
  store(rec, ok ? (uint8_t)(rec[1] & 0xFFu) : 0, ok ? (uint8_t)((rec[1] >> 8) & 0xFFu) : 0, true);
}
```

- [ ] **Step 4: Run it and see it pass**

Run: `pio test -e native -f test_mhi_safe_mode`
Expected: `16 Tests 0 Failures 0 Ignored` and `OK`. Then `pio test -e native` → `246 test cases: 246 succeeded`.

- [ ] **Step 5: Check that the tests catch a broken rule**

For each mutation, change the named line in a scratch copy of the whole tree (a copy of `mhi_safe_mode.{h,cpp}` alone is not enough: the test includes the header by its quoted name) and run `pio test -e native -f test_mhi_safe_mode`. Each one must fail at least one test. The trial run's counts, all caught:

| mutation | failing tests |
|---|---|
| threshold 4 | 3 |
| threshold 2 | 5 |
| soft watchdog not a crash | 3 |
| exception not a crash | 5 |
| the count wraps at 255 | 1 |
| the entries wrap at 255 | 1 |
| the entries never counted | 5 |
| power-on keeps the entries | 2 |
| the check word without the inversion | 15 |
| the check word not verified | 2 |
| the clear drops the entries | 1 |
| other reasons keep the count | 6 |
| the crashed bit ignored | 2 |
| the crashed bit counted at a power-on | 1 |
| the crashed bit read from an invalid record | 1 |
| the boot keeps the crashed bit | 5 |
| a crash reason and the bit count twice | 1 |
| marking drops the count | 2 |
| marking drops the entries | 1 |
| marking leaves an invalid record invalid | 1 |
| marking without the bit | 2 |

Do not commit a mutation.

- [ ] **Step 6: Commit**

```bash
git add lib/mhi_pure/mhi_safe_mode.h lib/mhi_pure/mhi_safe_mode.cpp test/test_mhi_safe_mode/test_mhi_safe_mode.cpp
git commit -m "feat: the crash-loop safe-mode decision as pure logic, host-tested (#23)

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013aKH8NAdtagPe21HRXJdt5"
```

---

### Task 8: #23 safe mode on the unit: the RTC record, the crash hook, `setup()`/`loop()`, `SafeMode`, `set/reset crash`

**Files:**
- Create: `src/safe_mode.h`
- Create: `src/safe_mode.cpp`
- Modify: `src/MHI-AC-Ctrl.h`: lines 44-46, 302-304 (each counted after the edits listed before it)
- Modify: `src/main.cpp`: lines 19-20, 25-26, 31, 228-234, 582-586, 633-635, 671 (each counted after the edits listed before it)
- Modify: `src/support.cpp`: lines 11, 288 (each counted after the edits listed before it)

**Interfaces:**
- Consumes: Task 7's `mhi_safe_boot`, `mhi_safe_record_clear_count`, `mhi_safe_record_mark_crashed`, `MHI_SAFE_CLEAR_MS`, `MHI_SAFE_RESTART_MS`, `MHI_RESET_*`; `initWiFi()`, `setupWiFi(int&)`, `setupOTA()`, `output_P()`.
- Produces:
  - `bool safe_mode_boot()`, `void safe_mode_clear_after_boot(uint32_t now_ms)`, `uint8_t safe_mode_entries()`, `void safe_mode_test_crash()` (`src/safe_mode.h`);
  - `extern "C" void custom_crash_callback(struct rst_info*, uint32_t, uint32_t)` in `src/safe_mode.cpp`: the core's weak hook, overridden;
  - `TOPIC_SAFE_MODE "SafeMode"` and `PAYLOAD_REQUEST_RESET_CRASH "crash"` (`src/MHI-AC-Ctrl.h`).

**What was checked in the core before this task was written (framework-arduinoespressif8266 3.30102.0, the one `espressif8266@4.2.1` pins):**
- **Who else uses RTC user memory.** A grep of the core, its libraries and its bootloaders, of `.pio/libdeps/d1_mini`, `src/` and `lib/` for `rtcUserMemory`, `system_rtc_mem_*`, `RTC_MEM`, `RTC_USER_MEM`, `0x600011` and `0x600012` finds one user: the OTA boot command.
  - `Updater.cpp:350,363` writes it through `eboot_command_write()` at `RTC_MEM` = `0x60001200`;
  - the bootloader `eboot.c:228-268` reads it and, when it is valid, clears its magic and CRC words before it loads the app.
  - `0x60001200` is user block 0 (spec §1.3 as corrected in `a6f16e3`). `rtcUserMemoryRead(offset)` calls `system_rtc_mem_read(64 + offset)` (`Esp.cpp:175-190`); system block 0 is `0x60001100`, since `hwdt_app_entry.cpp:394,629` reads the reset reason as `RTC_SYS[0]` at `0x60001100`; so user block 0 = `0x60001100` + 64 × 4 = `0x60001200`.
  - The core's own comment says so too (`Esp.cpp:165-168`): "the eboot command will be stored into the first 128 bytes of user data".
  - So the record goes to user block 32, the first word after the command, as the one define `SAFE_MODE_RTC_BLOCK 32`. An OTA update, including the one that rescues a unit from safe mode, leaves it alone.
- **What reset reason a deliberate crash leaves.** `postmortem_report()` (`core_esp8266_postmortem.cpp:132-146`), which the core wraps around the SDK's `system_restart_local`, reads `rst_info` from RTC system block 0. It accepts reasons 1, 2 and 3 as stored there by the SDK's fatal-exception handler, then restarts with `__real_system_restart_local()` (line 279). The next boot copies `system_get_rst_info()` into `resetInfo` (`core_esp8266_main.cpp:636-637`), which `ESP.getResetInfoPtr()` returns.
  - `abort()` and `panic()` take a different path, `raise_exception()` (lines 318-322): the core's own reason `REASON_USER_SWEXCEPTION_RST` (254), and a direct restart without the SDK handler, so nothing writes reason 2 for them.
  - So the crash has to be a real CPU exception. A store to address 0 raises StoreProhibited (29).
  - The pointer is a `static volatile` object: the compiler must load it at run time, so it can neither drop the store nor turn it into a trap instruction under `-fisolate-erroneous-paths-dereference`. `-Werror` accepts it.
  - The trial build's disassembly of `safe_mode_test_crash()` in `d1_mini` shows exactly that: `l32r a2, …; movi.n a3, 0; memw; l32i.n a2, a2, 0; memw; s32i.n a3, a2, 0; ret.n`, with the pointer in `.bss` (`_ZZ20safe_mode_test_crashvE6target`).
- **Where the crash hook runs (spec §1.2 since `725dd49`).** `custom_crash_callback(struct rst_info*, uint32_t, uint32_t)` is declared weak, as an alias of an empty `__custom_crash_callback`, inside the file's `extern "C"` block (`core_esp8266_postmortem.cpp:77-83`), so the override needs C linkage. `postmortem_report()` calls it after the stack dump and right before `__real_system_restart_local()` (lines 276-279), and it has no early return. Every software crash path ends there:
  - an exception and a software watchdog: the SDK calls `system_restart_local`, and the build links with `-Wl,-wrap,system_restart_local` (`tools/platformio-build.py:108`), so that call reaches `__wrap_system_restart_local`, which jumps to `postmortem_report()` (lines 118-130). In `NONOSDK22x_190703`, the default SDK, `app_main.o` and `pp.o` of `libmain.a`/`libpp.a` reference `system_restart_local` as undefined, which is what `-wrap` redirects;
  - `abort()`, `__unhandled_exception()` (a failed `new`: `abi.cpp:44,55`), `__assert_func()` and `__panic_func()` call `raise_exception()`, which calls `postmortem_report()` (lines 318-356);
  - the cont-stack check after every `loop()` (`core_esp8266_main.cpp:259`, `cont_util.cpp:49-58`) calls `__stack_chk_fail()`, which calls `postmortem_report()` (lines 359-371).
  - A hardware watchdog runs no code at all; it keeps reason 1.
  - `ESP.restart()` never gets there. `system_restart()` is defined in the same object as `system_restart_local` (`user_interface.o`), so `-wrap` does not touch its reference: in the trial build of this task (`d1_mini`), the timer function `system_restart()` arms is the literal `0x402468b0`, the real `system_restart_local`, while `__wrap_system_restart_local` is at `0x402139a4`. An OTA update and `set/reset` therefore never mark the record.
  - `postmortem_report()` itself reads RTC with `system_rtc_mem_read()` (line 137), the call `ESP.rtcUserMemoryRead()` wraps (`Esp.cpp:175-190`, a bounds check and nothing else), so the hook's two RTC accesses are what the crash context already does. The trial build's `custom_crash_callback` makes exactly three calls: `EspClass::rtcUserMemoryRead`, `mhi_safe_record_mark_crashed` and `EspClass::rtcUserMemoryWrite`.

Decision: the glue lives in `src/safe_mode.{h,cpp}` rather than in `main.cpp`. `support.cpp` needs the entries for `SafeMode`; the RTC access, the reason mapping and the crash are then in one place; and `main.cpp`'s `setup()` and `loop()` only branch. `main.cpp` still makes the decision first thing in `setup()`, as spec §1.4 says.
Decision: `safe_mode_boot()` runs right after `Serial.begin()`, before the existing `delay(100)`, and prints one Serial line with the reason, the count and the entries. The banner, "Starting MHI-AC-Ctrl build …", still comes before the safe-mode branch.
Decision: in safe mode, `loop()` runs the existing Wi-Fi state machine when the link is down, and `ArduinoOTA.handle()` when it is up. That is today's normal branch without MQTT and without the 12-minute rescan for a stronger AP, which a 10-minute safe mode never reaches.
Decision: the 120 s clear writes RTC only when the count is not 0. The boot already wrote a valid record, and the flag makes it once per boot.
Decision: `set/reset crash` answers `o.k.` on `cmd_received` and waits 500 ms before the crash, as `set/reset reset` does before its restart.
Decision: `SafeMode` is published at every connect right after `ResetReason`, which it explains.
Decision: the crash hook reads the record from RTC, marks it and writes it back; it does not use the copy `safe_mode_boot()` keeps in RAM. A crash before the boot decision still counts, and a crash may have hit that RAM. The hook calls nothing but the two RTC accesses and the pure helper: no Serial, no allocation, no wait.

- [ ] **Step 1: The two new defines**

In `src/MHI-AC-Ctrl.h`:

Replace (`src/MHI-AC-Ctrl.h`, lines 44-46):

```cpp
#ifndef TOPIC_RESET_REASON
#define TOPIC_RESET_REASON "ResetReason"
#endif
```

with:

```cpp
#ifndef TOPIC_RESET_REASON
#define TOPIC_RESET_REASON "ResetReason"
#endif
#ifndef TOPIC_SAFE_MODE
#define TOPIC_SAFE_MODE "SafeMode"             // boots into safe mode since power-on, a bare integer, at every connect (fork #23)
#endif
```

Replace (`src/MHI-AC-Ctrl.h`, lines 302-304):

```cpp
#ifndef PAYLOAD_REQUEST_RESET
#define PAYLOAD_REQUEST_RESET "reset"
#endif
```

with:

```cpp
#ifndef PAYLOAD_REQUEST_RESET
#define PAYLOAD_REQUEST_RESET "reset"
#endif
#ifndef PAYLOAD_REQUEST_RESET_CRASH
#define PAYLOAD_REQUEST_RESET_CRASH "crash"    // set/reset: one deliberate exception, reset reason 2; the safe-mode proof (fork #23)
#endif
```

- [ ] **Step 2: `src/safe_mode.h`**

```cpp
// Crash-loop safe mode on the unit (fork #23; spec
// docs/superpowers/specs/2026-09-19-safe-mode-and-ride-alongs-design.md §1):
// the glue around lib/mhi_pure/mhi_safe_mode. The reset reason and three words
// of RTC user memory go in, the decision comes out. Nothing here blocks, loops
// or touches the network. safe_mode.cpp also defines the core's crash hook,
// custom_crash_callback(), which marks the record at every software crash.

#pragma once

#include <stdint.h>

bool safe_mode_boot();                             // first thing in setup(), after Serial.begin(): true starts safe mode
void safe_mode_clear_after_boot(uint32_t now_ms);  // every normal loop() pass: at 120 s uptime, once, the crash count goes back to 0
uint8_t safe_mode_entries();                       // boots into safe mode since power-on: the SafeMode topic
void safe_mode_test_crash();                       // set/reset crash: one deliberate CPU exception, reset reason 2 (spec §1.5)
```

- [ ] **Step 3: `src/safe_mode.cpp`**

```cpp
#include "safe_mode.h"

#include <Arduino.h>
#include <user_interface.h>  // struct rst_info and the REASON_* values; the header has its own extern "C"

#include "mhi_safe_mode.h"

// The record's place (spec §1.3): RTC user block 32, three words. User block 0
// is 0x60001200, where eboot keeps its 128-byte OTA command (blocks 0-31), so
// block 32 is the first word an OTA does not overwrite.
#define SAFE_MODE_RTC_BLOCK 32

// mhi_safe_mode numbers the reasons itself, because lib/mhi_pure cannot include
// the SDK's user_interface.h; this is where the two spellings are tied together.
static_assert(MHI_RESET_POWER_ON == REASON_DEFAULT_RST && MHI_RESET_HW_WDT == REASON_WDT_RST &&
                  MHI_RESET_EXCEPTION == REASON_EXCEPTION_RST && MHI_RESET_SOFT_WDT == REASON_SOFT_WDT_RST &&
                  MHI_RESET_SOFT_RESTART == REASON_SOFT_RESTART && MHI_RESET_DEEP_SLEEP == REASON_DEEP_SLEEP_AWAKE &&
                  MHI_RESET_EXTERNAL == REASON_EXT_SYS_RST,
              "mhi_safe_mode's reset reasons are the SDK's");

static uint32_t record[3];              // as written to RTC at boot, and at the 120 s clear
static MhiSafeBoot boot = {false, 0, 0};
static bool count_cleared = false;      // the 120 s clear happens once per boot

bool safe_mode_boot() {
  const uint32_t reason = ESP.getResetInfoPtr()->reason;
  uint32_t in[3] = {0, 0, 0};  // an invalid record if the read is refused
  ESP.rtcUserMemoryRead(SAFE_MODE_RTC_BLOCK, in, sizeof(in));
  boot = mhi_safe_boot(reason, in, record);
  ESP.rtcUserMemoryWrite(SAFE_MODE_RTC_BLOCK, record, sizeof(record));
  Serial.printf_P(PSTR("\nSafe mode check: reset reason %u, crashes in a row %u, safe-mode entries %u\n"),
                  (unsigned)reason, (unsigned)boot.count, (unsigned)boot.entries);
  return boot.safe_mode;
}

void safe_mode_clear_after_boot(uint32_t now_ms) {
  if (count_cleared || now_ms < MHI_SAFE_CLEAR_MS) return;
  count_cleared = true;
  if (boot.count == 0) return;  // the record already says 0
  mhi_safe_record_clear_count(record);
  ESP.rtcUserMemoryWrite(SAFE_MODE_RTC_BLOCK, record, sizeof(record));
}

uint8_t safe_mode_entries() {
  return boot.entries;
}

// The core's weak crash hook (core_esp8266_postmortem.cpp:77-83), defined here.
// postmortem_report() calls it on every software crash path, right before the
// restart (line 276): an exception and a software watchdog through the SDK's
// system_restart_local, which the build wraps; abort(), panic(), a failed
// assert and a failed new through raise_exception(); the cont-stack check after
// every loop() through __stack_chk_fail(). The SDK reports the last group as
// reason 4, a plain restart, so the record says it was a crash. ESP.restart()
// never gets here: its timer calls the unwrapped restart. This runs inside the
// crash, so it only reads and writes the three RTC words, as postmortem_report()
// itself reads RTC (line 137): no Serial, no allocation, nothing that waits.
extern "C" void custom_crash_callback(struct rst_info*, uint32_t, uint32_t) {
  uint32_t rec[3] = {0, 0, 0};  // an invalid record if the read is refused
  ESP.rtcUserMemoryRead(SAFE_MODE_RTC_BLOCK, rec, sizeof(rec));
  mhi_safe_record_mark_crashed(rec);
  ESP.rtcUserMemoryWrite(SAFE_MODE_RTC_BLOCK, rec, sizeof(rec));
}

// A store to address 0 raises the CPU exception StoreProhibited (29). The SDK's
// fatal-exception handler stores rst_info with REASON_EXCEPTION_RST in RTC
// before it restarts; the core's postmortem reads it back from there
// (core_esp8266_postmortem.cpp, postmortem_report()), and the next boot reports
// reason 2. abort() and panic() do not go through that handler: the next boot
// sees reason 4 and counts them only through the crash hook above, so they
// cannot prove reason 2. The pointer is a volatile object, so the compiler
// has to load it at run time: it can neither drop the store nor replace it
// with a trap instruction.
void safe_mode_test_crash() {
  static volatile uint32_t* volatile target = nullptr;
  *target = 0;
}
```

- [ ] **Step 4: `src/main.cpp`**

Replace (`src/main.cpp`, lines 19-20):

```cpp
#include "mhi_mqtt.h"
#include "mhi_status.h"
```

with:

```cpp
#include "mhi_mqtt.h"
#include "mhi_safe_mode.h"
#include "mhi_status.h"
```

Replace (`src/main.cpp`, lines 25-26):

```cpp
#include "mhi_vanes_lr.h"
#include "support.h"
```

with:

```cpp
#include "mhi_vanes_lr.h"
#include "safe_mode.h"
#include "support.h"
```

Replace (`src/main.cpp`, line 31):

```cpp
POWER_STATUS power_status = unknown;
```

with:

```cpp
POWER_STATUS power_status = unknown;

// Fork #23: decided first thing in setup(). In safe mode loop() runs only
// Wi-Fi and OTA, and restarts after 10 minutes.
static bool safe_mode = false;
```

Replace (`src/main.cpp`, lines 228-234):

```cpp
    if (strcmp_P(payload_str, PSTR(PAYLOAD_REQUEST_RESET)) == 0) {
      publish_cmd_ok();
      delay(500);
      ESP.restart();
    }
    else
      publish_cmd_invalidparameter();
```

with:

```cpp
    if (strcmp_P(payload_str, PSTR(PAYLOAD_REQUEST_RESET)) == 0) {
      publish_cmd_ok();
      delay(500);
      ESP.restart();
    }
    else if (strcmp_P(payload_str, PSTR(PAYLOAD_REQUEST_RESET_CRASH)) == 0) {
      // The safe-mode proof (fork #23 spec §1.5): three of these, each within
      // 120 s of the boot before, start safe mode. Every crash is one we send.
      publish_cmd_ok();
      delay(500);
      safe_mode_test_crash();
    }
    else
      publish_cmd_invalidparameter();
```

Replace (`src/main.cpp`, lines 582-586):

```cpp
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println(F("Starting MHI-AC-Ctrl build " VERSION));
```

with:

```cpp
void setup() {
  Serial.begin(115200);
  // Fork #23: before anything that crashed on the last boots can run again.
  // RTC reads, a pure function and one RTC write: nothing that blocks or
  // needs the network.
  safe_mode = safe_mode_boot();
  delay(100);
  Serial.println();
  Serial.println(F("Starting MHI-AC-Ctrl build " VERSION));
  if (safe_mode) {
    // Wi-Fi and OTA only: no pin measurement, no DS18x20, no MQTT, no AC core
    // (MISO is never driven), no discovery, no group.
    Serial.println(F("SAFE MODE: three crashes in a row; Wi-Fi and OTA only, a normal boot in 10 min"));
    initWiFi();
    setupOTA();
    return;
  }
```

Replace (`src/main.cpp`, lines 633-635):

```cpp
  static unsigned long previousMillis = millis();

  if (((WiFi.status() != WL_CONNECTED)  || 
```

with:

```cpp
  static unsigned long previousMillis = millis();

  if (safe_mode) {  // fork #23: Wi-Fi and OTA only, then ESP.restart(), reason 4: a normal boot
    if (WiFi.status() != WL_CONNECTED || WiFiStatus != WIFI_CONNECT_OK)
      setupWiFi(WiFiStatus);
    else
      ArduinoOTA.handle();
    if (millis() >= MHI_SAFE_RESTART_MS) {
      Serial.println(F("SAFE MODE: 10 min are up, restarting"));
      ESP.restart();
    }
    return;
  }

  if (((WiFi.status() != WL_CONNECTED)  || 
```

Replace (`src/main.cpp`, line 671):

```cpp
  publishTelemetry();  // every pass, connected or not, so the uptime counter never misses a millis() wrap
```

with:

```cpp
  publishTelemetry();  // every pass, connected or not, so the uptime counter never misses a millis() wrap
  safe_mode_clear_after_boot(millis());  // fork #23: up 120 s, so the crashes before do not count towards a loop
```

- [ ] **Step 5: `SafeMode` at every connect (`src/support.cpp`)**

Replace (`src/support.cpp`, line 11):

```cpp
#include "mhi_uptime.h"
```

with:

```cpp
#include "mhi_uptime.h"
#include "safe_mode.h"
```

Replace (`src/support.cpp`, line 288):

```cpp
      output_P((ACStatus)type_status, PSTR(TOPIC_RESET_REASON), ESP.getResetReason().c_str());
```

with:

```cpp
      output_P((ACStatus)type_status, PSTR(TOPIC_RESET_REASON), ESP.getResetReason().c_str());
      itoa(safe_mode_entries(), strtmp, 10);  // fork #23: 0 on a healthy unit
      output_P((ACStatus)type_status, PSTR(TOPIC_SAFE_MODE), strtmp);
```

- [ ] **Step 6: Build all 12 (clean build procedure)**

Stage `src/safe_mode.h src/safe_mode.cpp src/MHI-AC-Ctrl.h src/main.cpp src/support.cpp` and build all 12 environments.
Expected: 12 × `SUCCESS`, no warning from `src/`. `pio test -e native` is unchanged: `246 test cases: 246 succeeded`.

- [ ] **Step 7: The crash really stores to address 0**

```bash
~/.platformio/packages/toolchain-xtensa/bin/xtensa-lx106-elf-objdump -d --no-show-raw-insn .pio/ci-tree/.pio/build/d1_mini/firmware.elf \
  | awk '/<_Z20safe_mode_test_crashv>:/{f=1} f{print} f&&/ret/{exit}'
```
Expected: `l32i.n` of the pointer, then `s32i.n a3, a2, 0` and `ret.n`. No `ill`, and no missing store.

- [ ] **Step 8: The crash hook is this task's, not the core's empty default**

```bash
~/.platformio/packages/toolchain-xtensa/bin/xtensa-lx106-elf-nm .pio/ci-tree/.pio/build/d1_mini/firmware.elf | grep -E ' (__)?custom_crash_callback$'
```
Expected: one line, `T custom_crash_callback`: this task's. Before this task there are two lines with one address, `T __custom_crash_callback` and `W custom_crash_callback`, the core's empty default; with the override, `--gc-sections` drops the default.

- [ ] **Step 9: Commit**

```bash
git add src/safe_mode.h src/safe_mode.cpp src/MHI-AC-Ctrl.h src/main.cpp src/support.cpp
git commit -m "feat: crash-loop safe mode on the unit, the crash hook, the SafeMode topic and set/reset crash (#23)

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013aKH8NAdtagPe21HRXJdt5"
```

---

### Task 9: #24 the Restart button and the `Discovery` `skipped` value

**Files:**
- Modify: `lib/mhi_pure/mhi_discovery.h`: lines 46-47, 114-115 (each counted after the edits listed before it)
- Modify: `lib/mhi_pure/mhi_discovery.cpp`: lines 23-29, 91-97, 335 (each counted after the edits listed before it)
- Test: `test/test_mhi_discovery/test_mhi_discovery.cpp`: lines 30-33, 56-57, 75-78, 101-102, 131, 560, 648 (each counted after the edits listed before it)
- Fixtures: create `test/fixtures/discovery/restart.txt` and `test/fixtures/discovery_all/restart.txt`
- Modify: `tools/discovery_payloads.cpp`: lines 23, 43, 80, 106-107 (each counted after the edits listed before it)
- Modify: `src/support.h`: lines 140-142
- Modify: `src/MHI-AC-Ctrl.h`: lines 189, 363-365 (each counted after the edits listed before it)
- Modify: `src/discovery.cpp`: lines 57, 97-98, 103, 125-131, 140-154, 165-181 (each counted after the edits listed before it)

**Interfaces:**
- Consumes: Task 3's table, context and fixtures; Task 5's two cursors and `publish_row()`.
- Produces:
  - `MHI_DISCOVERY_RESTART`, the last row, so `MHI_DISCOVERY_ROWS` is 24;
  - `MhiDiscoveryCtx` gains `const char* t_request_reset` and `const char* request_reset`, after `t_group`;
  - `HA_NAME_RESTART "Restart"` (`src/support.h`) and `PAYLOAD_DISCOVERY_SKIPPED "skipped"` (`src/MHI-AC-Ctrl.h`);
  - `--name-restart` in the renderer;
  - `publish_row()` in `src/discovery.cpp` returns `bool`: false only for a row that did not fit.

**Home Assistant, checked in its source** (`home-assistant/core`, branch `dev`, commit `69a5efb37b8b`, fetched 19 Sep 2026; Context7's docs name the button but not the abbreviations):
- `homeassistant/components/mqtt/abbreviations.py`: `"cmd_t": "command_topic"` (line 33), `"dev_cla": "device_class"` (47), `"ent_cat": "entity_category"` (57), `"pl_prs": "payload_press"` (150).
- `homeassistant/components/mqtt/button.py`: `command_topic` is required, `payload_press` defaults to `"PRESS"`, so the row sets it to `reset`, `retain` defaults to false, and `device_class` goes through `DEVICE_CLASSES_SCHEMA`.
- `homeassistant/components/button/const.py:25`: `ButtonDeviceClass.RESTART = "restart"`.
- `homeassistant/components/mqtt/schemas.py:183` allows `entity_category`, and `homeassistant/const.py` defines `EntityCategory.CONFIG = "config"`.
- A button has no state topic. Its state in Home Assistant is the time of its last press, `unknown` until the first; Task 12's check allows for that.

Decision: the Restart row sets `diagnostic = false` and writes `"ent_cat":"config"` itself, because `tail()` only knows `diagnostic`.
Decision: after the outdoor pass, `skipped` goes out once, when the cursor runs past the outdoor block. Nothing goes out when all six fit, as spec §2.1 says. A cancelled outdoor pass publishes nothing.
Decision: the outdoor pass publishes `skipped` only while the topic says `ok`, so the order is `modes` > `skipped` > `ok` across both passes: a unit whose climate row was skipped keeps `modes`, and a second `skipped` is not sent again. A connect sets the flag back until the unit rows are through.
Decision: the `Discovery` status stays glue (two flags in `src/discovery.cpp`), with no host test. It is two `if`s on flags the glue owns, and `post-flash-check.sh` reads it on the units (Task 12).
Decision: a test counts the rows a unit publishes: 17 with the 33-byte frame, 15 without. That makes "17 entities per unit" a host test, not only a toolkit check.

- [ ] **Step 1: Extend the header**

In `lib/mhi_pure/mhi_discovery.h`:

Replace (`lib/mhi_pure/mhi_discovery.h`, lines 46-47):

```cpp
  MHI_DISCOVERY_GROUP_ROLE,     // sensor   <id_prefix>_group_role, on the Group topic (fork #22): a unit row
  MHI_DISCOVERY_ROWS
```

with:

```cpp
  MHI_DISCOVERY_GROUP_ROLE,     // sensor   <id_prefix>_group_role, on the Group topic (fork #22): a unit row
  MHI_DISCOVERY_RESTART,        // button   <id_prefix>_restart, set/reset (fork #24): a unit row
  MHI_DISCOVERY_ROWS
```

Replace (`lib/mhi_pure/mhi_discovery.h`, lines 114-115):

```cpp
  const char* t_group;                // TOPIC_GROUP, relative to base
};
```

with:

```cpp
  const char* t_group;                // TOPIC_GROUP, relative to base
  // Fork #24 (the Restart button).
  const char* t_request_reset;        // TOPIC_REQUEST_RESET, relative to the set prefix
  const char* request_reset;          // PAYLOAD_REQUEST_RESET
};
```

- [ ] **Step 2: Extend the tests**

In `test/test_mhi_discovery/test_mhi_discovery.cpp`:

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, lines 30-33):

```cpp
  .names = {NULL, "Vanes", "Silent", "Problem", "Wiring", "Uptime", "Free heap", "Wi-Fi signal", "Reset reason", "Wi-Fi PHY",
            "Vanes left/right", "3D auto", "Frame errors", "Frame timeouts", "Error code",
            "Temperature", "Current", "Energy", "Compressor frequency", "Defrost", "Compressor run time", "Protection state",
            "Group role"},
```

with:

```cpp
  .names = {NULL, "Vanes", "Silent", "Problem", "Wiring", "Uptime", "Free heap", "Wi-Fi signal", "Reset reason", "Wi-Fi PHY",
            "Vanes left/right", "3D auto", "Frame errors", "Frame timeouts", "Error code",
            "Temperature", "Current", "Energy", "Compressor frequency", "Defrost", "Compressor run time", "Protection state",
            "Group role", "Restart"},
```

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, lines 56-57):

```cpp
  .group_base = "MHI-AC-Ctrl", .avty_topic = "MHI-AC-Ctrl/connected", .t_group = "Group",  // a single split: GROUP_ROOT = MQTT_PREFIX
};
```

with:

```cpp
  .group_base = "MHI-AC-Ctrl", .avty_topic = "MHI-AC-Ctrl/connected", .t_group = "Group",  // a single split: GROUP_ROOT = MQTT_PREFIX
  .t_request_reset = "reset", .request_reset = "reset",
};
```

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, lines 75-78):

```cpp
  .names = {NULL, "louvers", "quiet mode", "fault", "wiring fault", "time since boot", "heap free", "wifi-signal", "restart reason", "wifi-standard",
            "Vanes left/right", "3D auto", "Frame errors", "Frame timeouts", "Error code",
            "Temperature", "Current", "Energy", "Compressor frequency", "Defrost", "Compressor run time", "Protection state",
            "Group role"},
```

with:

```cpp
  .names = {NULL, "louvers", "quiet mode", "fault", "wiring fault", "time since boot", "heap free", "wifi-signal", "restart reason", "wifi-standard",
            "Vanes left/right", "3D auto", "Frame errors", "Frame timeouts", "Error code",
            "Temperature", "Current", "Energy", "Compressor frequency", "Defrost", "Compressor run time", "Protection state",
            "Group role", "Restart"},
```

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, lines 101-102):

```cpp
  .group_base = "airco/uitkijk", .avty_topic = "airco/uitkijk/connected", .t_group = "Group",
};
```

with:

```cpp
  .group_base = "airco/uitkijk", .avty_topic = "airco/uitkijk/connected", .t_group = "Group",
  .t_request_reset = "reset", .request_reset = "reset",
};
```

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, line 131):

```cpp
  "group_role"};
```

with:

```cpp
  "group_role", "restart"};
```

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, line 560):

```cpp
// --- the committed reference payloads ---------------------------------------
```

with:

```cpp
// --- fork #24: the Restart button ------------------------------------------------

static void test_the_restart_button_presses_set_reset(void) {
  char out[MHI_DISCOVERY_BUF], topic[MHI_DISCOVERY_TOPIC_MAX];
  TEST_ASSERT_FALSE(mhi_discovery_is_outdoor_row(MHI_DISCOVERY_RESTART));
  TEST_ASSERT_TRUE(mhi_discovery_row_enabled(MHI_DISCOVERY_RESTART, &kDefault));
  TEST_ASSERT_TRUE(mhi_discovery_topic(MHI_DISCOVERY_RESTART, &kSlaapkamer, topic, sizeof(topic)) > 0);
  TEST_ASSERT_EQUAL_STRING("homeassistant/button/ac_slaapkamer_restart/config", topic);
  TEST_ASSERT_TRUE(mhi_discovery_build(MHI_DISCOVERY_RESTART, &kSlaapkamer, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "{\"~\":\"airco/slaapkamer\",\"name\":\"Restart\",\"uniq_id\":\"ac_slaapkamer_restart\",\"default_entity_id\":\"button.ac_slaapkamer_restart\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"cmd_t\":\"~/set/reset\",\"pl_prs\":\"reset\",\"dev_cla\":\"restart\",\"ent_cat\":\"config\",\"avty_t\":\"~/connected\","));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"dev\":{\"ids\":[\"airco-slaapkamer\"],"));
  TEST_ASSERT_NULL(strstr(out, "diagnostic"));
  TEST_ASSERT_NULL(strstr(out, "stat_t"));  // a button has no state
}

static void test_a_unit_has_17_entities_with_the_33_byte_frame(void) {
  int with_lr = 0, without_lr = 0;
  for (int r = 0; r < MHI_DISCOVERY_ROWS; r++) {
    if (mhi_discovery_is_outdoor_row((MhiDiscoveryRow)r)) continue;
    if (mhi_discovery_row_enabled((MhiDiscoveryRow)r, &kUitkijk)) with_lr++;
    if (mhi_discovery_row_enabled((MhiDiscoveryRow)r, &kDefault)) without_lr++;
  }
  TEST_ASSERT_EQUAL_INT(17, with_lr);
  TEST_ASSERT_EQUAL_INT(15, without_lr);
}

// --- the committed reference payloads ---------------------------------------
```

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, line 648):

```cpp
  RUN_TEST(test_no_row_builds_an_empty_payload);
```

with:

```cpp
  RUN_TEST(test_no_row_builds_an_empty_payload);
  RUN_TEST(test_the_restart_button_presses_set_reset);
  RUN_TEST(test_a_unit_has_17_entities_with_the_33_byte_frame);
```

- [ ] **Step 3: Run the tests and see them fail**

Run: `pio test -e native -f test_mhi_discovery`
Expected: build errors in `lib/mhi_pure/mhi_discovery.cpp`: `static assertion failed: a row appended after MHI_DISCOVERY_GROUP_ROLE: classify it in mhi_discovery_is_outdoor_row() first`, and `enumeration value 'MHI_DISCOVERY_RESTART' not handled in switch [-Werror=switch]`. This is the tripwire Task 3 moved.

- [ ] **Step 4: Implement the builder**

In `lib/mhi_pure/mhi_discovery.cpp`:

Replace (`lib/mhi_pure/mhi_discovery.cpp`, lines 23-29):

```cpp
  "sensor", "sensor", "sensor", "sensor", "binary_sensor", "sensor", "sensor",
  "sensor"};
static const char* const kSuffix[MHI_DISCOVERY_ROWS] = {
  "", "vanes", "silent", "problem", "wiring", "uptime", "free_heap", "rssi", "reset_reason", "wifi_phy",
  "vanes_lr", "3d_auto", "frame_errors", "frame_timeouts", "error_code",
  "outdoor_temp", "current", "energy", "comp_freq", "defrost", "comp_run", "protection",
  "group_role"};
```

with:

```cpp
  "sensor", "sensor", "sensor", "sensor", "binary_sensor", "sensor", "sensor",
  "sensor", "button"};
static const char* const kSuffix[MHI_DISCOVERY_ROWS] = {
  "", "vanes", "silent", "problem", "wiring", "uptime", "free_heap", "rssi", "reset_reason", "wifi_phy",
  "vanes_lr", "3d_auto", "frame_errors", "frame_timeouts", "error_code",
  "outdoor_temp", "current", "energy", "comp_freq", "defrost", "comp_run", "protection",
  "group_role", "restart"};
```

Replace (`lib/mhi_pure/mhi_discovery.cpp`, lines 91-97):

```cpp
// The outdoor block is the seven rows of fork #19, and the table ends with the
// Group role row. MhiDiscoveryRow is append-only: whoever appends a row decides
// in mhi_discovery_is_outdoor_row() whether it is an outdoor row, then moves
// this line.
static_assert(MHI_DISCOVERY_OU_PROTECTION - MHI_DISCOVERY_OU_OUTDOOR == 6, "the outdoor block is OU_OUTDOOR..OU_PROTECTION");
static_assert(MHI_DISCOVERY_GROUP_ROLE + 1 == MHI_DISCOVERY_ROWS,
              "a row appended after MHI_DISCOVERY_GROUP_ROLE: classify it in mhi_discovery_is_outdoor_row() first");
```

with:

```cpp
// The outdoor block is the seven rows of fork #19, and the table ends with the
// Restart button. MhiDiscoveryRow is append-only: whoever appends a row decides
// in mhi_discovery_is_outdoor_row() whether it is an outdoor row, then moves
// this line.
static_assert(MHI_DISCOVERY_OU_PROTECTION - MHI_DISCOVERY_OU_OUTDOOR == 6, "the outdoor block is OU_OUTDOOR..OU_PROTECTION");
static_assert(MHI_DISCOVERY_RESTART + 1 == MHI_DISCOVERY_ROWS,
              "a row appended after MHI_DISCOVERY_RESTART: classify it in mhi_discovery_is_outdoor_row() first");
```

Replace (`lib/mhi_pure/mhi_discovery.cpp`, line 335):

```cpp
    case MHI_DISCOVERY_OU_KWH:  // retired (fork #22): never built, so never published, and never empty
```

with:

```cpp
    case MHI_DISCOVERY_RESTART:
      // set/reset reset, as the command does today (fork #24). Home Assistant's
      // button: cmd_t, pl_prs, dev_cla restart, ent_cat config; no state.
      diagnostic = false;
      put(&o, FMT("\"cmd_t\":\"~/%s%s\",\"pl_prs\":\"%s\",\"dev_cla\":\"restart\",\"ent_cat\":\"config\","),
          c->set_prefix, c->t_request_reset, c->request_reset);
      break;
    case MHI_DISCOVERY_OU_KWH:  // retired (fork #22): never built, so never published, and never empty
```

- [ ] **Step 5: Run the tests and see them pass**

Run: `pio test -e native -f test_mhi_discovery -v`
Expected: `34 Tests 0 Failures 0 Ignored`, and the three high-water marks unchanged (701, 910, 928 of 1024 bytes). `git status --porcelain -- test/fixtures` shows only `?? test/fixtures/discovery/restart.txt` and `?? test/fixtures/discovery_all/restart.txt`. The second reads:
```
homeassistant/button/ac_slaapkamer_restart/config
{"~":"airco/slaapkamer","name":"Restart","uniq_id":"ac_slaapkamer_restart","default_entity_id":"button.ac_slaapkamer_restart","cmd_t":"~/set/reset","pl_prs":"reset","dev_cla":"restart","ent_cat":"config","avty_t":"~/connected","pl_avail":"1","pl_not_avail":"0","dev":{"ids":["airco-slaapkamer"],"name":"AC Slaapkamer","mf":"Mitsubishi Heavy Industries","mdl":"MHI-AC-Ctrl","sw":"batchc-fixture"}}
```

- [ ] **Step 6: The renderer**

In `tools/discovery_payloads.cpp`:

Replace (`tools/discovery_payloads.cpp`, line 23):

```cpp
//   own <--base>/connected. The retired energy row is never rendered.
```

with:

```cpp
//   own <--base>/connected. The retired energy row is never rendered.
// The Restart button (fork #24) adds --name-restart.
```

Replace (`tools/discovery_payloads.cpp`, line 43):

```cpp
  "--name-ou-comp-run", "--name-ou-protection", "--name-group-role"};
```

with:

```cpp
  "--name-ou-comp-run", "--name-ou-protection", "--name-group-role", "--name-restart"};
```

Replace (`tools/discovery_payloads.cpp`, line 80):

```cpp
              "Group role"},
```

with:

```cpp
              "Group role", "Restart"},
```

Replace (`tools/discovery_payloads.cpp`, lines 106-107):

```cpp
    .group_base = "MHI-AC-Ctrl", .avty_topic = "MHI-AC-Ctrl/connected", .t_group = "Group",
  };
```

with:

```cpp
    .group_base = "MHI-AC-Ctrl", .avty_topic = "MHI-AC-Ctrl/connected", .t_group = "Group",
    .t_request_reset = "reset", .request_reset = "reset",
  };
```

Build and check it:
```bash
g++ -std=gnu++17 -Wall -Wextra -Werror -I lib/mhi_pure lib/mhi_pure/mhi_discovery.cpp lib/mhi_pure/mhi_group.cpp tools/discovery_payloads.cpp -o .pio/discovery_payloads
.pio/discovery_payloads | wc -l                 # 15
.pio/discovery_payloads --lr 1 --outdoor 1 | wc -l   # 23: 17 unit rows and 6 outdoor rows
.pio/discovery_payloads | grep -c '^homeassistant/button/MHI-AC-Ctrl_restart/config'   # 1
```

- [ ] **Step 7: The firmware**

In `src/support.h`:

Replace (`src/support.h`, lines 140-142):

```cpp
#ifndef HA_NAME_GROUP_ROLE
#define HA_NAME_GROUP_ROLE "Group role"             // the diagnostic sensor on the Group topic (fork #22)
#endif
```

with:

```cpp
#ifndef HA_NAME_GROUP_ROLE
#define HA_NAME_GROUP_ROLE "Group role"             // the diagnostic sensor on the Group topic (fork #22)
#endif
#ifndef HA_NAME_RESTART
#define HA_NAME_RESTART "Restart"                   // the button that sends set/reset reset (fork #24)
#endif
```

In `src/MHI-AC-Ctrl.h`:

Replace (`src/MHI-AC-Ctrl.h`, line 189):

```cpp
#define TOPIC_DISCOVERY "Discovery"           // retained, after the discovery configs went out: ok, or modes when the climate row was skipped (HA_DISCOVERY)
```

with:

```cpp
#define TOPIC_DISCOVERY "Discovery"           // retained, after the discovery configs went out: ok, modes when the climate row was skipped, skipped when a row did not fit (HA_DISCOVERY)
```

Replace (`src/MHI-AC-Ctrl.h`, lines 363-365):

```cpp
#ifndef PAYLOAD_DISCOVERY_MODES
#define PAYLOAD_DISCOVERY_MODES "modes"
#endif
```

with:

```cpp
#ifndef PAYLOAD_DISCOVERY_MODES
#define PAYLOAD_DISCOVERY_MODES "modes"
#endif
#ifndef PAYLOAD_DISCOVERY_SKIPPED
#define PAYLOAD_DISCOVERY_SKIPPED "skipped"   // a config did not fit its buffer and was not published (fork #24)
#endif
```

In `src/discovery.cpp`:

Replace (`src/discovery.cpp`, line 57):

```cpp
            HA_NAME_OU_COMP_RUN, HA_NAME_OU_PROTECTION, HA_NAME_GROUP_ROLE},
```

with:

```cpp
            HA_NAME_OU_COMP_RUN, HA_NAME_OU_PROTECTION, HA_NAME_GROUP_ROLE, HA_NAME_RESTART},
```

Replace (`src/discovery.cpp`, lines 97-98):

```cpp
  .t_group = TOPIC_GROUP,
};
```

with:

```cpp
  .t_group = TOPIC_GROUP,
  .t_request_reset = TOPIC_REQUEST_RESET, .request_reset = PAYLOAD_REQUEST_RESET,
};
```

Replace (`src/discovery.cpp`, line 103):

```cpp
static uint8_t next_outdoor_row = MHI_DISCOVERY_ROWS;  // the outdoor rows; only the group starts it
```

with:

```cpp
static uint8_t next_outdoor_row = MHI_DISCOVERY_ROWS;  // the outdoor rows; only the group starts it
static bool unit_row_skipped = false;     // a unit row did not fit since the connect (fork #24)
static bool outdoor_row_skipped = false;  // an outdoor row did not fit in this outdoor pass
static bool status_ok = false;            // the Discovery topic says ok; modes > skipped > ok over both passes
```

Replace (`src/discovery.cpp`, lines 125-131):

```cpp
void discovery_restart() {
  next_row = 0;
}

void discovery_start_outdoor() {
  next_outdoor_row = MHI_DISCOVERY_OU_OUTDOOR;
}
```

with:

```cpp
void discovery_restart() {
  next_row = 0;
  unit_row_skipped = false;
  status_ok = false;  // until the unit rows are through
}

void discovery_start_outdoor() {
  next_outdoor_row = MHI_DISCOVERY_OU_OUTDOOR;
  outdoor_row_skipped = false;
}
```

Replace (`src/discovery.cpp`, lines 140-154):

```cpp
// One row, retained. A row that is not part of this build is skipped, one that
// does not fit is refused, so a discovery topic never gets an empty payload.
static void publish_row(MhiDiscoveryRow row) {
  // Static, not on the stack: loop() runs on the ESP8266's 4 KB cont stack and
  // the publish path runs below this frame.
  static char payload[MHI_DISCOVERY_BUF];
  char topic[MHI_DISCOVERY_TOPIC_MAX];
  if (!mhi_discovery_row_enabled(row, &ctx)) return;  // has_lr off, or the retired energy row
  if (mhi_discovery_topic(row, &ctx, topic, sizeof(topic)) == 0 ||
      mhi_discovery_build(row, &ctx, payload, sizeof(payload)) == 0) {
    Serial.printf_P(PSTR("HA_DISCOVERY: row %u does not fit, not published\n"), (unsigned)row);
    return;
  }
  MQTTclient.publish(topic, payload, true);
}
```

with:

```cpp
// One row, retained. A row that is not part of this build is skipped, one that
// does not fit is refused, so a discovery topic never gets an empty payload.
// False only for a row that did not fit: the Discovery topic says "skipped".
static bool publish_row(MhiDiscoveryRow row) {
  // Static, not on the stack: loop() runs on the ESP8266's 4 KB cont stack and
  // the publish path runs below this frame.
  static char payload[MHI_DISCOVERY_BUF];
  char topic[MHI_DISCOVERY_TOPIC_MAX];
  if (!mhi_discovery_row_enabled(row, &ctx)) return true;  // has_lr off, or the retired energy row
  if (mhi_discovery_topic(row, &ctx, topic, sizeof(topic)) == 0 ||
      mhi_discovery_build(row, &ctx, payload, sizeof(payload)) == 0) {
    Serial.printf_P(PSTR("HA_DISCOVERY: row %u does not fit, not published\n"), (unsigned)row);
    return false;
  }
  MQTTclient.publish(topic, payload, true);
  return true;
}
```

Replace (`src/discovery.cpp`, lines 165-181):

```cpp
    else if (!mhi_discovery_is_outdoor_row(row)) {  // the outdoor rows are the group's
      publish_row(row);
    }
    if (next_row == MHI_DISCOVERY_ROWS) {
      if (modes_ok)
        output_P((ACStatus)type_status, PSTR(TOPIC_DISCOVERY), PSTR(PAYLOAD_DISCOVERY_OK));
      else
        output_P((ACStatus)type_status, PSTR(TOPIC_DISCOVERY), PSTR(PAYLOAD_DISCOVERY_MODES));
    }
  }
  else if (next_outdoor_row < MHI_DISCOVERY_ROWS) {  // after the unit rows, one per pass
    const MhiDiscoveryRow row = (MhiDiscoveryRow)next_outdoor_row++;
    if (mhi_discovery_is_outdoor_row(row))
      publish_row(row);
    else
      next_outdoor_row = MHI_DISCOVERY_ROWS;  // past the outdoor block: done
  }
```

with:

```cpp
    else if (!mhi_discovery_is_outdoor_row(row) && !publish_row(row)) {  // the outdoor rows are the group's
      unit_row_skipped = true;
    }
    if (next_row == MHI_DISCOVERY_ROWS) {  // "modes" first, then "skipped" (fork #24 spec §2.1)
      if (!modes_ok)
        output_P((ACStatus)type_status, PSTR(TOPIC_DISCOVERY), PSTR(PAYLOAD_DISCOVERY_MODES));
      else if (unit_row_skipped)
        output_P((ACStatus)type_status, PSTR(TOPIC_DISCOVERY), PSTR(PAYLOAD_DISCOVERY_SKIPPED));
      else
        output_P((ACStatus)type_status, PSTR(TOPIC_DISCOVERY), PSTR(PAYLOAD_DISCOVERY_OK));
      status_ok = modes_ok && !unit_row_skipped;
    }
  }
  else if (next_outdoor_row < MHI_DISCOVERY_ROWS) {  // after the unit rows, one per pass
    const MhiDiscoveryRow row = (MhiDiscoveryRow)next_outdoor_row++;
    if (mhi_discovery_is_outdoor_row(row)) {
      if (!publish_row(row)) outdoor_row_skipped = true;
    }
    else {
      next_outdoor_row = MHI_DISCOVERY_ROWS;  // past the outdoor block: done
      // Nothing when all six fit (fork #24 spec §2.1), and never over modes or
      // an earlier skipped: modes > skipped > ok across both passes.
      if (outdoor_row_skipped && status_ok) {
        status_ok = false;
        output_P((ACStatus)type_status, PSTR(TOPIC_DISCOVERY), PSTR(PAYLOAD_DISCOVERY_SKIPPED));
      }
    }
  }
```

- [ ] **Step 8: Build all 12 (clean build procedure), then commit**

Stage the files of this task and build all 12 environments.
Expected: 12 × `SUCCESS`, no warning from `src/`. `pio test -e native` → `248 test cases: 248 succeeded`.

```bash
git add lib/mhi_pure/mhi_discovery.h lib/mhi_pure/mhi_discovery.cpp test/test_mhi_discovery/test_mhi_discovery.cpp test/fixtures/discovery test/fixtures/discovery_all tools/discovery_payloads.cpp src/support.h src/MHI-AC-Ctrl.h src/discovery.cpp
git commit -m "feat: a Restart button in Home Assistant, and Discovery says skipped when a config did not fit (#24)

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013aKH8NAdtagPe21HRXJdt5"
```

---

### Task 10: #24 batch C cleanup and the CI fan names

**Files:**
- Modify: `lib/mhi_pure/mhi_vanes_lr.h`: line 28
- Modify: `src/MHI-AC-Ctrl-core.cpp`: lines 4-7
- Test: `test/test_mhi_discovery/test_mhi_discovery.cpp`: lines 217-220, 587-613 (each counted after the edits listed before it)
- Modify: `platformio.ini`: lines 106-110

**Interfaces:** none new. No behaviour changes: the fixtures do not change by one byte.

The items of #24 spec §2.2, as found in the code at `04fbecb`:
- `lib/mhi_pure/mhi_vanes_lr.h:28`: "positions 1..7, as seen on the unit (1 leftmost .. 7 spot)". `src/MHI-AC-Ctrl.h:302-303` already says 6 and 7 are spread modes.
- `src/MHI-AC-Ctrl-core.cpp:4-7`: `mhi_vanes_lr.h` comes before `mhi_status.h`.
- `lib/mhi_pure/mhi_vanes_lr.cpp`: **the `else` is gone.** Batch C's fix wave rewrote `mhi_vanes_lr_command()` with early returns (lines 4-15), and the file has no `else` left.
- `test/test_mhi_discovery/test_mhi_discovery.cpp`:
  - the blanket `"mf":…,"mdl":"` assertion in `every_row_fits()` sits right above the two kind-specific model checks, which assert more;
  - `test_second_fixture_set_is_written()` and `test_reference_fixtures_are_written()` assert only `> 0` before they write.
- The two `SW-Configuration.md` items (3D auto with a louver position, the `FrameErrors`/`FrameTimeouts` baseline) are docs and go in Task 11.

Decision: no change for the `else` item, because the cosmetic it names no longer exists in the code.
Decision: both fixture tests get the stronger check, through one helper that writes the file, reads it back and compares it byte for byte, and checks the payload's JSON shape. The spec names one test, but the two had the same weakness and write the two sets CI compares.
Decision: fan names 2 and 3 in `ci-custom-payloads` are `Medium` and `High`, between the spec's `Low` and `Top`.

- [ ] **Step 1: The comment and the include order**

Replace (`lib/mhi_pure/mhi_vanes_lr.h`, line 28):

```cpp
  const char* pos[7];  // positions 1..7, as seen on the unit (1 leftmost .. 7 spot)
```

with:

```cpp
  const char* pos[7];  // 1..5 positions as seen on the unit (1 leftmost .. 5 rightmost), 6 wide and 7 spot: spread modes, not positions
```

Replace (`src/MHI-AC-Ctrl-core.cpp`, lines 4-7):

```cpp
#include "MHI-AC-Ctrl-core.h"
#include "mhi_action.h"
#include "mhi_vanes_lr.h"
#include "mhi_status.h"
```

with:

```cpp
#include "MHI-AC-Ctrl-core.h"
#include "mhi_action.h"
#include "mhi_status.h"
#include "mhi_vanes_lr.h"
```

- [ ] **Step 2: The tests**

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, lines 217-220):

```cpp
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"mf\":\"Mitsubishi Heavy Industries\",\"mdl\":\""), msg);
    // The blanket model/version check above can no longer name one model: the
    // outdoor rows carry their own device. Each kind keeps its own full check,
    // so nothing the ten-row version asserted is lost.
```

with:

```cpp
    // The outdoor rows carry their own device: each kind of row has its own
    // full model check.
```

Replace (`test/test_mhi_discovery/test_mhi_discovery.cpp`, lines 587-613):

```cpp
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

static void test_reference_fixtures_are_written(void) {
  for (int r = 0; r < MHI_DISCOVERY_ROWS; r++) {
    if (!mhi_discovery_row_enabled((MhiDiscoveryRow)r, &kDefault)) continue;
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
```

with:

```cpp
// Writes every enabled row of ctx to <dir>/<fixture name>.txt as
// "<topic>\n<payload>\n", then reads each file back and compares: the file
// holds exactly the row the builder made, well-formed. CI then compares the
// files with the committed ones.
static void write_fixture_set(const MhiDiscoveryCtx* ctx, const char* dir) {
  for (int r = 0; r < MHI_DISCOVERY_ROWS; r++) {
    if (!mhi_discovery_row_enabled((MhiDiscoveryRow)r, ctx)) continue;
    char path[96], topic[MHI_DISCOVERY_TOPIC_MAX], payload[MHI_DISCOVERY_BUF];
    char expected[MHI_DISCOVERY_TOPIC_MAX + MHI_DISCOVERY_BUF + 2], back[sizeof(expected) + 1];
    snprintf(path, sizeof(path), "%s/%s.txt", dir, kFixtureName[r]);
    const size_t topic_len = mhi_discovery_topic((MhiDiscoveryRow)r, ctx, topic, sizeof(topic));
    const size_t payload_len = mhi_discovery_build((MhiDiscoveryRow)r, ctx, payload, sizeof(payload));
    TEST_ASSERT_TRUE_MESSAGE(topic_len > 0 && payload_len > 0, path);
    TEST_ASSERT_TRUE_MESSAGE(json_shape_ok(payload), path);
    snprintf(expected, sizeof(expected), "%s\n%s\n", topic, payload);
    FILE* f = fopen(path, "w");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "cannot write the fixture directory: run pio test from the project root");
    fputs(expected, f);
    fclose(f);
    f = fopen(path, "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    const size_t got = fread(back, 1, sizeof(back) - 1, f);
    fclose(f);
    back[got] = '\0';
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, back, path);
  }
}

static void test_second_fixture_set_is_written(void) { write_fixture_set(&kSlaapkamer, "test/fixtures/discovery_all"); }

static void test_reference_fixtures_are_written(void) { write_fixture_set(&kDefault, "test/fixtures/discovery"); }
```

Run: `pio test -e native` → `248 test cases: 248 succeeded`, and `git status --porcelain -- test/fixtures` is empty: the files are what the new helper wrote and read back.

- [ ] **Step 3: `ci-custom-payloads`**

Replace (`platformio.ini`, lines 106-110):

```ini
[env:ci-custom-payloads]
extends = ci
build_flags =
	${esp8266.build_flags}
	${ha_payloads.build_flags}
```

with:

```ini
; The fan names of fork #21 F6 replaced too (fork #24): proves their #ifndef
; defaults give way without a redefinition. Compile-only.
[env:ci-custom-payloads]
extends = ci
build_flags =
	${esp8266.build_flags}
	${ha_payloads.build_flags}
	-D PAYLOAD_FAN_1=\"Low\"
	-D PAYLOAD_FAN_2=\"Medium\"
	-D PAYLOAD_FAN_3=\"High\"
	-D PAYLOAD_FAN_4=\"Top\"
```

- [ ] **Step 4: Build (clean build procedure)**

Stage `lib/mhi_pure/mhi_vanes_lr.h src/MHI-AC-Ctrl-core.cpp test/test_mhi_discovery/test_mhi_discovery.cpp platformio.ini`, then build `ci-custom-payloads`, `ci-extended-frame` and `d1_mini`.
Expected: 3 × `SUCCESS`. `ci-custom-payloads` compiles `mhi_fan`'s names as `Low`/`Medium`/`High`/`Top` without a redefinition warning.

- [ ] **Step 5: Commit**

```bash
git add lib/mhi_pure/mhi_vanes_lr.h src/MHI-AC-Ctrl-core.cpp test/test_mhi_discovery/test_mhi_discovery.cpp platformio.ini
git commit -m "chore: batch C cleanup and fan names overridden in CI (#24)

The louver comment names 6 and 7 as spread modes, the core's includes are
sorted, the dead mdl assertion is gone, and the fixture tests read back what
they write. ci-custom-payloads overrides PAYLOAD_FAN_1..4.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013aKH8NAdtagPe21HRXJdt5"
```

---

### Task 11: Docs for #22, #23 and #24: README, `SW-Configuration.md`, `Troubleshooting.md`, `Version.md`

**Files:**
- Modify: `README.md` (Prerequisites): line 12
- Modify: `SW-Configuration.md`: status topic table, telemetry, operating data (new section), note 3, Home Assistant discovery, `reset_old_values()`, the batch C items, a new safe-mode section: lines 95, 139, 150-152, 458, 376, 385, 389-393, 526-527, 99, 119, 123-125, 379, 272 (each counted after the edits listed before it)
- Modify: `Troubleshooting.md`: lines 34, 98 (each counted after the edits listed before it)
- Modify: `Version.md`: lines 28-30

**Interfaces:** the section anchors `#several-indoor-units-on-one-outdoor-unit` and `#crash-loop-safe-mode` in `SW-Configuration.md`, and `#fire-the-unit-is-unavailable-for-10-minutes-safe-mode` in `Troubleshooting.md`.

Decision: the README has no feature list, so the #22 feature line goes into "Prerequisites", right after "This has to be a split device". The #23/#24 spec asks for no README change.
Decision: the #22 items go into one new section, "Several indoor units on one outdoor unit", under "MQTT operating data", where the 11 values live. The `Group` topic joins the program-status table, and the removed flag goes into the Home Assistant section.
Decision: safe mode gets its own section, "Crash-loop safe mode", right before "External Temperature Sensor Settings", after the OTA settings it protects. `SafeMode` joins the program-status table, and `crash` joins the `reset` row. `Troubleshooting.md` gets a section on how to recognise safe mode and use it, linked from "ESP8266 crashes periodically".
Decision: one `Version.md` bullet for all three issues, in batch C's style, because they ride one build.

- [ ] **Step 1: README**

Replace (`README.md`, line 12):

```markdown
cable connector to your air conditioner. This has to be a split device (separated indoor and outdoor unit).
```

with:

```markdown
cable connector to your air conditioner. This has to be a split device (separated indoor and outdoor unit).
With several indoor units on one outdoor unit, the units elect one of them to publish the outdoor unit's values and its Home Assistant device; see `GROUP_ROOT` in [SW-Configuration.md](SW-Configuration.md#several-indoor-units-on-one-outdoor-unit).
```

- [ ] **Step 2: `SW-Configuration.md`**

Replace (`SW-Configuration.md`, line 95):

```markdown
Discovery|r|"ok", "modes"|Only with `HA_DISCOVERY`: the Home Assistant discovery configs were published; "modes" means the climate config was skipped because the mode texts are not Home Assistant's, see [Home Assistant discovery](#home-assistant-discovery-supporth)
```

with:

```markdown
Discovery|r|"ok", "modes", "skipped"|Only with `HA_DISCOVERY`: the Home Assistant discovery configs were published; "modes" means the climate config was skipped because the mode texts are not Home Assistant's; "skipped" means a config did not fit its buffer and was not published: after the unit rows, and after the outdoor rows when one of those did not fit. See [Home Assistant discovery](#home-assistant-discovery-supporth)
Group|r|0, 1, 2, 3|this unit's part in the outdoor election: `0` member, `1` publisher, `2` its outdoor ID differs from the group's, `3` a unit with another group protocol version leads the group; see [Several indoor units on one outdoor unit](#several-indoor-units-on-one-outdoor-unit)
```

Replace (`SW-Configuration.md`, line 139):

```markdown
#define TELEMETRY_PERIOD 300   // seconds between the periodic publishes; 0 publishes them at MQTT connect only
```

with:

```markdown
#define TELEMETRY_PERIOD 300   // seconds between the periodic publishes, 1..86400; the group record goes out at the same rhythm, so the build refuses 0
```

Replace (`SW-Configuration.md`, lines 150-152):

```markdown
Without changes of the path, subscribe to `MHI-AC-Ctrl/OpData/#` for receiving all operating data. Please see section [Operating data](#operating-data-mhi-ac-ctrl-coreh) to find all supported operating data.

Note: The topic and the payload text is adaptable by defines in [MHI-AC-Ctrl.h](src/MHI-AC-Ctrl.h).
```

with:

````markdown
Without changes of the path, subscribe to `MHI-AC-Ctrl/OpData/#` for receiving all operating data. Please see section [Operating data](#operating-data-mhi-ac-ctrl-coreh) to find all supported operating data.

Note: The topic and the payload text is adaptable by defines in [MHI-AC-Ctrl.h](src/MHI-AC-Ctrl.h).

### Several indoor units on one outdoor unit

On a multi-split every indoor unit reads the same outdoor unit, so eleven operating values are the same on all of them. The indoor units of one outdoor unit elect one of them, the publisher, to write those eleven values under a topic root they share; everything else stays under each unit. A single split needs no configuration: its root is its own `MQTT_PREFIX`, so it elects itself and its topics stay where they are.

```cpp
#define GROUP_ROOT "airco/outdoor/"             // topic root shared by the units of one outdoor unit; default MQTT_PREFIX. 1..64 characters, ends in "/", no + # ;
//#define GROUP_OP_PREFIX GROUP_ROOT "OpData/"  // where the publisher writes the eleven values; without a GROUP_ROOT the default is MQTT_OP_PREFIX
```

Give every unit of one outdoor unit the same `GROUP_ROOT`, and the same `TOPIC_CONNECTED`, `PAYLOAD_CONNECTED_TRUE` and `PAYLOAD_CONNECTED_FALSE`: each unit watches the others' `<prefix>connected`. `GROUP_ROOT` is at most 64 characters so that the largest record message (5 bytes of header, 2 of topic length, the root, `members/`, a 32-character hostname and a 140-byte record: 251 bytes) fits PubSubClient's 256-byte receive buffer, which drops a larger message whole: with a longer root the units would never see each other's records, and two of them could publish at once. The build refuses a longer root, and a `HOSTNAME` longer than 32 characters.

Which values go where, measured on 17 Sep 2026 with two indoor units cooling on one outdoor unit:
- written by the publisher only, under `GROUP_OP_PREFIX`: `OUTDOOR`, `CT`, `COMP`, `DEFROST`, `TOTAL-COMP-RUN`, `PROTECTION-NO`, `TD`, `TDSH`, `THO-R1`, `THI-R2`, `OU-FANSPEED`;
- written by every unit under its own `MQTT_OP_PREFIX`, as before: `RETURN-AIR`, `THI-R1`, `THI-R3`, `IU-FANSPEED`, `TOTAL-IU-RUN`, `Tsetpoint`, `Mode`, `unknown`, `OU-EEV1` (each indoor circuit has its own valve: 97 and 164 on 17 Sep) and `KWH`;
- `ErrOpData/` stays per unit, the eleven included: it is the snapshot the unit read from its own indoor unit.

`KWH` is the outdoor unit's energy counted while *this* indoor unit is on, and it starts from 0 again when this unit is switched on (measured 18-19 Sep 2026: it followed the integral of `CT` × 230 V only while the unit was on). For the whole outdoor unit's energy, integrate the power, `CT` × 230 V, in Home Assistant.

The election in short:
- Every unit keeps a retained record at `<GROUP_ROOT>members/<HOSTNAME>`: `<proto>;<role>;<term>;<uptime>;<period>;<outdoor_id>;<prefix>`, e.g. `1;1;1;41382;60;ac_outdoor;airco/slaapkamer/`. That is the group protocol version (1), the role (0 member, 1 publisher), the publisher generation, the unit's `Uptime` in seconds, its `TELEMETRY_PERIOD`, its outdoor ID and its `MQTT_PREFIX`. The unit sends it again every `TELEMETRY_PERIOD`, which is why `TELEMETRY_PERIOD` must be 1..86400.
- Each unit publishes its part on `<MQTT_PREFIX>Group`: `0` member, `1` publisher, `2` outdoor ID mismatch, `3` protocol version mismatch.
- After every MQTT connect a unit only listens for 5 s. A publisher that rebooted finds its own record then and carries on without a handover.
- A unit counts as gone when its record has not changed for 3 of its own periods, or when its `connected` has read 0 for 30 s.
- When no publisher has been left for 5 s, the live unit with the lowest hostname takes over with a new generation. A takeover therefore takes 30 s + 5 s after the publisher's `connected` went to 0.
- A live publisher is never replaced, so a unit that comes back stays a member. When two units take over at the same moment, the newer generation keeps the role, and at equal generations the lower hostname.
- The live unit with the lowest hostname sets the group's outdoor ID and protocol version. A unit that differs stays out (`Group` `2` or `3`).
- The new publisher writes the eleven values within one operating-data cycle (20 s), and sends the outdoor device's Home Assistant configs 30 s after it took over.

Limits:
- A publisher that stays connected but cannot read its AC keeps the role: the others still see its `connected` 1 and its record changing.
- A dead unit whose retained `connected` still reads 1 (it died while the broker was down, so no will was sent) counts as gone only after 3 of its periods of continuous connection. A unit that reconnects more often than that keeps finding it fresh: if the dead unit has the lowest hostname, or was the publisher, it holds off a takeover for as long as the reconnects go on.
- To remove a unit from the group for good, delete its record: `mosquitto_pub -h <broker> -r -n -t <GROUP_ROOT>members/<HOSTNAME>`.
````

Replace (`SW-Configuration.md`, line 458):

```markdown
Note 3: The energy-used is the energy in kWh counting from power on the AC. If you power off the AC, the value (in kWh) will keep the last value. When you power on the AC again, it will start from 0 again.
```

with:

```markdown
Note 3: The energy-used (`KWH`) is the outdoor unit's energy in kWh counted while this indoor unit is on. It starts from 0 again when this indoor unit is switched on, and it is not the outdoor unit's total when several indoor units share it, see [Several indoor units on one outdoor unit](#several-indoor-units-on-one-outdoor-unit).
```

Replace (`SW-Configuration.md`, line 376):

```markdown
all under one device. With `USE_EXTENDED_FRAME_SIZE`, also a select for the left/right louvers and a switch for `3Dauto`. With `HA_OUTDOOR_DEVICE`, seven more entities for the shared outdoor unit's own device (temperature, current, energy, compressor frequency, defrost, compressor run time, compressor-protection number), linked with `via_device`, reading the publishing unit's own `OpData/` topics -- with two indoor units sharing one outdoor unit, only one of them should have `HA_OUTDOOR_DEVICE` on. 13 entities with neither option, up to 22 with both. Availability comes from `connected`.
```

with:

```markdown
all under one device, a diagnostic sensor for `Group` (fork #22) and a Restart button (fork #24: it sends `set/reset` `reset`, entity category config). With `USE_EXTENDED_FRAME_SIZE`, also a select for the left/right louvers and a switch for `3Dauto`: 15 entities per unit without that option, 17 with it. The outdoor unit has a device of its own with six entities (temperature, current, compressor frequency, defrost, compressor run time, compressor-protection number), linked with `via_device` to the unit that publishes them: only the group's publisher sends these configs, 30 s after it took over, reading the group root's `OpData/` topics and available while the publisher is (see [Several indoor units on one outdoor unit](#several-indoor-units-on-one-outdoor-unit)). There is no energy entity: `KWH` counts per indoor unit. Availability comes from `connected`. `HA_OUTDOOR_DEVICE` is gone: a build that still defines it stops with an error; give the units of one outdoor unit the same `GROUP_ROOT` instead.
```

Replace (`SW-Configuration.md`, line 385):

```cpp
#define HA_ID_PREFIX HOSTNAME             // unique_id prefix of the other entities: <prefix>_vanes, _silent, _problem, _wiring, _uptime, _free_heap, _rssi, _reset_reason, _wifi_phy, _vanes_lr, _3d_auto, _frame_errors, _frame_timeouts, _error_code
```

with:

```cpp
#define HA_ID_PREFIX HOSTNAME             // unique_id prefix of the other entities: <prefix>_vanes, _silent, _problem, _wiring, _uptime, _free_heap, _rssi, _reset_reason, _wifi_phy, _vanes_lr, _3d_auto, _frame_errors, _frame_timeouts, _error_code, _group_role, _restart
```

Replace (`SW-Configuration.md`, lines 389-393):

```cpp
//#define HA_OUTDOOR_DEVICE true            // also publish the outdoor unit's device; on for at most one of the units sharing it
#define HA_OUTDOOR_ID HA_ID_PREFIX "_outdoor"
#define HA_OUTDOOR_NAME "AC outdoor unit"
//#define HA_OUTDOOR_ENTITY_PREFIX "ac_outdoor"
#define HA_NAME_VANES_LR "Vanes left/right"  // entity names; likewise HA_NAME_3DAUTO, _FRAME_ERRORS, _FRAME_TIMEOUTS, _ERROR_CODE, _OU_OUTDOOR, _OU_CT, _OU_KWH, _OU_COMP, _OU_DEFROST, _OU_COMP_RUN, _OU_PROTECTION
```

with:

```cpp
//#define HA_OUTDOOR_ID "ac_outdoor"        // the outdoor device's id and unique_id prefix, 1..40 characters; default <slug of GROUP_ROOT>_outdoor, derived at boot so every unit of the group derives the same
#define HA_OUTDOOR_NAME "AC outdoor unit"
//#define HA_OUTDOOR_ENTITY_PREFIX "ac_outdoor"
#define HA_NAME_VANES_LR "Vanes left/right"  // entity names; likewise HA_NAME_3DAUTO, _FRAME_ERRORS, _FRAME_TIMEOUTS, _ERROR_CODE, _GROUP_ROLE, _RESTART, _OU_OUTDOOR, _OU_CT, _OU_COMP, _OU_DEFROST, _OU_COMP_RUN, _OU_PROTECTION
```

Replace (`SW-Configuration.md`, lines 526-527):

```markdown
### `reset_old_values()`
This should be called if you want to ensure that the receiver of the status data has the latest data. E.g. in case of a MQTT broker disconnect it should be called.
```

with:

```markdown
### `reset_old_values()`
This should be called if you want to ensure that the receiver of the status data has the latest data. E.g. in case of a MQTT broker disconnect it should be called. It calls `reset_system_values()`, which does the same for the eleven values the group's publisher writes; the group also calls that one when a unit becomes the publisher, so each of the eleven goes out at its next reading.
```

Replace (`SW-Configuration.md`, line 99):

```markdown
3Dauto|r/w|"On", "Off"|3D auto only works for mode Auto, Cool and heat <sup>4</sup>
```

with:

```markdown
3Dauto|r/w|"On", "Off"|3D auto only works for mode Auto, Cool and heat; choosing a left/right louver position leaves it on (seen 18 Sep 2026: `DB17 0f>0e`) <sup>4</sup>
```

Replace (`SW-Configuration.md`, line 119):

```markdown
reset|w|"reset"|resets the ESP8266
```

with:

```markdown
reset|w|"reset", "crash"|"reset" restarts the ESP8266; "crash" raises one deliberate exception (reset reason 2), the proof of [crash-loop safe mode](#crash-loop-safe-mode). Never send it retained, see there
```

Replace (`SW-Configuration.md`, lines 123-125):

```markdown
FrameErrors|r  |integer         |frames rejected for a bad signature or checksum since boot, at MQTT (re-)connect and every `TELEMETRY_PERIOD` seconds; saturates, never wraps
FrameTimeouts|r|integer         |SCK timeouts since boot, same publishing rhythm; what is normal (boot, OTA, Wi-Fi scans) is unknown until a unit has run with it for a while
ResetReason|r|string          |why the ESP8266 last started, at MQTT (re-)connect: `Power On`, `Software/System restart` (also after an OTA flash or `set/reset`), `Hardware Watchdog`, `Software Watchdog`, `Exception`, `External System`
```

with:

```markdown
FrameErrors|r  |integer         |frames rejected for a bad signature or checksum since boot, at MQTT (re-)connect and every `TELEMETRY_PERIOD` seconds; saturates, never wraps. 0 on both units over the batch C soak (18-19 Sep 2026)
FrameTimeouts|r|integer         |SCK timeouts since boot, same publishing rhythm. Over the batch C soak only at boot, 0 or 1 per boot, and none after
ResetReason|r|string          |why the ESP8266 last started, at MQTT (re-)connect: `Power On`, `Software/System restart` (also after an OTA flash or `set/reset`), `Hardware Watchdog`, `Software Watchdog`, `Exception`, `External System`
SafeMode |r  |integer         |boots into [crash-loop safe mode](#crash-loop-safe-mode) since power-on, at MQTT (re-)connect; `0` on a healthy unit
```

Replace (`SW-Configuration.md`, line 379):

```markdown
The firmware checks them at boot: with other texts the climate config is skipped, Serial says so and the retained `Discovery` topic reads `modes` instead of `ok`.
```

with:

```markdown
The firmware checks them at boot: with other texts the climate config is skipped, Serial says so and the retained `Discovery` topic reads `modes` instead of `ok`. A config that does not fit its 1024-byte buffer is not published either: Serial says so, and `Discovery` reads `skipped`, after the unit rows, and after the outdoor rows only when one of those did not fit.
```

Replace (`SW-Configuration.md`, line 272):

```markdown
## External Temperature Sensor Settings ([support.h](src/support.h))
```

with:

```markdown
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
```

- [ ] **Step 3: `Troubleshooting.md`**

Replace (`Troubleshooting.md`, line 34):

```markdown
A unit that keeps reporting a crash reason with a short uptime is the case below.
```

with:

```markdown
A unit that keeps reporting a crash reason with a short uptime is the case below. Three crashes in a row, each within 120 s of the boot, start [safe mode](#fire-the-unit-is-unavailable-for-10-minutes-safe-mode). `abort()`, `panic()`, a failed `assert` or `new` and a stack overflow show as `Software/System restart`, but safe mode counts them as crashes too.
```

Replace (`Troubleshooting.md`, line 98):

```markdown
## :fire: OTA cannot find the device
```

with:

```markdown
## :fire: The unit is unavailable for 10 minutes: safe mode
After three crashes in a row, each within 120 s of the boot before, the unit starts in [crash-loop safe mode](SW-Configuration.md#crash-loop-safe-mode): Wi-Fi and OTA only, for 10 minutes. You recognise it by:
- Home Assistant shows the unit unavailable (`connected` 0) for about 10 minutes, while the AC still works on its remote;
- the serial log starts with `Safe mode check: reset reason 2, crashes in a row 3, ...` (reason 4 when the last crash was an `abort()`, a failed `assert` or a stack overflow) and then `SAFE MODE: three crashes in a row; Wi-Fi and OTA only, a normal boot in 10 min`;
- the unit still answers OTA: it is on the network as `<hostname>._arduino._tcp` (`avahi-browse -rt _arduino._tcp` lists it);
- once it boots normally again, the retained `SafeMode` topic reads 1 or more, and `ResetReason` reads `Software/System restart`.

Use the 10 minutes to flash a build that works, as below in [OTA cannot find the device](#fire-ota-cannot-find-the-device). If you do nothing, the unit boots normally after 10 minutes, and a fault that is still there brings it back to safe mode after three more crashes.

If safe mode comes back after every normal boot while the build is known to be good, look for a retained `set/reset` (`mosquitto_sub -h <broker> -v -W 2 -t <MQTT_PREFIX>set/reset` prints it) and clear it: `mosquitto_pub -h <broker> -r -n -t <MQTT_PREFIX>set/reset`.

## :fire: OTA cannot find the device
```

- [ ] **Step 4: `Version.md`**

Replace (`Version.md`, lines 28-30):

```markdown


**v2.8** (September 2023)
```

with:

```markdown

- the outdoor election, crash-loop safe mode and the ride-alongs (#22, #23, #24): the indoor units of one outdoor unit share a `GROUP_ROOT` and elect, over retained records under `<GROUP_ROOT>members/`, the one that writes the eleven outdoor values under the group root and sends the outdoor device's Home Assistant configs; another unit takes over when it drops out, and the per-unit `Group` topic and a diagnostic sensor show each unit's part. `HA_OUTDOOR_DEVICE` is gone, `KWH` stays per unit (the outdoor device has no energy entity any more) and `TELEMETRY_PERIOD` must be 1..86400. Three crashes in a row within 120 s of boot start safe mode, Wi-Fi and OTA only for 10 minutes, counting `abort()`, `panic()`, failed asserts and stack overflows through the core's crash hook; the new `SafeMode` topic counts the entries since power-on, and `set/reset` `crash` proves the path. A Restart button in Home Assistant; `Discovery` reads `skipped` when a config did not fit its buffer

**v2.8** (September 2023)
```

- [ ] **Step 5: Check**

```bash
grep -c "several-indoor-units-on-one-outdoor-unit" SW-Configuration.md README.md   # SW-Configuration.md:3, README.md:1
grep -c "crash-loop-safe-mode" SW-Configuration.md Troubleshooting.md             # SW-Configuration.md:2, Troubleshooting.md:1
grep -n "HA_OUTDOOR_DEVICE" SW-Configuration.md      # one line: "`HA_OUTDOOR_DEVICE` is gone: ..."
grep -n "0 publishes them at MQTT connect only" SW-Configuration.md   # nothing
```

Every docs item of the two specs is covered:
- #22 §10:
  - `GROUP_ROOT`/`GROUP_OP_PREFIX`;
  - the record, the `Group` numbers and the election;
  - the system and per-unit lists with the 17 Sep measurement, and what `KWH` means;
  - the removed flag and `TELEMETRY_PERIOD` 1..86400;
  - the same `TOPIC_CONNECTED`/`PAYLOAD_CONNECTED_*` on all units of a group;
  - how to remove a unit for good;
  - the publisher that cannot read its AC;
  - the ghost limit;
  - the 64-character bound and why it exists;
  - the `Version.md` bullet.
- #23: safe mode in `SW-Configuration.md` and `Troubleshooting.md`, the `SafeMode` topic, `set/reset crash`, the crashed bit that counts `abort()` and its kin, and never to send `set/reset` retained, with how to clear one.
- #24:
  - the `Discovery` values;
  - the Restart button;
  - 3D auto staying on with a louver position;
  - the `FrameErrors`/`FrameTimeouts` baseline.

- [ ] **Step 6: Commit**

```bash
git add README.md SW-Configuration.md Troubleshooting.md Version.md
git commit -m "docs: the outdoor election, crash-loop safe mode, the Restart button and the Discovery values (#22, #23, #24)

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_013aKH8NAdtagPe21HRXJdt5"
```

---

### Task 12: The toolkit outside the repo (`~/.config/hass/tools/mhi/`)

**Files (not under git):**
- Modify: `airco-config.py`, `test_airco_config.py`, `units.sh`, `post-flash-check.sh`, `health-check.sh`, `remote-test.sh`, `rollback-unit.sh`, `README.md`
- Replace: `discovery-payloads.sh`

**Interfaces:**
- Consumes:
  - the renderer options of Tasks 3 and 9 (`--group-base`, `--outdoor 0|1`, `--outdoor-id`, `--outdoor-entity-prefix`);
  - `src/group.cpp` (Task 6), `src/safe_mode.cpp` (Task 8) and `MHI_DISCOVERY_RESTART` in `lib/mhi_pure/mhi_discovery.h` (Task 9), as the markers of the builds that have them;
  - the strings `SafeMode` (Task 8's `TOPIC_SAFE_MODE`) and `members/` (Task 6's record topic) in an image, as the markers `rollback-unit.sh` looks for.
- Produces:
  - `airco-config.py --discovery-args <unit> <version> [20|33] [--outdoor]`;
  - `discovery-payloads.sh <unit> [version] [--outdoor]`;
  - `GROUP=airco/outdoor` in `units.sh`.

The toolkit's tests run with `python3 -m pytest -q -p no:cacheprovider test_airco_config.py` (its README): 21 pass today, 23 after this task. `flash-unit.sh` and `rollback-unit.sh` read secrets, and this plan does not run them: it edits `rollback-unit.sh` and checks it with `bash -n` and `shellcheck` only. `post-flash-check.sh`, `health-check.sh` and `remote-test.sh` talk to Home Assistant and the broker, so this plan only checks their syntax.

Decision: both units get `GROUP_ROOT "airco/outdoor/"`, `HA_OUTDOOR_ID "ac_outdoor"` and `HA_OUTDOOR_ENTITY_PREFIX "ac_outdoor"`, in the same generated block as the identity. `GROUP_\w+` joins the pattern of dropped template defines, so a stale one never leaks in. `HA_OUTDOOR_NAME` is no longer written: it equals the repo default, and §10 lists only the three.
Decision: `--discovery-args` always passes `--group-base`, `--outdoor-id` and `--outdoor-entity-prefix`, and `--outdoor 1` only with `--outdoor`. The unit rows do not depend on the first three.
Decision: `post-flash-check.sh` recognises what a version has from its tree: `src/group.cpp` (#22), `src/safe_mode.cpp` (#23) and `MHI_DISCOVERY_RESTART` in `lib/mhi_pure/mhi_discovery.h` (#24). No commit hash has to be filled in after the fact. The check reads the header through a here-string, never `| grep -q` under `pipefail`.
Decision (confirmed by the owner, answer 4): `post-flash-check.sh` requires the other unit's record only when that unit's retained `Version` is a group build, and leaves `sensor.ac_outdoor_energy` out of the outdoor device's six with an info line.
Decision: the group checks in `post-flash-check.sh` wait until the unit's retained `Uptime` is 75 s or more. The configs go out about 40 s after a first boot's connect (§6.5 A).
Decision: `check_entities` accepts `unknown` for a `button.*` entity: a button's state is the time of its last press, `unknown` until the first. Otherwise the Restart button would fail every check. The button is found on the device, never pressed.
Decision: `post-flash-check.sh` expects `SafeMode` `0`. Right after the controlled safe-mode proof it reads `1`, and that FAIL is the expected one: the rollout step for the proof says so.
Decision: on a build without `SafeMode`, a retained `SafeMode` is an info line in `post-flash-check.sh`, not a FAIL. It is a later build's value, which `rollback-unit.sh` clears.
Decision: `rollback-unit.sh` clears what an older image does not publish, the way it clears `Action`, `Silent` and `Discovery`. It reads the image's strings, which `check_image` already wrote:
  - no line `SafeMode`: the retained `$PREFIX/SafeMode`;
  - no `members/` anywhere: `$PREFIX/Group`, `$GROUP/members/$NEW_HOSTNAME`, and the unit's two new discovery configs, `homeassistant/sensor/ac_<unit>_group_role/config` and `homeassistant/button/ac_<unit>_restart/config`. Those two are unit rows, so an empty payload is right: the older build has neither entity. An outdoor config is never cleared: it belongs to whichever unit publishes for the group.
Decision: `Discovery` `skipped` fails `post-flash-check.sh` with its own message, naming the unit's Serial log as the place to find the row.
Decision: `health-check.sh` prints `SafeMode` with the other retained values, and its rollback hint names a `SafeMode` that went up without the controlled proof.
Decision: `remote-test.sh` captures `airco/#` rather than two filters, because `ha-mqtt-capture.mjs` takes one topic filter.
Decision: the toolkit README rows for the changed scripts are updated too: it is the toolkit's only documentation.

- [ ] **Step 1: Back up what this task changes**

```bash
cd ~/.config/hass/tools/mhi
mkdir -p .bak-22 && cp -p README.md airco-config.py test_airco_config.py discovery-payloads.sh post-flash-check.sh health-check.sh remote-test.sh rollback-unit.sh units.sh .bak-22/
```

- [ ] **Step 2: The failing tests**

In `test_airco_config.py`:

Replace (`test_airco_config.py`, lines 154-156):

```python
# English again (18 Sep 2026): the units publish the repo's default entity names, so the config
# carries the identity only, and Slaapkamer alone publishes the shared outdoor unit's device.
def test_discovery_is_on_with_the_unit_identity_and_no_name_or_template_overrides():
```

with:

```python
# English again (18 Sep 2026): the units publish the repo's default entity names, so the config
# carries the identity only. Both units elect the outdoor unit's publisher (fork issue #22).
def test_discovery_is_on_with_the_unit_identity_and_no_name_or_template_overrides():
```

Replace (`test_airco_config.py`, lines 165-171):

```python

def test_only_slaapkamer_publishes_the_outdoor_device_under_a_room_neutral_id():
    s = defines(cfg.build(TEMPLATE, 'slaapkamer', SECRETS))
    assert s['HA_OUTDOOR_DEVICE'] == 'true'
    assert s['HA_OUTDOOR_ID'] == '"ac_outdoor"' and s['HA_OUTDOOR_ENTITY_PREFIX'] == '"ac_outdoor"'
    assert s['HA_OUTDOOR_NAME'] == '"AC outdoor unit"'
    assert not [k for k in defines(cfg.build(TEMPLATE, 'uitkijk', SECRETS)) if k.startswith('HA_OUTDOOR')]
```

with:

```python

def test_both_units_join_one_group_under_a_room_neutral_outdoor_id():
    for unit in ('slaapkamer', 'uitkijk'):
        d = defines(cfg.build(TEMPLATE, unit, SECRETS))
        assert d['GROUP_ROOT'] == '"airco/outdoor/"'
        assert d['HA_OUTDOOR_ID'] == '"ac_outdoor"' and d['HA_OUTDOOR_ENTITY_PREFIX'] == '"ac_outdoor"'
        assert 'HA_OUTDOOR_DEVICE' not in d   # removed from the firmware: a build with it stops (#error)
        assert 'HA_OUTDOOR_NAME' not in d     # the repo default, "AC outdoor unit"


def test_a_group_define_left_in_the_shared_template_is_replaced():
    stale = TEMPLATE + '#define GROUP_ROOT "old/"\n#define GROUP_OP_PREFIX "old/OpData/"\n'
    out = cfg.build(stale, 'uitkijk', SECRETS)
    assert out == cfg.build(TEMPLATE, 'uitkijk', SECRETS)
    assert out.count('GROUP_ROOT') == 1 and 'GROUP_OP_PREFIX' not in out
```

Replace (`test_airco_config.py`, lines 186-197):

```python

def test_discovery_args_give_the_identity_the_frame_and_the_outdoor_device():
    u = cfg.discovery_args('uitkijk', 'abc1234', 33)
    assert u[:2] == ['--base', 'airco/uitkijk']
    assert u[u.index('--version') + 1] == 'abc1234'
    assert u[u.index('--lr') + 1] == '1' and '--outdoor' not in u
    assert not [a for a in u if a.startswith('--name-')] and '--reset-reason-tpl' not in u
    s = cfg.discovery_args('slaapkamer', 'abc1234', 33)
    assert s[s.index('--outdoor') + 1] == '1'
    assert s[s.index('--outdoor-id') + 1] == 'ac_outdoor' and s[s.index('--outdoor-name') + 1] == 'AC outdoor unit'
    assert s[s.index('--outdoor-entity-prefix') + 1] == 'ac_outdoor'
    assert cfg.discovery_args('slaapkamer', 'abc1234', 20)[cfg.discovery_args('slaapkamer', 'abc1234', 20).index('--lr') + 1] == '0'
```

with:

```python

def test_discovery_args_give_the_identity_the_frame_and_the_group():
    for unit in ('slaapkamer', 'uitkijk'):
        a = cfg.discovery_args(unit, 'abc1234', 33)
        assert a[:2] == ['--base', f'airco/{unit}']
        assert a[a.index('--version') + 1] == 'abc1234'
        assert a[a.index('--lr') + 1] == '1'
        assert a[a.index('--group-base') + 1] == 'airco/outdoor'
        assert a[a.index('--outdoor-id') + 1] == 'ac_outdoor' and a[a.index('--outdoor-entity-prefix') + 1] == 'ac_outdoor'
        assert a[a.index('--outdoor') + 1] == '0'   # the unit's own rows only
        assert not [x for x in a if x.startswith('--name-')] and '--reset-reason-tpl' not in a
    o = cfg.discovery_args('uitkijk', 'abc1234', 33, outdoor=True)
    assert o[o.index('--outdoor') + 1] == '1'       # plus the six outdoor rows as Uitkijk publishes them
    s20 = cfg.discovery_args('slaapkamer', 'abc1234', 20)
    assert s20[s20.index('--lr') + 1] == '0'


def test_the_command_line_takes_outdoor_after_the_frame(capsys):
    cfg.main(['airco-config.py', '--discovery-args', 'uitkijk', 'abc1234', '33', '--outdoor'])
    printed = capsys.readouterr().out.split('\n')
    assert printed[printed.index('--outdoor') + 1] == '1' and printed[printed.index('--lr') + 1] == '1'
    cfg.main(['airco-config.py', '--discovery-args', 'uitkijk', 'abc1234'])
    printed = capsys.readouterr().out.split('\n')
    assert printed[printed.index('--outdoor') + 1] == '0' and printed[printed.index('--lr') + 1] == '0'
    with pytest.raises(SystemExit):
        cfg.main(['airco-config.py', '--discovery-args', 'uitkijk', 'abc1234', '25'])
```

Run: `python3 -m pytest -q -p no:cacheprovider test_airco_config.py`
Expected: `4 failed, 19 passed`. The four failures are `test_both_units_join_one_group_under_a_room_neutral_outdoor_id`, `test_a_group_define_left_in_the_shared_template_is_replaced`, `test_discovery_args_give_the_identity_the_frame_and_the_group` and `test_the_command_line_takes_outdoor_after_the_frame`.

- [ ] **Step 3: `airco-config.py`**

Replace (`airco-config.py`, lines 12-13):

```python
  - TELEMETRY_PERIOD 60 (fork issue #21): telemetry every minute on both units.
  - the frame size (fork issue #20): 20 bytes by default; 33 adds USE_EXTENDED_FRAME_SIZE,
```

with:

```python
  - TELEMETRY_PERIOD 60 (fork issue #21): telemetry every minute on both units.
  - the outdoor group (fork issue #22): GROUP_ROOT "airco/outdoor/" and the outdoor device's
    id and entity prefix "ac_outdoor", the same on both units: either may be the publisher.
  - the frame size (fork issue #20): 20 bytes by default; 33 adds USE_EXTENDED_FRAME_SIZE,
```

Replace (`airco-config.py`, lines 30-32):

```python
    r'^[ \t]*#[ \t]*define[ \t]+(HOSTNAME|OTA_HOSTNAME|MQTT_PREFIX|MQTT_SET_PREFIX|MQTT_OP_PREFIX|'
    r'MQTT_ERR_OP_PREFIX|CONTINUE_WITHOUT_MQTT|USE_EXTENDED_FRAME_SIZE|TELEMETRY_PERIOD|HA_\w+)\b.*$', re.M)
MARKER = '// Identity and MQTT behaviour, written by airco-config.py (fork issues #14, #15)'
```

with:

```python
    r'^[ \t]*#[ \t]*define[ \t]+(HOSTNAME|OTA_HOSTNAME|MQTT_PREFIX|MQTT_SET_PREFIX|MQTT_OP_PREFIX|'
    r'MQTT_ERR_OP_PREFIX|CONTINUE_WITHOUT_MQTT|USE_EXTENDED_FRAME_SIZE|TELEMETRY_PERIOD|HA_\w+|GROUP_\w+)\b.*$', re.M)
MARKER = '// Identity and MQTT behaviour, written by airco-config.py (fork issues #14, #15)'
```

Replace (`airco-config.py`, lines 34-38):

```python
# Home Assistant discovery (fork issue #4 batch B). English again since 18 Sep 2026: the units publish the
# repo's default entity names, so the config carries the identity only. Slaapkamer alone publishes the
# shared outdoor unit's device (fork issue #19), under an id that is not tied to a room.
OUTDOOR_UNIT = 'slaapkamer'
OUTDOOR = {'id': 'ac_outdoor', 'name': 'AC outdoor unit', 'entity_prefix': 'ac_outdoor'}
```

with:

```python
# Home Assistant discovery (fork issue #4 batch B). English again since 18 Sep 2026: the units publish the
# repo's default entity names, so the config carries the identity only. Both units share one outdoor unit
# and elect its publisher (fork issue #22): the same group root and the same room-neutral outdoor id on both.
GROUP = {'root': 'airco/outdoor/', 'outdoor_id': 'ac_outdoor', 'entity_prefix': 'ac_outdoor'}
```

Replace (`airco-config.py`, lines 41-59):

```python
    cap = unit.capitalize()
    lines = ['#define HA_DISCOVERY true', f'#define HA_DEVICE_NAME "AC {cap}"', f'#define HA_CLIMATE_ID "AC_{cap}"',
             f'#define HA_ID_PREFIX "ac_{unit}"', f'#define HA_ENTITY_PREFIX "ac_{unit}"']
    if unit == OUTDOOR_UNIT:
        lines += ['#define HA_OUTDOOR_DEVICE true', f'#define HA_OUTDOOR_ID "{OUTDOOR["id"]}"',
                  f'#define HA_OUTDOOR_NAME "{OUTDOOR["name"]}"', f'#define HA_OUTDOOR_ENTITY_PREFIX "{OUTDOOR["entity_prefix"]}"']
    return lines


def discovery_args(unit: str, version: str, frame: int = 20) -> list:
    """Arguments for tools/discovery_payloads (the repo) giving this unit's payloads."""
    cap = unit.capitalize()
    args = ['--base', f'airco/{unit}', '--hostname', f'airco-{unit}', '--device-name', f'AC {cap}',
            '--climate-id', f'AC_{cap}', '--id-prefix', f'ac_{unit}', '--entity-prefix', f'ac_{unit}',
            '--version', version, '--lr', '1' if frame == 33 else '0']   # left/right louvers only on the 33-byte frame
    if unit == OUTDOOR_UNIT:
        args += ['--outdoor', '1', '--outdoor-id', OUTDOOR['id'], '--outdoor-name', OUTDOOR['name'],
                 '--outdoor-entity-prefix', OUTDOOR['entity_prefix']]
    return args
```

with:

```python
    cap = unit.capitalize()
    return ['#define HA_DISCOVERY true', f'#define HA_DEVICE_NAME "AC {cap}"', f'#define HA_CLIMATE_ID "AC_{cap}"',
            f'#define HA_ID_PREFIX "ac_{unit}"', f'#define HA_ENTITY_PREFIX "ac_{unit}"',
            f'#define GROUP_ROOT "{GROUP["root"]}"', f'#define HA_OUTDOOR_ID "{GROUP["outdoor_id"]}"',
            f'#define HA_OUTDOOR_ENTITY_PREFIX "{GROUP["entity_prefix"]}"']


def discovery_args(unit: str, version: str, frame: int = 20, outdoor: bool = False) -> list:
    """Arguments for tools/discovery_payloads (the repo) giving this unit's payloads; with outdoor, also
    the outdoor device's six rows as this unit publishes them when it is the group's publisher."""
    cap = unit.capitalize()
    return ['--base', f'airco/{unit}', '--hostname', f'airco-{unit}', '--device-name', f'AC {cap}',
            '--climate-id', f'AC_{cap}', '--id-prefix', f'ac_{unit}', '--entity-prefix', f'ac_{unit}',
            '--version', version, '--lr', '1' if frame == 33 else '0',   # left/right louvers only on the 33-byte frame
            '--group-base', GROUP['root'].rstrip('/'), '--outdoor-id', GROUP['outdoor_id'],
            '--outdoor-entity-prefix', GROUP['entity_prefix'], '--outdoor', '1' if outdoor else '0']
```

Replace (`airco-config.py`, lines 103-106):

```python
def main(argv):
    if len(argv) in (4, 5) and argv[1] == '--discovery-args':   # --discovery-args <unit> <version> [20|33]
        print('\n'.join(discovery_args(argv[2], argv[3], int(argv[4]) if len(argv) == 5 else 20)))
        return
```

with:

```python
def main(argv):
    if len(argv) >= 2 and argv[1] == '--discovery-args':   # --discovery-args <unit> <version> [20|33] [--outdoor]
        rest = [a for a in argv[2:] if a != '--outdoor']
        if len(rest) not in (2, 3) or (len(rest) == 3 and rest[2] not in ('20', '33')):
            sys.exit(__doc__)
        print('\n'.join(discovery_args(rest[0], rest[1], int(rest[2]) if len(rest) == 3 else 20, '--outdoor' in argv[2:])))
        return
```

Replace (`airco-config.py`, lines 128-129):

```python
    print('  config written; defines: ' + ' '.join(re.findall(r'^#define (\w+)', out, flags=re.M)))
    print(f'  hostname airco-{unit}, prefix airco/{unit}/, MQTT user airco, CONTINUE_WITHOUT_MQTT on, HA_DISCOVERY on, frame {frame} bytes')
```

with:

```python
    print('  config written; defines: ' + ' '.join(re.findall(r'^#define (\w+)', out, flags=re.M)))
    print(f'  hostname airco-{unit}, prefix airco/{unit}/, group root {GROUP["root"]}, MQTT user airco, CONTINUE_WITHOUT_MQTT on, HA_DISCOVERY on, frame {frame} bytes')
```

Run: `python3 -m pytest -q -p no:cacheprovider test_airco_config.py`
Expected: `23 passed`.

- [ ] **Step 4: `units.sh` and `discovery-payloads.sh`**

In `units.sh`:

Replace (`units.sh`, line 14):

```bash
UNITS="slaapkamer uitkijk"
```

with:

```bash
UNITS="slaapkamer uitkijk"
GROUP=airco/outdoor   # GROUP_ROOT of both units (fork issue #22): members/<hostname>, OpData/<the 11 system values>
```

Replace `discovery-payloads.sh` with:

```bash
#!/usr/bin/env bash
# The discovery configs the firmware publishes for one unit, rendered on this PC
# from the repo's builder: one line per entity, topic TAB payload.
# Usage: discovery-payloads.sh slaapkamer|uitkijk [version, default master HEAD] [--outdoor]
# --outdoor adds the outdoor device's six configs as this unit publishes them
# when it is the group's publisher (fork issue #22).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$HERE/units.sh"
unit_vars "${1:-}" || { echo "usage: $0 slaapkamer|uitkijk [version] [--outdoor]" >&2; exit 2; }
shift
VERSION="" OUTDOOR=()
for a in "$@"; do
  case "$a" in
    --outdoor) OUTDOOR=(--outdoor) ;;
    *) [ -z "$VERSION" ] || { echo "usage: $0 slaapkamer|uitkijk [version] [--outdoor]" >&2; exit 2; }; VERSION=$a ;;
  esac
done
VERSION=${VERSION:-$(git -C "$REPO" rev-parse --short=7 "$BRANCH")}
TOOL=$REPO/.pio/discovery_payloads
SRC="$REPO/lib/mhi_pure/mhi_discovery.cpp $REPO/lib/mhi_pure/mhi_discovery.h $REPO/lib/mhi_pure/mhi_group.cpp $REPO/lib/mhi_pure/mhi_group.h $REPO/tools/discovery_payloads.cpp"
# shellcheck disable=SC2086
if [ ! -x "$TOOL" ] || [ -n "$(find $SRC -newer "$TOOL")" ]; then
  mkdir -p "$REPO/.pio"
  g++ -std=gnu++17 -Wall -Wextra -Werror -I "$REPO/lib/mhi_pure" "$REPO/lib/mhi_pure/mhi_discovery.cpp" \
    "$REPO/lib/mhi_pure/mhi_group.cpp" "$REPO/tools/discovery_payloads.cpp" -o "$TOOL"
fi
mapfile -t ARGS < <(python3 "$HERE/airco-config.py" --discovery-args "$UNIT" "$VERSION" "$FRAME" "${OUTDOOR[@]}")
"$TOOL" "${ARGS[@]}"
```

Check it (local only: it compiles the renderer from the repo's working tree, on the feature branch):
```bash
V=$(git -C /home/lucas/coding/MHI-AC-Ctrl rev-parse --short=7 HEAD)
./discovery-payloads.sh slaapkamer "$V" | wc -l            # 17
./discovery-payloads.sh uitkijk "$V" --outdoor | wc -l     # 23
./discovery-payloads.sh uitkijk "$V" | grep -c 'button/ac_uitkijk_restart'   # 1
./discovery-payloads.sh uitkijk "$V" --outdoor | grep ac_outdoor_current | grep -o '"avty_t":"[^"]*"\|"via_device":"[^"]*"'
#   "avty_t":"airco/uitkijk/connected"
#   "via_device":"airco-uitkijk"
```

- [ ] **Step 5: `post-flash-check.sh`, `health-check.sh`, `remote-test.sh`, `rollback-unit.sh`**

In `post-flash-check.sh`:

Replace (`post-flash-check.sh`, lines 17-18):

```bash
batch_c() { git -C "$REPO" merge-base --is-ancestor "$BATCH_C_COMMIT" "$EXPECT" 2>/dev/null; }
# entities_of <any entity of the device>: the device's entity IDs. Found through the device because hass-config may rename IDs.
```

with:

```bash
batch_c() { git -C "$REPO" merge-base --is-ancestor "$BATCH_C_COMMIT" "$EXPECT" 2>/dev/null; }
# has_file <version> <path>: the version contains the file.
has_file() { [ -n "$1" ] && git -C "$REPO" cat-file -e "$1:$2" 2>/dev/null; }
group_build() { has_file "$1" src/group.cpp; }        # fork issue #22: the outdoor election
safe_build() { has_file "$1" src/safe_mode.cpp; }     # fork issue #23: crash-loop safe mode and SafeMode
# restart_build <version>: the version has the Restart button (fork issue #24). No pipe into grep -q (SIGPIPE).
restart_build() { [ -n "$1" ] && grep -q MHI_DISCOVERY_RESTART <<<"$(git -C "$REPO" show "$1:lib/mhi_pure/mhi_discovery.h" 2>/dev/null)"; }
# entities_of <any entity of the device>: the device's entity IDs. Found through the device because hass-config may rename IDs.
```

Replace (`post-flash-check.sh`, lines 34-35):

```bash
    s=$(curl -s -m 10 -H "Authorization: Bearer $T" "$H/api/states/$e" | jq -r '.state // empty')
    case "$s" in ""|unavailable|unknown) bad "$e is '${s:-missing}'" ;; *) ok "$e = $s" ;; esac
```

with:

```bash
    s=$(curl -s -m 10 -H "Authorization: Bearer $T" "$H/api/states/$e" | jq -r '.state // empty')
    # A button's state is the time of its last press: unknown until someone presses it.
    case "$e:$s" in button.*:unknown) ok "$e = unknown (a button, never pressed)"; continue ;; esac
    case "$s" in ""|unavailable|unknown) bad "$e is '${s:-missing}'" ;; *) ok "$e = $s" ;; esac
```

Replace (`post-flash-check.sh`, lines 47-48):

```bash
last() { { grep -F " $PREFIX/$1 \"" "$2" 2>/dev/null || true; } | tail -1 | sed -E 's/^[^ ]+ [^ ]+ "([^"]*)".*/\1/'; }
live() { { grep -F " $PREFIX/$1 \"" "$2" 2>/dev/null || true; } | { grep -v '(retained)$' || true; } | tail -1 | sed -E 's/^[^ ]+ [^ ]+ "([^"]*)".*/\1/'; }
```

with:

```bash
last() { { grep -F " $PREFIX/$1 \"" "$2" 2>/dev/null || true; } | tail -1 | sed -E 's/^[^ ]+ [^ ]+ "([^"]*)".*/\1/'; }
# glast <full topic> <log>: like last, for a topic outside this unit's prefix
glast() { { grep -F " $1 \"" "$2" 2>/dev/null || true; } | tail -1 | sed -E 's/^[^ ]+ [^ ]+ "([^"]*)".*/\1/'; }
# compare_configs <expected tsv> <captured log>: every rendered config is on the broker byte for byte
compare_configs() {
  local topic expected got
  while IFS=$'\t' read -r topic expected; do
    got=$({ grep -F " $topic " "$2" 2>/dev/null || true; } | tail -1 | sed -E 's/^[^ ]+ [^ ]+ //; s/ \(retained\)$//' \
          | python3 -c 'import json,sys; s=sys.stdin.read().strip(); print(json.loads(s) if s else "")')
    [ "$got" = "$expected" ] && ok "config $topic matches" || bad "config $topic differs or is missing"
  done < "$1"
}
live() { { grep -F " $PREFIX/$1 \"" "$2" 2>/dev/null || true; } | { grep -v '(retained)$' || true; } | tail -1 | sed -E 's/^[^ ]+ [^ ]+ "([^"]*)".*/\1/'; }
```

Replace (`post-flash-check.sh`, lines 66-68):

```bash
capture 8 "$W/a.log"
for t in Version connected Wiring Errorcode WIFI_LOST MQTT_LOST WIFI_PHY Uptime ResetReason FreeHeap FrameErrors FrameTimeouts Diag Silent Discovery Power Mode Action Fan Vanes VanesLR 3Dauto Tsetpoint Troom; do
  printf '   %-10s %s\n' "$t" "$(last "$t" "$W/a.log")"
```

with:

```bash
capture 8 "$W/a.log"
for t in Version connected Wiring Errorcode WIFI_LOST MQTT_LOST WIFI_PHY Uptime ResetReason FreeHeap FrameErrors FrameTimeouts Diag Silent Discovery Group SafeMode Power Mode Action Fan Vanes VanesLR 3Dauto Tsetpoint Troom; do
  printf '   %-10s %s\n' "$t" "$(last "$t" "$W/a.log")"
```

Replace (`post-flash-check.sh`, lines 106-108):

```bash
  case "$vanes" in Up|UpCenter|CenterDown|Down|Swing|"?") ok "Vanes $vanes" ;; *) bad "Vanes is '$vanes', expected a name" ;; esac
  [ "$disc" = ok ] && ok "Discovery ok" || bad "Discovery is '$disc', expected ok"
  # Every retained config on the broker must be byte for byte what the tool renders for this unit and version.
```

with:

```bash
  case "$vanes" in Up|UpCenter|CenterDown|Down|Swing|"?") ok "Vanes $vanes" ;; *) bad "Vanes is '$vanes', expected a name" ;; esac
  case "$disc" in
    ok) ok "Discovery ok" ;;
    skipped) bad "Discovery skipped: a discovery config did not fit its buffer and was not published (the unit's Serial log names the row)" ;;
    *) bad "Discovery is '$disc', expected ok" ;;
  esac
  # Every retained config on the broker must be byte for byte what the tool renders for this unit and version.
```

Replace (`post-flash-check.sh`, lines 117-123):

```bash
  wait
  while IFS=$'\t' read -r topic expected; do
    got=$({ grep -F " $topic " "$W/d.log" 2>/dev/null || true; } | tail -1 | sed -E 's/^[^ ]+ [^ ]+ //; s/ \(retained\)$//' \
          | python3 -c 'import json,sys; s=sys.stdin.read().strip(); print(json.loads(s) if s else "")')
    [ "$got" = "$expected" ] && ok "config $topic matches" || bad "config $topic differs or is missing"
  done < "$W/expected.tsv"
else
```

with:

```bash
  wait
  compare_configs "$W/expected.tsv" "$W/d.log"
else
```

Replace (`post-flash-check.sh`, lines 132-133):

```bash
  [ -z "$vlr$d3" ] && ok "20-byte frame, no retained VanesLR/3Dauto" || bad "20-byte build but stale retained VanesLR '$vlr' 3Dauto '$d3'"
fi
```

with:

```bash
  [ -z "$vlr$d3" ] && ok "20-byte frame, no retained VanesLR/3Dauto" || bad "20-byte build but stale retained VanesLR '$vlr' 3Dauto '$d3'"
fi
if group_build "$EXPECT"; then
  # The outdoor election (fork issue #22). The publisher sends the outdoor configs 30 s after its claim or
  # restart, 40 s after a first boot's connect (spec §6.5 A): wait until the unit has been up long enough.
  echo; echo "== The outdoor group ($GROUP/)"
  up=$(last Uptime "$W/a.log")
  if [[ "$up" =~ ^[0-9]+$ ]] && [ "$up" -lt 75 ]; then echo "   unit up ${up} s, waiting $((75 - up)) s"; sleep $((75 - up)); fi
  capture 8 "$W/g.log" "airco/#"
  grp=$(glast "$PREFIX/Group" "$W/g.log")
  case "$grp" in
    0|1) ok "Group $grp" ;;
    2) bad "Group 2: this unit's outdoor ID differs from the group's" ;;
    3) bad "Group 3: another group protocol version leads the group" ;;
    *) bad "Group is '$grp'" ;;
  esac
  rec=$(glast "$GROUP/members/$NEW_HOSTNAME" "$W/g.log")
  [[ "$rec" =~ ^1\;[01]\;[0-9]+\;[0-9]+\;[0-9]+\;ac_outdoor\;$PREFIX/$ ]] && ok "record $rec" || bad "record of $NEW_HOSTNAME is '$rec'"
  # The other unit has a record once it runs a group build too (rollout step 5).
  other_ver=$(glast "airco/$OTHER/Version" "$W/g.log"); other_rec=$(glast "$GROUP/members/airco-$OTHER" "$W/g.log")
  if group_build "$other_ver"; then
    [ -n "$other_rec" ] && ok "record of airco-$OTHER $other_rec" || bad "airco-$OTHER runs $other_ver but has no record"
  else
    echo "   (airco-$OTHER runs ${other_ver:-?}, no group build yet: no record expected)"
  fi
  pubs=""; for u in $UNITS; do [ "$(glast "airco/$u/Group" "$W/g.log")" = 1 ] && pubs="$pubs $u"; done
  read -r -a pub <<<"$pubs"
  if [ "${#pub[@]}" = 1 ]; then
    ok "publisher ${pub[0]}"
    pver=$(glast "airco/${pub[0]}/Version" "$W/g.log")
    # The six outdoor configs on the broker are what the publisher renders, availability and via_device included.
    { "$HERE/discovery-payloads.sh" "${pub[0]}" "$pver" --outdoor || true; } | { grep -F "/ac_outdoor_" || true; } > "$W/outdoor.tsv"
    [ "$(wc -l < "$W/outdoor.tsv")" = 6 ] || bad "discovery-payloads.sh rendered $(wc -l < "$W/outdoor.tsv") outdoor configs for ${pub[0]} $pver, expected 6"
    capture 6 "$W/o.log" "homeassistant/#"
    compare_configs "$W/outdoor.tsv" "$W/o.log"
  else
    bad "expected exactly one unit with Group 1, found ${#pub[@]}:${pubs:- none}"
  fi
fi
sm=$(last SafeMode "$W/a.log")
if safe_build "$EXPECT"; then
  # Boots into safe mode since power-on (#23). Right after the controlled safe-mode proof it is 1, and that is this check's FAIL.
  [ "$sm" = 0 ] && ok "SafeMode 0" || bad "SafeMode is '$sm', expected 0: the unit entered safe mode since its power-on"
elif [ -z "$sm" ]; then
  ok "no retained SafeMode ($EXPECT predates it)"
else
  # A later build's value, left behind after a rollback; rollback-unit.sh clears it. Not this build's fault.
  echo "   (a retained SafeMode '$sm' from a later build: $EXPECT does not publish it)"
fi
```

Replace (`post-flash-check.sh`, lines 221-227):

```bash
  # The unit's entities, found through its device. Ten since batch B; the batch C build adds the louver select, the
  # 3D auto switch, the two frame counters and the error code. IDs are whatever the registry holds (hass-config renames
  # the Dutch ones of before 18 Sep to the English IDs the firmware now pins).
  read -r -a ents <<<"$(entities_of "$ENTITY" | tr -d '"')"
  if batch_c; then want=15; else want=10; fi
  check_entities "device of $ENTITY" "$want" "${ents[@]}"
  sw=""; for e in "${ents[@]}"; do case "$e" in switch.*_stil|switch.*_silent) sw=$e ;; esac; done
```

with:

```bash
  # The unit's entities, found through its device. Ten since batch B; the batch C build adds the louver select, the
  # 3D auto switch, the two frame counters and the error code; the group build the Group role sensor. IDs are
  # whatever the registry holds (hass-config renames the Dutch ones of before 18 Sep to the English IDs the firmware now pins).
  read -r -a ents <<<"$(entities_of "$ENTITY" | tr -d '"')"
  if restart_build "$EXPECT"; then want=17; elif group_build "$EXPECT"; then want=16; elif batch_c; then want=15; else want=10; fi
  check_entities "device of $ENTITY" "$want" "${ents[@]}"
  if restart_build "$EXPECT"; then   # the Restart button (#24); pressing it would restart the unit, so it is only found, not pressed
    rb=""; for e in "${ents[@]}"; do case "$e" in button.*_restart) rb=$e ;; esac; done
    [ -n "$rb" ] && ok "Restart button $rb" || bad "no Restart button on the device of $ENTITY"
  fi
  sw=""; for e in "${ents[@]}"; do case "$e" in switch.*_stil|switch.*_silent) sw=$e ;; esac; done
```

Replace (`post-flash-check.sh`, lines 233-235):

```bash
  [ -n "$sw" ] && [ "$s" = "$(echo "$silent" | tr 'A-Z' 'a-z')" ] && ok "$sw follows Silent" || bad "Silent switch '${sw:-not found}' is '$s', Silent is '$silent'"
  if batch_c && [ "$UNIT" = slaapkamer ]; then   # the shared outdoor unit's device, published by Slaapkamer only (#19)
    read -r -a ou <<<"$(entities_of sensor.ac_outdoor_temperature | tr -d '"')"
```

with:

```bash
  [ -n "$sw" ] && [ "$s" = "$(echo "$silent" | tr 'A-Z' 'a-z')" ] && ok "$sw follows Silent" || bad "Silent switch '${sw:-not found}' is '$s', Silent is '$silent'"
  if group_build "$EXPECT"; then   # the shared outdoor unit's device, whichever unit publishes it (#22)
    read -r -a all <<<"$(entities_of sensor.ac_outdoor_temperature | tr -d '"')"
    ou=()
    for e in "${all[@]}"; do
      # Batch C's Energy config stays retained until hass-config clears it (hass-config#315, rollout step 4).
      if [ "$e" = sensor.ac_outdoor_energy ]; then echo "   (sensor.ac_outdoor_energy is still on the device: the stale batch C config)"; else ou+=("$e"); fi
    done
    check_entities "AC outdoor unit" 6 "${ou[@]}"
  elif batch_c && [ "$UNIT" = slaapkamer ]; then   # batch C: published by Slaapkamer only (#19)
    read -r -a ou <<<"$(entities_of sensor.ac_outdoor_temperature | tr -d '"')"
```

In `health-check.sh`:

Replace (`health-check.sh`, lines 45-47):

```bash
  line=""
  for t in Version connected Errorcode WIFI_LOST MQTT_LOST FrameErrors FrameTimeouts WIFI_PHY Uptime ResetReason FreeHeap Diag Wiring Power Mode Action; do
    v=$({ grep -m1 -F " $PREFIX/$t \"" "$W/$u.log" 2>/dev/null || true; } | sed -E 's/^[^ ]+ [^ ]+ "([^"]*)".*/\1/')
```

with:

```bash
  line=""
  for t in Version connected Group SafeMode Errorcode WIFI_LOST MQTT_LOST FrameErrors FrameTimeouts WIFI_PHY Uptime ResetReason FreeHeap Diag Wiring Power Mode Action; do
    v=$({ grep -m1 -F " $PREFIX/$t \"" "$W/$u.log" 2>/dev/null || true; } | sed -E 's/^[^ ]+ [^ ]+ "([^"]*)".*/\1/')
```

Replace (`health-check.sh`, lines 56-57):

```bash

cat <<'EOF'
```

with:

```bash

echo; echo "-- Outdoor group records ($GROUP/members/: proto;role;term;uptime;period;outdoor_id;prefix; Group 0 member, 1 publisher, 2 ID mismatch, 3 version mismatch)"
(cd "$HASS" && timeout 8 node tools/ha-mqtt-capture.mjs "$GROUP/members/#" "$W/members.log" >/dev/null 2>&1)
recs=$({ grep -F " $GROUP/members/" "$W/members.log" 2>/dev/null || true; } | sed -E 's/^[^ ]+ ([^ ]+) "([^"]*)".*/\1 = \2/' | sort -u)
printf '%s\n' "${recs:-(none)}" | sed 's/^/   /'

cat <<'EOF'
```

Replace (`health-check.sh`, lines 64-66):

```bash
-- Roll a unit back (rollback-unit.sh <unit> <version>) if it shows, compared with its own history:
   clearly more broker connects, HA unavailability, an Errorcode other than 0,
   rising WIFI_LOST/MQTT_LOST, or HA commands that do not take effect.
```

with:

```bash
-- Roll a unit back (rollback-unit.sh <unit> <version>) if it shows, compared with its own history:
   clearly more broker connects, HA unavailability, an Errorcode other than 0, a SafeMode that went up
   without the controlled safe-mode proof,
   rising WIFI_LOST/MQTT_LOST, or HA commands that do not take effect.
```

In `remote-test.sh`:

Replace (`remote-test.sh`, lines 2-6):

```bash
# Remote-button test: show what a unit publishes while someone presses the IR
# remote. Captures airco/<unit>/# and prints, as they arrive, the lines that
# matter for a button test: diag/frame, diag/opdata, Diag, Fan, Mode, Power,
# Tsetpoint, Vanes, VanesLR, 3Dauto, Silent, OpData/unknown, OpData/Tsetpoint and the two fan speeds.
# Read-only. Usage: remote-test.sh slaapkamer|uitkijk [seconds, default 600]
```

with:

```bash
# Remote-button test: show what a unit publishes while someone presses the IR
# remote. Captures airco/# and prints, as they arrive, the lines that matter for
# a button test: diag/frame, diag/opdata, Diag, Fan, Mode, Power, Tsetpoint,
# Vanes, VanesLR, 3Dauto, Silent, OpData/unknown, OpData/Tsetpoint,
# OpData/IU-FANSPEED, and the outdoor fan speed from the group root (fork #22:
# only the group's publisher writes it, under airco/outdoor/OpData/).
# Read-only. Usage: remote-test.sh slaapkamer|uitkijk [seconds, default 600]
```

Replace (`remote-test.sh`, lines 14-18):

```bash
LOG=$(mktemp "${TMPDIR:-/tmp}/remote-test-$UNIT.XXXXXX")
echo "Capturing $PREFIX/# for $SECS s (Ctrl-C stops early). Full log: $LOG"
echo "Press the remote now; hold each state ~40 s. Times are local."
(cd "$HASS" && timeout "$SECS" node tools/ha-mqtt-capture.mjs "$PREFIX/#" "$LOG" >/dev/null 2>&1) &
CAP=$!
```

with:

```bash
LOG=$(mktemp "${TMPDIR:-/tmp}/remote-test-$UNIT.XXXXXX")
echo "Capturing airco/# for $SECS s (Ctrl-C stops early). Full log: $LOG"
echo "Press the remote now; hold each state ~40 s. Times are local."
(cd "$HASS" && timeout "$SECS" node tools/ha-mqtt-capture.mjs "airco/#" "$LOG" >/dev/null 2>&1) &
CAP=$!
```

Replace (`remote-test.sh`, lines 20-22):

```bash
tail -n0 -F --pid="$CAP" "$LOG" 2>/dev/null \
  | grep --line-buffered -E "$PREFIX/(diag/frame|diag/opdata|Diag|Fan|Mode|Power|Tsetpoint|Vanes|VanesLR|3Dauto|Silent|OpData/unknown|OpData/Tsetpoint|OpData/IU-FANSPEED|OpData/OU-FANSPEED) \"" \
  | grep --line-buffered -v '(retained)$' \
```

with:

```bash
tail -n0 -F --pid="$CAP" "$LOG" 2>/dev/null \
  | grep --line-buffered -E "($PREFIX/(diag/frame|diag/opdata|Diag|Fan|Mode|Power|Tsetpoint|Vanes|VanesLR|3Dauto|Silent|OpData/unknown|OpData/Tsetpoint|OpData/IU-FANSPEED)|$GROUP/OpData/OU-FANSPEED) \"" \
  | grep --line-buffered -v '(retained)$' \
```

Replace (`remote-test.sh`, lines 24-26):

```bash
      ts=${line%%Z *}; rest=${line#*Z }
      printf '%s %s\n' "$(date -d "${ts}Z" '+%H:%M:%S')" "${rest#"$PREFIX/"}"
    done &
```

with:

```bash
      ts=${line%%Z *}; rest=${line#*Z }
      rest=${rest#"$PREFIX/"}
      printf '%s %s\n' "$(date -d "${ts}Z" '+%H:%M:%S')" "${rest#airco/}"
    done &
```

In `rollback-unit.sh`:

Replace (`rollback-unit.sh`, lines 3-6):

```bash
# Asks for confirmation. If the image has no Action topic, no Silent/Discovery topics,
# or speaks the 20-byte frame (no VanesLR/3Dauto), those retained values are cleared so
# nothing keeps showing stale state for them.
# Usage: rollback-unit.sh slaapkamer|uitkijk <version>   (no version: list the images)
```

with:

```bash
# Asks for confirmation. If the image has no Action topic, no Silent/Discovery topics,
# speaks the 20-byte frame (no VanesLR/3Dauto), has no SafeMode (#23) or no outdoor
# election (#22: Group, its record, the Group role and Restart configs), those retained
# values are cleared so nothing keeps showing stale state for them.
# Usage: rollback-unit.sh slaapkamer|uitkijk <version>   (no version: list the images)
```

Replace (`rollback-unit.sh`, lines 56-57):

```bash
fi
[ "$IMAGE_FRAME" = "$FRAME" ] || echo "  NOTE: units.sh still says FRAME=$FRAME for $UNIT. Set it to $IMAGE_FRAME, or the next flash-unit.sh builds a $FRAME-byte image again."
```

with:

```bash
fi
# clear_retained <topic>: an empty retained payload through HA's mqtt.publish; prints the HTTP code.
clear_retained() {
  curl -s -m 10 -o /dev/null -w '%{http_code}' -H "Authorization: Bearer $(cat ~/.config/hass/token)" \
    -H 'Content-Type: application/json' -X POST "$H/api/services/mqtt/publish" \
    -d "$(jq -cn --arg t "$1" '{topic:$t,payload:"",retain:true}')"
}
if ! grep -qx 'SafeMode' "$WORK/strings.txt"; then
  echo "  $VERSION has no SafeMode topic; cleared the retained $PREFIX/SafeMode (HTTP $(clear_retained "$PREFIX/SafeMode"))"
fi
if ! grep -qF 'members/' "$WORK/strings.txt"; then
  # This unit's two unit rows of #22 and #24 go with it: the older build has neither entity, so the empty payload
  # removes them from HA. The outdoor device's configs belong to whichever unit publishes for the group: never cleared here.
  for t in "$PREFIX/Group" "$GROUP/members/$NEW_HOSTNAME" \
           "homeassistant/sensor/ac_${UNIT}_group_role/config" "homeassistant/button/ac_${UNIT}_restart/config"; do
    echo "  $VERSION has no outdoor election; cleared the retained $t (HTTP $(clear_retained "$t"))"
  done
fi
[ "$IMAGE_FRAME" = "$FRAME" ] || echo "  NOTE: units.sh still says FRAME=$FRAME for $UNIT. Set it to $IMAGE_FRAME, or the next flash-unit.sh builds a $FRAME-byte image again."
```

Check the syntax, and the helpers against the repo (never run the scripts themselves from the plan):
```bash
for f in discovery-payloads.sh post-flash-check.sh health-check.sh remote-test.sh rollback-unit.sh units.sh; do bash -n "$f" || echo "SYNTAX $f"; done
shellcheck -S warning discovery-payloads.sh post-flash-check.sh health-check.sh remote-test.sh rollback-unit.sh units.sh && echo clean
REPO=/home/lucas/coding/MHI-AC-Ctrl bash -c 'set -uo pipefail
  eval "$(grep -E "^(has_file|group_build|safe_build|restart_build)\(\)" post-flash-check.sh)"
  for v in 8a2c82d HEAD; do printf "%s group:%s safe:%s restart:%s\n" $v "$(group_build $v && echo y || echo n)" "$(safe_build $v && echo y || echo n)" "$(restart_build $v && echo y || echo n)"; done'
#   8a2c82d group:n safe:n restart:n
#   HEAD group:y safe:y restart:y
```
Expected: no `SYNTAX` line, then `clean`, then the two helper lines above. At the default severity, `post-flash-check.sh` shows only the info-level notes it had before (SC2015, SC2018, SC2019), now on more lines, and `rollback-unit.sh` only the info notes it had before (SC1091 twice, SC2012, SC2153).

The two markers `rollback-unit.sh` looks for, in this branch's build and in batch C's (the ci-tree has no secrets):
```bash
cd /home/lucas/coding/MHI-AC-Ctrl
B=.pio/ci-tree/.pio/build/d1_mini/firmware.bin
git -C .pio/ci-tree checkout -q --detach HEAD && (cd .pio/ci-tree && pio run -e d1_mini >/dev/null) && \
  echo "HEAD: $(strings -n 5 $B | grep -cx SafeMode) $(strings -n 5 $B | grep -c 'members/')"
git -C .pio/ci-tree checkout -q --detach 8a2c82d && (cd .pio/ci-tree && pio run -e d1_mini >/dev/null) && \
  echo "8a2c82d: $(strings -n 5 $B | grep -cx SafeMode) $(strings -n 5 $B | grep -c 'members/')"
cd ~/.config/hass/tools/mhi
```
Expected: `HEAD: 1 3` and `8a2c82d: 0 0`. Task 13 checks HEAD out again.

- [ ] **Step 6: The toolkit README**

Replace (`README.md`, lines 9-15):

```markdown
| `flash-unit.sh <unit>` | builds master for the unit, checks the image, keeps it in `images/`, OTA | Lucas (`!`, reads secrets) |
| `rollback-unit.sh <unit> [version]` | flashes a kept image; without a version it lists them | Lucas (`!`, reads secrets) |
| `post-flash-check.sh <unit> [version]` | retained status, Diag, Action, WIFI_PHY, two harmless commands, broker login, HA entity | Claude or Lucas |
| `control-test.sh <unit> <fan>` | fan-mode round trip through HA (run it back to the old value) | Claude or Lucas, on request |
| `remote-test.sh <unit> [seconds]` | shows what the unit publishes while the IR remote is pressed (diag/frame, diag/opdata, fan, setpoint, silent, left/right louvers, 3D auto) | Claude or Lucas |
| `health-check.sh [days]` | broker connects, HA unavailability, retained status | Claude or Lucas |
| `discovery-payloads.sh <unit> [version]` | the discovery configs the firmware publishes for the unit, from the repo's builder | Claude or Lucas |
```

with:

```markdown
| `flash-unit.sh <unit>` | builds master for the unit, checks the image, keeps it in `images/`, OTA | Lucas (`!`, reads secrets) |
| `rollback-unit.sh <unit> [version]` | flashes a kept image; without a version it lists them; clears the retained topics the image does not publish, since #22/#23 also `SafeMode`, `Group`, the unit's record and its Group role and Restart configs (never an outdoor config) | Lucas (`!`, reads secrets) |
| `post-flash-check.sh <unit> [version]` | retained status, Diag, Action, WIFI_PHY, two harmless commands, broker login, HA entity; for a group build (#22) also `Group`, both records and the outdoor configs of the unit whose `Group` is 1; `SafeMode` 0 (#23), the Restart button and `Discovery` `ok`, not `skipped` (#24) | Claude or Lucas |
| `control-test.sh <unit> <fan>` | fan-mode round trip through HA (run it back to the old value) | Claude or Lucas, on request |
| `remote-test.sh <unit> [seconds]` | shows what the unit publishes while the IR remote is pressed (diag/frame, diag/opdata, fan, setpoint, silent, left/right louvers, 3D auto, and the outdoor fan speed from `airco/outdoor/OpData/`) | Claude or Lucas |
| `health-check.sh [days]` | broker connects, HA unavailability, retained status including `Group` and `SafeMode`, the group records | Claude or Lucas |
| `discovery-payloads.sh <unit> [version] [--outdoor]` | the discovery configs the firmware publishes for the unit, from the repo's builder; `--outdoor` adds the outdoor device's six as the unit publishes them when it is the group's publisher | Claude or Lucas |
```

Replace (`README.md`, lines 30-31):

```markdown

`images/`: every flash keeps its image, so the image of the running version is the
```

with:

```markdown

Outdoor group (#22): both units have `GROUP_ROOT` `airco/outdoor/` (`GROUP` in `units.sh` and in `airco-config.py`)
and elect the one that writes the eleven outdoor values under `airco/outdoor/OpData/` and sends the outdoor device's six
configs. `post-flash-check.sh` waits until the unit has been up 75 s, then checks `Group`, the records under
`airco/outdoor/members/` and the six configs against a render for the unit whose `Group` is 1.

`images/`: every flash keeps its image, so the image of the running version is the
```

- [ ] **Step 7: Nothing to commit**

The toolkit is not a git repository. Report the nine changed files and the backup in `.bak-22/`. Keep the backup until the rollout is done; Lucas decides when it goes.

---

### Task 13: Final verification

No code changes. If something needs fixing, fix it following the conventions of Tasks 1-12, in a new commit.

- [ ] **Step 1: The host tests, the way CI runs them**

```bash
rm -f test/fixtures/discovery/*.txt test/fixtures/discovery_all/*.txt
pio test -e native                                  # 248 test cases: 248 succeeded
git diff --exit-code -- test/fixtures/discovery test/fixtures/discovery_all && \
test -z "$(git status --porcelain -- test/fixtures/discovery test/fixtures/discovery_all)" && echo fixtures-ok
```

- [ ] **Step 2: All 12 at HEAD, clean**

```bash
git -C .pio/ci-tree checkout -q --detach HEAD
(cd .pio/ci-tree && pio run -e d1_mini -e ci-extended-frame -e ci-enhanced-resolution -e ci-ds18x20-sensor -e ci-ds18x20-troom \
  -e ci-poweron-when-changing-mode -e ci-continue-without-mqtt -e ci-custom-payloads -e ci-ha-discovery \
  -e ci-ha-discovery-outdoor -e ci-config-defaults -e ci-all-options) 2>&1 | grep -E "Flash budget|^RAM:|SUCCESS|FAILED"
```
Expected: 12 × `SUCCESS`. The trial run's figures, built in scratch copies (the version string in `build_version.h` can move them by a few bytes):

| env | flash B | static RAM B |
|---|---|---|
| `d1_mini` | 350144 | 32176 |
| `ci-extended-frame` | 350928 | 32240 |
| `ci-enhanced-resolution` | 350432 | 32208 |
| `ci-ds18x20-sensor` | 354976 | 32264 |
| `ci-ds18x20-troom` | 355136 | 32312 |
| `ci-poweron-when-changing-mode` | 350272 | 32176 |
| `ci-continue-without-mqtt` | 350128 | 32172 |
| `ci-custom-payloads` | 350304 | 32192 |
| `ci-ha-discovery` | 356592 | 34804 |
| `ci-ha-discovery-outdoor` | 357296 | 34872 |
| `ci-config-defaults` | 350400 | 32204 |
| `ci-all-options` | 362672 | 35020 |

At `725dd49`, `d1_mini` is 343872 B of flash and 30672 B of static RAM, and `ci-all-options` is 355984 and 33404.

- [ ] **Step 3: History and tree**

```bash
git log --format='%h %s' 725dd49..HEAD      # eleven commits, each subject ending in (#22), (#23), (#24) or (#22, #23, #24)
git log --format=%B 725dd49..HEAD | grep -c '^Claude-Session: https://claude.ai/code/session_013aKH8NAdtagPe21HRXJdt5$'   # 11
git status --porcelain                       # empty
```

- [ ] **Step 4: Remove the build worktree**

```bash
git worktree remove .pio/ci-tree && git worktree list    # only the repo itself
```

- [ ] **Step 5: Report**

To the orchestrator:
- the eleven commit hashes;
- the host-test count;
- the flash and static-RAM figures of `d1_mini` and `ci-all-options` against `725dd49`;
- the three discovery high-water marks;
- the toolkit test count and the backup path.

The rollout (#22 spec §11, #23/#24 spec §3: hass-config's acks, flashing, the takeover test, the safe-mode proof, the cleanup) is not part of this plan.

---

## Risks and things to watch on the hardware

- **The sentinels (#22 §4.3, §12.6):** after every connect, and on every claim or publisher start, each of the 11 values goes out at its first reading. That includes `DEFROST` "Off" and readings of 0. hass-config should expect one publish of each on the group root per connect of the publisher.
- **FreeHeap drops by about 1.5 KB** (static RAM +1504 B on `d1_mini`). Compare the day-after check against a new baseline of about 40 KB at boot, not the old 41.5 KB.
- **The first boot (#22 §6.5 A):** until the claim, 10 s after the connect, no unit writes the 11 values anywhere. The old ones stay retained under `airco/<unit>/OpData/` until the cleanup of rollout step 9.
- **Group topics on a single split:** with the defaults, `GROUP_ROOT` = `MQTT_PREFIX` and `GROUP_OP_PREFIX` = `MQTT_OP_PREFIX`. A single split still publishes a record at `<MQTT_PREFIX>members/<HOSTNAME>` and a `Group` topic, and its 11 values start 10 s after each connect instead of at once.
- **A peer's old connected topic** stays subscribed until the next connect after its prefix changed (answer 7). Its messages match no peer and are swallowed by `group_handle_message()`.
- **The first boot on this build reads RTC user block 32 as whatever is there.** Nothing has written it before, so the record is invalid: count 0, entries 0, a normal boot. An OTA flash ends with reason 4 anyway.
- **The safe-mode proof** (rollout, not this plan) sends `set/reset crash` three times. Each crash is a reboot of a few seconds, like the quick reconnect of #22 §6.5. The 10 minutes of safe mode that follow are a real takeover: Uitkijk claims 30 s + 5 s after Slaapkamer's will, as #23 spec §3 expects, which is why it puts the proof after the takeover test.
  - Every `set/reset`, `crash` or `reset`, goes out without retain: `mosquitto_pub` without `-r`, or `mqtt.publish` with `retain: false`. A retained `crash` would crash the unit after every normal boot, so safe mode would come back every 11 minutes. One sent retained by mistake is cleared with `mosquitto_pub -h <broker> -r -n -t airco/slaapkamer/set/reset`. The docs say the same (Task 11).
- **The crash hook** runs inside every software crash, after the core's stack dump: two RTC accesses and a pure function. `abort()`, `panic()`, a failed `assert` or `new` and a stack overflow now count towards safe mode although `ResetReason` says `Software/System restart`, so a unit that asserts at every boot reaches safe mode too, as the spec intends.
- **The ghost limit (#22 §5.2)** and **the publisher that cannot read its AC** are documented, not solved, as the spec decides.
- **The toolkit recognises builds by files** (`src/group.cpp`, `src/safe_mode.cpp`, `MHI_DISCOVERY_RESTART`). Renaming one of them later means updating its helper in `post-flash-check.sh`.

## Self-review

**Spec coverage, #22 (`2026-09-19-outdoor-election-design.md`, with the answers of `04fbecb`):**

| Spec | Where |
|---|---|
| §1 what changes | Tasks 1-6 |
| §2 defines | Task 3 (`GROUP_ROOT`, `GROUP_OP_PREFIX` with its defaulting rule, `HA_NAME_GROUP_ROLE`; `TOPIC_GROUP` in `MHI-AC-Ctrl.h`), Task 5 (`HA_OUTDOOR_ID` derived, `#error`, `TELEMETRY_PERIOD`) |
| §2 compile-time checks | Task 1 (the rules, host-tested), Task 5 (the asserts, including the strict `HA_OUTDOOR_ID` and `MQTT_PREFIX` checks and the derived-ID length, and the six refusals in Step 7); `GROUP_OP_PREFIX` under `GROUP_ROOT` in Task 3 |
| §3 topics | Task 6 (record, `Group`, the system values), Task 3 (`Group` topic name) |
| §4.1 the list, `opdata_*` only | Task 6 `is_system_value()` |
| §4.2 KWH per unit | Task 3 (row retired, never built), Task 6 (KWH not in the list) |
| §4.3 | Task 4 |
| §5.1 record | Task 1, with the same field rules as the compile-time checks |
| §5.2 table: 6 entries, refresh, full table, cleared at connect | Task 2 |
| §5.3 definitions | Task 2 (`fresh`, `alive`, `gone`, `leader`, `compute_state`, `compatible`, incumbents, candidates, `beats`, `max_term_seen`) |
| §6.1 connect and grace, subscribe without unsubscribe | Task 2 (`mhi_group_connect`, tick, the `subscribed` flag), Task 6 (subscription at connect, subscriptions from `loop()`) |
| §6.2 rules 1-5, rule 3 at 35 s | Task 2 (`apply_rules`, `mhi_group_on_record`, the tick; scenario 7b pins 35 s) |
| §6.3 actions | Task 2 (flags), Task 6 (carrying them out) |
| §6.4 gate | Task 6 (`output_P`), Task 2 (`mhi_group_may_publish_system`) |
| §6.5 A, B, C, quick reconnect | Task 2 scenarios 1, 3, 8 and 4; the rest is rollout |
| §7 layout | the File map; the group's own 141-byte copy is in `group_handle_message` (Task 6) |
| §8.1 rows, closed range, retired KWH | Task 3 |
| §8.2 payloads | Task 3 (fixtures show exactly `~` and `avty_t`) |
| §8.3 cursors | Task 5 |
| §8.4 rules from hass-config | the topics are byte-identical for every publisher (Task 3 test); no empty payload (Task 3 test, Task 5 `publish_row`); only the publisher sends (Task 2 scenario 8, Task 6); values before configs (Task 2: 30 s, and the re-send at 35 s) |
| §8.5 the left-over Energy config | the firmware never writes it (Task 3); clearing it is hass-config#315 |
| §9 tests | Task 1 (record, invalid forms, foreign, empty, hostname, default ID), Task 2 (scenarios 1-17: `test_scenario_1` .. `test_scenario_17`; scenario 16 inside 1 and on its own; scenarios 18-30 pin what the plan review found untested), Task 3 (discovery items), Task 5 (CI envs, flash check) |
| §10 toolkit | Task 12 |
| §10 docs, `Version.md` included | Task 11 |
| §10 hass-config | out of scope (hass-config#315) |
| §11 rollout | out of scope |
| §12 refinements | 1 (Task 3), 2 (record period, Task 1), 3 (settle, Task 2), 4 (30 s, Task 2), 5 (re-send at 35 s, Task 2), 6 (sentinels, Task 4), 7 (Task 5), 8 (Tasks 3 and 5), 9 (Task 2 scenario 14), 10 (Task 2 scenario 11), 11 (Task 2 scenario 12), 12 (Task 5), 13 (Tasks 1 and 5) |

**Spec coverage, #23/#24 (`2026-09-19-safe-mode-and-ride-alongs-design.md`):**

| Spec | Where |
|---|---|
| §1.2 crash count, reasons, invalid record, 120 s clear | Task 7 (`mhi_safe_boot`, `mhi_safe_record_clear_count`), Task 8 (`safe_mode_clear_after_boot`) |
| §1.2 the crashed bit (`725dd49`): the hook sets it, the next boot counts it, every boot clears it | Task 7 (`mhi_safe_record_mark_crashed`, `mhi_safe_boot`, six tests), Task 8 (`custom_crash_callback`, and where the core calls it) |
| §1.2 safe mode: Wi-Fi and OTA only, 10-min restart, crash inside stays | Task 8 (`setup()` and `loop()` branches), Task 7 (the test for a crash inside safe mode) |
| §1.2 `SafeMode`, a bare integer at every connect | Task 8 (`support.cpp`), Task 11 (docs), Task 12 (checked and printed) |
| §1.3 RTC record, user block 32, magic, data with the crashed bit in bit 16, check | Task 7 (the words), Task 8 (`SAFE_MODE_RTC_BLOCK 32`, and the check of the core for other users) |
| §1.4 code layout, API names, no blocking; the pure helper for the bit and the hook without Serial or allocation | Tasks 7 and 8 (the glue in `src/safe_mode.{h,cpp}`, a `Decision:`) |
| §1.5 host tests, all eight items and the crashed bit | Task 7 Step 1; mutation check in Step 5 |
| §1.5 `set/reset crash`, reason 2 | Task 8 (evidence from the core, disassembly in Step 7) |
| §2.1 `Discovery` `skipped`, after the unit rows and after the outdoor rows | Task 9 |
| §2.2 batch C cleanup | Task 10 (code and tests; the `else` is already gone), Task 11 (the two docs items) |
| §2.3 CI fan names | Task 10 |
| §2.4 Restart button, abbreviations checked, 17 entities | Task 9 (HA source cited; `test_a_unit_has_17_entities_with_the_33_byte_frame`), Task 12 (17 on the device, the button found) |
| §3 rollout | out of scope; Task 12 prepares the checks it names (`SafeMode 0`, the button, `skipped`) |

**Placeholder scan:** no TBD or TODO. Every code step is a full code block or an exact replace/with pair, and every command has its expected output.

**Type consistency:** checked by construction. The blocks in this file were rendered from the one scratch tree that built and passed the tests, and each replace pair was checked to match its file exactly once at the time its step runs. The signatures named in the Interfaces blocks are the ones in that tree: `mhi_group_on_record(MhiGroup*, const char*, const char*, size_t, uint32_t)`, `mhi_group_tick(MhiGroup*, uint32_t, MhiGroupActions*)`, `group_handle_message(const char*, const uint8_t*, unsigned int)`, `outdoor_id()`, `uptime_seconds()`, `discovery_start_outdoor()`/`discovery_cancel_outdoor()`, `mhi_discovery_is_outdoor_row(MhiDiscoveryRow)`, `mhi_safe_boot(uint32_t, const uint32_t[3], uint32_t[3])`, `mhi_safe_record_clear_count(uint32_t[3])`, `mhi_safe_record_mark_crashed(uint32_t[3])`, `custom_crash_callback(struct rst_info*, uint32_t, uint32_t)`, `safe_mode_boot()`, `safe_mode_clear_after_boot(uint32_t)`, `safe_mode_entries()` and `safe_mode_test_crash()`.

---

## Questions for the spec owner

None open. The seven questions of the first version were answered in `04fbecb`, and this plan applies the answers:
- 1: the derived-ID refusal stays;
- 2: strict checks for `HA_OUTDOOR_ID` and `MQTT_PREFIX`;
- 3: the re-send at 35 s;
- 4: the `post-flash-check.sh` decisions;
- 5: `TOPIC_GROUP` in `MHI-AC-Ctrl.h`;
- 6: a `Version.md` bullet;
- 7: no unsubscribe, which saves 384 B of `MhiGroup` and 65 B of the action set.

The RTC question of the second version was answered in `a6f16e3`: the record goes to user block 32, after eboot's OTA command (Task 8). The plan review's crash-hook finding was answered in `725dd49`: the core's `custom_crash_callback()` sets the crashed bit, bit 16 of the data word, and every boot clears it (Tasks 7 and 8).
