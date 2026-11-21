// Crash details (fork #25, N2 from #11).
//
// ResetReason says only "Exception". The core's crash hook gets the SDK's
// rst_info (reason, exccause, epc1, excvaddr) on every software crash path, so
// it stores them in RTC; the next normal boot publishes them retained on
// CrashInfo and clears the record. epc1 goes through xtensa-lx106-elf-addr2line
// with the build's firmware.elf to find the source line. exccause means
// something only when reason is 2 (a CPU exception).

#pragma once

#include <stddef.h>
#include <stdint.h>

#define MHI_CRASH_RECORD_WORDS 6  // magic, reason, exccause, epc1, excvaddr, check
#define MHI_CRASH_INFO_JSON_MAX 144  // the worst case with the snapshot is 134 bytes with its NUL; mhi_crash_info.cpp asserts it
#define MHI_CRASH_SNAPSHOT_WORDS 3   // magic, the four bytes packed, check

// What the AC was doing when the unit crashed (fork #61): the last power, mode,
// setpoint (DB2 as the bus reported it) and room temperature (DB3) byte seen,
// 0xff for one not seen yet. It is a record of its own beside the crash record
// above, so a crash record from older firmware still reads: valid, without the
// snapshot.
void mhi_crash_snapshot_make(uint32_t snap[MHI_CRASH_SNAPSHOT_WORDS], uint8_t power, uint8_t mode, uint8_t tsetpoint,
                             uint8_t troom);
bool mhi_crash_snapshot_valid(const uint32_t snap[MHI_CRASH_SNAPSHOT_WORDS]);

void mhi_crash_record_make(uint32_t rec[MHI_CRASH_RECORD_WORDS], uint32_t reason, uint32_t exccause, uint32_t epc1,
                           uint32_t excvaddr);

// False for anything but a record mhi_crash_record_make() wrote, e.g. RTC after power-on.
bool mhi_crash_record_valid(const uint32_t rec[MHI_CRASH_RECORD_WORDS]);

// {"exccause":E,"reason":R,"epc1":"0x........","excvaddr":"0x........"} for a
// valid record, {"exccause":-1} for NULL or an invalid one. Returns the
// length, 0 (and an empty string) when it does not fit out_len.
size_t mhi_crash_info_json(const uint32_t rec[MHI_CRASH_RECORD_WORDS], char* out, size_t out_len);

// The same, and for a valid record with a valid snapshot (snap may be NULL) the
// keys "power", "mode", "tset" and "troom" appended, numbers only (fork #61).
// The existing keys and their order are unchanged.
size_t mhi_crash_info_json_ex(const uint32_t rec[MHI_CRASH_RECORD_WORDS], const uint32_t snap[MHI_CRASH_SNAPSHOT_WORDS],
                              char* out, size_t out_len);
