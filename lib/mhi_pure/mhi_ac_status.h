// What the core reports about the AC to its callbacks (fork #64): the types and
// the status numbers. They live here, as the frame layout does in mhi_frame.h,
// so the pure decoder (mhi_decode) and the core use the same enums.
// Pure, no Arduino.

#pragma once

enum ACType {   // Type enum
  type_status = 0x40, type_opdata = 0x80, type_erropdata = 0xc0
};

enum ACStatus { // Status enum
  status_power = type_status, status_mode, status_fan, status_vanes, status_vanesLR, status_3Dauto, status_troom, status_tsetpoint, status_errorcode, status_action, status_silent, status_remote,
  raw_frame, raw_opdata,  // cbiRawFunction only: the whole MOSI frame, and DB9..DB12 of unknown operating data. Never published through output_P
  opdata_mode = type_opdata, opdata_kwh, opdata_tsetpoint, opdata_return_air, opdata_outdoor, opdata_tho_r1, opdata_iu_fanspeed, opdata_thi_r1, opdata_thi_r2, opdata_thi_r3,
  opdata_ou_fanspeed, opdata_total_iu_run, opdata_total_comp_run, opdata_comp, opdata_ct, opdata_td,
  opdata_tdsh, opdata_protection_no, opdata_defrost, opdata_ou_eev1, opdata_unknown,
  erropdata_mode = type_erropdata, erropdata_tsetpoint, erropdata_return_air, erropdata_thi_r1, erropdata_thi_r2, erropdata_thi_r3,
  erropdata_iu_fanspeed, erropdata_total_iu_run, erropdata_outdoor, erropdata_tho_r1, erropdata_comp, erropdata_td, erropdata_ct, erropdata_ou_fanspeed,
  erropdata_total_comp_run, erropdata_ou_eev1, erropdata_errorcode
};
