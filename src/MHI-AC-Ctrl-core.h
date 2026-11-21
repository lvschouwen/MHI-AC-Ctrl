#pragma once

#include <Arduino.h>

#include "mhi_cmd_confirm.h"  // CmdUnconfirmed (fork #60)
#include "mhi_ac_status.h"  // ACType, ACStatus (fork #64)
#include "mhi_cmd_queue.h"  // queued one-shot commands, retired by a good frame (fork #48)
#include "mhi_decode.h"  // what a valid frame says, and what was reported before (fork #64)
#include "mhi_frame.h"  // frame layout + checksums, host-testable

// comment out the data you are not interested, but at least leave one row !
const byte opdata[][2] PROGMEM = {
  { 0xc0, 0x02},  //  1 "MODE"
  { 0xc0, 0x05},  //  2 "SET-TEMP" [°C]
  { 0xc0, 0x80},  //  3 "RETURN-AIR" [°C]
  { 0xc0, 0x81},  //  5 "THI-R1" [°C]
  { 0x40, 0x81},  //  6 "THI-R2" [°C]
  { 0xc0, 0x87},  //  7 "THI-R3" [°C]
  { 0xc0, 0x1f},  //  8 "IU-FANSPEED"
  { 0xc0, 0x1e},  // 12 "TOTAL-IU-RUN" [h]
  { 0x40, 0x80},  // 21 "OUTDOOR" [°C]
  { 0x40, 0x82},  // 22 "THO-R1" [°C]
  { 0x40, 0x11},  // 24 "COMP" [Hz]
  { 0x40, 0x85},  // 27 "TD" [°C]
  { 0x40, 0x90},  // 29 "CT" [A]
  { 0x40, 0xb1},  // 32 "TDSH" [°C]
  { 0x40, 0x7c},  // 33 "PROTECTION-No"
  { 0x40, 0x1f},  // 34 "OU-FANSPEED"
  { 0x40, 0x0c},  // 36 "DEFROST"
  { 0x40, 0x1e},  // 37 "TOTAL-COMP-RUN" [h]
  { 0x40, 0x13},  // 38 "OU-EEV" [Puls]
  { 0xc0, 0x94},  //    "energy-used" [kWh]
  { 0xc0, 0xdd},  //    "SILENT" (fork #4): DB11 bit 5; the AC also reports it unasked after a remote press

};

//#define NoFramesPerPacket 20                 // number of frames/packet, must be an even number
#define NoFramesPerOpDataCycle 400             // number of frames used for a OpData request cycle; will be 20s (20 frames are 1s)
#define minTimeInternalTroom 5000              // minimal time in ms used for Troom internal sensor changes for publishing to avoid jitter 

// pin defintions
#define SCK_PIN  14
#define MOSI_PIN 13
#define MISO_PIN 12

// constants for the frame moved to lib/mhi_pure/mhi_frame.h

enum ErrMsg {   // Error message enum
  err_msg_valid_frame = 0, err_msg_invalid_signature = -1, err_msg_invalid_checksum = -2, err_msg_timeout_SCK_low = -3, err_msg_timeout_SCK_high = -4
};

enum ACPower {  // Power enum
  power_off = 0, power_on = 1
};

enum ACMode {   // Mode enum
  mode_auto = 0b00000000, mode_dry = 0b00000100, mode_cool = 0b00001000, mode_fan = 0b00001100, mode_heat = 0b00010000
};

enum ACVanes {  // Vanes enum
  vanes_1 = 1, vanes_2 = 2, vanes_3 = 3, vanes_4 = 4, vanes_unknown = 0, vanes_swing = 5
};

enum ACVanesLR {  // Vanes Left Right enum
  vanesLR_1 = 1, vanesLR_2 = 2, vanesLR_3 = 3, vanesLR_4 = 4, vanesLR_5 = 5, vanesLR_6 = 6, vanesLR_7 = 7, vanesLR_swing = 8
};

enum AC3Dauto {  // 3D auto enum
  Dauto_off = 0b00000000, Dauto_on = 0b00000100
};

class CallbackInterface_Status {
  public:
    virtual void cbiStatusFunction(ACStatus status, int value) = 0;
    // Raw bytes for the protocol discovery tooling (fork #4): raw_frame with
    // the whole MOSI frame after checksum validation, raw_opdata with
    // DB9..DB12 of an operating-data answer the decoder does not know.
    // Optional: the default does nothing.
    virtual void cbiRawFunction(ACStatus status, const uint8_t* bytes, size_t len) { (void)status; (void)bytes; (void)len; }
};

class MHI_AC_Ctrl_Core {
  private:
    // What was reported before, to publish only what changed: see mhi_decode.h (fork #64).
    MhiDecodeState dec;

    // for writing to AC
    // The queued one-shot commands (power, mode, setpoint, fan, vanes, left/right
    // vanes, 3D auto, the error-data request and the Silent write): kept until a
    // frame that carried them has passed its checks, see mhi_cmd_queue.h (fork #48).
    MhiCmdQueue cq = {};
    // Writes the AC never showed, see mhi_cmd_confirm.h (fork #60).
    MhiCmdConfirm cc = {};
    // One-shot operating-data request from set/OpDataRequest (fork #4). Sent
    // in place of the next code of the normal cycle, once, see loop().
    bool request_opdata_pending = false;
    byte request_opdata_prefix = 0;
    byte request_opdata_code = 0;
    // Silent operation write (fork #4 batch B): DB6 0x80, DB9 0x21, DB10 0/1
    // in the next MISO frame pair, the write hberntsen/mhi-ac-ctrl-esp32 PR #42
    // carries (traced from a ProtoArt controller by mreijnde). Shares the 0x80
    // command slot with the error-data request (both are queue slots).
    bool passiveMode = false;
    byte new_Troom = 0xff;    // writing 0xff to DB3 indicates the usage of the internal room temperature sensor
    float Troom_offset = 0.0;
    
    byte frameSize = 20;
    uint call_counter = 0;                // counts how often loop() was called

    CallbackInterface_Status *m_cbiStatus;

  public:
    void MHIAcCtrlStatus(CallbackInterface_Status *cb) {
      m_cbiStatus = cb;
    };


    void init(bool drive_miso = true);    // initialization called once after boot
    void reset_old_values();              // resets the 'old' variables ensuring that all status information are resend
    void reset_system_values();           // the same for the 11 system values only: each is published at its next reading (fork #22)
    int loop(uint max_time_ms);           // receive / transmit a frame of 20 bytes; returns an ErrMsg
    uint get_call_counter();              // how often loop() has run since boot
    uint32_t get_cmd_unconfirmed();       // writes whose value the AC never showed within MHI_CC_WATCH_FRAMES valid frames
    void set_power(boolean power);        // power on/off the AC
    void set_mode(ACMode mode);           // change AC mode (e.g. heat, dry, cool etc.)
    void set_tsetpoint(uint tsetpoint);   // set the target temperature of the AC)
    void set_fan(uint fan);               // set the requested fan speed
    void set_vanes(uint vanes);           // set the vanes horizontal position (or swing)
    void set_troom(byte temperature);     // set the room temperature used by AC (0xff indicates the usage of the internal room temperature sensor)
    void request_ErrOpData();             // request that the AC provides the error data
    void request_OpData(byte prefix, byte code);  // ask the AC once for one operating-data code (prefix 0x40/0xc0 as in the request table)
    void set_silent(bool on);             // Silent operation on/off; the Silent status confirms it within a second or two
    float get_troom_offset();             // get troom offset, only usefull when ENHANCED_RESOLUTION is used
    void set_troom_offset(float offset);  // set troom offset, only usefull when ENHANCED_RESOLUTION is used
    void set_frame_size(byte framesize);  // set framesize to 20 or 33
    void set_3Dauto(AC3Dauto Dauto);      // set the 3D auto mode on or off
    void set_vanesLR(uint vanesLR);       // set the vanes LR (vertical) position
    void set_passive_mode(bool newMode);  // enable or disable passive mode

};
