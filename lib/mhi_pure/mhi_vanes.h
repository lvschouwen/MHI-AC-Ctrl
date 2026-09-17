// Named vane positions (fork issue #4, batch B; spec §4.2).
//
// The AC knows five vane settings: four up/down positions and swing, and it
// reports "unknown" after the IR remote moved them. The texts on the Vanes
// topic are the caller's (PAYLOAD_VANES_* in MHI-AC-Ctrl.h), so a
// configuration may keep v2.8's "1".."4"; set/Vanes accepts the names and the
// numbers either way.
//
// Pure logic, no Arduino.

#pragma once

// The core's ACVanes values.
#define MHI_VANES_UNKNOWN 0
#define MHI_VANES_SWING 5

struct MhiVanesNames {
  const char* pos[4];   // positions 1..4, top to bottom
  const char* swing;
  const char* unknown;  // published when the AC does not say where the vanes are
};

// 1..4 -> the position's name, MHI_VANES_SWING -> swing, anything else -> unknown.
const char* mhi_vanes_text(const MhiVanesNames* names, int value);

// A set/Vanes payload: one of the five names, or "1".."5" (5 = swing).
// MHI_VANES_UNKNOWN when it is none of them (NULL and "" included).
int mhi_vanes_parse(const MhiVanesNames* names, const char* payload);
