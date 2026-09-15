#include "mhi_link.h"

bool mhi_link_dropped(bool* was_up, bool is_up) {
  const bool dropped = *was_up && !is_up;
  *was_up = is_up;
  return dropped;
}

bool mhi_scan_gave_up(int scan_state, uint32_t waited_ms, uint32_t limit_ms) {
  if (scan_state == MHI_SCAN_FAILED) return true;
  if (scan_state == MHI_SCAN_RUNNING) return waited_ms > limit_ms;
  return false;
}
