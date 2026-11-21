// How long the AC link goes unserved (fork #51, for #31).
//
// mhi_ac_ctrl_core.loop() is the only thing that talks to the AC; whatever
// else the main loop does (Wi-Fi, MQTT, a publish burst, discovery) delays the
// next call. The status callbacks that publish after a reconnect run inside
// loop() too, so the gap is measured from one call starting to the next one
// starting: a healthy pass is one frame period (about 50 ms, up to the 80 ms
// the call waits when no frame comes), and whatever exceeds that is a stall,
// wherever it happened. LoopMaxMs reports the longest gap since the last
// report; ConnectBurstMs the synchronous MQTT connect and subscribe.
// Pure logic, no Arduino. Wrap-safe at the millis() rollover.

#pragma once

#include <stdint.h>

struct MhiLoopGap {
  uint32_t last_begin_ms;  // when the previous call started
  uint32_t max_ms;         // the longest gap since the last report
  bool have_begin;         // false until a first call has started
};

void mhi_loop_gap_init(MhiLoopGap* g);

// A call to loop() is about to start.
void mhi_loop_gap_begin(MhiLoopGap* g, uint32_t now_ms);

// The longest gap since the previous take, 0 when there was none; starts the next period.
uint32_t mhi_loop_gap_take_max(MhiLoopGap* g);

// Milliseconds from start to now, right across the millis() wrap.
uint32_t mhi_elapsed_ms(uint32_t start_ms, uint32_t now_ms);
