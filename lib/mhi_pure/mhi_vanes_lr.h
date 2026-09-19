// Left/right louvers and 3D auto, decoupled (fork #20; spec §2.1). Pure
// logic, no Arduino.
#pragma once
#include <stdint.h>

#define MHI_VANES_LR_UNKNOWN 0  // mhi_vanes_lr_parse's failure value
#define MHI_VANES_LR_SWING 8    // the core's ACVanesLR::vanesLR_swing

// DB16/DB17 for a position (1..7) or swing. Raises only the swing set flag
// (DB17 0x02); the 3D-auto set flag (0x08) is never touched.
// Both outputs are zeroed first on every path. A value that is neither 1..7
// nor MHI_VANES_LR_SWING is refused: false, with both bytes 0 (no set flag),
// because clamping it would move the louver somewhere nobody asked for.
bool mhi_vanes_lr_command(int value, uint8_t* db16, uint8_t* db17);

// DB17 for 3D auto on/off. Raises only the 3D-auto set flag (0x08); the
// swing set flag (0x02) is never touched.
uint8_t mhi_3dauto_command(bool on);

// Decodes DB16/DB17 into 1..7 or MHI_VANES_LR_SWING, masking the AC's echo
// of both set flags (DB16 & 0x10, DB17 & 0x0a) after a write.
int mhi_vanes_lr_decode(uint8_t db16, uint8_t db17);

// Decodes DB17 bit 2 into 3D auto's state; also unaffected by the echoes.
bool mhi_3dauto_decode(uint8_t db17);

struct MhiVanesLrNames {
  const char* pos[7];  // 1..5 positions as seen on the unit (1 leftmost .. 5 rightmost), 6 wide and 7 spot: spread modes, not positions
  const char* swing;
};

// 1..7 -> the position's name, MHI_VANES_LR_SWING -> swing, else NULL.
const char* mhi_vanes_lr_text(const MhiVanesLrNames* names, int value);

// A set/VanesLR payload: one of the eight names, or "1".."8" (8 = swing).
// MHI_VANES_LR_UNKNOWN when it is none of them (NULL and "" included).
int mhi_vanes_lr_parse(const MhiVanesLrNames* names, const char* payload);
