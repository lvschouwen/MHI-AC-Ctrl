// Did the AC take the command (fork #60)?
//
// The MQTT reply "o.k." only says a set/ payload parsed. After a frame that
// carried a write has passed its checks (the #48 commit point), the AC's own
// frames must show the written value within a number of frames; if they never
// do, the write was lost (protection state, a misaligned frame, ...) and
// CmdUnconfirmed counts it. Counter only: nothing is retried.
//
// It compares the value, not the AC's echo flags. The flags (mhi_remote.h) stay
// set until an IR remote press, so a flag cannot show that a write was lost
// when it was already set. A write whose target is already what the AC reports
// confirms at once, which is right: nothing that matters was lost.
//
// A newer write to the same field replaces the one being watched (the older one
// is neither confirmed nor counted). An IR remote press inside the watch
// resolves it without a count: once the AC's frames have shown the field's set
// flag, a frame where that flag is gone again (a press clears them all) means
// the remote took over the field. A flag that never shows is no press: the AC
// did not take the write.
//
// The watched value is the one the core put into the MISO frame, after the
// setpoint guard and the heat shift (#30) rewrote what the MQTT payload said.
// Known legitimate mismatches count as unconfirmed, by design and without
// heuristics: a field the AC clamps or overrides in some mode (a setpoint
// outside what a mode accepts, a fan level a mode ignores).
// Pure logic, no Arduino.

#pragma once

#include <stdint.h>

#define MHI_CC_WATCH_FRAMES 100  // valid frames the AC gets to show the value: 100 / 20 frames per second = 5 s; tune on the bench

enum MhiCcField : uint8_t {
  MHI_CC_POWER,
  MHI_CC_MODE,
  MHI_CC_TSETPOINT,
  MHI_CC_FAN,
  MHI_CC_VANES_POS,
  MHI_CC_VANES_SWING,
  MHI_CC_FIELDS
};

struct MhiCmdConfirm {
  uint8_t want[MHI_CC_FIELDS];         // the written value, as the field reads in the AC's frame
  uint16_t frames_left[MHI_CC_FIELDS];  // 0: not watched
  bool seen_echo[MHI_CC_FIELDS];        // the field's set flag has shown in the AC's frames since the write
  uint32_t unconfirmed;                 // writes whose value never showed
};

void mhi_cc_init(MhiCmdConfirm* c);

// A valid frame from the AC (MOSI, size 20 or 33): confirms or ages the watches.
void mhi_cc_observe(MhiCmdConfirm* c, const uint8_t* mosi);

// A valid frame this unit sent (MISO) has just passed its checks: start a watch
// for every field whose set flag it carries. The core calls it on the double
// frame of a pair only: the next frame repeats the same stale bytes, and arming
// again would forget an echo seen in between. Call it after mhi_cc_observe() on
// the same exchange: the AC's frame of that exchange predates the write.
void mhi_cc_sent(MhiCmdConfirm* c, const uint8_t* miso, uint16_t watch_frames);
