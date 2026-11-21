// The Wi-Fi join state machine's decisions (fork #64): what setupWiFi() does
// next, as a pure function of the state and the clock readings, so the
// transitions are tested on the build machine like mhi_link and mhi_rescue.
//
// Three places write the status: setupWiFi() (these steps), the async scan
// callback handleWiFiScanResult() and rescue_loop(). They keep writing it
// where they did; this only decides what setupWiFi() does for the status it
// finds. The caller does the side effects and the assignments, in the old
// order, and reads the scan state only while scanning.
// Pure logic, no Arduino.

#pragma once

#include <stdint.h>

// support.h's WIFI_CONNECT_* numbers (asserted in support.cpp).
#define MHI_WIFI_OK 0
#define MHI_WIFI_ONGOING 1
#define MHI_WIFI_TIMEOUT 2
#define MHI_WIFI_SCANNING_DONE 3
#define MHI_WIFI_SCANNING 4

#define MHI_WIFI_CONNECT_TIMEOUT_MS 10000  // a join attempt that has not come up after this long timed out

enum MhiWifiStep : uint8_t {
  MHI_WIFI_STEP_NONE,           // nothing this pass
  MHI_WIFI_STEP_START_SCAN,     // status OK or TIMEOUT: delete the old scan, start an async one, status SCANNING
  MHI_WIFI_STEP_SCAN_GAVE_UP,   // status SCANNING and the scan did not start or finish: connect without it, status ONGOING
  MHI_WIFI_STEP_SCAN_DONE,      // status SCANNING_DONE (a scan for a stronger AP while connected): status OK
  MHI_WIFI_STEP_CONNECTED,      // status ONGOING and the link is up: status OK
  MHI_WIFI_STEP_TIMED_OUT,      // status ONGOING, no link, longer than MHI_WIFI_CONNECT_TIMEOUT_MS: status TIMEOUT
};

// scan_state: WiFi.scanComplete(), read only when status is SCANNING (else 0).
// scan_waited_ms: since the scan was started; connect_waited_ms: since the join attempt began.
MhiWifiStep mhi_wifi_decide(int status, bool link_up, int scan_state, uint32_t scan_waited_ms, uint32_t scan_deadline_ms,
                            uint32_t connect_waited_ms);

// MQTTreconnect() counts failed attempts and resets Wi-Fi after ten in a row
// (esp8266/Arduino#7432). Counts this attempt; true when more than ten went by.
// The caller resets the counter after acting on it.
bool mhi_reconnect_trials_exceeded(int* trials);
