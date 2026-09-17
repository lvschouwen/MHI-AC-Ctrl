#include "mhi_vanes.h"

#include <string.h>

const char* mhi_vanes_text(const MhiVanesNames* names, int value) {
  if (value >= 1 && value <= 4) return names->pos[value - 1];
  if (value == MHI_VANES_SWING) return names->swing;
  return names->unknown;
}

int mhi_vanes_parse(const MhiVanesNames* names, const char* payload) {
  if (!payload || !*payload) return MHI_VANES_UNKNOWN;
  for (int i = 0; i < 4; i++)
    if (strcmp(payload, names->pos[i]) == 0) return i + 1;
  if (strcmp(payload, names->swing) == 0) return MHI_VANES_SWING;
  // v2.8's numbers: exactly one digit 1..5.
  if (payload[1] == '\0' && payload[0] >= '1' && payload[0] <= '5') return payload[0] - '0';
  return MHI_VANES_UNKNOWN;
}
