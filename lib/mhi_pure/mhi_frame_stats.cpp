#include "mhi_frame_stats.h"

static void bump(uint32_t* n) {
  if (*n < UINT32_MAX) (*n)++;
}

void mhi_frame_stats_count(MhiFrameStats* stats, int err_msg) {
  switch (err_msg) {
    case -1: case -2: bump(&stats->errors); break;
    case -3: case -4: bump(&stats->timeouts); break;
    default: break;  // err_msg_valid_frame (0) and anything else
  }
}
