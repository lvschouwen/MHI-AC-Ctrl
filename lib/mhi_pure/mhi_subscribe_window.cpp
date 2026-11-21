#include "mhi_subscribe_window.h"

void mhi_subscribe_window_mark(MhiSubscribeWindow* w, uint32_t now_ms) {
  w->marked = true;
  w->since_ms = now_ms;
}

bool mhi_subscribe_window_in(const MhiSubscribeWindow* w, uint32_t now_ms, uint32_t window_ms) {
  if (!w->marked) return false;
  return (uint32_t)(now_ms - w->since_ms) < window_ms;
}
