#include "mhi_cmd.h"

#include <string.h>

#include "mhi_diag_frame.h"
#include "mhi_temp.h"

static bool eq(const char* a, const char* b) {
  return a != NULL && b != NULL && strcmp(a, b) == 0;
}

// An on/off payload: value 1, 0, or error.
static void on_off(MhiCmd* c, const char* payload, const char* on, const char* off) {
  if (eq(payload, on)) c->value = 1;
  else if (eq(payload, off)) c->value = 0;
  else c->error = true;
}

static void parse_mode(const MhiCmdConfig* cfg, const char* payload, MhiCmd* c) {
  if (cfg->mode_carries_power && eq(payload, cfg->mode_off)) c->value = MHI_CMD_MODE_OFF;
  else if (eq(payload, cfg->mode_auto)) c->value = MHI_CMD_MODE_AUTO;
  else if (eq(payload, cfg->mode_dry)) c->value = MHI_CMD_MODE_DRY;
  else if (eq(payload, cfg->mode_cool)) c->value = MHI_CMD_MODE_COOL;
  else if (eq(payload, cfg->mode_fan)) c->value = MHI_CMD_MODE_FAN;
  else if (eq(payload, cfg->mode_heat)) c->value = MHI_CMD_MODE_HEAT;
  else c->error = true;
}

static void parse_reset(const MhiCmdConfig* cfg, const char* payload, MhiCmd* c) {
  if (eq(payload, cfg->reset)) c->value = MHI_CMD_RESET_RESTART;
  else if (cfg->allow_crash && eq(payload, cfg->reset_crash)) c->value = MHI_CMD_RESET_CRASH;
  else c->error = true;
}

void mhi_cmd_parse(const MhiCmdConfig* cfg, const char* topic, const char* payload, MhiCmd* cmd) {
  *cmd = MhiCmd{MHI_CMD_UNKNOWN, false, 0, 0.0f, 0, 0};
  const size_t plen = strlen(cfg->set_prefix);
  if (strncmp(topic, cfg->set_prefix, plen) != 0) return;
  const char* t = topic + plen;

  if (cfg->t_power != NULL && !cfg->mode_carries_power && eq(t, cfg->t_power)) {
    cmd->kind = MHI_CMD_POWER;
    on_off(cmd, payload, cfg->power_on, cfg->power_off);
  } else if (eq(t, cfg->t_mode)) {
    cmd->kind = MHI_CMD_MODE;
    parse_mode(cfg, payload, cmd);
  } else if (eq(t, cfg->t_tsetpoint)) {
    cmd->kind = MHI_CMD_TSETPOINT;
    if (!mhi_parse_celsius(payload, &cmd->fvalue)) cmd->error = true;
  } else if (eq(t, cfg->t_fan)) {
    cmd->kind = MHI_CMD_FAN;
    const int level = mhi_fan_parse(cfg->fan_names, payload);
    if (level == MHI_FAN_AUTO || (level >= 1 && level <= 4)) cmd->value = level;
    else cmd->error = true;
  } else if (eq(t, cfg->t_vanes)) {
    cmd->kind = MHI_CMD_VANES;
    cmd->value = mhi_vanes_parse(cfg->vanes_names, payload);
    if (cmd->value == MHI_VANES_UNKNOWN) cmd->error = true;
  } else if (cfg->vanes_lr_names != NULL && eq(t, cfg->t_vaneslr)) {
    cmd->kind = MHI_CMD_VANESLR;
    cmd->value = mhi_vanes_lr_parse(cfg->vanes_lr_names, payload);
    if (cmd->value == MHI_VANES_LR_UNKNOWN) cmd->error = true;
  } else if (cfg->vanes_lr_names != NULL && eq(t, cfg->t_3dauto)) {
    cmd->kind = MHI_CMD_3DAUTO;
    on_off(cmd, payload, cfg->dauto_on, cfg->dauto_off);
  } else if (eq(t, cfg->t_troom)) {
    cmd->kind = MHI_CMD_TROOM;
    // Junk is refused, not read as 0 degrees C (sweep #33 F1). fvalue is 0 then.
    float f = 0.0f;
    if (!mhi_parse_celsius(payload, &f)) cmd->error = true;
    cmd->fvalue = f;
  } else if (eq(t, cfg->t_erropdata)) {
    cmd->kind = MHI_CMD_ERROPDATA;
  } else if (eq(t, cfg->t_reset)) {
    cmd->kind = MHI_CMD_RESET;
    parse_reset(cfg, payload, cmd);
  } else if (eq(t, cfg->t_passive)) {
    cmd->kind = MHI_CMD_PASSIVE;
    on_off(cmd, payload, cfg->passive_on, cfg->passive_off);
  } else if (eq(t, cfg->t_diag)) {
    cmd->kind = MHI_CMD_DIAG;
    on_off(cmd, payload, cfg->diag_on, cfg->diag_off);
  } else if (eq(t, cfg->t_opdata)) {
    cmd->kind = MHI_CMD_OPDATA;
    if (!mhi_opdata_request_parse(payload, &cmd->a, &cmd->b)) cmd->error = true;
  } else if (eq(t, cfg->t_silent)) {
    cmd->kind = MHI_CMD_SILENT;
    on_off(cmd, payload, cfg->silent_on, cfg->silent_off);
  }
}
