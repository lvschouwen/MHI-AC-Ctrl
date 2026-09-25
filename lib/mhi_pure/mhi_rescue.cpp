#include "mhi_rescue.h"

void mhi_rescue_init(MhiRescue* r, uint32_t now) {
  r->ap_up = false;
  r->since_ms = now;
}

MhiRescueAction mhi_rescue_tick(MhiRescue* r, bool link_up, bool client, uint32_t now, uint32_t down_ms, uint32_t ap_ms) {
  if (!r->ap_up) {
    if (link_up) {
      r->since_ms = now;
      return MHI_RESCUE_NONE;
    }
    if (now - r->since_ms < down_ms) return MHI_RESCUE_NONE;
    r->ap_up = true;
    r->since_ms = now;
    return MHI_RESCUE_START_AP;
  }
  if (client) {
    r->since_ms = now;
    return MHI_RESCUE_NONE;
  }
  if (!link_up && now - r->since_ms < ap_ms) return MHI_RESCUE_NONE;
  r->ap_up = false;
  r->since_ms = now;
  return MHI_RESCUE_STOP_AP;
}
