#include "mhi_remote.h"

#include "mhi_frame.h"

bool mhi_remote_last(const uint8_t* frame, uint8_t size) {
  if (frame[DB0] & 0xa2) return false;
  if (frame[DB1] & 0x88) return false;
  if (frame[DB2] & 0x80) return false;
  if (size >= 33 && ((frame[DB16] & 0x10) || (frame[DB17] & 0x0a))) return false;
  return true;
}
