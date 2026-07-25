#include "mhi_frame.h"

uint16_t mhi_calc_checksum(const uint8_t* frame) {
  uint16_t checksum = 0;
  for (int i = 0; i < CBH; i++) checksum += frame[i];
  return checksum;
}

uint16_t mhi_calc_checksum_frame33(const uint8_t* frame) {
  uint16_t checksum = 0;
  for (int i = 0; i < CBL2; i++) checksum += frame[i];
  return checksum;
}
