#include "mhi_wifi_sm.h"

#include "mhi_link.h"

MhiWifiStep mhi_wifi_decide(int status, bool link_up, int scan_state, uint32_t scan_waited_ms, uint32_t scan_deadline_ms,
                            uint32_t connect_waited_ms) {
  if (status != MHI_WIFI_ONGOING) {  // OK, TIMEOUT, SCANNING or SCANNING_DONE
    if (status == MHI_WIFI_OK || status == MHI_WIFI_TIMEOUT) return MHI_WIFI_STEP_START_SCAN;
    if (status == MHI_WIFI_SCANNING) {
      return mhi_scan_gave_up(scan_state, scan_waited_ms, scan_deadline_ms) ? MHI_WIFI_STEP_SCAN_GAVE_UP
                                                                            : MHI_WIFI_STEP_NONE;
    }
    if (status == MHI_WIFI_SCANNING_DONE) return MHI_WIFI_STEP_SCAN_DONE;  // after scanning for a stronger AP: still connected
    return MHI_WIFI_STEP_NONE;
  }
  // ONGOING
  if (link_up) return MHI_WIFI_STEP_CONNECTED;
  if (connect_waited_ms > MHI_WIFI_CONNECT_TIMEOUT_MS) return MHI_WIFI_STEP_TIMED_OUT;  // strictly more than 10 s
  return MHI_WIFI_STEP_NONE;
}

bool mhi_reconnect_trials_exceeded(int* trials) {
  return (*trials)++ > 9;
}
