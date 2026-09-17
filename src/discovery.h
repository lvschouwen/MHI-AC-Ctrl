// Home Assistant MQTT discovery on the unit (fork #4 batch B, spec §4.4).
// Real code only with HA_DISCOVERY; otherwise three empty functions, so
// main.cpp needs no #ifdef.

#pragma once

void discovery_setup();    // once at boot: checks the mode names, says so on Serial
void discovery_restart();  // after every MQTT connect: the rows go out again, one per loop() pass
void discovery_loop();     // every loop() pass: publishes the next row while connected, then the Discovery topic
