// The set/ command handler (fork #55): MQTT_subscribe_callback() parses with
// mhi_cmd_parse() and applies the command. It lives apart from main.cpp but
// works on main.cpp's state, declared here.
#pragma once

#include <Arduino.h>

#include "MHI-AC-Ctrl-core.h"
#include "mhi_diag_frame.h"
#include "mhi_heat_shift.h"
#include "mhi_link.h"
#include "mhi_setpoint.h"

void MQTT_subscribe_callback(const char* topic, byte* payload, unsigned int length);

extern MHI_AC_Ctrl_Core mhi_ac_ctrl_core;
extern bool troom_was_set_by_MQTT;
extern unsigned long room_temp_set_timeout_Millis;
extern MhiSetpointGuard setpoint_guard;
extern MhiHeatShift heat_shift;
extern float troom_external_celsius;
extern MhiDiagFrame diag_frame;
extern MhiRetryPacer diag_pacer;
extern bool diag_on;

void publish_tsetpoint(float celsius);
void apply_external_troom();
void ds18x20_reapply();
void heat_shift_ended(bool publish_setpoint = true);
void set_mode_checked(ACMode mode);
void publish_diag_state();
