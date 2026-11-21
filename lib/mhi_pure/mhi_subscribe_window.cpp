#include "mhi_subscribe_window.h"

void mhi_subscribe_window_mark(MhiSubscribeWindow* w, uint32_t now_ms, uint32_t min_ms) {
  w->open = true;
  w->since_ms = now_ms;
  w->end_off = min_ms;
}

void mhi_subscribe_window_tick(MhiSubscribeWindow* w, uint32_t now_ms) {
  if (w->open && (uint32_t)(now_ms - w->since_ms) >= w->end_off) w->open = false;
}

bool mhi_subscribe_window_hit(MhiSubscribeWindow* w, uint32_t now_ms, uint32_t quiet_ms, uint32_t cap_ms) {
  mhi_subscribe_window_tick(w, now_ms);
  if (!w->open) return false;
  const uint32_t elapsed = (uint32_t)(now_ms - w->since_ms);
  uint32_t end = elapsed + quiet_ms;
  if (end > cap_ms) end = cap_ms;
  if (end > w->end_off) w->end_off = end;
  return true;
}
