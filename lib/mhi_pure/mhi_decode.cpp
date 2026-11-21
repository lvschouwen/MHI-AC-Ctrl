#include "mhi_decode.h"

#include "mhi_action.h"
#include "mhi_frame.h"
#include "mhi_remote.h"
#include "mhi_status.h"
#include "mhi_vanes.h"
#include "mhi_vanes_lr.h"

// Moved out of MHI_AC_Ctrl_Core::loop() branch for branch (fork #64): same
// comparisons, same order, same values; only `m_cbiStatus->cbiStatusFunction`
// became a call through the sink. Operating data is `opd`, error operating data
// the else branch of each case, as before.

// A reading changed since it was last reported when it differs from the old one.
#define EMIT(k, st, v) ((k)->status((k)->ctx, (st), (v)))

void mhi_decode_reset_system(MhiDecodeState* s) {
  s->op_outdoor_old = 0xffff;
  s->op_ct_old = 0xffff;
  s->op_comp_old = 0xffff;
  s->op_defrost_old = 0xffff;
  s->op_total_comp_run_old = 0xffff;
  s->op_protection_no_old = 0xffff;
  s->op_td_old = 0xffff;
  s->op_tdsh_old = 0xffff;
  s->op_tho_r1_old = 0xffff;
  s->op_thi_r2_old = 0xffff;
  s->op_ou_fanspeed_old = 0xffff;
}

void mhi_decode_reset_old(MhiDecodeState* s) {
  // old status
  s->status_power_old = 0xff;
  s->status_mode_old = 0xff;
  s->status_fan_old = 0xff;
  s->status_vanes_old = 0xff;
  s->status_troom_old = 0xfe;
  s->status_tsetpoint_old = 0x00;
  s->status_errorcode_old = 0xff;
  s->status_action_old = 0xff;
  s->status_silent_old = 0xff;
  s->status_remote_old = 0xff;
  s->status_vanesLR_old = 0xff;
  s->status_3Dauto_old = 0xff;

  // old operating data
  s->op_kwh_old = 0xffff;
  s->op_mode_old = 0xff;
  s->op_settemp_old = 0xff;
  s->op_return_air_old = 0xff;
  s->op_iu_fanspeed_old = 0xff;
  s->op_thi_r1_old = 0x00;
  s->op_thi_r3_old = 0x00;
  s->op_total_iu_run_old = 0xff;  // a counter of 0 is published once too (sweep #33 F3)
  s->op_ou_eev1_old = 0xffff;
  mhi_decode_reset_system(s);
}

// --- status ------------------------------------------------------------------

// Only the 33-byte frame (WF-RAC) carries these.
static void decode_extended(MhiDecodeState* s, const uint8_t* m, const MhiDecodeSink* k) {
  const uint8_t vanesLRtmp = (uint8_t)mhi_vanes_lr_decode(m[DB16], m[DB17]);
  if (vanesLRtmp != s->status_vanesLR_old) {
    s->status_vanesLR_old = vanesLRtmp;
    EMIT(k, status_vanesLR, vanesLRtmp);  // 1..7, or MHI_VANES_LR_SWING == vanesLR_swing
  }

  const uint8_t dauto_tmp = mhi_3dauto_decode(m[DB17]) ? MHI_DECODE_DAUTO_ON : MHI_DECODE_DAUTO_OFF;
  if (dauto_tmp != s->status_3Dauto_old) {
    s->status_3Dauto_old = dauto_tmp;
    EMIT(k, status_3Dauto, dauto_tmp);
  }
}

// Power and Mode go out in the order mhi_status.h explains; then fan, vanes and
// whether the last change came from the IR remote.
static void decode_status_a(MhiDecodeState* s, const MhiDecodeFrame* f, const MhiDecodeSink* k) {
  const uint8_t* m = f->mosi;
  MhiDb0Change db0_changes[2];
  const size_t db0_change_count = mhi_db0_changes(m[DB0], &s->status_power_old, &s->status_mode_old, db0_changes);
  for (size_t i = 0; i < db0_change_count; i++)
    EMIT(k, db0_changes[i].field == MHI_DB0_POWER ? status_power : status_mode, db0_changes[i].value);

  const unsigned int fantmp = m[DB1] & 0x07;
  if (fantmp != s->status_fan_old) {
    s->status_fan_old = fantmp;
    EMIT(k, status_fan, s->status_fan_old);
  }

  // The IR remote leaves the echo flags clear but still reports the setting (fork #38).
  const uint8_t vanestmp = (uint8_t)mhi_vanes_decode(m[DB0], m[DB1]);
  if (vanestmp != s->status_vanes_old) {
    s->status_vanes_old = vanestmp;
    EMIT(k, status_vanes, vanestmp);
  }

  const uint8_t remotetmp = mhi_remote_last(m, f->frame_size) ? 1 : 0;
  if (remotetmp != s->status_remote_old) {
    s->status_remote_old = remotetmp;
    EMIT(k, status_remote, remotetmp);
  }
}

static void decode_troom(MhiDecodeState* s, const MhiDecodeFrame* f, const MhiDecodeSink* k) {
  const uint8_t* m = f->mosi;
  if (m[DB3] != s->status_troom_old) {
    // To avoid jitter with the fast changing AC internal temperature sensor
    if (f->miso[DB3] != 0xff) {  // not internal sensor used, just publish
      s->status_troom_old = m[DB3];
      EMIT(k, status_troom, s->status_troom_old);
      *f->last_troom_ms = 0;
    }
    else  //  internal sensor used
      if ((uint32_t)(k->now_ms(k->ctx) - *f->last_troom_ms) > MHI_DECODE_TROOM_JITTER_MS) {  // Only publish when last change was more then minTimeInternalTroom ago
        *f->last_troom_ms = k->now_ms(k->ctx);
        s->status_troom_old = m[DB3];
        EMIT(k, status_troom, s->status_troom_old);
      }
  }
}

static void decode_status_b(MhiDecodeState* s, const uint8_t* m, const MhiDecodeSink* k) {
  if (m[DB2] != s->status_tsetpoint_old) {  // Temperature setpoint
    s->status_tsetpoint_old = m[DB2];
    EMIT(k, status_tsetpoint, s->status_tsetpoint_old);
  }

  if (m[DB4] != s->status_errorcode_old) {  // error code
    s->status_errorcode_old = m[DB4];
    EMIT(k, status_errorcode, s->status_errorcode_old);
  }

  const uint8_t actiontmp = mhi_hvac_action(m[DB0], m[DB13]);
  if (actiontmp != s->status_action_old) {  // what the AC is doing: off, idle, cooling etc.
    s->status_action_old = actiontmp;
    EMIT(k, status_action, s->status_action_old);
  }
}

// --- operating data and error operating data ----------------------------------

static void op_kwh(MhiDecodeState* s, const uint8_t* m, bool opd, const MhiDecodeSink* k) {  // 0 energy-kwh n * 0.25 kWh used since power on
  if ((m[DB6] & 0x80) != 0) {
    if (opd) {
      if (((m[DB12] << 8) + (m[DB11])) != s->op_kwh_old) {
        s->op_kwh_old = (m[DB12] << 8) + (m[DB11]);
        EMIT(k, opdata_kwh, s->op_kwh_old);
      }
    }
  }
}

static void op_mode(MhiDecodeState* s, const uint8_t* m, bool opd, const MhiDecodeSink* k) {  // 1 MODE
  if ((m[DB6] & 0x80) != 0) {
    if (opd) {
      if ((m[DB10] != s->op_mode_old)) {
        s->op_mode_old = m[DB10];
        EMIT(k, opdata_mode, (s->op_mode_old & 0x0f) << 2);
      }
    }
    else
      EMIT(k, erropdata_mode, (m[DB10] & 0x0f) << 2);
  }
}

static void op_settemp(MhiDecodeState* s, const uint8_t* m, const MhiDecodeSink* k) {  // 2 SET-TEMP
  if ((m[DB6] & 0x80) != 0) {
    if (m[DB10] == 0x13) {
      if (m[DB11] != s->op_settemp_old) {
        s->op_settemp_old = m[DB11];
        EMIT(k, opdata_tsetpoint, s->op_settemp_old);
      }
    }
    else if (m[DB10] == 0x33)
      EMIT(k, erropdata_tsetpoint, m[DB11]);
  }
}

static void op_thi_r1_r2(MhiDecodeState* s, const uint8_t* m, bool opd, const MhiDecodeSink* k) {  // 5 THI-R1 or 6 THI-R2
  if ((m[DB6] & 0x80) != 0) {  // 5 THI-R1
    if ((m[DB10] & 0x30) == 0x20) {
      if (m[DB11] != s->op_thi_r1_old) {
        s->op_thi_r1_old = m[DB11];
        EMIT(k, opdata_thi_r1, s->op_thi_r1_old);
      }
    }
    else
      EMIT(k, erropdata_thi_r1, m[DB11]);
  }
  else {  // 6 THI-R2
    if (opd) {
      if (m[DB11] != s->op_thi_r2_old) {
        s->op_thi_r2_old = m[DB11];
        EMIT(k, opdata_thi_r2, s->op_thi_r2_old);
      }
    }
    else
      EMIT(k, erropdata_thi_r2, m[DB11]);
  }
}

static void op_thi_r3(MhiDecodeState* s, const uint8_t* m, bool opd, const MhiDecodeSink* k) {  // 7 THI-R3
  if ((m[DB6] & 0x80) != 0) {
    if (opd) {
      if (m[DB11] != s->op_thi_r3_old) {
        s->op_thi_r3_old = m[DB11];
        EMIT(k, opdata_thi_r3, s->op_thi_r3_old);
      }
    }
    else
      EMIT(k, erropdata_thi_r3, m[DB11]);
  }
}

static void op_return_air_outdoor(MhiDecodeState* s, const uint8_t* m, bool opd, const MhiDecodeSink* k) {  // 3 RETURN-AIR or 21 OUTDOOR
  if ((m[DB6] & 0x80) != 0) {  // 3 RETURN-AIR
    if ((m[DB10] & 0x30) == 0x20) {  // operating Data
      if (m[DB11] != s->op_return_air_old) {
        s->op_return_air_old = m[DB11];
        EMIT(k, opdata_return_air, s->op_return_air_old);
      }
    }
    else
      EMIT(k, erropdata_return_air, m[DB11]);
  }
  else {  // 21 OUTDOOR
    if (opd) {
      if (m[DB11] != s->op_outdoor_old) {
        s->op_outdoor_old = m[DB11];
        EMIT(k, opdata_outdoor, s->op_outdoor_old);
      }
    }
    else
      EMIT(k, erropdata_outdoor, m[DB11]);
  }
}

static void op_fanspeed(MhiDecodeState* s, const uint8_t* m, bool opd, const MhiDecodeSink* k) {  // 8 IU-FANSPEED or 34 OU-FANSPEED
  if ((m[DB6] & 0x80) != 0) {  // 8 IU-FANSPEED
    if (opd) {
      if (m[DB10] != s->op_iu_fanspeed_old) {
        s->op_iu_fanspeed_old = m[DB10];
        EMIT(k, opdata_iu_fanspeed, s->op_iu_fanspeed_old & 0x0f);
      }
    }
    else
      EMIT(k, erropdata_iu_fanspeed, m[DB10] & 0x0f);
  }
  else {  // 34 OU-FANSPEED
    if (opd) {
      if (m[DB10] != s->op_ou_fanspeed_old) {
        s->op_ou_fanspeed_old = m[DB10];
        EMIT(k, opdata_ou_fanspeed, s->op_ou_fanspeed_old & 0x0f);
      }
    }
    else
      EMIT(k, erropdata_ou_fanspeed, m[DB10] & 0x0f);
  }
}

static void op_run_hours(MhiDecodeState* s, const uint8_t* m, bool opd, const MhiDecodeSink* k) {  // 12 TOTAL-IU-RUN or 37 TOTAL-COMP-RUN
  if ((m[DB6] & 0x80) != 0) {  // 12 TOTAL-IU-RUN
    if (opd) {
      if (m[DB11] != s->op_total_iu_run_old) {
        s->op_total_iu_run_old = m[DB11];
        EMIT(k, opdata_total_iu_run, s->op_total_iu_run_old);
      }
    }
    else
      EMIT(k, erropdata_total_iu_run, m[DB11]);
  }
  else {  // 37 TOTAL-COMP-RUN
    if (m[DB10] == 0x11) {
      if (m[DB11] != s->op_total_comp_run_old) {
        s->op_total_comp_run_old = m[DB11];
        EMIT(k, opdata_total_comp_run, s->op_total_comp_run_old);
      }
    }
    else
      EMIT(k, erropdata_total_comp_run, m[DB11]);
  }
}

static void op_tho_r1(MhiDecodeState* s, const uint8_t* m, bool opd, const MhiDecodeSink* k) {  // 22 ThO-R1
  if ((m[DB6] & 0x80) == 0) {
    if (opd) {  // operating data
      if (m[DB11] != s->op_tho_r1_old) {
        s->op_tho_r1_old = m[DB11];
        EMIT(k, opdata_tho_r1, s->op_tho_r1_old);
      }
    }
    else
      EMIT(k, erropdata_tho_r1, m[DB11]);
  }
}

static void op_comp(MhiDecodeState* s, const uint8_t* m, bool opd, const MhiDecodeSink* k) {  // 24 COMP
  if ((m[DB6] & 0x80) == 0) {
    if (opd) {
      if ((m[DB10] << 8 | m[DB11]) != s->op_comp_old) {
        s->op_comp_old = m[DB10] << 8 | m[DB11];
        EMIT(k, opdata_comp, s->op_comp_old & 0x0fff);
      }
    }
    else
      EMIT(k, erropdata_comp, (m[DB10] << 8 | m[DB11]) & 0x0fff);
  }
}

static void op_td(MhiDecodeState* s, const uint8_t* m, bool opd, const MhiDecodeSink* k) {  // 27 Td
  if ((m[DB6] & 0x80) == 0) {
    if (opd) {
      if (m[DB11] != s->op_td_old) {
        s->op_td_old = m[DB11];
        EMIT(k, opdata_td, s->op_td_old);
      }
    }
    else
      EMIT(k, erropdata_td, m[DB11]);
  }
}

static void op_ct(MhiDecodeState* s, const uint8_t* m, bool opd, const MhiDecodeSink* k) {  // 29 CT
  if ((m[DB6] & 0x80) == 0) {
    if (opd) {
      if (m[DB11] != s->op_ct_old) {
        s->op_ct_old = m[DB11];
        EMIT(k, opdata_ct, s->op_ct_old);
      }
    }
    else
      EMIT(k, erropdata_ct, m[DB11]);
  }
}

static void op_tdsh(MhiDecodeState* s, const uint8_t* m, bool opd, const MhiDecodeSink* k) {  // 32 TDSH
  if ((m[DB6] & 0x80) == 0) {
    if (opd) {
      if (m[DB11] != s->op_tdsh_old) {
        s->op_tdsh_old = m[DB11];
        EMIT(k, opdata_tdsh, s->op_tdsh_old);  // main.cpp halves it (fork #42)
      }
    }
  }
}

static void op_protection_no(MhiDecodeState* s, const uint8_t* m, bool opd, const MhiDecodeSink* k) {  // 33 PROTECTION-No
  if ((m[DB6] & 0x80) == 0) {
    if (opd) {
      if (m[DB11] != s->op_protection_no_old) {
        s->op_protection_no_old = m[DB11];
        EMIT(k, opdata_protection_no, s->op_protection_no_old);
      }
    }
  }
}

static void op_defrost(MhiDecodeState* s, const uint8_t* m, bool opd, const MhiDecodeSink* k) {  // 36 DEFROST
  if ((m[DB6] & 0x80) == 0) {
    if (opd) {
      if (m[DB10] != s->op_defrost_old) {
        s->op_defrost_old = m[DB10];
        EMIT(k, opdata_defrost, s->op_defrost_old & 0b1);
      }
    }
  }
}

static void op_ou_eev(MhiDecodeState* s, const uint8_t* m, bool opd, const MhiDecodeSink* k) {  // 38 OU-EEV
  if ((m[DB6] & 0x80) == 0) {
    if (opd) {
      if ((m[DB12] << 8 | m[DB11]) != s->op_ou_eev1_old) {
        s->op_ou_eev1_old = m[DB12] << 8 | m[DB11];
        EMIT(k, opdata_ou_eev1, s->op_ou_eev1_old);
      }
    }
    else
      EMIT(k, erropdata_ou_eev1, m[DB12] << 8 | m[DB11]);
  }
}

// last error number or count of following error operating data
static void op_error_number(const MhiDecodeFrame* f, const MhiDecodeSink* k) {
  const uint8_t* m = f->mosi;
  if ((m[DB6] & 0x80) != 0) {
    if (m[DB10] == 0x11) {  // last error number
      EMIT(k, erropdata_errorcode, m[DB11]);
    }
    else if (m[DB10] == 0x12) {  // count of following error operating data
      *f->erropdata_cnt = m[DB11] + 4;
    }
  }
}

// Silent operation (fork #4): reported unasked after a remote press, and on request
static void op_silent(MhiDecodeState* s, const uint8_t* m, const MhiDecodeSink* k) {
  if (m[DB10] == 0x80) {  // the answer's type byte; 'dd 80 20 00' on, 'dd 80 00 00' off (Uitkijk, 16 Sep 2026)
    const uint8_t silenttmp = (m[DB11] & 0x20) != 0;
    if (silenttmp != s->status_silent_old) {
      s->status_silent_old = silenttmp;
      EMIT(k, status_silent, silenttmp);
    }
  }
}

static void decode_opdata(MhiDecodeState* s, const MhiDecodeFrame* f, const MhiDecodeSink* k) {
  const uint8_t* m = f->mosi;
  // Evaluate Operating Data and Error Operating Data
  const bool opd = (m[DB10] & 0x30) == 0x10;

  switch (m[DB9]) {
    case 0x94: op_kwh(s, m, opd, k); break;
    case 0x02: op_mode(s, m, opd, k); break;
    case 0x05: op_settemp(s, m, k); break;
    case 0x81: op_thi_r1_r2(s, m, opd, k); break;
    case 0x87: op_thi_r3(s, m, opd, k); break;
    case 0x80: op_return_air_outdoor(s, m, opd, k); break;
    case 0x1f: op_fanspeed(s, m, opd, k); break;
    case 0x1e: op_run_hours(s, m, opd, k); break;
    case 0x82: op_tho_r1(s, m, opd, k); break;
    case 0x11: op_comp(s, m, opd, k); break;
    case 0x85: op_td(s, m, opd, k); break;
    case 0x90: op_ct(s, m, opd, k); break;
    case 0xb1: op_tdsh(s, m, opd, k); break;
    case 0x7c: op_protection_no(s, m, opd, k); break;
    case 0x0c: op_defrost(s, m, opd, k); break;
    case 0x13: op_ou_eev(s, m, opd, k); break;
    case 0x45: op_error_number(f, k); break;
    case 0xdd: op_silent(s, m, k); break;
    case 0x00:  // dummy
      break;
    case 0xff:  // default
      break;
    default:    // unknown operating data
      k->raw(k->ctx, raw_opdata, &m[DB9], 4);  // the value bytes, which the number below drops
      EMIT(k, opdata_unknown, m[DB10] << 8 | m[DB9]);
      k->unknown_opdata(k->ctx, m[DB9], m[DB10]);
  }
}

void mhi_decode_frame(MhiDecodeState* s, const MhiDecodeFrame* f, const MhiDecodeSink* k) {
  if (f->frame_size == 33) decode_extended(s, f->mosi, k);  // Only for framesize 33 (WF-RAC)
  // evaluate status
  decode_status_a(s, f, k);
  decode_troom(s, f, k);
  decode_status_b(s, f->mosi, k);
  decode_opdata(s, f, k);
}
