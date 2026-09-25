#include "mhi_cleaning.h"

static const uint8_t kModeFan = 0x0c;

void mhi_cleaning_init(MhiCleaning* c) {
  c->power = c->mode = c->on_mode = c->state = MHI_CLEANING_UNKNOWN;
}

static bool update(MhiCleaning* c) {
  if (c->power == MHI_CLEANING_UNKNOWN) return false;
  uint8_t state = 0;
  if (c->power == 1)
    c->on_mode = c->mode;
  else
    state = c->mode == kModeFan && c->on_mode != MHI_CLEANING_UNKNOWN && c->on_mode != kModeFan;
  const bool changed = state != c->state;
  c->state = state;
  return changed;
}

bool mhi_cleaning_on_power(MhiCleaning* c, uint8_t power) {
  c->power = power;
  return update(c);
}

bool mhi_cleaning_on_mode(MhiCleaning* c, uint8_t mode) {
  c->mode = mode;
  return update(c);
}

void mhi_cleaning_republish(MhiCleaning* c) {
  c->state = MHI_CLEANING_UNKNOWN;
}
