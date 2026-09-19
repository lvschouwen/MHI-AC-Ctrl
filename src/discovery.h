// Home Assistant MQTT discovery on the unit (fork #4 batch B, spec §4.4).
// Real code only with HA_DISCOVERY; otherwise five empty functions, so
// main.cpp needs no #ifdef.

#pragma once

void discovery_setup();    // once at boot: checks the mode names, says so on Serial
void discovery_restart();  // after every MQTT connect: the rows go out again, one per loop() pass
void discovery_loop();     // every loop() pass: publishes the next unit row while connected, then the Discovery topic; then the next outdoor row
// The outdoor device's rows have their own cursor, and only the group moves it (fork #22 spec §8.3).
void discovery_start_outdoor();   // the six outdoor rows go out, one per loop() pass, after the unit rows
void discovery_cancel_outdoor();  // outdoor rows not yet sent are not sent
