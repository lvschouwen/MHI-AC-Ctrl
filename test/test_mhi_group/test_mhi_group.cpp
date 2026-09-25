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
  // Unescaped in the outdoor configs' "~" (fork #25): no quote, backslash,
  // space or control character.
  TEST_ASSERT_FALSE(mhi_group_root_valid("air\"co/"));
  TEST_ASSERT_FALSE(mhi_group_root_valid("air\\co/"));
  TEST_ASSERT_FALSE(mhi_group_root_valid("air co/"));
  TEST_ASSERT_FALSE(mhi_group_root_valid("air\tco/"));
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

// 1. A lone unit, cold start: Group 0 at once, grace, record role 0, claim
// term 1 at 10 s with the configs in the same tick (fork #29).
static void test_scenario_1_lone_unit_cold_start(void) {
  boot("airco-slaapkamer", 0);
  tick(100);
  TEST_ASSERT_EQUAL_UINT8(MHI_GROUP_ACT_STATE, act.flags);  // Group 0 at once (fork #29)
  TEST_ASSERT_EQUAL_UINT8(0, act.state);
  reset_seen();
  run(200, 4800);
  TEST_ASSERT_EQUAL_UINT8(0, seen);  // 16. nothing else in the grace period
  tick(5000);
  TEST_ASSERT_EQUAL_UINT8(MHI_GROUP_ACT_RECORD, act.flags);  // Group 0 is already out
  char rec[MHI_GROUP_RECORD_MAX + 1];
  own_record(rec);
  TEST_ASSERT_EQUAL_STRING("1;0;0;7;60;ac_outdoor;airco/me/", rec);
  reset_seen();
  run(5100, 4900);
  TEST_ASSERT_EQUAL_UINT8(0, seen);
  tick(10000);  // the settle clock started at 5 s
  TEST_ASSERT_EQUAL_UINT8(MHI_GROUP_ACT_RECORD | MHI_GROUP_ACT_STATE | MHI_GROUP_ACT_START | MHI_GROUP_ACT_CONFIGS,
                          act.flags);
  TEST_ASSERT_EQUAL_UINT8(1, act.state);
  own_record(rec);
  TEST_ASSERT_EQUAL_STRING("1;1;1;7;60;ac_outdoor;airco/me/", rec);
  TEST_ASSERT_TRUE(mhi_group_may_publish_system(&g));
  reset_seen();
  run(10100, 120000);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_CONFIGS));  // once: the list did not change
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_START));
}

// 2. A rebooted publisher resumes from its own record without a new term.
static void test_scenario_2_rebooted_publisher_resumes(void) {
  boot("airco-slaapkamer", 0);  // a reboot: role 0, term 0
  record("airco-slaapkamer", "1;1;3;900;60;ac_outdoor;airco/me/", 0);
  TEST_ASSERT_FALSE(mhi_group_may_publish_system(&g));  // still in the grace period
  run(100, 4900);
  TEST_ASSERT_EQUAL_UINT8(MHI_GROUP_ACT_STATE, seen);  // only Group 0, at once (fork #29)
  TEST_ASSERT_EQUAL_UINT32(100, when(MHI_GROUP_ACT_STATE));
  tick(5000);
  // The publisher start: the configs at once, not 30 s later (fork #29).
  TEST_ASSERT_EQUAL_UINT8(MHI_GROUP_ACT_RECORD | MHI_GROUP_ACT_STATE | MHI_GROUP_ACT_START | MHI_GROUP_ACT_CONFIGS,
                          act.flags);
  TEST_ASSERT_EQUAL_UINT8(1, act.state);
  char rec[MHI_GROUP_RECORD_MAX + 1];
  own_record(rec);
  TEST_ASSERT_EQUAL_STRING("1;1;3;7;60;ac_outdoor;airco/me/", rec);
  reset_seen();
  run(5100, 30000);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_CONFIGS));
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
  TEST_ASSERT_EQUAL_UINT8(MHI_GROUP_ACT_RECORD | MHI_GROUP_ACT_STATE, seen);  // Group 0 at once, the record at 5 s, no claim
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
  TEST_ASSERT_EQUAL_UINT8(MHI_GROUP_ACT_RECORD | MHI_GROUP_ACT_STATE | MHI_GROUP_ACT_START | MHI_GROUP_ACT_CONFIGS,
                          act.flags);
  char rec[MHI_GROUP_RECORD_MAX + 1];
  own_record(rec);
  TEST_ASSERT_EQUAL_STRING("1;1;2;7;60;ac_outdoor;airco/me/", rec);
  // The ex-publisher is down but stays in the list, so the configs are the
  // ones it sent itself (fork #29).
  MhiGroupAvty list;
  mhi_group_availability(&g, &list);
  TEST_ASSERT_EQUAL_UINT8(2, list.count);
  TEST_ASSERT_EQUAL_STRING("airco-slaapkamer", list.host[0]);
  TEST_ASSERT_EQUAL_STRING("airco/slaapkamer/", list.prefix[0]);
  TEST_ASSERT_EQUAL_STRING("airco-uitkijk", list.host[1]);
  TEST_ASSERT_EQUAL_STRING("airco/me/", list.prefix[1]);
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

// 7a. Simultaneous claims, equal terms: the higher hostname demotes and sends no more configs.
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
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_CONFIGS));  // a member never sends them
}

// 7b. ... and the lower hostname sends its configs again once the list that
// now holds the loser has settled for 10 s: after the loser's own configs from
// its claim, which listed only itself (fork #29; rule 3 is gone).
static void test_scenario_7_the_lower_hostname_resends(void) {
  boot("airco-slaapkamer", 0);
  run(100, 10000);
  TEST_ASSERT_EQUAL_UINT32(10000, when(MHI_GROUP_ACT_START));
  TEST_ASSERT_EQUAL_UINT32(10000, when(MHI_GROUP_ACT_CONFIGS));  // the claim's own configs: itself alone
  record("airco-uitkijk", "1;1;1;500;60;ac_outdoor;airco/uitkijk/", 10050);
  reset_seen();
  run(10100, 9900);  // up to 19.9 s
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_CONFIGS));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_DEMOTE));
  run(20000, 1000);  // the list changed at the 10.1 s tick: due at 20.1 s
  TEST_ASSERT_EQUAL_UINT32(20100, when(MHI_GROUP_ACT_CONFIGS));
  // The loser's record delivered again, or with a new uptime and its demote:
  // the list is the same, so no further configs.
  record("airco-uitkijk", "1;0;1;530;60;ac_outdoor;airco/uitkijk/", 21000);
  reset_seen();
  run(21000, 60000);
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

// 16. Nothing but Group 0 is published during the grace period, whatever arrives.
static void test_scenario_16_nothing_in_the_grace_period(void) {
  boot("airco-a", 0);
  record("airco-a", "1;1;4;900;60;ac_outdoor;airco/me/", 0);
  record("airco-b", "1;1;9;900;60;ac_outdoor;airco/b/", 0);
  mhi_group_on_connected(&g, "airco-b", true, 0);
  run(100, 4900);
  TEST_ASSERT_EQUAL_UINT8(MHI_GROUP_ACT_STATE, seen);
  TEST_ASSERT_EQUAL_UINT32(100, when(MHI_GROUP_ACT_STATE));
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

// 22. A third unit with a higher term arrives while a list change settles:
// the demote drops it, so no configs.
static void test_scenario_22_a_demote_cancels_the_resend(void) {
  boot("airco-slaapkamer", 0);
  run(100, 10000);                                                           // claims term 1 at 10 s, configs at once
  record("airco-uitkijk", "1;1;1;500;60;ac_outdoor;airco/uitkijk/", 10050);  // loses: the list settles at 20.1 s
  run(10100, 5000);
  record("airco-zolder", "1;1;2;900;60;ac_outdoor;airco/zolder/", 15100);    // term 2 beats this unit
  reset_seen();
  tick(15100);
  TEST_ASSERT_TRUE(act.flags & MHI_GROUP_ACT_DEMOTE);
  run(15200, 60000);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_CONFIGS));
}

// 23. A reconnect while a list change settles: nothing pending from before
// the connect goes out; the publisher start after the grace period sends the
// configs at once, with the list as the broker delivered it again.
static void test_scenario_23_a_reconnect_drops_what_was_pending(void) {
  boot("airco-slaapkamer", 0);
  run(100, 10000);                                                           // claims at 10 s
  record("airco-uitkijk", "1;1;1;500;60;ac_outdoor;airco/uitkijk/", 10050);  // the list settles at 20.1 s
  run(10100, 4900);
  mhi_group_connect(&g, 15000);  // the broker connection dropped and came back
  reset_seen();
  record("airco-uitkijk", "1;1;1;500;60;ac_outdoor;airco/uitkijk/", 15000);  // retained, delivered again
  run(15100, 35000);
  TEST_ASSERT_EQUAL_UINT32(20000, when(MHI_GROUP_ACT_START));    // the publisher start after the grace period
  TEST_ASSERT_EQUAL_UINT32(20000, when(MHI_GROUP_ACT_CONFIGS));  // with it, both units listed; nothing at 20.1 s
  MhiGroupAvty list;
  mhi_group_availability(&g, &list);
  TEST_ASSERT_EQUAL_UINT8(2, list.count);
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

// 30. A losing claim seen during the grace period: the publisher start after
// it sends the configs once, with that unit already in the list.
static void test_scenario_30_no_resend_armed_in_the_grace_period(void) {
  mhi_group_init(&g, "airco-slaapkamer", "ac_outdoor", "airco/me/", 60);
  g.role = 1;  // a publisher that reconnects without a reboot
  g.term = 1;
  g.max_term_seen = 1;
  mhi_group_connect(&g, 0);
  reset_seen();
  run(100, 1900);
  record("airco-uitkijk", "1;1;1;500;60;ac_outdoor;airco/uitkijk/", 2000);  // loses to this unit
  run(2000, 3100);  // up to 5.0 s
  TEST_ASSERT_EQUAL_UINT32(5000, when(MHI_GROUP_ACT_START));
  TEST_ASSERT_EQUAL_UINT32(5000, when(MHI_GROUP_ACT_CONFIGS));
  reset_seen();
  run(5100, 60000);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_CONFIGS));
}

// --- the plumbing the glue relies on ------------------------------------------

// 31. A claim when a peer's term is already UINT32_MAX saturates rather than
// wrapping to the invalid term 0 (fork #25).
static void test_scenario_31_a_claim_at_the_highest_term_saturates(void) {
  boot("airco-a", 0);
  record("airco-b", "1;0;4294967295;500;60;ac_outdoor;airco/b/", 0);
  run(100, 10000);
  TEST_ASSERT_EQUAL_UINT32(10000, when(MHI_GROUP_ACT_START));
  char rec[MHI_GROUP_RECORD_MAX + 1];
  own_record(rec);
  TEST_ASSERT_EQUAL_STRING("1;1;4294967295;7;60;ac_outdoor;airco/me/", rec);
}

// 32. A mismatch that is fixed publishes the Group state again (fork #25).
static void test_scenario_32_the_group_state_leaves_a_mismatch(void) {
  boot("airco-b", 0);
  record("airco-a", "1;0;0;500;60;other_outdoor;airco/a/", 0);
  mhi_group_on_connected(&g, "airco-a", true, 0);
  run(100, 10000);
  TEST_ASSERT_EQUAL_UINT8(2, g.state);
  record("airco-a", "1;0;0;510;60;ac_outdoor;airco/a/", 10100);  // the leader now shares our ID
  reset_seen();
  tick(10200);
  TEST_ASSERT_EQUAL_UINT32(10200, when(MHI_GROUP_ACT_STATE));
  TEST_ASSERT_EQUAL_UINT8(0, act.state);
}

// 33. A record that changes only its role, term or uptime leaves the list as
// it was: no configs (fork #29).
static void test_scenario_33_the_resend_is_not_restarted(void) {
  boot("airco-slaapkamer", 0);
  record("airco-uitkijk", "1;0;0;500;60;ac_outdoor;airco/uitkijk/", 0);
  run(100, 10000);
  TEST_ASSERT_EQUAL_UINT32(10000, when(MHI_GROUP_ACT_CONFIGS));
  reset_seen();
  record("airco-uitkijk", "1;0;0;560;60;ac_outdoor;airco/uitkijk/", 60000);
  record("airco-uitkijk", "1;0;3;620;60;ac_outdoor;airco/uitkijk/", 120000);
  run(10100, 180000);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_CONFIGS));
}

// --- fork #29: the availability list -------------------------------------------

// 34. Who is listed: this unit and every member with its outdoor ID, alive or
// not; never a foreign record or another outdoor ID; sorted by hostname.
static void test_scenario_34_the_list_holds_the_compatible_members(void) {
  boot("airco-m", 0);
  record("airco-z", "1;0;0;500;60;ac_outdoor;airco/z/", 0);
  record("airco-b", "1;0;0;500;60;ac_outdoor;airco/b/", 0);
  record("airco-a", "2;foreign", 0);
  record("airco-c", "1;0;0;500;60;other_outdoor;airco/c/", 0);
  mhi_group_on_connected(&g, "airco-b", false, 0);  // down: still listed
  MhiGroupAvty list;
  mhi_group_availability(&g, &list);
  TEST_ASSERT_EQUAL_UINT8(3, list.count);
  TEST_ASSERT_EQUAL_STRING("airco-b", list.host[0]);
  TEST_ASSERT_EQUAL_STRING("airco/b/", list.prefix[0]);
  TEST_ASSERT_EQUAL_STRING("airco-m", list.host[1]);
  TEST_ASSERT_EQUAL_STRING("airco/me/", list.prefix[1]);
  TEST_ASSERT_EQUAL_STRING("airco-z", list.host[2]);
  // Days later, stale and gone: still listed.
  run(100, 1000);
  tick(400000);
  mhi_group_availability(&g, &list);
  TEST_ASSERT_EQUAL_UINT8(3, list.count);
  // An empty record (a unit taken out of the group) removes it.
  record("airco-z", "", 400000);
  mhi_group_availability(&g, &list);
  TEST_ASSERT_EQUAL_UINT8(2, list.count);
  TEST_ASSERT_EQUAL_STRING("airco-m", list.host[1]);
}

// 35. More units than MHI_GROUP_AVTY_MAX: this unit always, then the lowest others.
static void test_scenario_35_the_list_is_capped_and_keeps_this_unit(void) {
  boot("airco-y", 0);
  record("airco-d", "1;0;0;500;60;ac_outdoor;airco/d/", 0);
  record("airco-b", "1;0;0;500;60;ac_outdoor;airco/b/", 0);
  record("airco-c", "1;0;0;500;60;ac_outdoor;airco/c/", 0);
  record("airco-z", "1;0;0;500;60;ac_outdoor;airco/z/", 0);
  MhiGroupAvty list;
  mhi_group_availability(&g, &list);
  TEST_ASSERT_EQUAL_UINT8(MHI_GROUP_AVTY_MAX, list.count);
  TEST_ASSERT_EQUAL_STRING("airco-b", list.host[0]);
  TEST_ASSERT_EQUAL_STRING("airco-c", list.host[1]);
  TEST_ASSERT_EQUAL_STRING("airco-y", list.host[2]);
  // A lone unit lists itself.
  boot("airco-solo", 0);
  mhi_group_availability(&g, &list);
  TEST_ASSERT_EQUAL_UINT8(1, list.count);
  TEST_ASSERT_EQUAL_STRING("airco-solo", list.host[0]);
  TEST_ASSERT_EQUAL_STRING("airco/me/", list.prefix[0]);
}

// 36. A publisher sends the configs again when a unit joins or leaves, once
// the list has held still for 10 s; changes inside that time restart the wait.
static void test_scenario_36_a_list_change_resends_after_it_settles(void) {
  boot("airco-a", 0);
  run(100, 10000);
  TEST_ASSERT_EQUAL_UINT32(10000, when(MHI_GROUP_ACT_CONFIGS));
  reset_seen();
  record("airco-b", "1;0;0;500;60;ac_outdoor;airco/b/", 20000);  // joins
  run(20000, 5000);
  record("airco-c", "1;0;0;500;60;ac_outdoor;airco/c/", 25000);  // joins before the first settled
  run(25000, 9900);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_CONFIGS));
  run(34900, 1000);
  TEST_ASSERT_EQUAL_UINT32(35000, when(MHI_GROUP_ACT_CONFIGS));  // one re-send, 10 s after the last change
  reset_seen();
  record("airco-c", "", 40000);                                  // leaves
  record("airco-c", "1;0;0;500;60;ac_outdoor;airco/c/", 42000);  // and is back before it settled
  run(40000, 30000);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_CONFIGS));  // the same list as sent: nothing
}

// 37. Group 0 goes out at the first tick after every connect, even for a
// publisher that resumes, so a retained Group 1 from before never stands next
// to the real publisher's (fork #29); a member publishes no second 0.
static void test_scenario_37_group_0_at_once_after_a_connect(void) {
  member_with_publisher(0);  // asserts Group 0 and the record, no claim
  mhi_group_connect(&g, 20000);
  reset_seen();
  record("airco-slaapkamer", "1;1;1;500;60;ac_outdoor;airco/slaapkamer/", 20000);
  mhi_group_on_connected(&g, "airco-slaapkamer", true, 20000);
  tick(20000);
  TEST_ASSERT_EQUAL_UINT8(MHI_GROUP_ACT_STATE, act.flags);
  TEST_ASSERT_EQUAL_UINT8(0, act.state);
  reset_seen();
  run(20100, 10000);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, when(MHI_GROUP_ACT_STATE));
  TEST_ASSERT_EQUAL_UINT32(25000, when(MHI_GROUP_ACT_RECORD));
  TEST_ASSERT_FALSE(mhi_group_may_publish_system(&g));
}

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
  RUN_TEST(test_scenario_31_a_claim_at_the_highest_term_saturates);
  RUN_TEST(test_scenario_32_the_group_state_leaves_a_mismatch);
  RUN_TEST(test_scenario_33_the_resend_is_not_restarted);
  RUN_TEST(test_scenario_34_the_list_holds_the_compatible_members);
  RUN_TEST(test_scenario_35_the_list_is_capped_and_keeps_this_unit);
  RUN_TEST(test_scenario_36_a_list_change_resends_after_it_settles);
  RUN_TEST(test_scenario_37_group_0_at_once_after_a_connect);
  RUN_TEST(test_connected_topics_are_subscribed_from_the_tick_even_in_the_grace_period);
  RUN_TEST(test_a_connected_topic_maps_to_its_peer);
  RUN_TEST(test_own_record_after_the_grace_period_is_ignored);
  RUN_TEST(test_invalid_records_and_hosts_are_reported);
  RUN_TEST(test_the_group_state_fits_in_modest_ram);
  return UNITY_END();
}
