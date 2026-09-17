// Protocol discovery tooling: turning a raw MOSI frame and unknown operating
// data into text a human can read on an MQTT topic (fork issue #4, batch A,
// spec docs/superpowers/specs/2026-09-16-phase-4-batches-design.md §3).
//
// Pure logic, no Arduino. main.cpp owns the MQTT client, the Diag switch and
// the rate limit; this file only compares, formats and parses.

#pragma once

#include <stddef.h>
#include <stdint.h>

// A standard frame is 20 bytes, the WF-RAC extended frame 33.
#define MHI_DIAG_FRAME_MAX 33
// At most this many changed bytes are named; "+" says there were more.
#define MHI_DIAG_CHANGES_MAX 6
// Room for "DB26 ff>ff " x 6, "+ ", "|" and 33 bytes as " xx", plus the NUL.
#define MHI_DIAG_TEXT_MAX 200

// The compare mask: which bits of each MOSI byte take part. The header, DB3
// (raw Troom, which dithers at a temperature boundary), the operating-data
// bytes DB9-DB12, the checksum bytes and the frame toggle in DB14 bit 2 are
// ignored; DB6 keeps only its low six bits, because bits 0xc0 echo the
// operating-data request prefix (0x40 / 0xc0). A byte that proves noisy on
// hardware is masked here, with a test.
void mhi_diag_mask_default(uint8_t* mask, size_t len);

struct MhiDiagFrame {
  uint8_t last[MHI_DIAG_FRAME_MAX];  // the last published frame
  bool have_last;                    // false until the first publish, and after a reconnect
};

// Compares frame (len bytes, 20 or 33) with the last published frame under
// mask. When a masked bit differs, or nothing was published yet, writes
// "DB5 00>10 DB13 05>07 | 6c 80 04 ..." ("first | ..." the first time) into
// out, remembers frame as published and returns the text length. Returns 0
// and leaves out and the state alone when nothing changed. out_len must be
// at least MHI_DIAG_TEXT_MAX.
size_t mhi_diag_frame_changes(MhiDiagFrame* d, const uint8_t* frame, size_t len, const uint8_t* mask, char* out, size_t out_len);

// "dd 80 01 00": the four operating-data bytes DB9..DB12. Returns the length,
// 0 when out is too small.
size_t mhi_diag_opdata_text(const uint8_t* db9, char* out, size_t out_len);

// Parses a set/OpDataRequest payload such as "c021" into the DB6 request
// prefix (only 0x40 and 0xc0 are accepted, the two the request table uses)
// and the DB9 code. Exactly four hex digits, either case. False leaves the
// outputs untouched.
bool mhi_opdata_request_parse(const char* payload, uint8_t* prefix, uint8_t* code);
