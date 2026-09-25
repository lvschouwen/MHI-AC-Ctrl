// The Allergen Clear state (fork #25).
//
// Allergen Clear on the IR remote runs a 1.5 h cleaning cycle with the unit
// off: DB0 turns from 0x51 (on, heat) into 0x4c (off, fan) and back into 0x50
// (off, heat) when it ends (bus capture 25 Sep 2026). A unit switched off from
// fan mode also reads off + fan, so cleaning is only reported when the mode
// reads fan while off and the last mode the unit ran in was not fan. The cycle
// started from fan mode, or before a boot, is not seen.

#pragma once

#include <stdint.h>

#define MHI_CLEANING_UNKNOWN 0xff

struct MhiCleaning {
  uint8_t power;    // 0/1 as the parser reported it
  uint8_t mode;     // DB0 mode bits
  uint8_t on_mode;  // the mode while the unit was last on
  uint8_t state;    // 0/1, MHI_CLEANING_UNKNOWN until power is known
};

void mhi_cleaning_init(MhiCleaning* c);

// The parser reported power or mode. Returns true when state changed, which
// includes the first known state.
bool mhi_cleaning_on_power(MhiCleaning* c, uint8_t power);
bool mhi_cleaning_on_mode(MhiCleaning* c, uint8_t mode);

// An MQTT (re)connect: the next report counts as a change, so the state goes
// out again; the last mode the unit ran in is kept.
void mhi_cleaning_republish(MhiCleaning* c);
