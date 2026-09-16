// Frame layout and checksums for the MHI SPI protocol.
//
// Pure logic, extracted from MHI-AC-Ctrl-core.cpp so it can be compiled and
// tested on the build machine. Nothing here may depend on Arduino or on
// hardware: the native test environment compiles this file with plain g++.

#pragma once

#include <stdint.h>

// Constants for the frame. A standard frame is 20 bytes; the WF-RAC module
// uses an extended 33-byte frame with a second checksum byte at the end.
#define SB0 0
#define SB1 SB0 + 1
#define SB2 SB0 + 2
#define DB0 SB2 + 1
#define DB1 SB2 + 2
#define DB2 SB2 + 3
#define DB3 SB2 + 4
#define DB4 SB2 + 5
#define DB5 SB2 + 6
#define DB6 SB2 + 7
#define DB7 SB2 + 8
#define DB8 SB2 + 9
#define DB9 SB2 + 10
#define DB10 SB2 + 11
#define DB11 SB2 + 12
#define DB12 SB2 + 13
#define DB13 SB2 + 14  // outdoor unit state, see mhi_action.h
#define DB14 SB2 + 15
#define CBH DB14 + 1
#define CBL DB14 + 2
#define DB15 CBL + 1
#define DB16 CBL + 2
#define DB17 CBL + 3
#define DB18 CBL + 4
#define DB19 CBL + 5
#define DB20 CBL + 6
#define DB21 CBL + 7
#define DB22 CBL + 8
#define DB23 CBL + 9
#define DB24 CBL + 10
#define DB25 CBL + 11
#define DB26 CBL + 12
#define CBL2 DB26 + 1

// Sum of every byte ahead of the 16-bit checksum field: bytes 0..CBH-1.
uint16_t mhi_calc_checksum(const uint8_t* frame);

// Sum of every byte ahead of the extended frame's checksum byte: 0..CBL2-1.
// Only the low byte is transmitted, in CBL2.
uint16_t mhi_calc_checksum_frame33(const uint8_t* frame);
