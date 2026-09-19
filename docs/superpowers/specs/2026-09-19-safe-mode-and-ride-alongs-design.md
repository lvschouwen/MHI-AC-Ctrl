# Crash-loop safe mode (#23) and the ride-alongs (#24): addendum to the #22 build

Lucas decided on 19 Sep 2026 that these ride the #22 build (`2026-09-19-outdoor-election-design.md`): one build, one flash per unit. The conventions of that spec hold here too.

## 1. #23: crash-loop safe mode

### 1.1 Why
OTA is the only way to reach the two units without tools: Uitkijk means opening the AC, Slaapkamer a USB cable. A build that crashes shortly after it connects reboots every ~10 s. An OTA upload of ~350 KB needs about 15-20 s of uptime, so such a build can lock itself out. #22 brings the largest new module the fork has had, and it runs right after the MQTT connect. The build that brings that risk should also carry the way back.

### 1.2 Behaviour
**The crash count.** At the start of `setup()`, right after `Serial.begin()`, the unit reads its reset reason and a small record in RTC user memory (it survives resets, not power loss):
- A reset reason of 1 (hardware watchdog), 2 (exception) or 3 (software watchdog) is a crash: the count goes up by one, capped at 255.
- So is a boot that finds the **crashed bit** set in the record. The core's weak hook `custom_crash_callback()` runs on every software crash path: exception, software watchdog, `abort()`, `panic()`, a failed `assert`, a failed `new`, and the cont-stack overflow check after every `loop()`. The unit defines the hook, and it does one thing: it sets that bit in the RTC record. The SDK reports `abort`, `panic`, `assert` and a stack overflow as reason 4, so the reason alone would miss exactly the likelier failures of new code; the plan review found this on 19 Sep, and Lucas approved the hook. A hardware watchdog runs no hook but keeps reason 1. Every boot clears the bit.
- Every other reason sets the count to 0: power-on (0), `ESP.restart()` (4, which is also how an OTA update and `set/reset` end), deep-sleep wake (5), external reset (6).
- An invalid record (magic or check word wrong, as after a power-on) also counts as 0.
- Once the unit has been up for 120 s in normal mode, it writes the count back to 0, once per boot. So only crashes that come less than 120 s after boot count as a loop.

**Safe mode** starts when the count is 3 or more after this boot's update. The unit then does only this:
- `initWiFi()`, `setupOTA()`, then in `loop()` only the Wi-Fi state machine (`setupWiFi`) and `ArduinoOTA.handle()`;
- no `MeasureFrequency()`, no DS18x20, no MQTT, no AC core (MISO is never driven), no discovery, no group;
- after 10 min, `ESP.restart()`. That is reason 4, so the count goes back to 0 and the next boot is normal. If the fault is still there, three more crashes lead to safe mode again. The cycle is about 11 min, 10 of them reachable over OTA.
- A crash inside safe mode counts as a crash, so the unit stays in safe mode. It never falls back into the loop it was protecting against.

**Visible:**
- In safe mode: Serial lines, the OTA service on the network, and in HA the unit reads unavailable, because its will set `connected 0` when the crashed session dropped.
- Afterwards: the record also counts **safe-mode entries** since power-on. Every normal connect publishes that as a new retained topic `SafeMode`: a bare integer, `0` on a healthy unit. hass-config logs every increase. It gets no HA entity; `health-check.sh` prints it. Without it, a unit that had crash-looped would show nothing once it booted normally again.

**The AC** runs on its own remote meanwhile. The bus is silent, as it already is during a crash loop or an OTA upload.

### 1.3 RTC record
- Three 32-bit words at RTC **user block 32** (`ESP.rtcUserMemoryRead/Write`, offset 32, 12 bytes): `magic` (`0x4D484953`), `data` (count in the low byte, safe-mode entries in the next byte, the crashed bit in bit 16), and `check` (`magic ^ data ^ 0xFFFFFFFF`).
- **Why block 32** (corrected on 19 Sep during planning; this spec first said block 0).
  - In the pinned core (framework 3.30102.0, `platform espressif8266@4.2.1`), `ESP.rtcUserMemory*(offset)` maps to `system_rtc_mem_*(64 + offset)`. System block 0 is `0x60001100`, so user block 0 is `0x60001200`: exactly where eboot keeps its 128-byte OTA command, user blocks 0-31. `Esp.cpp` says so itself: "the eboot command will be stored into the first 128 bytes of user data".
  - A record there could not break an OTA, because eboot checks its own magic and CRC. But every OTA would wipe the record, including the OTA that rescues a unit from safe mode.
  - Block 32 is the first word after the command. Nothing else in the core, its libraries or this project uses RTC user memory.
- The plan checks the core once more for any other user of RTC user memory.

### 1.4 Code
- **`lib/mhi_pure/mhi_safe_mode.{h,cpp}`**, host-tested:
  - `MhiSafeBoot mhi_safe_boot(uint32_t reset_reason, const uint32_t rec_in[3], uint32_t rec_out[3])` returns `{bool safe_mode; uint8_t count; uint8_t entries;}` and writes the updated record. Safe-mode entries go up by one when this boot enters safe mode.
  - `void mhi_safe_record_clear_count(uint32_t rec[3])` for the 120 s clear; it keeps the entries.
  - The crashed bit: a pure helper sets it in a record, and `mhi_safe_boot` counts and clears it.
- **`src/safe_mode.cpp`** defines `custom_crash_callback()`: read the record, set the bit, recompute the check word, write it. No Serial, no allocation.
  - No Arduino, no RTC access. The glue reads and writes.
- **`src/main.cpp`:**
  - `setup()` starts with the decision.
  - In safe mode, `setup()` sets up Wi-Fi and OTA and returns early.
  - `loop()` checks a `safe_mode` flag first and runs only Wi-Fi, OTA and the 10-min restart.
  - In normal mode, `loop()` clears the count once at 120 s uptime.
  - `SafeMode` is published at connect with the other status topics (`support.cpp`, `TOPIC_SAFE_MODE "SafeMode"`).
- **Constraint** (the fork's rule since July): nothing in the decision path may block, loop or depend on the network. It is a few RTC word reads, a pure function and one RTC write.

### 1.5 Tests and proof
- **Host tests:**
  - every reset reason 0-6 and one out of range;
  - the crashed bit with reason 4 counts as a crash, and the bit is cleared after every boot;
  - an invalid record for each of: magic, check and garbage;
  - count 2 → 3 enters safe mode;
  - the cap at 255;
  - a crash inside safe mode stays in safe mode;
  - a soft restart after safe mode goes back to normal;
  - the entries counter;
  - the 120 s clear keeps the entries.
- **On the units, every normal boot proves the normal path.** A broken read or write would show at once as a unit that misbehaves at boot, and Slaapkamer goes first as always.
- **Decided (Lucas, 19 Sep): a controlled proof of the safe-mode path, included.**
  - `set/reset` accepts a second payload, `crash`. It triggers one deliberate exception, reset reason 2. Sending it three times, each after the unit is back (within 120 s of its boot), must put Slaapkamer in safe mode:
    - HA unavailable for 10 min;
    - OTA advertised on mDNS (`_arduino._tcp`, checked with `avahi-browse`);
    - after it: a normal boot with `SafeMode 1`.
  - It can never start an uncontrolled loop: every crash is one we send. If safe mode failed to engage, the unit would simply boot normally again.
  - Cost: about 5 lines, and a command that reboots the unit, which `set/reset` already does.
  - Without it, the safe-mode path is proven by host tests and review only, until a real crash loop.
  - Lucas approved the addendum with it on 19 Sep.

## 2. #24: the ride-alongs

### 2.1 `Discovery` status after a skipped row
- Today the `Discovery` topic reads `ok` even when a row did not fit its buffer and was skipped (Serial only). New payload `PAYLOAD_DISCOVERY_SKIPPED "skipped"`:
  - after the unit rows: `modes` when the climate row was skipped for its mode names (as today, it takes precedence), otherwise `skipped` when any unit row did not fit, otherwise `ok`;
  - after the outdoor rows (#22's second cursor): `skipped` when an outdoor row did not fit; nothing is published when all fit.
- hass-config and `post-flash-check.sh` expect `ok`; they are told.

### 2.2 Batch C cleanup (no behaviour change)
- `SW-Configuration.md`: the AC keeps 3D auto on when a louver position is chosen (seen 18 Sep, `DB17 0f>0e`), and the `FrameErrors`/`FrameTimeouts` baseline from the batch C soak (0 errors over the soak, timeouts only at boot, 0-1).
- `lib/mhi_pure/mhi_vanes_lr.h`: the comment "1 leftmost .. 7 spot" says that 6 wide and 7 spot are spread modes, not positions, as `MHI-AC-Ctrl.h` already does.
- `src/MHI-AC-Ctrl-core.cpp`: include order. `lib/mhi_pure/mhi_vanes_lr.cpp`: the `else` on its own line.
- `test/test_mhi_discovery`: remove the dead weaker `"mdl"` assertion, and replace the fixture-written test that asserts only `> 0` with one that checks what it writes.

### 2.3 CI: fan names overridden
The `ci-custom-payloads` env adds `-D PAYLOAD_FAN_1=\"Low\"` … `PAYLOAD_FAN_4=\"Top\"`, compile-only. It proves the `#ifndef` defaults of #21 F6 can be replaced without a redefinition.

### 2.4 Restart button in Home Assistant
- A new discovery row `MHI_DISCOVERY_RESTART`, appended after `MHI_DISCOVERY_GROUP_ROLE`, as a unit row:
  - component `button`, uniq_id `<id_prefix>_restart`, name `HA_NAME_RESTART "Restart"`;
  - command topic `~/<set_prefix><TOPIC_REQUEST_RESET>`, payload-press `PAYLOAD_REQUEST_RESET` (`reset`);
  - device class `restart`, entity category `config`, the unit's availability;
  - `default_entity_id button.<entity_prefix>_restart` when set.
- The abbreviations (`cmd_t`, `pl_prs`, `dev_cla`, `ent_cat`) are checked against HA's `abbreviations.py` before use.
- 17 entities per unit.
- Pressing it on the outdoor publisher is the quick-reconnect case of #22: no handover.

## 3. Rollout
Everything rides the #22 flash (#22 spec §11). Additions:
- After the Slaapkamer flash: `SafeMode 0` retained, and the restart button present.
- The safe-mode proof on Slaapkamer (§1.5). Conditions from hass-config: daytime, not near Bedtijd, HI POWER/ECO off on Slaapkamer, zonnekoeling not running that room, ideally Slaapkamer off. No push fires, only log lines. The proof comes after the #22 reconnect and takeover tests. The takeover test must end first, because safe mode takes Slaapkamer off the network for 10 min, which is itself a takeover. Uitkijk takes over (term n+1), and Slaapkamer returns as a member.
- hass-config: fixtures with 17 rows per unit, the `skipped` value, and the `button` entity (`button.ac_<unit>_restart`).
