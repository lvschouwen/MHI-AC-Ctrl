// Heating below 18 °C by a shifted room temperature (fork #30).
//
// The unit takes a heat setpoint of 10-17 °C on DB2 but clamps it to 18
// internally (live test 25 Sep 2026). So for a target T below 18 in heat, DB2
// stays at 18 and the unit is fed a room temperature that reads (18 − T) warmer
// than the room, plus the heat offset the unit adds to its own setpoint (2 °C:
// OpData/Tsetpoint reads 20 at DB2 18). The unit then regulates the room to T.
//
// The base is an external room sensor (set/Troom, fork #25 C3): the unit
// echoes the room temperature it is sent, so its own reading is hidden while
// one is sent, and in heat its intake reads 3-5 °C high anyway (hass-config's
// history, 25 Sep 2026). Without a fresh external value the unit heats to 18
// on its own sensor: it fails warm.
//
// The target lives in RAM only: after a reboot the unit reports its DB2 of 18
// and Home Assistant sends T again (Lucas, 25 Sep 2026: HA owns it).
//
// Pure logic, no Arduino.

#pragma once

#include <stdint.h>

#define MHI_HEAT_SHIFT_BELOW 18           // °C: a heat target below this is shifted
#define MHI_HEAT_SHIFT_DB2 (2 * MHI_HEAT_SHIFT_BELOW)  // DB2 while shifting, half degrees
#define MHI_HEAT_SHIFT_DB2_UNKNOWN 0xff   // no DB2 seen yet

struct MhiHeatShift {
  float target;     // °C, the target below 18; NAN: no shift
  bool confirmed;   // the bus has shown DB2 at 18 since the request
  uint8_t bus_db2;  // DB2 as the bus last reported it, half degrees; MHI_HEAT_SHIFT_DB2_UNKNOWN
};

void mhi_heat_shift_init(MhiHeatShift* s);

// A set/Tsetpoint the setpoint limits accepted (mhi_setpoint), while the unit
// is (or was just commanded) in heat or not. *db2 is the setpoint to write, in
// half degrees; a target below 18 in heat turns it into 18 and starts or moves
// the shift. Returns true when an active shift ended (a target of 18 or more,
// or a request outside heat): the caller drops the external room temperature.
bool mhi_heat_shift_request(MhiHeatShift* s, float celsius, bool heat, uint8_t* db2);

// The bus reported DB2 (bit 7 is ignored). Returns true when an active shift
// ended because DB2 moved away from 18 after the bus had shown it: the IR
// remote set another temperature.
bool mhi_heat_shift_on_bus_db2(MhiHeatShift* s, uint8_t db2);

// The unit's mode changed, on the bus or by set/Mode. Returns true when an
// active shift ended because the mode is not heat.
bool mhi_heat_shift_on_mode(MhiHeatShift* s, bool heat);

bool mhi_heat_shift_active(const MhiHeatShift* s);

// What the unit is sent for a room at room_celsius: room + (18 − T) + offset
// while a shift is active, room otherwise. offset is the heat offset, 2 °C.
float mhi_heat_shift_troom(const MhiHeatShift* s, float room_celsius, float offset);

// The setpoint to publish for a DB2 of db2 (bit 7 ignored): T while shifting
// with DB2 at 18, DB2 otherwise; °C.
float mhi_heat_shift_setpoint(const MhiHeatShift* s, uint8_t db2);
