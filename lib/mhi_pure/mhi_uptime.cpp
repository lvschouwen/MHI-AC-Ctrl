#include "mhi_uptime.h"

uint32_t mhi_uptime_advance(MhiUptime* u, uint32_t now_ms) {
  // Unsigned subtraction, so the millis() wrap after 49.7 days is harmless
  // as long as the counter is advanced more often than that.
  const uint32_t elapsed_ms = now_ms - u->last_ms;
  u->last_ms = now_ms;
  u->seconds += elapsed_ms / 1000;
  u->rest_ms += elapsed_ms % 1000;
  if (u->rest_ms >= 1000) {
    u->seconds++;
    u->rest_ms -= 1000;
  }
  return u->seconds;
}
