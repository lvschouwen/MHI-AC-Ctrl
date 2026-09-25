#include "mhi_heat_shift.h"

#include <math.h>

void mhi_heat_shift_init(MhiHeatShift* s) {
  s->target = NAN;
  s->confirmed = false;
  s->bus_db2 = MHI_HEAT_SHIFT_DB2_UNKNOWN;
}

bool mhi_heat_shift_active(const MhiHeatShift* s) {
  return !isnan(s->target);
}

static bool end(MhiHeatShift* s) {
  const bool was = mhi_heat_shift_active(s);
  s->target = NAN;
  s->confirmed = false;
  return was;
}

bool mhi_heat_shift_request(MhiHeatShift* s, float celsius, bool heat, uint8_t* db2) {
  if (!heat || !(celsius < MHI_HEAT_SHIFT_BELOW)) return end(s);
  // A move within a confirmed shift keeps DB2 at 18, so it stays confirmed;
  // otherwise the echo confirms it, or DB2 is already 18 and none will come.
  if (!(mhi_heat_shift_active(s) && s->confirmed)) s->confirmed = s->bus_db2 == MHI_HEAT_SHIFT_DB2;
  s->target = celsius;
  *db2 = MHI_HEAT_SHIFT_DB2;
  return false;
}

bool mhi_heat_shift_on_bus_db2(MhiHeatShift* s, uint8_t db2) {
  db2 &= 0x7f;
  s->bus_db2 = db2;
  if (!mhi_heat_shift_active(s)) return false;
  if (db2 == MHI_HEAT_SHIFT_DB2) {
    s->confirmed = true;
    return false;
  }
  return s->confirmed && end(s);
}

bool mhi_heat_shift_on_mode(MhiHeatShift* s, bool heat) {
  return !heat && end(s);
}

float mhi_heat_shift_troom(const MhiHeatShift* s, float room_celsius, float offset) {
  if (!mhi_heat_shift_active(s)) return room_celsius;
  return room_celsius + (MHI_HEAT_SHIFT_BELOW - s->target) + offset;
}

float mhi_heat_shift_setpoint(const MhiHeatShift* s, uint8_t db2) {
  db2 &= 0x7f;
  if (mhi_heat_shift_active(s) && db2 == MHI_HEAT_SHIFT_DB2) return s->target;
  return db2 / 2.0f;
}
