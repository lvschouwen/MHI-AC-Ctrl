// Link supervision: counting lost connections and giving up on a scan.
//
// Pure logic, no Arduino. support.cpp measures the facts (is Wi-Fi up, is
// the broker connected, what does scanComplete() say, how long has the scan
// been running) and asks here what they mean.

#pragma once

#include <stdint.h>

// True once for every up -> down transition of a link, so the caller can
// count outages. Stores the new state in *was_up. Anything that was never up
// cannot drop, so a link that starts down is not counted.
bool mhi_link_dropped(bool* was_up, bool is_up);

// The Wi-Fi state machine already knows whether it believed the link was up
// (its state is "connected" only after a successful connect, and "ongoing"
// during a deliberate roam to a stronger AP), so it does not need an edge
// memory: the link is lost when it was believed up and is not connected now.
// That leaves a roam, and the connect attempts at boot, uncounted.
bool mhi_wifi_link_lost(bool believed_up, bool connected_now);

// The ESP8266 core's scanComplete() values, mirrored here so this file does
// not depend on ESP8266WiFiType.h: a count of 0 or more means finished,
// WIFI_SCAN_RUNNING is -1 and WIFI_SCAN_FAILED is -2.
#define MHI_SCAN_RUNNING -1
#define MHI_SCAN_FAILED -2

// Whether a scan that was started asynchronously should be abandoned. The
// core does not report a scan the SDK refused to start: the completion
// callback then never fires and scanComplete() says failed forever. A scan
// that is still running past the deadline is abandoned too. A finished scan
// belongs to its callback and is never abandoned here.
bool mhi_scan_gave_up(int scan_state, uint32_t waited_ms, uint32_t limit_ms);

// Paces reconnect attempts. MQTTreconnect() used to try on every loop() pass,
// and it resets Wi-Fi after ten failures in a row (a workaround for
// esp8266/Arduino#7432). A restarting broker refuses at once, so every broker
// restart also cycled Wi-Fi. The first attempt after a reset is due at once;
// each later one is due interval_ms after the previous one. Reset the pacer
// while the link is up, so the first attempt after the next drop is immediate.
struct MhiRetryPacer {
  uint32_t last_ms;  // when the previous attempt was allowed
  bool attempted;    // false after a reset: the next attempt is due at once
};

void mhi_retry_reset(MhiRetryPacer* pacer);

// Whether an attempt may be made at now_ms. Records it when it may.
bool mhi_retry_due(MhiRetryPacer* pacer, uint32_t now_ms, uint32_t interval_ms);
