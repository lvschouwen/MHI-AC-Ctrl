#include "mhi_cmd_queue.h"

void mhi_cq_init(MhiCmdQueue* q) {
  for (uint8_t i = 0; i < MHI_CQ_SLOTS; i++) {
    q->value[i] = 0;
    q->attempts[i] = 0;
    q->inflight[i] = false;
  }
  q->dropped = 0;
}

void mhi_cq_set(MhiCmdQueue* q, MhiCqSlot slot, uint8_t value) {
  q->value[slot] = value;
  q->attempts[slot] = 0;
  q->inflight[slot] = false;
}

uint8_t mhi_cq_pending(const MhiCmdQueue* q, MhiCqSlot slot) {
  return q->value[slot];
}

uint8_t mhi_cq_take(MhiCmdQueue* q, MhiCqSlot slot) {
  if (q->value[slot] == 0) return 0;
  if (q->attempts[slot] >= MHI_CQ_MAX_ATTEMPTS) {
    mhi_cq_set(q, slot, 0);
    if (q->dropped < UINT32_MAX) q->dropped++;
    return 0;
  }
  q->attempts[slot]++;
  q->inflight[slot] = true;
  return q->value[slot];
}

void mhi_cq_begin(MhiCmdQueue* q) {
  for (uint8_t i = 0; i < MHI_CQ_SLOTS; i++) q->inflight[i] = false;
}

void mhi_cq_commit(MhiCmdQueue* q) {
  for (uint8_t i = 0; i < MHI_CQ_SLOTS; i++) {
    if (q->inflight[i]) mhi_cq_set(q, (MhiCqSlot)i, 0);
  }
}
