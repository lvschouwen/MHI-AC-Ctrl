// Room-temperature conversions for the MHI protocol.
//
// The AC carries Troom as a single byte in 0.25 degC steps offset by 61, so
// byte 61 is 0 degC and byte 145 is 21 degC. The same arithmetic was inlined
// in three places across main.cpp and support.cpp; it lives here now so it can
// be tested on the build machine.
//
// Pure logic: no Arduino, no hardware.

#pragma once

#include <stdint.h>

// Encode degC as the MHI Troom byte. Truncates rather than rounds, matching
// the original (byte)(celsius * 4 + 61). Only meaningful for temperatures
// inside mhi_troom_celsius_plausible(); out-of-range input is clamped so the
// conversion stays defined.
uint8_t mhi_troom_from_celsius(float celsius);

// Encode degC as the MHI Troom byte, rounded to the nearest quarter degree
// (half a step up), clamped like mhi_troom_from_celsius(). For a room sensor's
// value on set/Troom (fork #25 C3): truncation made 23.93 read 23.75.
uint8_t mhi_troom_round_from_celsius(float celsius);

// Decode an MHI Troom byte back to degC.
float mhi_celsius_from_troom(int troom);

// Accept a room temperature supplied over MQTT. The window is exclusive at
// both ends, matching the original (f > -10) & (f < 48).
bool mhi_troom_celsius_plausible(float celsius);

// The same window applied to an already-encoded Troom byte, so the MQTT path,
// the sensor path and the write path cannot drift apart.
bool mhi_troom_byte_plausible(uint8_t troom);

// Accept a raw DS18x20 reading, in 1/128 degC units. The window is inclusive
// at both ends, unlike mhi_troom_celsius_plausible(). The two have always
// disagreed on the boundary; that is preserved here rather than quietly
// changed, and the difference is only ever visible at exactly -10 or 48 degC.
bool mhi_ds18x20_raw_plausible(int16_t raw);

// Convert a raw DS18x20 reading, in 1/128 degC units, to the MHI Troom byte.
// Sub-zero readings encode normally. They used to return 0, which the caller's
// plausible check then dropped, so a DS18x20 could not report anything below
// freezing even though the MQTT path accepted down to -10 degC.
uint8_t mhi_troom_from_ds18x20_raw(int16_t raw);
