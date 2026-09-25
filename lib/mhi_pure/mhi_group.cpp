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
  // The loser's next changed record (its uptime moves) does not push the
  // re-send back (fork #25): it stays 35 s after the first.
  if (changed && !g->in_grace && g->role == 1 && kind == MHI_GROUP_KIND_MEMBER && rec.role == 1 &&
      compatible(g, p, now) && beats(g->term, g->host, rec.term, p->host) && !g->resend_pending) {
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
