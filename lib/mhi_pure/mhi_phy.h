// Wi-Fi PHY-mode fallback: which of 802.11 g or n the next join should use.
//
// Pure logic, no Arduino. Upstream #224: an ESP8266 in its default 11n mode
// stopped joining a router with 802.11ax on 2.4 GHz, reporting a wrong
// password with the right one, and 11g fixed it. A setting cannot reach a
// unit that is already off the network, so support.cpp asks here before
// every join attempt. The rule: the mode the link last had for the first
// period without a link, then the other mode, alternating every period, so a
// router that rejects 11g gets the default back. A join restarts the clock.

#pragma once

#include <stdint.h>

// The ESP8266 core's WiFiPhyMode_t values, mirrored here so this file does
// not depend on ESP8266WiFiType.h.
#define MHI_PHY_11B 1
#define MHI_PHY_11G 2
#define MHI_PHY_11N 3

struct MhiPhyFallback {
  uint32_t link_seen_ms;  // when the link was last up; 0 at boot
  uint8_t link_mode;      // the mode the link had then; the default at boot
};

// Records that the link is up in `mode` at now_ms. Call on every pass while
// connected, so the time without a link is measured from the last pass that
// saw one.
void mhi_phy_link_up(MhiPhyFallback* f, uint8_t mode, uint32_t now_ms);

// The mode for a join attempt at now_ms, while the link is down.
uint8_t mhi_phy_mode_for_join(const MhiPhyFallback* f, uint32_t now_ms, uint32_t period_ms);

// "11b", "11g", "11n", or "?" for a value the SDK does not define.
const char* mhi_phy_mode_text(uint8_t mode);
