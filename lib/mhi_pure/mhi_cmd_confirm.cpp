#include "mhi_cmd_confirm.h"

#include "mhi_frame.h"

// The field as it reads in either direction's frame: the set flag a write
// carries is not part of the value.
static uint8_t field_value(MhiCcField f, const uint8_t* frame) {
  switch (f) {
    case MHI_CC_POWER: return frame[DB0] & 0x01;
    case MHI_CC_MODE: return frame[DB0] & 0x1c;
    case MHI_CC_TSETPOINT: return frame[DB2] & 0x7f;
    case MHI_CC_FAN: return frame[DB1] & 0x07;
    case MHI_CC_VANES_POS: return frame[DB1] & 0x30;
    case MHI_CC_VANES_SWING: return frame[DB0] & 0x40;
    case MHI_CC_FIELDS: break;
  }
  return 0;
}

// Which set flag of a MISO frame says the field was written.
static bool written(MhiCcField f, const uint8_t* miso) {
  switch (f) {
    case MHI_CC_POWER: return (miso[DB0] & 0x02) != 0;
    case MHI_CC_MODE: return (miso[DB0] & 0x20) != 0;
    case MHI_CC_TSETPOINT: return (miso[DB2] & 0x80) != 0;
    case MHI_CC_FAN: return (miso[DB1] & 0x08) != 0;
    case MHI_CC_VANES_POS: return (miso[DB1] & 0x80) != 0;
    case MHI_CC_VANES_SWING: return (miso[DB0] & 0x80) != 0;
    case MHI_CC_FIELDS: break;
  }
  return false;
}

// The set flag of a field in the AC's own frame.
static bool echo_flag(MhiCcField f, const uint8_t* mosi) {
  return written(f, mosi);
}

void mhi_cc_init(MhiCmdConfirm* c) {
  for (uint8_t i = 0; i < MHI_CC_FIELDS; i++) {
    c->want[i] = 0;
    c->frames_left[i] = 0;
    c->seen_echo[i] = false;
  }
  c->unconfirmed = 0;
}

void mhi_cc_observe(MhiCmdConfirm* c, const uint8_t* mosi) {
  for (uint8_t i = 0; i < MHI_CC_FIELDS; i++) {
    if (c->frames_left[i] == 0) continue;
    const bool flag = echo_flag((MhiCcField)i, mosi);
    if (field_value((MhiCcField)i, mosi) == c->want[i]) {
      c->frames_left[i] = 0;
    } else if (c->seen_echo[i] && !flag) {
      c->frames_left[i] = 0;  // the IR remote took the field over: not a lost write
    } else {
      if (flag) c->seen_echo[i] = true;
      if (--c->frames_left[i] == 0) {
        if (c->unconfirmed < UINT32_MAX) c->unconfirmed++;
      }
    }
  }
}

void mhi_cc_sent(MhiCmdConfirm* c, const uint8_t* miso, uint16_t watch_frames) {
  for (uint8_t i = 0; i < MHI_CC_FIELDS; i++) {
    if (!written((MhiCcField)i, miso)) continue;
    c->want[i] = field_value((MhiCcField)i, miso);
    c->frames_left[i] = watch_frames;
    c->seen_echo[i] = false;
  }
}
