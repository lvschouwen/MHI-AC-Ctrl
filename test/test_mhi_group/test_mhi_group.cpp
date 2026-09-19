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
