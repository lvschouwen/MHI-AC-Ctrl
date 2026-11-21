// Host tests for the queued one-shot commands (issue #48).
//
// A command must survive a frame that failed, go out again, and be retired by
// a frame that passed its checks; a newer command overwrites an older one.

#include <unity.h>

#include "mhi_cmd_queue.h"

void setUp(void) {}
void tearDown(void) {}

static MhiCmdQueue fresh() {
  MhiCmdQueue q;
  mhi_cq_init(&q);
  return q;
}

static void test_nothing_pending_takes_zero(void) {
  MhiCmdQueue q = fresh();
  TEST_ASSERT_EQUAL_UINT8(0, mhi_cq_take(&q, MHI_CQ_POWER));
  TEST_ASSERT_EQUAL_UINT8(0, mhi_cq_pending(&q, MHI_CQ_POWER));
}

static void test_a_good_frame_retires_the_command(void) {
  MhiCmdQueue q = fresh();
  TEST_ASSERT_EQUAL_UINT32(0, q.dropped);
  mhi_cq_set(&q, MHI_CQ_TSETPOINT, 0xac);
  mhi_cq_begin(&q);
  TEST_ASSERT_EQUAL_UINT8(0xac, mhi_cq_take(&q, MHI_CQ_TSETPOINT));
  TEST_ASSERT_EQUAL_UINT8(0xac, mhi_cq_pending(&q, MHI_CQ_TSETPOINT));  // still pending until the frame passed
  mhi_cq_commit(&q);
  TEST_ASSERT_EQUAL_UINT8(0, mhi_cq_pending(&q, MHI_CQ_TSETPOINT));
  mhi_cq_begin(&q);
  TEST_ASSERT_EQUAL_UINT8(0, mhi_cq_take(&q, MHI_CQ_TSETPOINT));
}

// The frame failed: no commit, so the next build carries the command again.
static void test_a_failed_frame_leaves_the_command_pending(void) {
  MhiCmdQueue q = fresh();
  mhi_cq_set(&q, MHI_CQ_POWER, 0x03);
  mhi_cq_begin(&q);
  TEST_ASSERT_EQUAL_UINT8(0x03, mhi_cq_take(&q, MHI_CQ_POWER));
  // timeout or bad checksum: loop() returns without a commit
  mhi_cq_begin(&q);
  TEST_ASSERT_EQUAL_UINT8(0x03, mhi_cq_take(&q, MHI_CQ_POWER));
  mhi_cq_commit(&q);
  TEST_ASSERT_EQUAL_UINT8(0, mhi_cq_pending(&q, MHI_CQ_POWER));
}

// A frame that carried nothing, or only another command, retires only what it carried.
static void test_commit_retires_only_what_the_frame_carried(void) {
  MhiCmdQueue q = fresh();
  mhi_cq_set(&q, MHI_CQ_MODE, 0x28);
  mhi_cq_set(&q, MHI_CQ_FAN, 0x0a);
  mhi_cq_begin(&q);
  TEST_ASSERT_EQUAL_UINT8(0x28, mhi_cq_take(&q, MHI_CQ_MODE));
  mhi_cq_commit(&q);
  TEST_ASSERT_EQUAL_UINT8(0, mhi_cq_pending(&q, MHI_CQ_MODE));
  TEST_ASSERT_EQUAL_UINT8(0x0a, mhi_cq_pending(&q, MHI_CQ_FAN));
}

// A build clears the marks left by a failed frame, so a later good frame that
// carries something else (the odd frame of a pair) does not retire the command.
static void test_a_later_good_frame_does_not_retire_a_failed_one(void) {
  MhiCmdQueue q = fresh();
  mhi_cq_set(&q, MHI_CQ_TSETPOINT, 0x90);
  mhi_cq_begin(&q);
  mhi_cq_take(&q, MHI_CQ_TSETPOINT);  // frame fails, no commit
  mhi_cq_begin(&q);                   // the next build takes nothing of it
  mhi_cq_commit(&q);                  // and passes
  TEST_ASSERT_EQUAL_UINT8(0x90, mhi_cq_pending(&q, MHI_CQ_TSETPOINT));
}

static void test_a_newer_command_overwrites_the_pending_one(void) {
  MhiCmdQueue q = fresh();
  mhi_cq_set(&q, MHI_CQ_TSETPOINT, 0x90);
  mhi_cq_set(&q, MHI_CQ_TSETPOINT, 0xa0);
  mhi_cq_begin(&q);
  TEST_ASSERT_EQUAL_UINT8(0xa0, mhi_cq_take(&q, MHI_CQ_TSETPOINT));
}

// A setter that runs between the take and the commit (the status callback
// calls set_fan() inside loop()) must not be wiped by the commit.
static void test_a_command_set_after_the_take_survives_the_commit(void) {
  MhiCmdQueue q = fresh();
  mhi_cq_set(&q, MHI_CQ_FAN, 0x09);
  mhi_cq_begin(&q);
  mhi_cq_take(&q, MHI_CQ_FAN);
  mhi_cq_set(&q, MHI_CQ_FAN, 0x0f);
  mhi_cq_commit(&q);
  TEST_ASSERT_EQUAL_UINT8(0x0f, mhi_cq_pending(&q, MHI_CQ_FAN));
  mhi_cq_begin(&q);
  TEST_ASSERT_EQUAL_UINT8(0x0f, mhi_cq_take(&q, MHI_CQ_FAN));
}

// Setting 0 clears the slot, like assigning 0 to the old member (set_vanesLR
// does it for the left/right byte of a swing command).
static void test_setting_zero_clears(void) {
  MhiCmdQueue q = fresh();
  mhi_cq_set(&q, MHI_CQ_VANESLR1, 0x12);
  mhi_cq_set(&q, MHI_CQ_VANESLR1, 0);
  mhi_cq_begin(&q);
  TEST_ASSERT_EQUAL_UINT8(0, mhi_cq_take(&q, MHI_CQ_VANESLR1));
}

// Slots are independent: a swing command leaves a queued position byte alone.
static void test_slots_are_independent(void) {
  MhiCmdQueue q = fresh();
  mhi_cq_set(&q, MHI_CQ_VANES1, 0x90);
  mhi_cq_set(&q, MHI_CQ_VANES0, 0xc0);
  TEST_ASSERT_EQUAL_UINT8(0x90, mhi_cq_pending(&q, MHI_CQ_VANES1));
}

// A bus that keeps failing must not replay a command for ever.
static void test_a_command_is_dropped_after_the_attempt_cap(void) {
  MhiCmdQueue q = fresh();
  mhi_cq_set(&q, MHI_CQ_POWER, 0x03);
  for (int i = 0; i < MHI_CQ_MAX_ATTEMPTS; i++) {
    mhi_cq_begin(&q);
    TEST_ASSERT_EQUAL_UINT8(0x03, mhi_cq_take(&q, MHI_CQ_POWER));  // each one fails
  }
  mhi_cq_begin(&q);
  TEST_ASSERT_EQUAL_UINT8(0, mhi_cq_take(&q, MHI_CQ_POWER));
  TEST_ASSERT_EQUAL_UINT8(0, mhi_cq_pending(&q, MHI_CQ_POWER));
  TEST_ASSERT_EQUAL_UINT32(1, q.dropped);  // a command the AC never got is counted
  mhi_cq_begin(&q);
  mhi_cq_take(&q, MHI_CQ_POWER);           // nothing pending any more: not counted again
  TEST_ASSERT_EQUAL_UINT32(1, q.dropped);
}

static void test_a_new_command_restarts_the_attempts(void) {
  MhiCmdQueue q = fresh();
  mhi_cq_set(&q, MHI_CQ_POWER, 0x03);
  for (int i = 0; i < MHI_CQ_MAX_ATTEMPTS - 1; i++) {
    mhi_cq_begin(&q);
    mhi_cq_take(&q, MHI_CQ_POWER);
  }
  mhi_cq_set(&q, MHI_CQ_POWER, 0x02);
  for (int i = 0; i < MHI_CQ_MAX_ATTEMPTS; i++) {
    mhi_cq_begin(&q);
    TEST_ASSERT_EQUAL_UINT8(0x02, mhi_cq_take(&q, MHI_CQ_POWER));
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_pending_takes_zero);
  RUN_TEST(test_a_good_frame_retires_the_command);
  RUN_TEST(test_a_failed_frame_leaves_the_command_pending);
  RUN_TEST(test_commit_retires_only_what_the_frame_carried);
  RUN_TEST(test_a_later_good_frame_does_not_retire_a_failed_one);
  RUN_TEST(test_a_newer_command_overwrites_the_pending_one);
  RUN_TEST(test_a_command_set_after_the_take_survives_the_commit);
  RUN_TEST(test_setting_zero_clears);
  RUN_TEST(test_slots_are_independent);
  RUN_TEST(test_a_command_is_dropped_after_the_attempt_cap);
  RUN_TEST(test_a_new_command_restarts_the_attempts);
  return UNITY_END();
}
