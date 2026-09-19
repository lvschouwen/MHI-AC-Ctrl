#include "mhi_safe_mode.h"

// <magic> <data: count in bits 0-7, entries in bits 8-15, the crashed bit 16> <check>
static uint32_t check_of(uint32_t data) {
  return MHI_SAFE_MAGIC ^ data ^ 0xFFFFFFFFu;
}

static bool valid(const uint32_t rec[3]) {
  return rec[0] == MHI_SAFE_MAGIC && rec[2] == check_of(rec[1]);
}

static void store(uint32_t rec[3], uint8_t count, uint8_t entries, bool crashed) {
  const uint32_t data = (uint32_t)count | ((uint32_t)entries << 8) | (crashed ? MHI_SAFE_CRASHED_BIT : 0u);
  rec[0] = MHI_SAFE_MAGIC;
  rec[1] = data;
  rec[2] = check_of(data);
}

// Hardware watchdog, exception, software watchdog.
static bool is_crash(uint32_t reason) {
  return reason == MHI_RESET_HW_WDT || reason == MHI_RESET_EXCEPTION || reason == MHI_RESET_SOFT_WDT;
}

MhiSafeBoot mhi_safe_boot(uint32_t reset_reason, const uint32_t rec_in[3], uint32_t rec_out[3]) {
  const bool ok = valid(rec_in);  // an invalid record, as after a power-on, counts as 0
  uint8_t count = ok ? (uint8_t)(rec_in[1] & 0xFFu) : 0;
  uint8_t entries = ok ? (uint8_t)((rec_in[1] >> 8) & 0xFFu) : 0;
  // abort(), panic(), a failed assert or new, a stack overflow: reason 4, but the hook marked the record.
  const bool marked = ok && (rec_in[1] & MHI_SAFE_CRASHED_BIT) != 0;
  if (reset_reason == MHI_RESET_POWER_ON) entries = 0;  // the entries count since power-on
  if (is_crash(reset_reason) || (marked && reset_reason != MHI_RESET_POWER_ON)) {  // once, even with both
    if (count < 255) count++;
  }
  else {
    count = 0;  // every other reason, and one out of range
  }
  const bool safe = count >= MHI_SAFE_THRESHOLD;
  if (safe && entries < 255) entries++;
  store(rec_out, count, entries, false);  // every boot clears the crashed bit
  const MhiSafeBoot boot = {safe, count, entries};
  return boot;
}

void mhi_safe_record_clear_count(uint32_t rec[3]) {
  const uint8_t entries = valid(rec) ? (uint8_t)((rec[1] >> 8) & 0xFFu) : 0;
  store(rec, 0, entries, false);
}

void mhi_safe_record_mark_crashed(uint32_t rec[3]) {
  const bool ok = valid(rec);
  store(rec, ok ? (uint8_t)(rec[1] & 0xFFu) : 0, ok ? (uint8_t)((rec[1] >> 8) & 0xFFu) : 0, true);
}
