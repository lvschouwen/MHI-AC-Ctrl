#include "mhi_crash_info.h"

#include <stdio.h>

static const uint32_t kMagic = 0x4D484943u;  // "MHIC"
static const uint32_t kSnapMagic = 0x4D48494Eu;  // "MHIN"
static const uint32_t kCheckSalt = 0xA5A5A5A5u;

// The longest CrashInfo payload; the buffer must hold it and its NUL.
static_assert(sizeof("{\"exccause\":4294967295,\"reason\":4294967295,\"epc1\":\"0xffffffff\",\"excvaddr\":\"0xffffffff\","
                     "\"power\":255,\"mode\":255,\"tset\":255,\"troom\":255}") <= MHI_CRASH_INFO_JSON_MAX,
              "MHI_CRASH_INFO_JSON_MAX holds the largest CrashInfo payload");

static uint32_t check_word(const uint32_t rec[MHI_CRASH_RECORD_WORDS]) {
  return rec[0] ^ rec[1] ^ rec[2] ^ rec[3] ^ rec[4] ^ kCheckSalt;
}

void mhi_crash_record_make(uint32_t rec[MHI_CRASH_RECORD_WORDS], uint32_t reason, uint32_t exccause, uint32_t epc1,
                           uint32_t excvaddr) {
  rec[0] = kMagic;
  rec[1] = reason;
  rec[2] = exccause;
  rec[3] = epc1;
  rec[4] = excvaddr;
  rec[5] = check_word(rec);
}

bool mhi_crash_record_valid(const uint32_t rec[MHI_CRASH_RECORD_WORDS]) {
  return rec[0] == kMagic && rec[5] == check_word(rec);
}

void mhi_crash_snapshot_make(uint32_t snap[MHI_CRASH_SNAPSHOT_WORDS], uint8_t power, uint8_t mode, uint8_t tsetpoint,
                             uint8_t troom) {
  snap[0] = kSnapMagic;
  snap[1] = (uint32_t)power | ((uint32_t)mode << 8) | ((uint32_t)tsetpoint << 16) | ((uint32_t)troom << 24);
  snap[2] = snap[0] ^ snap[1] ^ kCheckSalt;
}

bool mhi_crash_snapshot_valid(const uint32_t snap[MHI_CRASH_SNAPSHOT_WORDS]) {
  return snap[0] == kSnapMagic && snap[2] == (snap[0] ^ snap[1] ^ kCheckSalt);
}

size_t mhi_crash_info_json(const uint32_t rec[MHI_CRASH_RECORD_WORDS], char* out, size_t out_len) {
  return mhi_crash_info_json_ex(rec, NULL, out, out_len);
}

size_t mhi_crash_info_json_ex(const uint32_t rec[MHI_CRASH_RECORD_WORDS], const uint32_t snap[MHI_CRASH_SNAPSHOT_WORDS],
                              char* out, size_t out_len) {
  if (!out || out_len == 0) return 0;
  int r;
  if (rec && mhi_crash_record_valid(rec) && snap && mhi_crash_snapshot_valid(snap))
    r = snprintf(out, out_len,
                 "{\"exccause\":%lu,\"reason\":%lu,\"epc1\":\"0x%08lx\",\"excvaddr\":\"0x%08lx\","
                 "\"power\":%lu,\"mode\":%lu,\"tset\":%lu,\"troom\":%lu}",
                 (unsigned long)rec[2], (unsigned long)rec[1], (unsigned long)rec[3], (unsigned long)rec[4],
                 (unsigned long)(snap[1] & 0xff), (unsigned long)((snap[1] >> 8) & 0xff),
                 (unsigned long)((snap[1] >> 16) & 0xff), (unsigned long)((snap[1] >> 24) & 0xff));
  else if (rec && mhi_crash_record_valid(rec))
    r = snprintf(out, out_len, "{\"exccause\":%lu,\"reason\":%lu,\"epc1\":\"0x%08lx\",\"excvaddr\":\"0x%08lx\"}",
                 (unsigned long)rec[2], (unsigned long)rec[1], (unsigned long)rec[3], (unsigned long)rec[4]);
  else
    r = snprintf(out, out_len, "{\"exccause\":-1}");
  if (r < 0 || (size_t)r >= out_len) {
    out[0] = '\0';
    return 0;
  }
  return (size_t)r;
}
