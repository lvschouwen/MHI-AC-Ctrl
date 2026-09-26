# Design notes

Rules and invariants that are not obvious from reading the code, kept here so a
change does not silently break them. The full design history (specs, plans,
measurement sessions) that led to these lived in `docs/superpowers/` and is in
git history; this file keeps only what still constrains the code.

## Outdoor election (fork #22, amended fork #29)

The indoor units of one shared outdoor unit elect one publisher for the
outdoor values and Home Assistant configs. Implementation:
`lib/mhi_pure/mhi_group.{h,cpp}` (pure rules) + `src/group.{h,cpp}` (glue).
User-facing behaviour is in `SW-Configuration.md` under "Several indoor units
on one outdoor unit"; this section is the internal rules that back it.

- **Member record** (`<proto>;<role>;<term>;<uptime>;<period>;<outdoor_id>;<prefix>`,
  ≤140 bytes) is capped so the worst-case retained packet (header + topic +
  record) fits PubSubClient3's 256-byte *receive* buffer — see "MQTT buffer
  sizing" below. This is why `GROUP_ROOT` is capped at 64 characters and
  `HOSTNAME` at 32: a longer root would make units silently drop each other's
  records (the client discards an oversized packet whole), and two publishers
  could exist without ever knowing about each other.
- **Foreign records** (a different protocol major) are read for their version
  number only. During a mixed-version upgrade, the lowest alive hostname's
  version wins the group, so only one side of a split upgrade can publish at a
  time — this is intentional, not a bug to fix.
- **Staleness ("gone")**: a peer counts as gone when its record has not
  refreshed for 3× its own advertised period, or its `connected` has read 0
  for 30 s. Using the peer's own period (not this unit's) avoids flagging a
  slower-polling peer as dead. `TELEMETRY_PERIOD` must be 1..86400 because the
  liveness window depends on it.
- **The peer table is cleared at every MQTT connect.** The broker replays
  retained records and `connected` topics within the 5 s grace window right
  after connect, and staleness clocks restart from there — otherwise a unit
  that was itself offline for a while would see all its peers as stale and
  wrongly claim the role.
- **Ghost record limit**: a peer that died while the broker itself was down
  never got its will (`connected` stays retained at 1). Such a ghost only
  ages out after 3 of its own periods of *continuous* connection — a unit
  that reconnects more often than that keeps seeing it as fresh. Persisting
  liveness across reconnects was considered and rejected: it would let a
  single lost QoS-0 publish after an outage cause a false takeover. If a dead
  unit is the lowest hostname or a publisher, removing its retained
  `members/<hostname>` record (empty retained payload) is the only way to
  force a takeover.
- **Settle time**: a unit only claims the publisher role after there have
  been no incumbents for 5 s (avoids two units that boot together both
  claiming). A takeover after a publisher's `connected` drops therefore takes
  30 s (gone threshold) + 5 s (settle).
- **Nothing ever preempts a live incumbent** — a unit claims only when there
  is none. A live publisher that returns after being beaten stays a member
  (no hand-back).
- **Outdoor discovery availability** (fork #29): rather than tie the outdoor
  device's availability to the current publisher's `connected` (which made it
  flicker unavailable for ~20 s on every publisher reboot), the six outdoor
  configs list up to `MHI_GROUP_AVTY_MAX` (3) units' `connected` topics with
  `avty_mode: any`, and use the lowest hostname as `via_device`. Every
  publisher computes the same list, so a takeover changes nothing in the
  payload HA sees, and reconfigs are only re-sent when the list itself
  changes (after a `MHI_GROUP_AVTY_SETTLE_MS` (10 s) debounce).
- **KWH stays per unit, not a system value**: it is the outdoor unit's energy
  counted only while *that* indoor unit is on, reset when that unit powers on
  — it is not comparable across units, so it is never written to the group
  root and the outdoor device has no energy entity.

## MQTT buffer sizing

PubSubClient3 **streams** an outgoing publish (`beginPublishImpl()` only needs
the fixed header + topic to fit the client buffer; the payload is written
through it in chunks and flushed as it fills). This is why the 1024-byte
static discovery payload builder (`MHI_DISCOVERY_BUF`,
`lib/mhi_pure/mhi_discovery.h`) needs no matching `setBufferSize()` call on
the client — an earlier draft of that call was dropped once this was found,
saving ~768 B of heap.

The client buffer **does** bound *incoming* packets: `readPacket()` drops
anything larger than the configured size (default `MQTT_MAX_PACKET_SIZE`,
256 bytes) whole, with no partial delivery and no error to the callback. The
firmware relies on this default rather than enlarging it, which is why:

- the outdoor election's member records and `GROUP_ROOT` length are bounded
  to fit inside it (see above);
- `set/` commands are assumed short (a few bytes) and never split across
  reads.

If a future change needs a larger incoming payload, it must either shrink
something else that shares the 256-byte budget or call `setBufferSize()`
deliberately (which costs heap for every connection) — do not assume a bigger
buffer is already there because outgoing discovery payloads are big.
