// Host tests for bounded MQTT payload handling.
//
// The callback used to NUL-terminate in place with payload[length] = 0.
// payload points into pubsubclient3's receive buffer at _buffer + offset, so
// for a message that fills the buffer that write lands one byte past the end.
// These tests cover the replacement, which copies out instead.

#include <unity.h>
#include <string.h>

#include "mhi_mqtt.h"

void setUp(void) {}
void tearDown(void) {}

static void test_copies_a_normal_payload(void) {
  char out[16];
  const uint8_t payload[] = {'O', 'n'};

  TEST_ASSERT_EQUAL_size_t(2, mhi_copy_payload(out, sizeof(out), payload, 2));
  TEST_ASSERT_EQUAL_STRING("On", out);
}

static void test_terminates_an_empty_payload(void) {
  char out[16];
  memset(out, 'x', sizeof(out));

  TEST_ASSERT_EQUAL_size_t(0, mhi_copy_payload(out, sizeof(out), NULL, 0));
  TEST_ASSERT_EQUAL_STRING("", out);
}

static void test_does_not_read_a_terminator_from_the_source(void) {
  // The source is a byte range, not a C string. Anything after `length` in
  // the broker's buffer belongs to the next message.
  char out[16];
  const uint8_t payload[] = {'2', '1', '.', '5', 'G', 'A', 'R', 'B', 'A', 'G', 'E'};

  TEST_ASSERT_EQUAL_size_t(4, mhi_copy_payload(out, sizeof(out), payload, 4));
  TEST_ASSERT_EQUAL_STRING("21.5", out);
}

static void test_truncates_rather_than_overflowing(void) {
  char out[4];
  const uint8_t payload[] = {'a', 'b', 'c', 'd', 'e', 'f'};

  TEST_ASSERT_EQUAL_size_t(3, mhi_copy_payload(out, sizeof(out), payload, 6));
  TEST_ASSERT_EQUAL_STRING("abc", out);
}

static void test_writes_nothing_past_the_destination(void) {
  // Fill a guard byte after the buffer we hand over and check it survives.
  char guarded[8];
  memset(guarded, '#', sizeof(guarded));
  const uint8_t payload[] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};

  mhi_copy_payload(guarded, 4, payload, 8);

  TEST_ASSERT_EQUAL_STRING("abc", guarded);
  TEST_ASSERT_EQUAL_CHAR('#', guarded[4]);
  TEST_ASSERT_EQUAL_CHAR('#', guarded[7]);
}

static void test_tolerates_a_zero_length_destination(void) {
  char out[2] = {'x', 'y'};
  const uint8_t payload[] = {'a'};

  TEST_ASSERT_EQUAL_size_t(0, mhi_copy_payload(out, 0, payload, 1));
  TEST_ASSERT_EQUAL_CHAR('x', out[0]);
}

static void test_exact_fit_keeps_room_for_the_terminator(void) {
  char out[4];
  const uint8_t payload[] = {'a', 'b', 'c'};

  TEST_ASSERT_EQUAL_size_t(3, mhi_copy_payload(out, sizeof(out), payload, 3));
  TEST_ASSERT_EQUAL_STRING("abc", out);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_copies_a_normal_payload);
  RUN_TEST(test_terminates_an_empty_payload);
  RUN_TEST(test_does_not_read_a_terminator_from_the_source);
  RUN_TEST(test_truncates_rather_than_overflowing);
  RUN_TEST(test_writes_nothing_past_the_destination);
  RUN_TEST(test_tolerates_a_zero_length_destination);
  RUN_TEST(test_exact_fit_keeps_room_for_the_terminator);
  return UNITY_END();
}
