#include "mhi_diag_frame.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "mhi_frame.h"

void mhi_diag_mask_default(uint8_t* mask, size_t len) {
  memset(mask, 0xff, len);
  for (size_t i = SB0; i <= SB2 && i < len; i++) mask[i] = 0x00;
  if (len > DB6) mask[DB6] = 0x3f;   // bits 0xc0 echo the request prefix
  for (size_t i = DB9; i <= DB12 && i < len; i++) mask[i] = 0x00;
  if (len > DB14) mask[DB14] = (uint8_t)~0x04;  // the frame toggle
  if (len > CBH) mask[CBH] = 0x00;
  if (len > CBL) mask[CBL] = 0x00;
  if (len > CBL2) mask[CBL2] = 0x00;
}

// Byte names as in mhi_frame.h. out holds 5 characters ("DB26" and the NUL).
static void byte_name(size_t i, char* out) {
  if (i <= SB2) snprintf(out, 5, "SB%u", (unsigned)i);
  else if (i <= DB14) snprintf(out, 5, "DB%u", (unsigned)(i - (DB0)));
  else if (i == CBH) snprintf(out, 5, "CBH");
  else if (i == CBL) snprintf(out, 5, "CBL");
  else if (i <= DB26) snprintf(out, 5, "DB%u", (unsigned)(i - (DB15) + 15));
  else snprintf(out, 5, "CB2");
}

// Appends to out without ever writing past out_len; n stays below out_len.
static void append(char* out, size_t out_len, size_t* n, const char* fmt, ...) {
  if (*n >= out_len) return;
  va_list ap;
  va_start(ap, fmt);
  const int w = vsnprintf(out + *n, out_len - *n, fmt, ap);
  va_end(ap);
  if (w < 0) return;
  *n = ((size_t)w < out_len - *n) ? *n + (size_t)w : out_len - 1;
}

size_t mhi_diag_frame_changes(MhiDiagFrame* d, const uint8_t* frame, size_t len, const uint8_t* mask, char* out, size_t out_len) {
  if (len > MHI_DIAG_FRAME_MAX || out_len < MHI_DIAG_TEXT_MAX) return 0;
  size_t n = 0;
  if (d->have_last) {
    size_t changed = 0;
    for (size_t i = 0; i < len; i++) {
      if (((frame[i] ^ d->last[i]) & mask[i]) == 0) continue;
      changed++;
      if (changed > MHI_DIAG_CHANGES_MAX) continue;
      char name[5];
      byte_name(i, name);
      append(out, out_len, &n, "%s %02x>%02x ", name, d->last[i], frame[i]);
    }
    if (changed == 0) return 0;
    if (changed > MHI_DIAG_CHANGES_MAX) append(out, out_len, &n, "+ ");
  } else {
    append(out, out_len, &n, "first ");
  }
  append(out, out_len, &n, "|");
  for (size_t i = 0; i < len; i++) append(out, out_len, &n, " %02x", frame[i]);
  memcpy(d->last, frame, len);
  d->have_last = true;
  return n;
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
