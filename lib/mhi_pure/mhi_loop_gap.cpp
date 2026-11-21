#include "mhi_loop_gap.h"

void mhi_loop_gap_init(MhiLoopGap* g) {
  g->last_begin_ms = 0;
  g->max_ms = 0;
  g->have_begin = false;
}

uint32_t mhi_elapsed_ms(uint32_t start_ms, uint32_t now_ms) {
  return (uint32_t)(now_ms - start_ms);
}

void mhi_loop_gap_begin(MhiLoopGap* g, uint32_t now_ms) {
  if (g->have_begin) {
    const uint32_t gap = mhi_elapsed_ms(g->last_begin_ms, now_ms);
    if (gap > g->max_ms) g->max_ms = gap;
  }
  g->last_begin_ms = now_ms;
  g->have_begin = true;
}

uint32_t mhi_loop_gap_take_max(MhiLoopGap* g) {
  const uint32_t m = g->max_ms;
  g->max_ms = 0;
  return m;
}
