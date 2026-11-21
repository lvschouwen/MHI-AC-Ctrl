// Host tests for the retain-flag peek (issue #67).
//
// The buffers are built the way PubSubClient3 3.3.2 leaves them at callback
// time: fixed header, remaining length, topic length MSB, then the topic
// moved one byte to the front over the topic length LSB. The packet sits at
// the start of a heap block of exactly its size plus a little, so under
// native-san a read before the buffer would be reported.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "mhi_mqtt_retain.h"

void setUp(void) {}
void tearDown(void) {}

struct Packet {
  uint8_t* buf;  // exactly the bytes PubSubClient3's _buffer holds
  const char* topic;
};

// flags: the low nibble of the fixed header (bit 0 RETAIN, bits 1-2 QoS).
static Packet make(uint8_t flags, const char* topic, size_t payload_len) {
  const size_t tlen = strlen(topic);
  const uint8_t qos = (flags >> 1) & 3;
  const size_t remaining = 2 + tlen + (qos ? 2 : 0) + payload_len;
  const uint8_t hdrlen = remaining > 127 ? 2 : 1;
  const size_t total = 1 + hdrlen + 2 + tlen + 1 + (qos ? 2 : 0) + payload_len;
  Packet p;
  p.buf = (uint8_t*)malloc(total);  // exact size: reads outside it trip ASan
  memset(p.buf, 'x', total);
  size_t i = 0;
  p.buf[i++] = 0x30 | (flags & 0x0f);
  if (hdrlen == 1) {
    p.buf[i++] = (uint8_t)remaining;
  } else {
    p.buf[i++] = (uint8_t)((remaining & 0x7f) | 0x80);
    p.buf[i++] = (uint8_t)(remaining >> 7);
  }
  p.buf[i++] = (uint8_t)(tlen >> 8);  // topic length MSB; the LSB is overwritten by the topic's move
  char* topic_ptr = (char*)(p.buf + i);
  memcpy(topic_ptr, topic, tlen);
  topic_ptr[tlen] = '\0';
  p.topic = topic_ptr;
  return p;
}

static MhiRetain peek(uint8_t flags, size_t payload_len) {
  Packet p = make(flags, "unit/set/Mode", payload_len);
  const MhiRetain r = mhi_retain_from_topic(p.topic);
  free(p.buf);
  return r;
}

static void test_one_byte_remaining_length(void) {
  TEST_ASSERT_EQUAL(MHI_RETAIN_LIVE, peek(0x00, 4));
  TEST_ASSERT_EQUAL(MHI_RETAIN_RETAINED, peek(0x01, 4));
}

static void test_two_byte_remaining_length(void) {
  // a topic plus payload over 127 bytes needs two remaining-length bytes
  TEST_ASSERT_EQUAL(MHI_RETAIN_LIVE, peek(0x00, 200));
  TEST_ASSERT_EQUAL(MHI_RETAIN_RETAINED, peek(0x01, 200));
  // remaining length 127 is the last one-byte value (2 + 13 + 112), 128 the first two-byte one
  TEST_ASSERT_EQUAL(MHI_RETAIN_LIVE, peek(0x00, 112));
  TEST_ASSERT_EQUAL(MHI_RETAIN_RETAINED, peek(0x01, 112));
  TEST_ASSERT_EQUAL(MHI_RETAIN_LIVE, peek(0x00, 113));
  TEST_ASSERT_EQUAL(MHI_RETAIN_RETAINED, peek(0x01, 113));
}

static void test_qos_1_and_the_dup_flag(void) {
  TEST_ASSERT_EQUAL(MHI_RETAIN_LIVE, peek(0x02, 4));       // QoS 1
  TEST_ASSERT_EQUAL(MHI_RETAIN_RETAINED, peek(0x03, 4));   // QoS 1, retained
  TEST_ASSERT_EQUAL(MHI_RETAIN_RETAINED, peek(0x03, 300)); // QoS 1, two-byte length
  TEST_ASSERT_EQUAL(MHI_RETAIN_LIVE, peek(0x08, 4));       // DUP set, not retained
  TEST_ASSERT_EQUAL(MHI_RETAIN_RETAINED, peek(0x09, 4));
}

// The shortest packet the reader can see: header, one length byte, the topic
// length MSB and a one byte topic. topic[-3] is the header itself.
static void test_a_minimal_packet_reads_inside_the_buffer(void) {
  Packet p = make(0x01, "a", 0);
  TEST_ASSERT_EQUAL(MHI_RETAIN_RETAINED, mhi_retain_from_topic(p.topic));
  free(p.buf);
  p = make(0x00, "a", 0);
  TEST_ASSERT_EQUAL(MHI_RETAIN_LIVE, mhi_retain_from_topic(p.topic));
  free(p.buf);
}

// Bytes that do not look like a PUBLISH header read as UNKNOWN.
static void test_garbage_is_unknown(void) {
  Packet p = make(0x01, "unit/set/Mode", 4);
  uint8_t* topic = (uint8_t*)p.topic;
  // header type is not PUBLISH (0x20 CONNACK, 0x40 PUBACK, ...). Bit 7 stays clear:
  // PubSubClient3 only calls back for a PUBLISH, so _buffer[0] is 0x3n.
  const uint8_t bad_types[] = {0x00, 0x21, 0x41, 0x10, 0x31 | 0x40};
  for (size_t i = 0; i < sizeof bad_types; i++) {
    p.buf[0] = bad_types[i];
    TEST_ASSERT_EQUAL_MESSAGE(MHI_RETAIN_UNKNOWN, mhi_retain_from_topic(p.topic), "header type");
  }
  p.buf[0] = 0x31;
  TEST_ASSERT_EQUAL(MHI_RETAIN_RETAINED, mhi_retain_from_topic(p.topic));
  // the last remaining-length byte (topic[-2]) must have no continuation bit
  topic[-2] |= 0x80;
  TEST_ASSERT_EQUAL(MHI_RETAIN_UNKNOWN, mhi_retain_from_topic(p.topic));
  topic[-2] &= 0x7f;
  free(p.buf);
}

static void test_a_continuation_byte_but_a_bad_header_is_unknown(void) {
  Packet p = make(0x01, "unit/set/Mode", 200);  // two remaining-length bytes
  uint8_t* topic = (uint8_t*)p.topic;
  TEST_ASSERT_EQUAL(MHI_RETAIN_RETAINED, mhi_retain_from_topic(p.topic));
  p.buf[0] = 0x21;
  TEST_ASSERT_EQUAL(MHI_RETAIN_UNKNOWN, mhi_retain_from_topic(p.topic));
  p.buf[0] = 0x31;
  topic[-3] &= 0x7f;  // now it reads like a one byte length: buf[1] (0x57 here) is taken as the header
  TEST_ASSERT_EQUAL(MHI_RETAIN_UNKNOWN, mhi_retain_from_topic(p.topic));
  free(p.buf);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_one_byte_remaining_length);
  RUN_TEST(test_two_byte_remaining_length);
  RUN_TEST(test_qos_1_and_the_dup_flag);
  RUN_TEST(test_a_minimal_packet_reads_inside_the_buffer);
  RUN_TEST(test_garbage_is_unknown);
  RUN_TEST(test_a_continuation_byte_but_a_bad_header_is_unknown);
  return UNITY_END();
}
