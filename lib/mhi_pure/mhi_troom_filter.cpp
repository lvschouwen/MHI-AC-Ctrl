#include "mhi_troom_filter.h"

static const float kCelsiusPerStep = 0.25f;

void mhi_troom_filter_reset(MhiTroomFilter* filter) {
  filter->last = 0;
  filter->has_last = false;
}

bool mhi_troom_filter_pass(MhiTroomFilter* filter, uint8_t value, float limit_celsius) {
  if (filter->has_last) {
    int steps = (int)value - (int)filter->last;
    if (steps < 0) steps = -steps;
    if (!((float)steps > limit_celsius / kCelsiusPerStep)) return false;
  }
  filter->last = value;
  filter->has_last = true;
  return true;
}
