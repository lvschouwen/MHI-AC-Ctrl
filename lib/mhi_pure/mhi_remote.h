// Whether the unit's current settings came from the IR remote (fork #39).
//
// After a write over SPI the AC echoes a set flag per field in its own frame:
// DB0 0x80 vanes, 0x20 mode, 0x02 power; DB1 0x80 vanes, 0x08 fan; DB2 0x80
// setpoint; in the 33-byte frame DB16 0x10 left/right position and DB17 0x08
// 3D auto, 0x02 left/right swing. Any IR remote press clears all of them in one
// frame, and every SPI write sets at least its own. So "no flag set" means the
// last change came from the remote. It also reads that way after the unit lost
// 230 V, or before the controller ever wrote anything.
//
// Pure logic, no Arduino.

#pragma once

#include <stdint.h>

// frame: the whole MOSI frame, size 20 or 33.
bool mhi_remote_last(const uint8_t* frame, uint8_t size);
