#include "mhi_mqtt_retain.h"

#include <stdint.h>

MhiRetain mhi_retain_from_topic(const char* topic) {
  const uint8_t* t = (const uint8_t*)topic;
  if (t[-2] & 0x80) return MHI_RETAIN_UNKNOWN;  // the last remaining-length byte has no continuation bit
  uint8_t hdr;
  if ((t[-3] & 0x80) == 0)
    hdr = t[-3];  // remaining length of one byte: the header sits right before it
  else
    hdr = t[-4];  // two bytes: t[-3] is the first of them, so t[-4] exists
  if ((hdr & 0xF0) != 0x30) return MHI_RETAIN_UNKNOWN;  // not a PUBLISH
  return (hdr & 0x01) ? MHI_RETAIN_RETAINED : MHI_RETAIN_LIVE;
}
