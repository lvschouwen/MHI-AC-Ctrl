// Setpoint limits per mode (fork #25 F3).
//
// The IR remote's night setback is a plain heat setpoint of 10 °C in DB2 (bus
// capture 25 Sep 2026), so heat accepts 10-30 °C. Every other mode keeps 18-30:
// Home Assistant's MQTT climate has one min_temp for all modes, so the firmware
// is the one that refuses a low setpoint outside heat.

#pragma once

#include <stdint.h>

#define MHI_SETPOINT_MIN_HEAT 10
#define MHI_SETPOINT_MIN 18
#define MHI_SETPOINT_MAX 30
#define MHI_SETPOINT_MODE_UNKNOWN 0xff  // no mode known yet: treated as not heat
#define MHI_SETPOINT_UNKNOWN 0xff       // no DB2 seen yet

// Whether set/Tsetpoint may write `celsius` while the unit is in `mode` (the
// DB0 mode bits, or the mode just commanded). NaN is refused.
bool mhi_setpoint_allowed(float celsius, uint8_t mode);

// The setpoint to write together with a change to `new_mode`, in DB2 half
// degrees, or 0 for none: 18 °C when the unit leaves heat with a setpoint
// below 18 (`setpoint_db2` as DB2 last reported it; bit 7 is ignored).
uint8_t mhi_setpoint_on_mode_change(uint8_t new_mode, uint8_t setpoint_db2);

// What the limits go by: the mode and setpoint last requested over MQTT or
// reported by the bus, whichever came last. A request counts at once: the bus
// echoes it a few frames later, and a mode change in between must see it
// (review 25 Sep: set/Tsetpoint 10 in heat, then set/Mode cool before the
// echo, would otherwise send cool with 10).
struct MhiSetpointGuard {
  uint8_t mode;  // DB0 mode bits, MHI_SETPOINT_MODE_UNKNOWN until known
  uint8_t db2;   // half degrees, MHI_SETPOINT_UNKNOWN until known
};

void mhi_setpoint_guard_init(MhiSetpointGuard* g);
void mhi_setpoint_guard_on_bus_mode(MhiSetpointGuard* g, uint8_t mode);
void mhi_setpoint_guard_on_bus_db2(MhiSetpointGuard* g, uint8_t db2);

// set/Tsetpoint: true and *db2_out (half degrees) when allowed in the current
// mode; the guard then holds it. False changes nothing.
bool mhi_setpoint_guard_request(MhiSetpointGuard* g, float celsius, uint8_t* db2_out);

// set/Mode: records the mode and returns the setpoint to write with it (0 for
// none), which the guard then holds, as mhi_setpoint_on_mode_change().
uint8_t mhi_setpoint_guard_mode(MhiSetpointGuard* g, uint8_t mode);
