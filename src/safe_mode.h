// Crash-loop safe mode on the unit (fork #23; spec
// docs/superpowers/specs/2026-09-19-safe-mode-and-ride-alongs-design.md §1):
// the glue around lib/mhi_pure/mhi_safe_mode. The reset reason and three words
// of RTC user memory go in, the decision comes out. Nothing here blocks, loops
// or touches the network. safe_mode.cpp also defines the core's crash hook,
// custom_crash_callback(), which marks the record at every software crash.

#pragma once

#include <stdint.h>

bool safe_mode_boot();                             // first thing in setup(), after Serial.begin(): true starts safe mode
void safe_mode_clear_after_boot(uint32_t now_ms);  // every normal loop() pass: at 120 s uptime, once, the crash count goes back to 0
uint8_t safe_mode_entries();                       // boots into safe mode since power-on: the SafeMode topic
void safe_mode_test_crash();                       // set/reset crash: one deliberate CPU exception, reset reason 2 (spec §1.5)
