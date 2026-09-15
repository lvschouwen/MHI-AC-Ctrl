#include "mhi_status.h"

size_t mhi_db0_changes(uint8_t db0, uint8_t* power_old, uint8_t* mode_old, MhiDb0Change out[2]) {
  const uint8_t power = db0 & 0x01;
  const uint8_t mode = db0 & 0x1c;
  const bool power_changed = power != *power_old;
  const bool mode_changed = mode != *mode_old;
  const bool switch_off = power_changed && power == 0;
  *power_old = power;
  *mode_old = mode;

  size_t count = 0;
  if (switch_off)
    out[count++] = {MHI_DB0_POWER, power};
  if (mode_changed)
    out[count++] = {MHI_DB0_MODE, mode};
  if (power_changed && !switch_off)
    out[count++] = {MHI_DB0_POWER, power};
  return count;
}

bool mhi_mode_topic_on_mode(MhiModeTopic* topic, uint8_t mode) {
  topic->mode = mode;
  return topic->power == 1;
}

MhiModeTopicAction mhi_mode_topic_on_power(MhiModeTopic* topic, uint8_t power) {
  topic->power = power;
  return power == 0 ? MHI_MODE_TOPIC_OFF : MHI_MODE_TOPIC_MODE;
}
