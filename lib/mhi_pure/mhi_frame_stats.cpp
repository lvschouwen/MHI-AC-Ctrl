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

void mhi_frame_stats_republish(MhiFrameStatsPublished* published) {
  published->valid = false;
}

bool mhi_frame_stats_due(const MhiFrameStats* stats, MhiFrameStatsPublished* published) {
  if (published->valid && published->last.errors == stats->errors && published->last.timeouts == stats->timeouts)
    return false;
  published->last = *stats;
  published->valid = true;
  return true;
}
