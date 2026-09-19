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
