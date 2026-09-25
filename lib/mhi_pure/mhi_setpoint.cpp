#include "mhi_setpoint.h"

#include <math.h>

static const uint8_t kModeHeat = 0x10;

bool mhi_setpoint_allowed(float celsius, uint8_t mode) {
  const float min = mode == kModeHeat ? MHI_SETPOINT_MIN_HEAT : MHI_SETPOINT_MIN;
  if (!(celsius >= min && celsius <= MHI_SETPOINT_MAX)) return false;  // also NaN
  return 2 * celsius == floorf(2 * celsius);  // DB2 carries half degrees
}

uint8_t mhi_setpoint_on_mode_change(uint8_t new_mode, uint8_t setpoint_db2) {
  if (new_mode == kModeHeat || setpoint_db2 == MHI_SETPOINT_UNKNOWN) return 0;
  return (setpoint_db2 & 0x7f) < 2 * MHI_SETPOINT_MIN ? 2 * MHI_SETPOINT_MIN : 0;
}

void mhi_setpoint_guard_init(MhiSetpointGuard* g) {
  g->mode = MHI_SETPOINT_MODE_UNKNOWN;
  g->db2 = MHI_SETPOINT_UNKNOWN;
}

void mhi_setpoint_guard_on_bus_mode(MhiSetpointGuard* g, uint8_t mode) {
  g->mode = mode;
}

void mhi_setpoint_guard_on_bus_db2(MhiSetpointGuard* g, uint8_t db2) {
  g->db2 = db2 & 0x7f;
}

bool mhi_setpoint_guard_request(MhiSetpointGuard* g, float celsius, uint8_t* db2_out) {
  if (!mhi_setpoint_allowed(celsius, g->mode)) return false;
  g->db2 = (uint8_t)(2 * celsius);
  *db2_out = g->db2;
  return true;
}

uint8_t mhi_setpoint_guard_mode(MhiSetpointGuard* g, uint8_t mode) {
  g->mode = mode;
  const uint8_t setpoint = mhi_setpoint_on_mode_change(mode, g->db2);
  if (setpoint != 0) g->db2 = setpoint;
  return setpoint;
}
