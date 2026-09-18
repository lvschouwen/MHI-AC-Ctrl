// Running counts of protocol errors/timeouts (fork #21 F1; spec §4.1), for
// FrameErrors/FrameTimeouts and the soak that establishes a normal
// FrameTimeouts rate. Pure logic, no Arduino.
#pragma once
#include <stdint.h>

struct MhiFrameStats {
  uint32_t errors;
  uint32_t timeouts;
};

// err_msg is ErrMsg as MHI_AC_Ctrl_Core::loop() returns it: -1/-2 (invalid
// signature/checksum) count as errors, -3/-4 (SCK timeouts) as timeouts, 0
// and anything else as neither. Both fields saturate at UINT32_MAX. A plain
// int so this file need not include MHI-AC-Ctrl-core.h (pulls in Arduino.h).
void mhi_frame_stats_count(MhiFrameStats* stats, int err_msg);
