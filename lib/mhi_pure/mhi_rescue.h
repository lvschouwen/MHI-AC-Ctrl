// The rescue access point (fork #28).
//
// Wi-Fi credentials are compiled in: a wrong value, a replaced router or a new
// password takes a unit off the network for good, and only opening the AC
// brings it back. After down_ms without a Wi-Fi link the unit opens its own
// WPA2 access point that serves OTA only, for ap_ms; then it tries the normal
// join again, and repeats while there is no link. A station connected to the
// access point keeps it up, so an upload is never cut off; ap_ms counts from
// when the last one left. The station is off while the access point is up
// (on the ESP8266 a station scan would move the access point's channel).
//
// Back off (fork #58): every window that ends with no link doubles the wait
// before the next one, up to max_down_ms; a link resets it. The first window
// always opens after down_ms, exactly as without the back off: it is the
// recovery path of a unit with no spare board. Pure logic, no Arduino.

#pragma once

#include <stdint.h>

enum MhiRescueAction : uint8_t {
  MHI_RESCUE_NONE,
  MHI_RESCUE_START_AP,  // stop the station, open the access point
  MHI_RESCUE_STOP_AP,   // close the access point, start the normal join
};

struct MhiRescue {
  bool ap_up;
  uint32_t since_ms;  // station: the link was last up (or the boot); access point: opened, or the last station left
  uint8_t windows;    // access point windows opened since the link was last up; 0 for the first
};

void mhi_rescue_init(MhiRescue* r, uint32_t now);

// Every loop() pass. link_up: the station has a link; client: a station is
// connected to the access point. down_ms is the wait before the first window;
// later waits double up to max_down_ms (a max below down_ms means no back off).
// Wrap-safe.
MhiRescueAction mhi_rescue_tick(MhiRescue* r, bool link_up, bool client, uint32_t now, uint32_t down_ms, uint32_t ap_ms,
                                uint32_t max_down_ms);
