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
