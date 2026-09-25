#include "mhi_setpoint.h"

static const uint8_t kModeHeat = 0x10;

bool mhi_setpoint_allowed(float celsius, uint8_t mode) {
  const float min = mode == kModeHeat ? MHI_SETPOINT_MIN_HEAT : MHI_SETPOINT_MIN;
  return celsius >= min && celsius <= MHI_SETPOINT_MAX;  // false for NaN
}

uint8_t mhi_setpoint_on_mode_change(uint8_t new_mode, uint8_t setpoint_db2) {
  if (new_mode == kModeHeat || setpoint_db2 == MHI_SETPOINT_UNKNOWN) return 0;
  return (setpoint_db2 & 0x7f) < 2 * MHI_SETPOINT_MIN ? 2 * MHI_SETPOINT_MIN : 0;
}
