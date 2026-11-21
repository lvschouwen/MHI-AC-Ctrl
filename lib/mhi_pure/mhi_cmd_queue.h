// Queued one-shot commands for the AC (fork #48).
//
// set_power(), set_mode(), ... queue a byte that loop() puts into the next
// MISO frame pair. It used to clear the queue when it built the frame, so a
// frame that then failed (a timeout, a bad checksum or signature) took the
// command with it: the MQTT callback had already answered "o.k." and the AC
// never saw it. Now a command stays pending until a frame that carried it has
// passed the signature and checksum checks:
//
//   set     a newer command overwrites the pending one, whatever its state
//   take    the frame build puts the value on the wire and marks it in flight
//   begin   every build starts by clearing the in-flight marks, so an error
//           return needs no hook: whatever was in flight is simply pending again
//   commit  the frame passed its checks: what was in flight is retired
//
// A command that never gets a good frame is dropped after MHI_CQ_MAX_ATTEMPTS
// builds, so a bus that keeps failing cannot replay a stale command over the
// IR remote forever. A value of 0 means "nothing pending" (set(slot, 0)
// clears the slot, as assigning 0 to the old member did).
// Pure logic, no Arduino.

#pragma once

#include <stdint.h>

#define MHI_CQ_MAX_ATTEMPTS 5  // frame builds one command may ride on

enum MhiCqSlot : uint8_t {
  MHI_CQ_POWER,
  MHI_CQ_MODE,
  MHI_CQ_TSETPOINT,
  MHI_CQ_FAN,
  MHI_CQ_VANES0,
  MHI_CQ_VANES1,
  MHI_CQ_VANESLR0,
  MHI_CQ_VANESLR1,
  MHI_CQ_3DAUTO,
  MHI_CQ_ERROPDATA,  // value 1
  MHI_CQ_SILENT,     // value 1 + on (so off is not 0)
  MHI_CQ_SLOTS
};

struct MhiCmdQueue {
  uint8_t value[MHI_CQ_SLOTS];
  uint8_t attempts[MHI_CQ_SLOTS];
  bool inflight[MHI_CQ_SLOTS];
  uint32_t dropped;  // commands given up on after MHI_CQ_MAX_ATTEMPTS builds (fork #60 counts them)
};

void mhi_cq_init(MhiCmdQueue* q);

// Queues value, replacing whatever was pending (and dropping its in-flight mark).
void mhi_cq_set(MhiCmdQueue* q, MhiCqSlot slot, uint8_t value);

// The pending value, 0 for none; changes nothing.
uint8_t mhi_cq_pending(const MhiCmdQueue* q, MhiCqSlot slot);

// The value to put into the frame being built, 0 when none (or when it has
// been tried MHI_CQ_MAX_ATTEMPTS times, which drops it). Marks it in flight.
uint8_t mhi_cq_take(MhiCmdQueue* q, MhiCqSlot slot);

// Start of a frame build: nothing is in flight yet.
void mhi_cq_begin(MhiCmdQueue* q);

// The frame passed its signature and checksum checks: retire what it carried.
void mhi_cq_commit(MhiCmdQueue* q);
