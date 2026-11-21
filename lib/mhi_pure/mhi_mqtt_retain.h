// The MQTT retain flag of the message being delivered (fork #67).
//
// A broker sets RETAIN=1 on a PUBLISH only when it sends a stored message
// because of a new subscription, and RETAIN=0 on a live forward (MQTT 3.1.1
// 3.3.1.3). That separates a replay of an old retained set/* command from a
// live command exactly, at any time after a connect. PubSubClient3 does not
// pass the flag to the callback, but at callback time the PUBLISH fixed header
// is still in its receive buffer, right in front of the topic:
//
//   _buffer[0]              fixed header: 0x3n, bit 0 = RETAIN, bits 1-2 = QoS
//   _buffer[1 .. hdrLen]    remaining length, 7 bits per byte, bit 7 = more
//   _buffer[hdrLen+1]       topic length MSB   <- topic[-1]
//   _buffer[hdrLen+2 ..]    topic, which handlePacket() moves one byte to the
//                           front over the topic length LSB, so topic ==
//                           _buffer + hdrLen + 2
//
// so topic[-2] is the last remaining-length byte (bit 7 clear), and topic[-3]
// is the header itself when hdrLen is 1 (bit 7 clear, as 0x3n), or the first
// remaining-length byte when hdrLen is 2 (bit 7 set), the header then at
// topic[-4]. QoS 0 and 1 leave _buffer[0] alone until after the callback.
// The 256 byte buffer keeps hdrLen at 1 or 2.
//
// This reads library internals: re-verify it on any PubSubClient3 bump (#69).
// Pure logic, no Arduino. Anything that does not look like a PUBLISH header
// reads as UNKNOWN, which callers treat as live.

#pragma once

enum MhiRetain {
  MHI_RETAIN_UNKNOWN,   // the bytes before the topic are not a PUBLISH header we recognise
  MHI_RETAIN_LIVE,      // RETAIN=0: a live message
  MHI_RETAIN_RETAINED,  // RETAIN=1: a stored message replayed after a subscribe
};

// topic: the pointer PubSubClient3 passes to its callback. Reads topic[-2],
// topic[-3] and, only when topic[-3] shows a continuation bit, topic[-4]: never
// before the start of the receive buffer.
MhiRetain mhi_retain_from_topic(const char* topic);
