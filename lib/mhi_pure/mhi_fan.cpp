#include "mhi_fan.h"

#include <string.h>

const char* mhi_fan_text(const MhiFanNames* names, int level) {
  if (level >= 1 && level <= 4) return names->levels[level - 1];
  if (level == MHI_FAN_AUTO) return names->auto_name;
  return NULL;
}

int mhi_fan_parse(const MhiFanNames* names, const char* payload) {
  if (!payload || !*payload) return MHI_FAN_NONE;
  for (int i = 0; i < 4; i++)
    if (strcmp(payload, names->levels[i]) == 0) return i + 1;
  if (strcmp(payload, names->auto_name) == 0) return MHI_FAN_AUTO;
  if (payload[1] == '\0' && payload[0] >= '1' && payload[0] <= '4') return payload[0] - '0';
  return MHI_FAN_NONE;
}
