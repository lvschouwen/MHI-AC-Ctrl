#include "safe_mode.h"

#include <Arduino.h>
#include <user_interface.h>  // struct rst_info and the REASON_* values; the header has its own extern "C"

#include "mhi_crash_info.h"
#include "mhi_safe_mode.h"

// The record's place (spec §1.3): RTC user block 32, three words. User block 0
// is 0x60001200, where eboot keeps its 128-byte OTA command (blocks 0-31), so
// block 32 is the first word an OTA does not overwrite.
#define SAFE_MODE_RTC_BLOCK 32
// The crash details (fork #25): six words after the safe-mode record.
#define CRASH_INFO_RTC_BLOCK 36
static_assert(SAFE_MODE_RTC_BLOCK + 3 <= CRASH_INFO_RTC_BLOCK, "the crash details must not overlap the safe-mode record");

// mhi_safe_mode numbers the reasons itself, because lib/mhi_pure cannot include
// the SDK's user_interface.h; this is where the two spellings are tied together.
static_assert(MHI_RESET_POWER_ON == REASON_DEFAULT_RST && MHI_RESET_HW_WDT == REASON_WDT_RST &&
                  MHI_RESET_EXCEPTION == REASON_EXCEPTION_RST && MHI_RESET_SOFT_WDT == REASON_SOFT_WDT_RST &&
                  MHI_RESET_SOFT_RESTART == REASON_SOFT_RESTART && MHI_RESET_DEEP_SLEEP == REASON_DEEP_SLEEP_AWAKE &&
                  MHI_RESET_EXTERNAL == REASON_EXT_SYS_RST,
              "mhi_safe_mode's reset reasons are the SDK's");

static uint32_t record[3];              // as written to RTC at boot, and at the 120 s clear
static MhiSafeBoot boot = {false, 0, 0};
static bool count_cleared = false;      // the 120 s clear happens once per boot

bool safe_mode_boot() {
  const uint32_t reason = ESP.getResetInfoPtr()->reason;
  uint32_t in[3] = {0, 0, 0};  // an invalid record if the read is refused
  ESP.rtcUserMemoryRead(SAFE_MODE_RTC_BLOCK, in, sizeof(in));
  boot = mhi_safe_boot(reason, in, record);
  ESP.rtcUserMemoryWrite(SAFE_MODE_RTC_BLOCK, record, sizeof(record));
  Serial.printf_P(PSTR("\nSafe mode check: reset reason %u, crashes in a row %u, safe-mode entries %u\n"),
                  (unsigned)reason, (unsigned)boot.count, (unsigned)boot.entries);
  return boot.safe_mode;
}

void safe_mode_clear_after_boot(uint32_t now_ms) {
  if (count_cleared || now_ms < MHI_SAFE_CLEAR_MS) return;
  count_cleared = true;
  if (boot.count == 0) return;  // the record already says 0
  mhi_safe_record_clear_count(record);
  ESP.rtcUserMemoryWrite(SAFE_MODE_RTC_BLOCK, record, sizeof(record));
}

uint8_t safe_mode_entries() {
  return boot.entries;
}

// The core's weak crash hook (core_esp8266_postmortem.cpp:77-83), defined here.
// postmortem_report() calls it on every software crash path, right before the
// restart (line 276): an exception and a software watchdog through the SDK's
// system_restart_local, which the build wraps; abort(), panic(), a failed
// assert and a failed new through raise_exception(); the cont-stack check after
// every loop() through __stack_chk_fail(). The SDK reports the last group as
// reason 4, a plain restart, so the record says it was a crash. ESP.restart()
// never gets here: its timer calls the unwrapped restart. This runs inside the
// crash, so it only reads and writes the three RTC words, as postmortem_report()
// itself reads RTC (line 137): no Serial, no allocation, nothing that waits.
// It also stores the crash's rst_info for CrashInfo (fork #25), the last crash
// winning.
extern "C" void custom_crash_callback(struct rst_info* info, uint32_t, uint32_t) {
  uint32_t rec[3] = {0, 0, 0};  // an invalid record if the read is refused
  ESP.rtcUserMemoryRead(SAFE_MODE_RTC_BLOCK, rec, sizeof(rec));
  mhi_safe_record_mark_crashed(rec);
  ESP.rtcUserMemoryWrite(SAFE_MODE_RTC_BLOCK, rec, sizeof(rec));
  if (info != nullptr) {
    uint32_t crash[MHI_CRASH_RECORD_WORDS];
    mhi_crash_record_make(crash, info->reason, info->exccause, info->epc1, info->excvaddr);
    ESP.rtcUserMemoryWrite(CRASH_INFO_RTC_BLOCK, crash, sizeof(crash));
  }
}

static char crash_json[MHI_CRASH_INFO_JSON_MAX] = "{\"exccause\":-1}";

// Not in safe mode: that boot never connects, so the record waits for the
// normal boot after it.
void crash_info_boot() {
  uint32_t crash[MHI_CRASH_RECORD_WORDS] = {0};  // an invalid record if the read is refused
  ESP.rtcUserMemoryRead(CRASH_INFO_RTC_BLOCK, crash, sizeof(crash));
  mhi_crash_info_json(crash, crash_json, sizeof(crash_json));
  if (mhi_crash_record_valid(crash)) {
    Serial.printf_P(PSTR("Last crash: %s\n"), crash_json);
    const uint32_t cleared[MHI_CRASH_RECORD_WORDS] = {0};
    ESP.rtcUserMemoryWrite(CRASH_INFO_RTC_BLOCK, const_cast<uint32_t*>(cleared), sizeof(cleared));
  }
}

const char* crash_info_json() {
  return crash_json;
}

// A store to address 0 raises the CPU exception StoreProhibited (29). The SDK's
// fatal-exception handler stores rst_info with REASON_EXCEPTION_RST in RTC
// before it restarts; the core's postmortem reads it back from there
// (core_esp8266_postmortem.cpp, postmortem_report()), and the next boot reports
// reason 2. abort() and panic() do not go through that handler: the next boot
// sees reason 4 and counts them only through the crash hook above, so they
// cannot prove reason 2. The pointer is a volatile object, so the compiler
// has to load it at run time: it can neither drop the store nor replace it
// with a trap instruction.
void safe_mode_test_crash() {
  static volatile uint32_t* volatile target = nullptr;
  *target = 0;
}
