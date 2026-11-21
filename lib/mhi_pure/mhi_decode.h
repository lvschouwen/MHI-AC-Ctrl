// The decode half of MHI_AC_Ctrl_Core::loop() (fork #64): what a valid frame
// from the AC says, compared with what was reported before, and the callbacks
// for whatever changed. It runs after the frame (never between SCK edges) and
// reports through a sink at the exact point the old inline code called its
// callbacks, in the same order: a callback may call back into the core
// (set_fan(7) from the power status, set_troom(0xff) from a heat shift) and
// the Troom jitter limit reads the clock after the earlier callbacks ran.
// Pure logic, no Arduino: the sink, the clock and the frames are the caller's.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mhi_ac_status.h"

// 3D auto's numbers, as the core's AC3Dauto (the core asserts it).
#define MHI_DECODE_DAUTO_ON 0b00000100
#define MHI_DECODE_DAUTO_OFF 0b00000000

// The Troom jitter limit of the AC's internal sensor, minTimeInternalTroom in the core (asserted there).
#define MHI_DECODE_TROOM_JITTER_MS 5000

// Everything reported before. Field widths and the "nothing reported yet"
// sentinels are those of the members they replace (reset_old_values() sets them).
struct MhiDecodeState {
  uint8_t status_power_old, status_mode_old, status_fan_old, status_vanes_old, status_troom_old, status_tsetpoint_old;
  uint8_t status_errorcode_old, status_action_old, status_silent_old, status_remote_old;
  uint8_t status_vanesLR_old, status_3Dauto_old;
  uint16_t op_kwh_old;
  uint8_t op_mode_old, op_settemp_old, op_return_air_old, op_iu_fanspeed_old, op_thi_r1_old, op_thi_r3_old, op_total_iu_run_old;
  uint16_t op_ou_eev1_old;
  // The 11 system values: 16 bits wide, so the sentinel 0xFFFF equals no one-byte reading.
  uint16_t op_outdoor_old, op_ct_old, op_comp_old, op_defrost_old, op_total_comp_run_old, op_protection_no_old;
  uint16_t op_td_old, op_tdsh_old, op_tho_r1_old, op_thi_r2_old, op_ou_fanspeed_old;
};

// Every status is reported again at its next reading.
void mhi_decode_reset_old(MhiDecodeState* s);
// The same for the 11 system values only.
void mhi_decode_reset_system(MhiDecodeState* s);

struct MhiDecodeSink {
  void* ctx;
  void (*status)(void* ctx, ACStatus status, int value);                         // cbiStatusFunction
  void (*raw)(void* ctx, ACStatus status, const uint8_t* bytes, size_t len);     // cbiRawFunction
  uint32_t (*now_ms)(void* ctx);                                                 // millis()
  void (*unknown_opdata)(void* ctx, int db9, int db10);                          // the serial line for an unknown code
};

struct MhiDecodeFrame {
  const uint8_t* mosi;   // the AC's frame, checks passed
  const uint8_t* miso;   // the frame this unit sent (its DB3 tells whether the internal room sensor is in use)
  uint8_t frame_size;    // 20 or 33
  uint8_t* erropdata_cnt;      // error operating data still to come; set by the 0x45 answer, counted down by the frame build
  uint32_t* last_troom_ms;     // when the internal Troom was last reported (jitter limit)
};

// The status half, then the operating data: decode everything a frame whose
// bytes changed says.
void mhi_decode_frame(MhiDecodeState* s, const MhiDecodeFrame* f, const MhiDecodeSink* k);
