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
