#include "mhi_vanes_lr.h"
#include <string.h>

void mhi_vanes_lr_command(int value, uint8_t* db16, uint8_t* db17) {
  if (value == MHI_VANES_LR_SWING) {
    *db16 = 0x00;
    *db17 = 0x03;  // swing set flag (0x02) + swing on (0x01)
  }
  else {
    *db16 = 0x10 | (uint8_t)(value - 1);  // set flag + position, 0-based
    *db17 = 0x02;                          // swing set flag, swing off
  }
}

uint8_t mhi_3dauto_command(bool on) {
  return 0x08 | (on ? 0x04 : 0);
}

int mhi_vanes_lr_decode(uint8_t db16, uint8_t db17) {
  if (db17 & 0x01) return MHI_VANES_LR_SWING;
  return (db16 & 0x07) + 1;
}

bool mhi_3dauto_decode(uint8_t db17) {
  return (db17 & 0x04) != 0;
}

const char* mhi_vanes_lr_text(const MhiVanesLrNames* names, int value) {
  if (value >= 1 && value <= 7) return names->pos[value - 1];
  if (value == MHI_VANES_LR_SWING) return names->swing;
  return NULL;
}

int mhi_vanes_lr_parse(const MhiVanesLrNames* names, const char* payload) {
  if (!payload || !*payload) return MHI_VANES_LR_UNKNOWN;
  for (int i = 0; i < 7; i++)
    if (strcmp(payload, names->pos[i]) == 0) return i + 1;
  if (strcmp(payload, names->swing) == 0) return MHI_VANES_LR_SWING;
  if (payload[1] == '\0' && payload[0] >= '1' && payload[0] <= '8') return payload[0] - '0';
  return MHI_VANES_LR_UNKNOWN;
}
