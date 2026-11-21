// MHI-AC-Ctrol-core
// implements the core functions (read & write SPI)

#include "MHI-AC-Ctrl-core.h"
#include "mhi_action.h"
#include "mhi_remote.h"
#include "mhi_status.h"
#include "mhi_vanes.h"
#include "mhi_vanes_lr.h"

// The checksum helpers moved to lib/mhi_pure/mhi_frame.cpp, where they can be
// tested on the build machine.

void MHI_AC_Ctrl_Core::reset_old_values() {  // used e.g. when MQTT connection to broker is lost, to re-output data
  mhi_decode_reset_old(&dec);
}

// The group root starts empty and a new publisher must fill it in its first
// operating-data cycle (fork #22 spec §4.3). The old sentinels were valid
// readings for several of these (DEFROST "Off" was 0x00, OUTDOOR 0xff is
// 40.25 degC), so those values were never republished after a connect.
void MHI_AC_Ctrl_Core::reset_system_values() {
  mhi_decode_reset_system(&dec);
}

void MHI_AC_Ctrl_Core::init(bool drive_miso) {
  //MeasureFrequency(m_cbiStatus);
  pinMode(SCK_PIN, INPUT);
  pinMode(MOSI_PIN, INPUT);
  // An input when something else drives MISO (see mhi_miso_may_be_driven).
  // loop() still calls digitalWrite on it, which only sets the output latch.
  pinMode(MISO_PIN, drive_miso ? OUTPUT : INPUT);
  MHI_AC_Ctrl_Core::reset_old_values();
}

void MHI_AC_Ctrl_Core::set_power(boolean power) {
  mhi_cq_set(&cq, MHI_CQ_POWER, 0b10 | power);
}

void MHI_AC_Ctrl_Core::set_mode(ACMode mode) {
  mhi_cq_set(&cq, MHI_CQ_MODE, 0b00100000 | mode);
}

void MHI_AC_Ctrl_Core::set_tsetpoint(uint tsetpoint) {
  mhi_cq_set(&cq, MHI_CQ_TSETPOINT, 0b10000000 | tsetpoint);
}

void MHI_AC_Ctrl_Core::set_fan(uint fan) {
  mhi_cq_set(&cq, MHI_CQ_FAN, 0b00001000 | fan);
}

void MHI_AC_Ctrl_Core::set_3Dauto(AC3Dauto Dauto) {
  mhi_cq_set(&cq, MHI_CQ_3DAUTO, mhi_3dauto_command(Dauto == Dauto_on));
}

void MHI_AC_Ctrl_Core::set_vanes(uint vanes) {
  if (vanes == vanes_swing) {
    mhi_cq_set(&cq, MHI_CQ_VANES0, 0b11000000); // enable swing
  }
  else {
    mhi_cq_set(&cq, MHI_CQ_VANES0, 0b10000000); // disable swing
    mhi_cq_set(&cq, MHI_CQ_VANES1, 0b10000000 | ((vanes - 1) << 4));
  }
}

void MHI_AC_Ctrl_Core::set_vanesLR(uint vanesLR) {
  uint8_t db16, db17;
  // A value the command does not know is dropped here: leaving new_VanesLR*
  // alone keeps a valid command that is still queued instead of wiping it.
  if (!mhi_vanes_lr_command((int)vanesLR, &db16, &db17)) return;
  mhi_cq_set(&cq, MHI_CQ_VANESLR1, db16);  // ORed into MISO_frame[DB16] in loop()
  mhi_cq_set(&cq, MHI_CQ_VANESLR0, db17);  // ORed into MISO_frame[DB17] in loop()
}

void MHI_AC_Ctrl_Core::set_passive_mode(bool newPassiveMode) {
  passiveMode = newPassiveMode;
}

void MHI_AC_Ctrl_Core::request_ErrOpData() {
  mhi_cq_set(&cq, MHI_CQ_ERROPDATA, 1);
}

void MHI_AC_Ctrl_Core::request_OpData(byte prefix, byte code) {
  request_opdata_prefix = prefix;
  request_opdata_code = code;
  request_opdata_pending = true;  // a second command before it is sent replaces it
}

void MHI_AC_Ctrl_Core::set_silent(bool on) {
  mhi_cq_set(&cq, MHI_CQ_SILENT, on ? 2 : 1);  // 1 + the DB10 value, so off is not 0; a second command before it is sent replaces it
}

void MHI_AC_Ctrl_Core::set_troom(byte troom) {
  new_Troom = troom;
}

float MHI_AC_Ctrl_Core::get_troom_offset() {
  return Troom_offset;
}

void MHI_AC_Ctrl_Core::set_troom_offset(float offset) {
  Troom_offset = offset;
}

void MHI_AC_Ctrl_Core::set_frame_size(byte framesize) {
  if (framesize == 20 || framesize == 33)
    frameSize = framesize;
}

// The decoder's sink: the callbacks, the clock and the serial line (fork #64).
static void sink_status(void* ctx, ACStatus status, int value) {
  static_cast<CallbackInterface_Status*>(ctx)->cbiStatusFunction(status, value);
}
static void sink_raw(void* ctx, ACStatus status, const uint8_t* bytes, size_t len) {
  static_cast<CallbackInterface_Status*>(ctx)->cbiRawFunction(status, bytes, len);
}
static uint32_t sink_now_ms(void*) {
  return millis();
}
static void sink_unknown_opdata(void*, int db9, int db10) {
  Serial.printf_P(PSTR("Unknown operating data, MOSI_frame[DB9]=%i MOSI_frame[D10]=%i\n"), db9, db10);
}

// mhi_decode spells these numbers itself, because lib/mhi_pure cannot include this header.
static_assert(MHI_DECODE_DAUTO_ON == Dauto_on && MHI_DECODE_DAUTO_OFF == Dauto_off, "mhi_decode's 3D auto numbers are the core's AC3Dauto");
static_assert(MHI_DECODE_TROOM_JITTER_MS == minTimeInternalTroom, "mhi_decode's Troom jitter limit is minTimeInternalTroom");

int MHI_AC_Ctrl_Core::loop(uint max_time_ms) {
  const byte opdataCnt = sizeof(opdata) / sizeof(byte) / 2;
  static byte opdataNo = 0;               //
  long startMillis = millis();             // start time of this loop run
  byte MOSI_byte;                         // received MOSI byte
  bool new_datapacket_received = false;   // indicated that a new frame was received
  static byte erropdataCnt = 0;           // number of expected error operating data
  static bool doubleframe = false;
  static int frame = 1;
  static byte MOSI_frame[33];
  //                            sb0   sb1   sb2   db0   db1   db2   db3   db4   db5   db6   db7   db8   db9  db10  db11  db12  db13  db14  chkH  chkL  db15  db16  db17  db18  db19  db20  db21  db22  db23  db24  db25  db26  chk2L
  static byte MISO_frame[] = { 0xA9, 0x00, 0x07, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x22 };

  static uint32_t lastTroomInternalMillis = 0; // remember when Troom internal has changed
  
  if (frameSize == 33)
    MISO_frame[0] = 0xAA;

  call_counter++;
  int SCKMillis = millis();               // time of last SCK low level
  while (millis() - SCKMillis < 5) {      // wait for 5ms stable high signal to detect a frame start
    if (!digitalRead(SCK_PIN))
      SCKMillis = millis();
    if (millis() - startMillis > max_time_ms)
      return err_msg_timeout_SCK_low;       // SCK stuck@ low error detection
  }
  // build the next MISO frame
  mhi_cq_begin(&cq);                      // nothing of the queue is in flight until this build takes it (fork #48)

  doubleframe = !doubleframe;             // toggle every frame
  MISO_frame[DB14] = doubleframe << 2;    // MISO_frame[DB14] bit2 toggles with every frame
  
  // Requesting all different opdata's is an opdata cycle. A cycle will take 20s.
  // With the current 20 different opdata's, every opdata request will take 1sec (interval).
  // If there are only 5 different opdata's defined, these 5 will be spread about the 20s cycle. The interval will increase.
  // requesting a new opdata will always start at a doubleframe start
  if ((frame > (NoFramesPerOpDataCycle / opdataCnt)) && doubleframe ) {    // interval for requesting new opdata depending on de number of opdata requests
    frame = 1;                              // start requesting new OpData
  }

  if (frame++ <= 2) {                       // use opdata request only for 2 subsequent frames
    if (doubleframe) {                      // start when MISO_frame[DB14] bit2 is set
      if (erropdataCnt == 0) {
        if (request_opdata_pending && !mhi_cq_pending(&cq, MHI_CQ_ERROPDATA) && !mhi_cq_pending(&cq, MHI_CQ_SILENT)) {
          // The 0x80 command slot below would overwrite DB6/DB9 in this same
          // pair; the probe waits for the next window instead of being lost.
          // The probe takes this slot instead of the next code; opdataNo is
          // not advanced, so the cycle resumes with the code it would have sent.
          MISO_frame[DB6] = request_opdata_prefix;
          MISO_frame[DB9] = request_opdata_code;
          request_opdata_pending = false;
        }
        else {
          MISO_frame[DB6] = pgm_read_word(opdata + opdataNo);
          MISO_frame[DB9] = pgm_read_word(opdata + opdataNo) >> 8;
          opdataNo = (opdataNo + 1) % opdataCnt;
        }
      }

    }
  }
  else  // reset OpData request; DB10 idles with it, so a Silent parameter is never on the wire without its command
  {
    MISO_frame[DB6] = 0x80;
    MISO_frame[DB9] = 0xff;
    MISO_frame[DB10] = 0xff;
  }
  
  if (doubleframe) {                        // and the other MISO data changes are updated when MISO_frame[DB14] bit2 is set
    MISO_frame[DB0] = 0x00;
    MISO_frame[DB1] = 0x00;
    MISO_frame[DB2] = 0x00;

    if (erropdataCnt > 0) {                 // error operating data available
      MISO_frame[DB6] = 0x80;
      MISO_frame[DB9] = 0xff;
      erropdataCnt--;
    }

    // set Power, Mode, Tsetpoint, Fan, Vanes
    // Taken, not cleared: they stay queued until a frame that carried them has
    // passed its checks (mhi_cq_commit() below), so a failed frame loses nothing.
    MISO_frame[DB0] = mhi_cq_take(&cq, MHI_CQ_POWER);

    MISO_frame[DB0] |= mhi_cq_take(&cq, MHI_CQ_MODE);

    MISO_frame[DB2] = mhi_cq_take(&cq, MHI_CQ_TSETPOINT);

    MISO_frame[DB1] = mhi_cq_take(&cq, MHI_CQ_FAN);

    MISO_frame[DB0] |= mhi_cq_take(&cq, MHI_CQ_VANES0);
    MISO_frame[DB1] |= mhi_cq_take(&cq, MHI_CQ_VANES1);

    if (mhi_cq_take(&cq, MHI_CQ_ERROPDATA)) {
      MISO_frame[DB6] = 0x80;
      MISO_frame[DB9] = 0x45;
    }
    else if (mhi_cq_pending(&cq, MHI_CQ_SILENT) && erropdataCnt == 0) {  // the same 0x80 command slot; waits one pair behind ErrOpData and for an error-data dump to end
      const byte silent = mhi_cq_take(&cq, MHI_CQ_SILENT);              // 0: tried too often, dropped
      if (silent != 0) {
        MISO_frame[DB6] = 0x80;
        MISO_frame[DB9] = 0x21;
        MISO_frame[DB10] = silent - 1;
        if (!request_opdata_pending)    // read it back so the Silent topic confirms the write; a user's probe is not replaced
          request_OpData(0xc0, 0xdd);
      }
    }
  }

  MISO_frame[DB3] = new_Troom;  // from MQTT or DS18x20

  uint16_t checksum = mhi_calc_checksum(MISO_frame);
  MISO_frame[CBH] = highByte(checksum);
  MISO_frame[CBL] = lowByte(checksum);

  if (frameSize == 33) { // Only for framesize 33 (WF-RAC)
    MISO_frame[DB16] = 0;
    MISO_frame[DB16] |= mhi_cq_take(&cq, MHI_CQ_VANESLR1);
    MISO_frame[DB17] = 0;
    MISO_frame[DB17] |= mhi_cq_take(&cq, MHI_CQ_VANESLR0);
    MISO_frame[DB17] |= mhi_cq_take(&cq, MHI_CQ_3DAUTO);

    checksum = mhi_calc_checksum_frame33(MISO_frame);
    MISO_frame[CBL2] = lowByte(checksum);
  }
  // read/write MOSI/MISO frame
  for (uint8_t byte_cnt = 0; byte_cnt < frameSize; byte_cnt++) { // read and write a data packet of 20 bytes
    MOSI_byte = 0;
    byte bit_mask = 1;
    for (uint8_t bit_cnt = 0; bit_cnt < 8; bit_cnt++) { // read and write 1 byte
      SCKMillis = millis();
      while (digitalRead(SCK_PIN)) { // wait for falling edge
        if (millis() - startMillis > max_time_ms)
          return err_msg_timeout_SCK_high;       // SCK stuck@ high error detection
      } 
      if ((MISO_frame[byte_cnt] & bit_mask) || passiveMode)
        digitalWrite(MISO_PIN, 1);
      else
        digitalWrite(MISO_PIN, 0);
      while (!digitalRead(SCK_PIN)) { // wait for rising edge
        if (millis() - startMillis > max_time_ms)
          return err_msg_timeout_SCK_low;        // SCK stuck@ low error detection
      }
      if (digitalRead(MOSI_PIN))
        MOSI_byte += bit_mask;
      bit_mask = bit_mask << 1;
    }
    if (MOSI_frame[byte_cnt] != MOSI_byte) {
      new_datapacket_received = true;
      MOSI_frame[byte_cnt] = MOSI_byte;
    }
  }

  checksum = mhi_calc_checksum(MOSI_frame);
  if (((MOSI_frame[SB0] & 0xfe) != 0x6c) | (MOSI_frame[SB1] != 0x80) | (MOSI_frame[SB2] != 0x04))
    return err_msg_invalid_signature;
  if ((MOSI_frame[CBH] << 8 | MOSI_frame[CBL]) != checksum)
    return err_msg_invalid_checksum;

  if (frameSize == 33) { // Only for framesize 33 (WF-RAC)
    checksum = mhi_calc_checksum_frame33(MOSI_frame);
    if (MOSI_frame[CBL2] != lowByte(checksum)) 
      return err_msg_invalid_checksum;
  }

  // The AC has seen a whole frame that passed both checks: what this build put
  // on the wire is done with (fork #48). Before the first callback, which may
  // queue a new command (set_fan(7) from the power status).
  mhi_cq_commit(&cq);
  // CmdUnconfirmed (fork #60): the AC's frame of this exchange predates the write, so it ages the
  // watches before this frame's own writes start theirs. Passive mode puts nothing on the wire.
  // The watch compares MISO_frame: what this core put on the wire, after the setpoint guard and
  // the heat shift (#30) rewrote the MQTT payload, not the payload itself.
  mhi_cc_observe(&cc, MOSI_frame);
  // Armed on the double-frame exchange only: the odd frame of the pair carries the same stale
  // DB0..DB2, and re-arming there would forget an echo already seen between the two.
  if (!passiveMode && doubleframe)
    mhi_cc_sent(&cc, MISO_frame, MHI_CC_WATCH_FRAMES);

  // Every valid frame goes to the discovery tooling; main.cpp decides whether
  // anything in it is worth publishing.
  m_cbiStatus->cbiRawFunction(raw_frame, MOSI_frame, frameSize);

  if (new_datapacket_received) {
    // The decode half lives in lib/mhi_pure/mhi_decode.cpp and reports through
    // the sink at the points the old inline code called its callbacks (fork #64).
    const MhiDecodeSink sink = {m_cbiStatus, sink_status, sink_raw, sink_now_ms, sink_unknown_opdata};
    const MhiDecodeFrame decoded = {MOSI_frame, MISO_frame, frameSize, &erropdataCnt, &lastTroomInternalMillis};
    mhi_decode_frame(&dec, &decoded, &sink);
  }
  // Used to return call_counter, which at ~20 frames/sec goes negative after
  // about 3.4 years and then makes the caller's `ret < 0` test log a phantom
  // error on every frame, forever. The counter is available via
  // get_call_counter().
  return err_msg_valid_frame;
}

uint MHI_AC_Ctrl_Core::get_call_counter() {
  return call_counter;
}

uint32_t MHI_AC_Ctrl_Core::get_cmd_unconfirmed() {
  return cc.unconfirmed + cq.dropped;  // a command the queue gave up on (#48) never had a good frame to be watched in
}
