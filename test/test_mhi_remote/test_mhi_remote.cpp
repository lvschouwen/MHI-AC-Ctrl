// Host tests for the "last change came from the IR remote" flag (fork #39).
// The frames are the 33-byte MOSI frames Uitkijk sent on 26 Sep 2026 while the
// remote stepped through the up/down vane positions (fork #38).

#include <string.h>
#include <unity.h>

#include "mhi_frame.h"
#include "mhi_remote.h"

void setUp(void) {}
void tearDown(void) {}

// 16:37:46, the last frame before the first remote press: HA had written
// power, mode, fan, vanes and the setpoint.
static const uint8_t kBeforeRemote[33] = {0x6d, 0x80, 0x04, 0xab, 0x8e, 0xa8, 0x95, 0x00, 0x00, 0x88, 0x00,
                                          0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x08, 0xeb, 0x00, 0x02,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xdc};
// 16:37:50, the first remote press: every echo flag cleared in one frame.
static const uint8_t kAfterRemote[33] = {0x6d, 0x80, 0x04, 0x09, 0x16, 0x28, 0x95, 0x00, 0x00, 0x88, 0x00,
                                         0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x07, 0x51, 0x00, 0x02,
                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xa7};

static void test_frames_written_over_spi_are_not_remote(void) {
  TEST_ASSERT_FALSE(mhi_remote_last(kBeforeRemote, 33));
  TEST_ASSERT_FALSE(mhi_remote_last(kBeforeRemote, 20));
}

static void test_a_remote_press_clears_every_flag(void) {
  TEST_ASSERT_TRUE(mhi_remote_last(kAfterRemote, 33));
  TEST_ASSERT_TRUE(mhi_remote_last(kAfterRemote, 20));
}

static void expect_one_flag_is_enough(int index, uint8_t flag, uint8_t size) {
  uint8_t frame[33];
  memcpy(frame, kAfterRemote, sizeof frame);
  frame[index] |= flag;
  TEST_ASSERT_FALSE_MESSAGE(mhi_remote_last(frame, size), "one echo flag means the controller wrote last");
}

static void test_each_echo_flag_alone_means_the_controller(void) {
  expect_one_flag_is_enough(DB0, 0x80, 20);  // vanes
  expect_one_flag_is_enough(DB0, 0x20, 20);  // mode
  expect_one_flag_is_enough(DB0, 0x02, 20);  // power
  expect_one_flag_is_enough(DB1, 0x80, 20);  // vanes
  expect_one_flag_is_enough(DB1, 0x08, 20);  // fan
  expect_one_flag_is_enough(DB2, 0x80, 20);  // setpoint
  expect_one_flag_is_enough(DB16, 0x10, 33);  // left/right position
  expect_one_flag_is_enough(DB17, 0x08, 33);  // 3D auto
  expect_one_flag_is_enough(DB17, 0x02, 33);  // left/right swing
}

static void test_state_bits_are_not_flags(void) {
  uint8_t frame[33];
  memcpy(frame, kAfterRemote, sizeof frame);
  frame[DB0] |= 0x40 | 0x1c | 0x01;  // swing, mode bits, power on
  frame[DB1] |= 0x30 | 0x07;         // vane position, fan
  frame[DB2] |= 0x7f;                // setpoint
  frame[DB16] |= 0x07;               // left/right position
  frame[DB17] |= 0x05;               // 3D auto on, left/right swing on
  TEST_ASSERT_TRUE(mhi_remote_last(frame, 33));
}

static void test_the_short_frame_ignores_the_extended_bytes(void) {
  uint8_t frame[33];
  memcpy(frame, kAfterRemote, sizeof frame);
  frame[DB16] = 0x10;  // beyond a 20-byte frame: not the AC's data there
  TEST_ASSERT_TRUE(mhi_remote_last(frame, 20));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_frames_written_over_spi_are_not_remote);
  RUN_TEST(test_a_remote_press_clears_every_flag);
  RUN_TEST(test_each_echo_flag_alone_means_the_controller);
  RUN_TEST(test_state_bits_are_not_flags);
  RUN_TEST(test_the_short_frame_ignores_the_extended_bytes);
  return UNITY_END();
}
