// The moment right after a subscribe completes (fork #50, #67).
//
// The unit subscribes to set/# on every connect. Messages retained on the
// broker are redelivered right after each subscribe, and PubSubClient3 hides
// the retain flag from the callback, so the firmware cannot tell them from a
// live command. Anything that must not be acted on from a retained message
// (set/reset first, every set/* topic later) asks whether it is still inside
// a short window after the subscribe: retained messages arrive inside it, a
// live press from Home Assistant does not.
// Pure logic, no Arduino. Wrap-safe at the millis() rollover.

#pragma once

#include <stdint.h>

struct MhiSubscribeWindow {
  bool marked;          // a subscribe has completed at least once
  uint32_t since_ms;    // millis() when the latest subscribe completed
};

// A subscribe has just completed (every successful connect).
void mhi_subscribe_window_mark(MhiSubscribeWindow* w, uint32_t now_ms);

// True while now is within window_ms of the latest subscribe. False before
// any subscribe: nothing can have been redelivered yet.
bool mhi_subscribe_window_in(const MhiSubscribeWindow* w, uint32_t now_ms, uint32_t window_ms);
