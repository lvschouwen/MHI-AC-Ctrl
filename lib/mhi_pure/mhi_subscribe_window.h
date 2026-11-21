// The retained burst right after a subscribe (fork #50, #67).
//
// The unit subscribes to set/# on every connect. Messages retained on the
// broker are redelivered after each subscribe, and PubSubClient3 hides the
// retain flag from the callback, so the firmware cannot tell them from a live
// command. PubSubClient3 also reads one packet per loop() call and every pass
// runs other work, so a burst of retained messages is spread over a while: a
// fixed wall-clock window could close before the last one arrives.
//
// So the window follows the burst. It opens at the subscribe and lasts at
// least min_ms. Every message that arrives while it is open extends its end
// to now + quiet_ms (never beyond cap_ms after the subscribe). Once a message
// or a tick finds it expired it latches closed until the next subscribe, so a
// live command is accepted from then on, and a long uptime (the millis()
// rollover at 49.7 days) cannot reopen it.
//
// Pure logic, no Arduino. All times are offsets from the subscribe, so
// unsigned subtraction keeps it wrap-safe.

#pragma once

#include <stdint.h>

struct MhiSubscribeWindow {
  bool open;          // between a subscribe and the first expiry check past the end
  uint32_t since_ms;  // millis() at the latest subscribe
  uint32_t end_off;   // the window's end, in ms after since_ms
};

// A subscribe has just completed (every successful connect): open the window.
void mhi_subscribe_window_mark(MhiSubscribeWindow* w, uint32_t now_ms, uint32_t min_ms);

// Every loop() pass: latch the window closed once its end has passed, even if
// no message arrives.
void mhi_subscribe_window_tick(MhiSubscribeWindow* w, uint32_t now_ms);

// A set/* message has arrived. True when it is inside the window, i.e. may be
// a retained redelivery and must not be acted on if it is dangerous; that
// also extends the window. False once the window has expired (it stays so).
bool mhi_subscribe_window_hit(MhiSubscribeWindow* w, uint32_t now_ms, uint32_t quiet_ms, uint32_t cap_ms);
