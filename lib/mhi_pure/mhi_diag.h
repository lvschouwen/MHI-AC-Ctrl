// Boot-time wiring check.
//
// The frequencies measured on SCK, MOSI and MISO during the first second after
// boot say whether the board is wired to the AC correctly. Judging them is
// pure arithmetic, so it lives here and is tested on the build machine; the
// measuring itself stays in support.cpp.
//
// A fault is reported, never fatal. Reaching loop() is what makes OTA
// possible, and OTA is the only way to recover a unit that is already inside
// an air conditioner.

#pragma once

#include <stddef.h>
#include <stdint.h>

enum MhiWiringFault : uint8_t {
  MHI_WIRING_FAULT_SCK = 1 << 0,   // no clock: the AC is not talking to us
  MHI_WIRING_FAULT_MOSI = 1 << 1,  // no data, or data faster than the clock
  MHI_WIRING_FAULT_MISO = 1 << 2,  // our own output line is being driven
};

// Bitmask of MhiWiringFault, 0 when everything looks right. All three pins are
// judged independently so one bad reading does not hide the others.
uint8_t mhi_wiring_faults(uint32_t sck_hz, uint32_t mosi_hz, uint32_t miso_hz);

// Render a fault mask as "o.k.", "MISO", "SCK,MOSI" and so on, for the
// diagnostics topic. Always NUL-terminates unless out_size is 0, in which case
// it writes nothing.
void mhi_wiring_fault_text(uint8_t faults, char* out, size_t out_size);
