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
