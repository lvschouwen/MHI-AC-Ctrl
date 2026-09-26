// The outdoor election (fork #22; rules and invariants in
// docs/design-notes.md). The indoor units that share one outdoor unit keep a
// retained record each under <GROUP_ROOT>members/<HOSTNAME> and elect, from
// those records and their connected topics, the one unit that publishes the
// outdoor unit's values and its Home Assistant device.
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

// GROUP_ROOT (spec §2): 1..64 characters, ends in /, no + # ; " \, space or
// control character. It goes unescaped into the outdoor configs' "~" (fork #25).
constexpr bool mhi_group_root_valid(const char* s) {
  return s != nullptr && mhi_group_text_ok(s, 1, MHI_GROUP_ROOT_MAX, "+#;\"\\", true) && s[mhi_group_len(s) - 1] == '/';
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

// --- the election (spec §5.2-§6.3) ---------------------------------------------

#define MHI_GROUP_MAX_PEERS 6
#define MHI_GROUP_GRACE_MS 5000        // after a connect: only collect (§6.1)
#define MHI_GROUP_SETTLE_MS 5000       // no incumbent for this long before a claim (§6.2 rule 4)
#define MHI_GROUP_DOWN_GONE_MS 30000   // connected 0 for this long: gone (§5.3)
#define MHI_GROUP_AVTY_MAX 3           // units in the outdoor configs' availability list (fork #29); = MHI_DISCOVERY_AVTY_MAX
#define MHI_GROUP_AVTY_SETTLE_MS 10000 // the list unchanged this long -> the publisher re-sends the configs (fork #29)

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
  bool configs_sent;        // the outdoor configs went out in this publisher period (fork #29)
  uint32_t configs_hash;    // of the availability list they carried
  uint32_t avty_hash;       // of the availability list as last seen
  uint32_t avty_ms;         // when that list last changed
  MhiGroupPeer peers[MHI_GROUP_MAX_PEERS];
};

enum : uint8_t {
  MHI_GROUP_ACT_RECORD = 0x01,   // publish this unit's record, retained, on <GROUP_ROOT>members/<HOSTNAME>
  MHI_GROUP_ACT_STATE = 0x02,    // publish MhiGroupActions.state, retained, on <MQTT_PREFIX><TOPIC_GROUP>
  MHI_GROUP_ACT_START = 0x04,    // call reset_system_values(): a claim or a publisher start
  MHI_GROUP_ACT_DEMOTE = 0x08,   // call discovery_cancel_outdoor()
  MHI_GROUP_ACT_CONFIGS = 0x10,  // call discovery_start_outdoor() with mhi_group_availability()
};

// What one tick asks the glue to do. The glue (group.cpp's group_loop()) runs
// them in this fixed order, not the bit order above: subscribe, then DEMOTE,
// RECORD, STATE, START, CONFIGS. There is no unsubscribe: an old peer topic
// goes at the next connect (clean session), and a message on it matches no
// peer (spec §6.1 step 2).
//
// Contract: `now` must never go backwards between calls into mhi_group. Every
// elapsed-time check here is an unsigned subtraction from a stored uint32_t
// timestamp, so a `now` that regresses (not just the millis() wrap, which
// wraps forward) can make an elapsed time appear huge and fire early.
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

// Every loop() pass while connected: the rules of §6.1-§6.3. From the
// connect on it publishes Group 0 until the grace period has run (fork #29).
// A publisher asks for the outdoor configs at once after a claim or a
// publisher start, and again when its availability list has changed and then
// stayed the same for MHI_GROUP_AVTY_SETTLE_MS (fork #29), which also lets a
// rival's configs from a simultaneous claim go out first.
void mhi_group_tick(MhiGroup* g, uint32_t now, MhiGroupActions* act);

// The outdoor configs' availability list (fork #29): this unit and every peer
// with a valid record of this protocol and this unit's outdoor ID, whether it
// is alive or not (only an empty record removes one), at most
// MHI_GROUP_AVTY_MAX: this unit and the lowest other hostnames, sorted by
// hostname. Copies, so the glue may keep them across ticks.
struct MhiGroupAvty {
  uint8_t count;  // 1..MHI_GROUP_AVTY_MAX; [0] is the lowest hostname, the outdoor device's via_device
  char host[MHI_GROUP_AVTY_MAX][MHI_GROUP_HOST_MAX + 1];
  char prefix[MHI_GROUP_AVTY_MAX][MHI_GROUP_ROOT_MAX + 1];
};
void mhi_group_availability(const MhiGroup* g, MhiGroupAvty* out);

// The system-value gate (§6.4): past the grace period, role 1 and state 1.
bool mhi_group_may_publish_system(const MhiGroup* g);

// This unit's record with the given uptime. Returns the length, 0 when it does not fit.
size_t mhi_group_own_record(const MhiGroup* g, uint32_t uptime_s, char* out, size_t out_len);
