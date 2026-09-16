// What the AC is doing, for Home Assistant's hvac_action.
//
// Mode says what the unit is set to; it does not say whether the outdoor unit
// is working. DB13 does: bit 2 is set while the compressor runs, bit 1 while it
// heats rather than cools. The mapping follows hberntsen/mhi-ac-ctrl-esp32
// (mhi_ac_ctrl.h), which reads the same byte.
//
// Pure logic, no Arduino.

#pragma once

#include <stdint.h>

#define MHI_DB13_HEATING 0x02
#define MHI_DB13_COMPRESSOR 0x04

// Values match Home Assistant's hvac_action names in the same order as the
// PAYLOAD_ACTION_* texts in MHI-AC-Ctrl.h.
enum MhiHvacAction : uint8_t {
  MHI_ACTION_OFF,
  MHI_ACTION_IDLE,
  MHI_ACTION_COOLING,
  MHI_ACTION_HEATING,
  MHI_ACTION_DRYING,
  MHI_ACTION_FAN,
};

// db0 and db13 as the AC sends them in a MOSI frame. A unit that is off is off.
// Fan mode is fan. Otherwise a stopped compressor is idle, and a running one is
// cooling, heating or drying by mode; auto, and any mode this firmware does not
// know, take heating or cooling from DB13.
MhiHvacAction mhi_hvac_action(uint8_t db0, uint8_t db13);
