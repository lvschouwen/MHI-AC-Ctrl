#include "mhi_crash_info.h"

#include <stdio.h>

static const uint32_t kMagic = 0x4D484943u;  // "MHIC"
static const uint32_t kCheckSalt = 0xA5A5A5A5u;

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

size_t mhi_crash_info_json(const uint32_t rec[MHI_CRASH_RECORD_WORDS], char* out, size_t out_len) {
  if (!out || out_len == 0) return 0;
  int r;
  if (rec && mhi_crash_record_valid(rec))
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
