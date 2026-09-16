#include "mhi_action.h"

MhiHvacAction mhi_hvac_action(uint8_t db0, uint8_t db13) {
  if ((db0 & 0x01) == 0)
    return MHI_ACTION_OFF;
  const uint8_t mode = db0 & 0x1c;
  if (mode == 0x0c)  // fan
    return MHI_ACTION_FAN;
  if ((db13 & MHI_DB13_COMPRESSOR) == 0)
    return MHI_ACTION_IDLE;
  switch (mode) {
    case 0x04: return MHI_ACTION_DRYING;
    case 0x08: return MHI_ACTION_COOLING;
    case 0x10: return MHI_ACTION_HEATING;
    default:   return (db13 & MHI_DB13_HEATING) ? MHI_ACTION_HEATING : MHI_ACTION_COOLING;
  }
}
