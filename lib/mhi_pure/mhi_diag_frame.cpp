#include "mhi_diag_frame.h"

#include <stdio.h>
#include <string.h>

#include "mhi_frame.h"

void mhi_diag_mask_default(uint8_t* mask, size_t len) {
  (void)mask;
  (void)len;
}

size_t mhi_diag_frame_changes(MhiDiagFrame* d, const uint8_t* frame, size_t len, const uint8_t* mask, char* out, size_t out_len) {
  (void)d; (void)frame; (void)len; (void)mask; (void)out; (void)out_len;
  return 0;
}

size_t mhi_diag_opdata_text(const uint8_t* db9, char* out, size_t out_len) {
  if (out_len < 12) return 0;  // "xx xx xx xx" and the NUL
  const int n = snprintf(out, out_len, "%02x %02x %02x %02x", db9[0], db9[1], db9[2], db9[3]);
  return n > 0 ? (size_t)n : 0;
}

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool mhi_opdata_request_parse(const char* payload, uint8_t* prefix, uint8_t* code) {
  if (payload == NULL || strlen(payload) != 4) return false;
  int v[4];
  for (int i = 0; i < 4; i++) {
    v[i] = hex_nibble(payload[i]);
    if (v[i] < 0) return false;
  }
  const uint8_t p = (uint8_t)(v[0] << 4 | v[1]);
  if (p != 0x40 && p != 0xc0) return false;  // the request table's two prefixes
  *prefix = p;
  *code = (uint8_t)(v[2] << 4 | v[3]);
  return true;
}
