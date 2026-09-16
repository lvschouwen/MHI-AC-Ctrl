// Seconds since boot that survive the millis() wrap.
//
// Pure logic, no Arduino. millis() wraps after 49.7 days; a plain
// millis()/1000 would then read 0 again and look like a reboot, which is
// what the Uptime topic exists to make visible. Advance the counter on every
// loop pass and it never misses a wrap.

#pragma once

#include <stdint.h>

struct MhiUptime {
  uint32_t last_ms;  // millis() at the previous advance; 0 at boot
  uint32_t seconds;  // whole seconds since boot
  uint16_t rest_ms;  // the part of a second not yet counted
};

// Adds the time since the previous advance and returns the whole seconds.
uint32_t mhi_uptime_advance(MhiUptime* u, uint32_t now_ms);
