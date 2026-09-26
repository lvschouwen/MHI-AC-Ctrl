// Named vane positions (fork issue #4, batch B; spec §4.2).
//
// The AC knows five vane settings: four up/down positions and swing. It
// reports them after an IR remote press too (fork #38); only the set flags it
// echoes after a write over SPI are missing then. The texts on the Vanes
// topic are the caller's (PAYLOAD_VANES_* in MHI-AC-Ctrl.h), so a
// configuration may keep v2.8's "1".."4"; set/Vanes accepts the names and the
// numbers either way.
//
// Pure logic, no Arduino.

#pragma once

#include <stdint.h>

// The core's ACVanes values.
#define MHI_VANES_UNKNOWN 0
#define MHI_VANES_SWING 5

struct MhiVanesNames {
  const char* pos[4];   // positions 1..4, top to bottom
  const char* swing;
  const char* unknown;  // the text for a value that is no setting (kept in the Home Assistant options for v2.8 configs)
};

// DB0/DB1 of the status frame -> 1..4 or MHI_VANES_SWING: swing when DB0 & 0x40,
// else the position in DB1 & 0x30 (0 top .. 3 bottom), the code set_vanes()
// writes. The echo flags (DB0/DB1 & 0x80) are ignored: the IR remote leaves
// them clear but still reports the setting (measured 26 Sep 2026, fork #38).
int mhi_vanes_decode(uint8_t db0, uint8_t db1);

// 1..4 -> the position's name, MHI_VANES_SWING -> swing, anything else -> unknown.
const char* mhi_vanes_text(const MhiVanesNames* names, int value);

// A set/Vanes payload: one of the five names, or "1".."5" (5 = swing).
// MHI_VANES_UNKNOWN when it is none of them (NULL and "" included).
int mhi_vanes_parse(const MhiVanesNames* names, const char* payload);
