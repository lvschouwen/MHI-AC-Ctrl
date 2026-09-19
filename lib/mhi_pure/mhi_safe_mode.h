// Crash-loop safe mode (fork #23; spec
// docs/superpowers/specs/2026-09-19-safe-mode-and-ride-alongs-design.md §1).
// A unit that crashes three times in a row, each time within 120 s of its
// boot, starts in safe mode: Wi-Fi and OTA only, for 10 minutes. The count is
// kept in three words of RTC user memory, which survive a reset but not a
// power loss.
//
// Pure logic, no Arduino and no RTC access: the glue (src/safe_mode.cpp)
// reads the words, calls this, and writes them back.

#pragma once

#include <stdint.h>

#define MHI_SAFE_MAGIC 0x4D484953u   // "MHIS"
#define MHI_SAFE_THRESHOLD 3         // crashes in a row that start safe mode
#define MHI_SAFE_CLEAR_MS 120000u    // up this long in normal mode: the count goes back to 0, once per boot
#define MHI_SAFE_RESTART_MS 600000u  // in safe mode: ESP.restart() after this long
#define MHI_SAFE_CRASHED_BIT 0x10000u  // data bit 16: the core's crash hook ran before this boot

// The SDK's reset reasons (rst_info.reason), mirrored so this file needs no
// user_interface.h; src/safe_mode.cpp checks that they match.
#define MHI_RESET_POWER_ON 0
#define MHI_RESET_HW_WDT 1
#define MHI_RESET_EXCEPTION 2
#define MHI_RESET_SOFT_WDT 3
#define MHI_RESET_SOFT_RESTART 4  // ESP.restart(): also how an OTA update and set/reset end
#define MHI_RESET_DEEP_SLEEP 5
#define MHI_RESET_EXTERNAL 6

struct MhiSafeBoot {
  bool safe_mode;
  uint8_t count;    // crashes in a row, this boot's included; saturates at 255
  uint8_t entries;  // boots into safe mode since power-on, this one's included; saturates at 255
};

// The decision at boot (spec §1.2). rec_in is the record as read from RTC
// (<magic, data, check>; anything after a power-on); rec_out gets the record
// to write back, always valid and without the crashed bit. They may be the
// same array. A crash reason (1, 2, 3) or the crashed bit counts one crash,
// except at a power-on.
MhiSafeBoot mhi_safe_boot(uint32_t reset_reason, const uint32_t rec_in[3], uint32_t rec_out[3]);

// The crash hook's part (src/safe_mode.cpp, custom_crash_callback): sets the
// crashed bit and recomputes the check word. A valid record keeps its count
// and entries; an invalid one becomes count 0, entries 0, so the crash is
// still counted.
void mhi_safe_record_mark_crashed(uint32_t rec[3]);

// The 120 s clear: the count goes to 0, the entries stay. The record is valid afterwards.
void mhi_safe_record_clear_count(uint32_t rec[3]);
