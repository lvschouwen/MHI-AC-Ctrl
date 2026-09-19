# Outdoor election: the indoor units of one outdoor unit elect the publisher of its values (fork #22)

Design for fork issue **#22**. Sections 1-5 of the design were approved one by one by Lucas on 18-19 Sep 2026 after two Codex rounds, and hass-config checked the Home Assistant side in HA 2026.9's source and in its sandbox (rules in §8.4). This spec writes that design out in full. It also adds the KWH decision of 19 Sep (§4.2) and the refinements listed in §12, which were not in the approved text. The conventions of the earlier specs hold unchanged: pure logic in `lib/mhi_pure` with host tests first, `#ifndef`-guarded defines, the append-only discovery table, the 1024 B static buffer, and reference fixtures checked by CI.

## 1. What changes

Today one unit carries the build flag `HA_OUTDOOR_DEVICE` and the outdoor device reads that unit's own `OpData/` topics. After this change:

- Every unit is a candidate publisher. The units of one outdoor unit share a topic root, `GROUP_ROOT`, and elect one publisher over MQTT. A single split needs no configuration: its group root defaults to its own `MQTT_PREFIX`, so it elects itself and its topics stay where they are.
- The 11 system values (§4.1) are written to `<GROUP_OP_PREFIX><name>`, by the current publisher only. Every other value stays under each unit.
- The outdoor device's discovery configs point at the group root, and only the current publisher sends them.
- When the publisher drops out, another unit takes over, and nothing hands back when it returns.

On Lucas's units: `GROUP_ROOT "airco/outdoor/"` and `HA_OUTDOOR_ID "ac_outdoor"` on both (toolkit). So the system values move from `airco/slaapkamer/OpData/<name>` to `airco/outdoor/OpData/<name>`, and the six outdoor configs keep their topics and uniq_ids.

## 2. Configuration (`src/support.h`)

| define | default | meaning |
|---|---|---|
| `GROUP_ROOT` | `MQTT_PREFIX` | Topic root shared by the units of one outdoor unit. Ends in `/`. |
| `GROUP_OP_PREFIX` | `MQTT_OP_PREFIX` when `GROUP_ROOT` is not defined, otherwise `GROUP_ROOT "OpData/"` | Where the publisher writes the system values. With the defaults a single split keeps its topics even with a custom `MQTT_OP_PREFIX`. |
| `HA_OUTDOOR_ID` | derived at boot: slug of `GROUP_ROOT` + `_outdoor` (`airco/outdoor/` → `airco_outdoor_outdoor`, `MHI-AC-Ctrl/` → `mhi_ac_ctrl_outdoor`) | The outdoor device's identifier and uniq_id prefix. Derived from the group root so that every member derives the same one. The suffix keeps it apart from a unit whose `HA_ID_PREFIX` equals the slug. |
| `HA_NAME_GROUP_ROLE` | `"Group role"` | Name of the new diagnostic entity. |
| `TOPIC_GROUP` | `"Group"` | The per-unit role topic. |

- `HA_OUTDOOR_DEVICE` is removed. A build that still defines it stops with `#error "HA_OUTDOOR_DEVICE was removed (fork #22): every unit is a candidate publisher now; give the units of one outdoor unit the same GROUP_ROOT"`.
- `HA_OUTDOOR_NAME` and `HA_OUTDOOR_ENTITY_PREFIX` stay as they are.
- `TELEMETRY_PERIOD` must be 1..86400. The build refuses 0: the member record's refresh and the liveness rule depend on it. The upper bound keeps `3 × period` in milliseconds inside 31 bits.
- Compile-time checks (constexpr, like the existing `starts_with`):
  - `GROUP_ROOT` is 1..64 characters, ends in `/` and contains no `+ # ;`;
  - the worst incoming record packet fits PubSubClient3's receive buffer, which drops any larger packet whole (`MQTT_MAX_PACKET_SIZE`, 256 bytes; the firmware does not enlarge it): 5 bytes of fixed header + 2 + `GROUP_ROOT` + `members/` + a 32-byte hostname + a 140-byte record ≤ `MQTT_MAX_PACKET_SIZE`. With the 64-character bound that is 251. Without this check a long root would make every unit drop the others' records, and two publishers would never see each other (Codex review, 19 Sep);
  - `GROUP_OP_PREFIX` starts with `GROUP_ROOT` (with `HA_DISCOVERY` only, as for `MQTT_OP_PREFIX` today);
  - `<GROUP_ROOT>members/` does not start with `MQTT_SET_PREFIX`;
  - `HOSTNAME` is 1..32 characters without `/ + # ; " \`;
  - an explicit `HA_OUTDOOR_ID` is 1..40 characters without `/ + # ; " \`.
- Group protocol: all units of a group use the same `TOPIC_CONNECTED` and `PAYLOAD_CONNECTED_TRUE`/`_FALSE` (the documentation says so). Peers are watched on `<their prefix><TOPIC_CONNECTED>`.

## 3. Topics and payloads

All retained, QoS 0.

| topic | written by | payload |
|---|---|---|
| `<GROUP_ROOT>members/<HOSTNAME>` | every unit, about itself | the member record, §5.1 |
| `<MQTT_PREFIX>Group` | every unit, about itself | `0` member, `1` publisher, `2` outdoor ID mismatch, `3` protocol version mismatch |
| `<GROUP_OP_PREFIX><name>` | the current publisher only | the 11 system values, same text as today's `OpData/<name>` |
| `<MQTT_PREFIX>OpData/<name>` | every unit | the per-unit values including `KWH` (unchanged); the system values are no longer written here |

On Lucas's units, for example:
- `airco/outdoor/members/airco-slaapkamer` = `1;1;1;41382;60;ac_outdoor;airco/slaapkamer/`
- `airco/slaapkamer/Group` = `1`
- `airco/outdoor/OpData/CT` = `3.29`

## 4. System values and per-unit values

### 4.1 The fixed list
Measured on 17 Sep 2026 with both units cooling. The list is fixed: it does not follow the request class byte.
- **System (11), group root, publisher only:** OUTDOOR, CT, COMP, DEFROST, TOTAL-COMP-RUN, PROTECTION-NO, TD, TDSH, THO-R1, THI-R2, OU-FANSPEED. THI-R2 is requested with the outdoor class byte and reads the same on both units.
- **Per unit (under `MQTT_OP_PREFIX`, as today):**
  - RETURN-AIR, THI-R1, THI-R3, IU-FANSPEED, TOTAL-IU-RUN, `Tsetpoint`, `Mode`, `unknown`;
  - OU-EEV1: requested with the outdoor class byte, but each indoor circuit has its own valve (97 vs 164 on 17 Sep);
  - KWH, see §4.2.
- **ErrOpData** stays per unit, system values included: it is a snapshot the unit requested from its own indoor unit.

Only the `opdata_*` statuses of the 11 are routed; the `erropdata_*` statuses that share their case labels in `main.cpp` keep the unit's `MQTT_ERR_OP_PREFIX`.

### 4.2 KWH is per unit (measured and decided 19 Sep 2026)
HA history of 18 Sep 20:04 to 19 Sep 07:20:
- Slaapkamer was switched on at 23:33:15, and its KWH went 3.25 → 0.00 at 23:33:20.
- From 20:04 to 23:33 Slaapkamer was off while Uitkijk cooled. CT × 230 V integrates to 0.90 kWh over that window, and Slaapkamer's KWH did not move.
- After the reset every KWH step matched the system CT integral since 23:33, also while both units ran: 0.25 at 0.27, 1.00 at 1.06, 2.50 at 2.64 kWh.
- Uitkijk read 2.00 at the same moment Slaapkamer read 2.50.

So code `0xc0 0x94` is the outdoor unit's energy counted while **this** indoor unit is on, reset when this unit is switched on. It is not a system value (hass-config's logbook agrees). **Lucas's decision:** KWH stays under each unit, with no entity. The outdoor device loses its Energy row, and hass-config computes the house's AC energy by integrating power (CT × 230 V) in HA. The 17 Sep note "KWH is a system counter" was two sessions that had started together.

### 4.3 Publishing every value after a reset (core change)
The core publishes an operating value only when it differs from the last one. `reset_old_values()` sets that last value to a sentinel, and for several system values the sentinel is a valid reading:
- DEFROST, THO-R1, TD and THI-R2 use `0x00`, so DEFROST "Off" is never republished after a connect;
- OUTDOOR uses `0xff`, which is 40.25 °C.

This matters now: the group-root topics start empty, and a new publisher must fill all 11 in its first op-data cycle.

New `MHI_AC_Ctrl_Core::reset_system_values()`:
- The last-value fields of the 11 become `uint16_t`, and it sets them to `0xFFFF`. No one-byte reading equals that. COMP already is 16-bit.
- `reset_old_values()` calls it, so after a connect too, each of the 11 is published at its first reading.
- The per-unit values keep their current sentinels (out of scope).

## 5. Member records and liveness

### 5.1 Record
`<proto>;<role>;<term>;<uptime>;<period>;<outdoor_id>;<prefix>`

| field | meaning | valid |
|---|---|---|
| proto | group protocol major, this firmware: `1` | 1..255, digits only |
| role | `0` member, `1` publisher | `0` or `1` |
| term | publisher generation, see §6 | 32-bit unsigned, ≥ 1 when role is 1 |
| uptime | the unit's uptime counter in seconds | 32-bit unsigned |
| period | the unit's `TELEMETRY_PERIOD` | 1..86400 |
| outdoor_id | the unit's outdoor ID (explicit or derived) | 1..40 chars, no `; / + # " \`, space or control character |
| prefix | the unit's `MQTT_PREFIX` | 1..64 chars, ends in `/`, no `; + # " \`, space or control character |

- Decimal numbers have no sign and no leading `+`. The record is at most 140 bytes (the longest valid one is 139).
- The hostname comes from the topic level after `members/`: 1..32 characters without `/ + # ; " \`.
- Parsing:
  - A payload whose first field is a valid number other than `1` is a **foreign** record. Only that number is read.
  - An empty payload means the unit's record was deleted: the entry is removed from the table.
  - Anything else that does not fit the table above is ignored, with one Serial line.
- The unit publishes its own record:
  - at the end of the grace period (§6.1);
  - on every change of role or term;
  - every `TELEMETRY_PERIOD` after that. Uptime changes every time, so the payload always differs.

### 5.2 Peer table
- Up to 6 other units (`MHI_GROUP_MAX_PEERS`). A unit's own record is handled separately (§6.1).
- An entry holds:
  - the hostname and its kind: member (proto 1) or foreign;
  - the parsed fields;
  - `refreshed_ms`: when the record was first seen, or when any field last changed;
  - `connected`: unknown, up or down, with `down_since_ms`.
- A retained copy delivered again with the same content is not a refresh.
- When the table is full, a new unit replaces the entry that has been gone (§5.3) the longest. With none gone, the new unit is ignored with one Serial line.
- **The table is cleared at every MQTT connect.** The broker hands the retained records and `connected` topics over again within the grace period, and every staleness clock starts at that moment. So a unit that was offline for an hour does not find all its peers stale and claim the role.
- **Limit (Codex review, 19 Sep).** A dead unit whose retained `connected` still reads 1 (it died while the broker was down, so no will was sent) ages out only after 3 of its periods of continuous connection. A unit that reconnects more often than that keeps finding it fresh. If that ghost is the lowest hostname, or a publisher, it can hold off a claim for as long as the flapping lasts. PubSubClient does not pass the retain flag to the callback, so a retained copy cannot be told from a live publish. Persisting freshness across reconnects was considered and rejected: after an outage it would make one lost QoS-0 record publish enough to cause a false takeover. The documentation says so, and says how to remove a unit for good (§10).

### 5.3 Definitions
All times are `millis()` differences in unsigned arithmetic, so they survive the 49.7-day wrap.
- **fresh(p):** `now − refreshed_ms < 3 × period_p × 1000`, where period_p is the peer's own period (for a foreign record: this unit's period).
- **alive(p):**
  - member kind: `connected == up` and fresh(p);
  - foreign kind: fresh(p).
- **gone(p):** not fresh(p), or `connected == down` for at least 30 s. A publisher whose `connected` went to 0 ten seconds ago is neither alive nor gone.
- **leader:** among this unit and every alive peer of either kind, the one with the lowest hostname (strcmp). The leader's proto and outdoor ID are the group's.
- **state** (the `Group` number):
  - `3` when the group's proto is not 1;
  - otherwise `2` when the group's outdoor ID differs from this unit's;
  - otherwise the unit's role.
- **compatible(p):** member kind, and p's outdoor ID equals the group's.
- **incumbents:** every compatible peer with role 1 that is not gone, plus this unit when its role is 1 and its state is not 2 or 3.
- **candidates:** this unit when its state is 0 or 1, plus every compatible alive peer.
- **a beats b:** a's term is higher, or the terms are equal and a's hostname is lower.
- **max_term_seen:** the highest term in any proto-1 record seen since boot, the unit's own record and its own term included. It never decreases, not even across connects.

## 6. Election

### 6.1 Connect and grace
At every MQTT connect:
1. The unit subscribes to `<GROUP_ROOT>members/+` next to `MQTT_SET_PREFIX "#"`, and clears its peer table.
2. For 5 s (grace) it only collects. While a member-kind peer's record comes in, the unit subscribes (from `loop()`, never inside the MQTT callback) to that peer's `<prefix><TOPIC_CONNECTED>`. When a peer's prefix changes, it unsubscribes the old topic first.
3. The unit's own retained record (`members/<HOSTNAME>`) is read only during the grace period. When it is proto 1 and carries a higher term than the unit holds, the unit adopts its role and term. After a reboot this is what lets a publisher resume without a handover (a reboot starts at role 0, term 0).
4. During the grace period the unit publishes no record, no `Group`, no system value and no outdoor config. Its own status, per-unit values and unit discovery rows go out as today.
5. At the end of the grace period the unit first applies the rules in §6.2 once, which may demote it. Only then does it publish its record and its `Group`. If its role is 1 after that, it runs the publisher start (§6.3).

### 6.2 Rules, applied on every `loop()` pass after the grace period
1. If the role is 1 and the state is 2 or 3 → **demote**.
2. If the role is 1 and a peer among the incumbents beats this unit → **demote**.
3. If the role is 1 and a record arrives from a compatible peer claiming role 1 that this unit beats → re-send the outdoor configs 5 s later, once per such record. This covers a near-simultaneous claim: the loser may already have sent some of its configs before it saw the winner.
4. If the role is 0 and the state is 0:
   - With no incumbents, a settle clock starts.
   - When there have been no incumbents for 5 s and this unit has the lowest hostname among the candidates → **claim**: role 1, term = max_term_seen + 1.
   - As soon as an incumbent exists, the settle clock stops.
5. With the role unchanged, the record is re-sent every `TELEMETRY_PERIOD` (§5.1).

Nothing ever preempts a live incumbent: a unit claims only when there is none. On Lucas's units, a takeover after the publisher's `connected` goes to 0 happens after 30 s + 5 s settle.

### 6.3 Actions
- **Claim**:
  1. publish the record (role 1, new term) and `Group` `1`;
  2. call `reset_system_values()`, so each of the 11 values is written to the group root at its next reading (within one op-data cycle, 20 s);
  3. send the outdoor configs 30 s later, so the group root holds fresh retained values before HA subscribes to them.
- **Publisher start** at the end of a grace period (a reconnect or resume as publisher): steps 2 and 3 of a claim. The record and `Group` have just been sent. Configs that are byte-identical to the retained ones are ignored by HA.
- **Demote**:
  - role 0;
  - publish the record and `Group`;
  - stop writing system values at once;
  - cancel outdoor configs that are pending or partly sent.
  - A demoted or member unit never publishes on an outdoor config topic.
- **Group changes to 2 or 3** without a role change: publish `Group`.

### 6.4 The system-value gate
- `output_P()` routes the `opdata_*` statuses of the 11 to `<GROUP_OP_PREFIX><name>`, retained. It does so only while the unit is past its grace period, has role 1 and has state 1.
- Otherwise it drops them. They never go to the unit's own `MQTT_OP_PREFIX` any more.
- The classification is one `switch` over `ACStatus` in `src/support.cpp`, the only file that sees both the enum and the topics.

### 6.5 The three moments hass-config asked about (Lucas's configuration)
**A. First boot of Slaapkamer on this build, with Uitkijk still on `8a2c82d`.** There are no records on the broker yet.
- t 0:
  - Slaapkamer publishes `connected 1` and its status, per-unit values and 16 unit configs, then `Discovery ok`.
  - No system values are published anywhere. The old ones under `airco/slaapkamer/OpData/` stay retained until the cleanup.
- t 5 s: record `1;0;0;…;60;ac_outdoor;airco/slaapkamer/`, `Group 0`.
- t 10 s:
  - claim: record `1;1;1;…`, `Group 1`;
  - over the next ~20 s the 11 values appear at `airco/outdoor/OpData/`.
- t 40 s: the six outdoor configs arrive. The only changes in their payload are `~` and `avty_t` (§8.2), so HA updates them in place.
- Uitkijk keeps writing its system values to `airco/uitkijk/OpData/` until it is flashed.

**B. Takeover.** Slaapkamer publishes with term 1 and drops off the network.
- t ≈ 22 s: the broker notices (keepalive 15 s × 1.5) and publishes Slaapkamer's will, `airco/slaapkamer/connected 0`. HA shows the outdoor entities unavailable.
- t ≈ 52 s: Slaapkamer is gone.
- t ≈ 57 s:
  - Uitkijk claims: record `1;1;2;…;airco/uitkijk/`, `airco/uitkijk/Group 1`;
  - its values reach `airco/outdoor/OpData/` within ~20 s.
- t ≈ 87 s: the six configs with `avty_t airco/uitkijk/connected` and `via_device airco-uitkijk`.

**C. The ex-publisher returns.** Slaapkamer holds term 1, Uitkijk term 2.
- Slaapkamer connects: `connected 1`, its status and its 16 unit configs.
- During the grace period it reads its own record, role 1 term 1 (after a reboot it adopts it; without a reboot it already holds it), and Uitkijk's, role 1 term 2, with Uitkijk connected.
- At the end of the grace period Uitkijk beats it:
  - record `1;0;1;…`, `Group 0`;
  - no system value and no outdoor config, at any point.

**Quick reconnect of the publisher (`set/reset`).**
- Uitkijk sees `connected 0`, then `1` about 10 s later. That is under 30 s, so there is no takeover.
- Slaapkamer resumes term 1 from its own record: record, `Group 1`, values, and 30 s later configs identical to the retained ones.

## 7. Code layout

- **`lib/mhi_pure/mhi_group.{h,cpp}`** (new, host-tested):
  - record format and parse, hostname check;
  - the default outdoor ID (reuses `mhi_discovery_slug`);
  - the peer table and the definitions of §5.3;
  - the rules of §6.1-6.3 as `mhi_group_connect(now)`, `mhi_group_on_record(host, payload, now)`, `mhi_group_on_connected(host, up, now)` and `mhi_group_tick(now)`.
  - The tick returns an action set: publish record, publish Group, publisher start, demote, send outdoor configs now, and which peer topics to subscribe or unsubscribe.
  - No Arduino and no clock of its own. Every entry point takes `now`.
- **`src/group.{h,cpp}`** (new, Arduino glue):
  - owns the `MhiGroup` state;
  - `group_setup()` at boot, `group_connected()` after every connect, `group_loop()` on every pass while connected;
  - `group_handle_message(topic, payload, len)` returns true when the topic was a group topic. It uses its own 141-byte copy, because the command path copies only 32 bytes.
  - It executes the actions: publishes, core reset, discovery start/cancel, subscriptions.
- **`src/main.cpp`:**
  - `MQTT_subscribe_callback` offers each message to `group_handle_message` first. A group message never produces `cmd_received`.
  - `loop()` calls `group_connected()` next to `discovery_restart()`, and `group_loop()` next to `discovery_loop()`.
- **`src/support.cpp`:**
  - the system-value gate (§6.4);
  - the subscribe to `members/+` at connect;
  - a getter for the uptime counter the record carries.
- **`src/MHI-AC-Ctrl-core.{h,cpp}`:** `reset_system_values()` and the widened fields (§4.3).
- **`src/discovery.cpp`:** two cursors, below.

## 8. Home Assistant discovery

### 8.1 Rows
- **New row** `MHI_DISCOVERY_GROUP_ROLE`, appended after the outdoor block:
  - sensor, uniq_id `<HA_ID_PREFIX>_group_role`, name `HA_NAME_GROUP_ROLE`;
  - `stat_t ~/Group`, `ent_cat diagnostic`;
  - no device class, unit or state class;
  - `default_entity_id sensor.<entity_prefix>_group_role` when `HA_ENTITY_PREFIX` is set.
  - `is_outdoor_row()` becomes the range `OU_OUTDOOR..OU_PROTECTION`, and the `static_assert` that guards it changes with it.
- **`MHI_DISCOVERY_OU_KWH` is retired:**
  - `mhi_discovery_row_enabled()` returns false for it in every build;
  - the enum value stays, because the table is append-only;
  - the firmware never publishes on its topic, and never an empty payload.
- **Per unit:** 16 entities with the 33-byte frame. **Outdoor device:** 6 (Temperature, Current, Compressor frequency, Defrost, Compressor run time, Protection state).

### 8.2 Outdoor payloads
New context fields: `group_base` (`GROUP_ROOT` without its trailing slash), `avty_topic` (`MQTT_PREFIX TOPIC_CONNECTED`, written out in full) and `t_group`. `op_prefix` becomes what `GROUP_OP_PREFIX` adds to `GROUP_ROOT` (`OpData/`).

For the six outdoor rows:
- `"~":"<group_base>"`, `stat_t "~/<op_prefix><topic>"`;
- `"avty_t":"<avty_topic>"`, absolute;
- `via_device` = this unit's hostname.

Topic, uniq_id, names, `default_entity_id`, device ids, name, `mf` and `mdl` are unchanged. `~` never ends in `/`, so no expanded topic contains `//` (host test).

On Lucas's units each of the six changes, compared with batch C, only in:
- `"~":"airco/slaapkamer"` → `"~":"airco/outdoor"`;
- `"avty_t":"~/connected"` → `"avty_t":"airco/slaapkamer/connected"`.

After a takeover `avty_t` and `via_device` name the new publisher. Everything else stays byte-identical.

### 8.3 When configs are sent (`src/discovery.cpp`)
- **Unit rows** (every enabled row that is not an outdoor row) go out after every connect, one per `loop()` pass, as today, and then the `Discovery` topic.
- **Outdoor rows** have their own cursor. `discovery_start_outdoor()` sets it, `discovery_cancel_outdoor()` clears it, and only the group calls either. It also advances one row per pass, after the unit rows.
- A row that does not fit is skipped as today. Nothing ever publishes an empty payload on a discovery topic.

### 8.4 Rules from hass-config (HA 2026.9 source and sandbox, 18 Sep)
- A changed payload on the same discovery topic updates the entity in place: same registry entry, entity_id and statistics. An unchanged payload is ignored.
- The discovery topic is byte-identical for every publisher. The same uniq_id on another topic is rejected.
- An empty retained payload on those topics deletes the registry entry, and area and name overrides are lost.
- Only the current publisher sends the outdoor configs. A returning unit never sends them at connect.
- The takeover gap reads unavailable, and statistics skip it.
- The values must be on the group root, retained, before the configs (§6.3).

### 8.5 The left-over Energy config
The retained `homeassistant/sensor/ac_outdoor_energy/config` from batch C stays on the broker: the firmware no longer writes it. It keeps reading `airco/slaapkamer/OpData/KWH`, which Slaapkamer still publishes as its own counter, and it keeps `via_device airco-slaapkamer`. That could flip the outdoor device's `via_device` whenever HA replays the retained configs, for example after a restart, once Uitkijk publishes. hass-config clears it with one empty retained payload (hass-config#315), under two conditions: its power integral is live and the Energie dashboard and the Systeemdetails row read it, and Slaapkamer runs this build (batch C republishes that config at every connect). The timing is right after the Slaapkamer flash and before the takeover test (§11). The entity's long-term statistics stay as an orphan; deleting them is Lucas's call.

## 9. Host tests

- **`test_mhi_group`:**
  - record round trip; the longest valid record is 139 bytes;
  - every invalid form rejected: field count, sign, leading `+`, empty field, out-of-range number, role 1 with term 0, a forbidden character in ID or prefix, a prefix without `/`, too long, a non-digit proto;
  - foreign record detected from its first field;
  - empty payload removes the entry;
  - hostname check;
  - default outdoor ID for `airco/outdoor/`, `MHI-AC-Ctrl/` and a root that slugs to nothing.
- **Election scenarios with a simulated clock:**
  1. lone unit, cold start: grace, record role 0, claim term 1 at 10 s, configs at 40 s;
  2. a rebooted publisher resumes from its own record without a new term;
  3. a member waits 30 s after `connected 0`, then settles 5 s and claims max+1; no claim at 34.9 s;
  4. `connected` 0 then 1 within 30 s: no takeover;
  5. a stale record (`connected` stays 1, no refresh for 3 periods) counts as gone;
  6. the peer's own period, not this unit's, decides staleness;
  7. simultaneous claims with equal terms: the higher hostname demotes and cancels its configs; the lower re-sends 5 s after the loser's record;
  8. the returning ex-publisher becomes a member and never gets a start or config action;
  9. three units: only the lowest alive candidate claims;
  10. an outdoor ID mismatch gives `Group 2`, never a claim, and demotes a publisher;
  11. a foreign record from a lower hostname gives `Group 3`; from a higher hostname it has no effect;
  12. a full table replaces the longest-gone entry, otherwise ignores the new unit;
  13. a retained record delivered again is not a refresh;
  14. the table is cleared at connect, so a long own outage does not make peers stale;
  15. times around the `millis()` wrap;
  16. nothing is published during the grace period;
  17. max_term_seen never goes down.
- **`test_mhi_discovery`:**
  - the Group role row is a unit row: `<id_prefix>_group_role`, the unit's device block, `~/connected` availability (the classification of `is_outdoor_row()` is a closed range, Codex review);
  - the outdoor rows with group base and absolute availability;
  - the retired KWH row disabled;
  - no `//` after expanding `~` in any row;
  - no row builds an empty payload;
  - fixtures regenerated (`test/fixtures/discovery*`).
- **CI:**
  - `ci-ha-discovery-outdoor` and `ci-all-options` drop `HA_OUTDOOR_DEVICE` and set `GROUP_ROOT` to a shared root;
  - the flash-size check still passes.

What the host cannot test is verified on the units (§11): the core's sentinels, the subscriptions, the gate and the real broker's will.

## 10. Toolkit, docs, hass-config

- **Toolkit** (`~/.config/hass/tools/mhi/`):
  - `airco-config.py`: drop `HA_OUTDOOR_DEVICE`; on both units set `GROUP_ROOT "airco/outdoor/"`, `HA_OUTDOOR_ID "ac_outdoor"` and `HA_OUTDOOR_ENTITY_PREFIX "ac_outdoor"`.
  - `discovery-payloads.sh <unit> <version> [--outdoor]`: that unit's 16 rows; with `--outdoor`, also the 6 outdoor rows as that unit publishes them. The renderer gets `--group-base`, and derives `avty_topic` from `--base`.
  - `post-flash-check.sh`:
    - 16 entities per unit and 6 through the outdoor device;
    - the retained outdoor configs compared with a render for the unit whose `Group` is 1;
    - both records present.
  - `remote-test.sh` reads OU-FANSPEED from `airco/outdoor/OpData/`.
  - `health-check.sh` prints `Group` and the records.
- **Docs:**
  - README: feature line.
  - `SW-Configuration.md`:
    - `GROUP_ROOT` and `GROUP_OP_PREFIX`;
    - the record, the `Group` numbers and the election in short;
    - the system and per-unit lists with the 17 Sep measurement, and KWH's meaning;
    - the removed flag, and `TELEMETRY_PERIOD` 1..86400;
    - the same `TOPIC_CONNECTED`/`PAYLOAD_CONNECTED_*` on all units of a group;
    - how to remove a unit for good (publish an empty retained payload on its `members/<hostname>`);
    - the limit that a publisher which stays connected but cannot read its AC keeps the role;
    - the ghost limit of §5.2;
    - the 64-character bound on `GROUP_ROOT` and why it exists.
- **hass-config (#315):**
  - fixtures per unit, 16 rows, plus 6 outdoor rows per possible publisher;
  - the assertions in `tests/test_airco_topics_302.py`;
  - Dutch text for the Group numbers, as a device-less `sensor.ac_<unit>_groepsrol`;
  - the power integral and the Energie dashboard;
  - the sandbox replay of §6.5 A-C.

## 11. Rollout and rollback

Lucas authorized Claude on 19 Sep to flash this build. hass-config acks each sync point, and without an ack Claude waits.
1. Branch reviewed, CI green. Hash, renderer output and the §6.5 payloads go to hass-config. It re-renders fixtures, runs the sandbox replay and acks.
2. Final batch C soak check (`health-check.sh 1`), then close #19, #20 and #21.
3. Flash **Slaapkamer**. Expect moment A. `post-flash-check.sh` must pass.
4. hass-config clears the stale Energy config (§8.5) and acks.
5. hass-config acks. Then flash **Uitkijk** and expect a member: `Group 0`, its record, no new writes to `airco/uitkijk/OpData/<system>`.
6. **Quick-reconnect test:** `set/reset` on Slaapkamer. No handover; Uitkijk stays `Group 0`.
7. **Takeover test:** Slaapkamer off the network for at least 3 min (Lucas: block it in the Deco app, or switch its AC off at the breaker). Expect moment B, then moment C when it returns: Uitkijk keeps term 2.
8. Day-after `health-check.sh 1`.
9. **Cleanup**, after hass-config's ack: empty retained payloads on `airco/slaapkamer/OpData/<11>` and `airco/uitkijk/OpData/<11>`.

**Rollback:** in reverse order, Uitkijk first, then Slaapkamer, to the kept `8a2c82d` images. Slaapkamer's `8a2c82d` sends the seven batch C configs again: the six are an in-place update back, and Energy comes back as a new entity if hass-config had already cleared it. Afterwards, empty retained payloads on the records and `Group` topics.

## 12. Refinements beyond the approved design (for Lucas's review)

1. **KWH per unit** (§4.2, Lucas 19 Sep): 11 system values, 6 outdoor entities.
2. The record gets a **period** field. With only its own period, a unit would find a peer with a longer period stale.
3. A **5 s settle** before a claim. It keeps two units that boot together from both claiming. Takeover = 30 s + 5 s.
4. **Outdoor configs 30 s after a claim**, so the values are on the group root first (hass-config's retain trap). On Lucas's units the gap in HA grows to about 90 s from the drop to available.
5. **Conflict re-send:** the winner of a simultaneous claim re-sends its configs 5 s after the loser's record.
6. **Sentinels** of the 11 system values (§4.3). Side effect: DEFROST "Off" and a reading of 0 are published after every connect.
7. **`TELEMETRY_PERIOD` 1..86400.**
8. **Default outdoor ID** = slug of `GROUP_ROOT` + `_outdoor`. The default `GROUP_OP_PREFIX` keeps a custom `MQTT_OP_PREFIX` when no `GROUP_ROOT` is set.
9. **Table cleared at every connect** (§5.2). Codex disagreed unless the ghost case is mitigated. Its first mitigation, documenting the limit and requiring the removal of dead units' records, is what §5.2 and §10 do.
10. **Foreign records:** only the version is read. The lowest alive hostname's version wins, so during a mixed-version upgrade only one side of the group can publish.
11. **Full table:** a gone entry makes room.
12. **`HA_OUTDOOR_DEVICE` → `#error`** with a pointer to `GROUP_ROOT`.
13. **Receive-buffer bound** (Codex review, critical): record ≤ 140 bytes, `GROUP_ROOT` ≤ 64 characters, and a compile-time proof that the worst record packet fits PubSubClient3's 256-byte receive buffer.
