#include "mhi_rescue.h"

#include <stdint.h>

void mhi_rescue_init(MhiRescue* r, uint32_t now) {
  r->ap_up = false;
  r->since_ms = now;
  r->windows = 0;
}

// down_ms doubled once per window already opened, never past max_down_ms and
// never below down_ms.
static uint32_t wait_before_window(uint8_t windows, uint32_t down_ms, uint32_t max_down_ms) {
  uint32_t wait = down_ms;
  for (uint8_t i = 0; i < windows && wait <= max_down_ms / 2; i++) wait *= 2;
  return wait;
}

MhiRescueAction mhi_rescue_tick(MhiRescue* r, bool link_up, bool client, uint32_t now, uint32_t down_ms, uint32_t ap_ms,
                                uint32_t max_down_ms) {
  if (!r->ap_up) {
    if (link_up) {
      r->since_ms = now;
      r->windows = 0;
      return MHI_RESCUE_NONE;
    }
    if (now - r->since_ms < wait_before_window(r->windows, down_ms, max_down_ms)) return MHI_RESCUE_NONE;
    r->ap_up = true;
    if (r->windows < UINT8_MAX) r->windows++;
    r->since_ms = now;
    return MHI_RESCUE_START_AP;
  }
  if (client) {
    r->since_ms = now;
    return MHI_RESCUE_NONE;
  }
  if (link_up) r->windows = 0;
  if (!link_up && now - r->since_ms < ap_ms) return MHI_RESCUE_NONE;
  r->ap_up = false;
  r->since_ms = now;
  return MHI_RESCUE_STOP_AP;
}
