// Home Assistant MQTT discovery payloads (fork issue #4, batch B; spec
// docs/superpowers/specs/2026-09-16-phase-4-batches-design.md §4.4).
//
// One JSON config per entity, published retained under
// <discovery_prefix>/<component>/<uniq_id>/config after every MQTT connect.
// Every text comes in through the context (topics, payloads and names are the
// firmware's #ifndef-guarded macros), so this file is the same on the ESP8266,
// in the host tests and in tools/discovery_payloads.cpp.
//
// The row table is append-only: Home Assistant keeps a retained config per
// topic, and the topic carries the uniq_id, so renumbering orphans entities.

#pragma once

#include <stddef.h>
#include <stdint.h>

#define MHI_DISCOVERY_BUF 1024      // the payload buffer; the host test measures every row against it
#define MHI_DISCOVERY_TOPIC_MAX 96

enum MhiDiscoveryRow : uint8_t {
  MHI_DISCOVERY_CLIMATE,       // climate  <climate_id>
  MHI_DISCOVERY_VANES,         // select   <id_prefix>_vanes
  MHI_DISCOVERY_SILENT,        // switch   <id_prefix>_silent
  MHI_DISCOVERY_PROBLEM,       // binary_sensor <id_prefix>_problem (Errorcode != 0)
  MHI_DISCOVERY_WIRING,        // binary_sensor <id_prefix>_wiring (Wiring != o.k.)
  MHI_DISCOVERY_UPTIME,        // sensor   <id_prefix>_uptime
  MHI_DISCOVERY_FREE_HEAP,     // sensor   <id_prefix>_free_heap
  MHI_DISCOVERY_RSSI,          // sensor   <id_prefix>_rssi
  MHI_DISCOVERY_RESET_REASON,  // sensor   <id_prefix>_reset_reason
  MHI_DISCOVERY_WIFI_PHY,      // sensor   <id_prefix>_wifi_phy
  MHI_DISCOVERY_VANES_LR,       // select   <id_prefix>_vanes_lr (fork #20)
  MHI_DISCOVERY_3DAUTO,         // switch   <id_prefix>_3d_auto (fork #20)
  MHI_DISCOVERY_FRAME_ERRORS,   // sensor   <id_prefix>_frame_errors (fork #21 F1)
  MHI_DISCOVERY_FRAME_TIMEOUTS, // sensor   <id_prefix>_frame_timeouts (fork #21 F1)
  MHI_DISCOVERY_ERROR_CODE,     // sensor   <id_prefix>_error_code, on the existing Errorcode topic (fork #21 F2)
  // From here on, the outdoor device (fork #19): uniq_id <outdoor_id>_<suffix>.
  MHI_DISCOVERY_OU_OUTDOOR,     // sensor        <outdoor_id>_outdoor_temp
  MHI_DISCOVERY_OU_CT,          // sensor        <outdoor_id>_current
  MHI_DISCOVERY_OU_KWH,         // sensor        <outdoor_id>_energy
  MHI_DISCOVERY_OU_COMP,        // sensor        <outdoor_id>_comp_freq
  MHI_DISCOVERY_OU_DEFROST,     // binary_sensor <outdoor_id>_defrost
  MHI_DISCOVERY_OU_COMP_RUN,    // sensor        <outdoor_id>_comp_run
  MHI_DISCOVERY_OU_PROTECTION,  // sensor        <outdoor_id>_protection
  MHI_DISCOVERY_ROWS
};

// Everything a payload is made of. Field order is binding: main.cpp and the
// tests fill it with designated initialisers, which GCC requires in order.
// Only entity_prefix, outdoor_entity_prefix, reset_reason_tpl and
// names[MHI_DISCOVERY_CLIMATE] may be NULL.
// Names, the device name and the template are JSON-escaped; every other
// text goes into the JSON as it is and must not contain '"' or '\\' (they are
// compile-time macros today; escape them here if they ever become runtime).
struct MhiDiscoveryCtx {
  const char* discovery_prefix;  // "homeassistant"
  const char* base;              // MQTT_PREFIX without its trailing slash, the payload's "~"
  const char* set_prefix;        // what MQTT_SET_PREFIX adds to MQTT_PREFIX, "set/"
  const char* hostname;          // the device identifier (dev.ids)
  const char* device_name;       // dev.name; HA prefixes every entity name with it
  const char* version;           // dev.sw
  const char* climate_id;        // uniq_id of the climate, e.g. "AC_Slaapkamer"
  const char* id_prefix;         // uniq_id prefix of the other rows, e.g. "ac_slaapkamer"
  const char* entity_prefix;     // default_entity_id prefix, e.g. "ac_slaapkamer" -> climate.ac_slaapkamer, select.ac_slaapkamer_<slug of the name>; NULL: none
  const char* names[MHI_DISCOVERY_ROWS];  // entity names; [MHI_DISCOVERY_CLIMATE] is unused (the climate is named after the device)
  const char* reset_reason_tpl;  // value template of the reset-reason sensor; NULL: none
  // Topic texts (TOPIC_*), relative to base.
  const char* t_mode;
  const char* t_tsetpoint;
  const char* t_fan;
  const char* t_vanes;
  const char* t_troom;
  const char* t_action;
  const char* t_connected;
  const char* t_silent;
  const char* t_errorcode;
  const char* t_wiring;
  const char* t_uptime;
  const char* t_free_heap;
  const char* t_rssi;
  const char* t_reset_reason;
  const char* t_wifi_phy;
  // Payload texts.
  const char* modes[6];          // PAYLOAD_MODE_OFF, _AUTO, _DRY, _COOL, _FAN, _HEAT
  const char* fan_auto;          // PAYLOAD_FAN_AUTO; the four levels are fan[] below
  const char* vanes[6];          // PAYLOAD_VANES_1..4, _SWING, _UNKNOWN
  const char* connected_on;      // PAYLOAD_CONNECTED_TRUE
  const char* connected_off;     // PAYLOAD_CONNECTED_FALSE
  const char* silent_on;         // PAYLOAD_SILENT_ON
  const char* silent_off;        // PAYLOAD_SILENT_OFF
  const char* wiring_ok;         // MHI_WIRING_OK
  // Fork #20/#19/#21 (batch C). Only outdoor_entity_prefix may be NULL, like entity_prefix.
  bool has_lr;                        // USE_EXTENDED_FRAME_SIZE
  const char* t_vaneslr;              // TOPIC_VANESLR
  const char* t_3dauto;               // TOPIC_3DAUTO
  const char* vanes_lr[8];            // PAYLOAD_VANESLR_1..7, _SWING
  const char* threedauto_on;          // PAYLOAD_3DAUTO_ON
  const char* threedauto_off;         // PAYLOAD_3DAUTO_OFF
  bool has_outdoor;                   // HA_OUTDOOR_DEVICE
  const char* outdoor_id;             // HA_OUTDOOR_ID; dev.ids and uniq_id prefix of the OU_* rows
  const char* outdoor_name;           // HA_OUTDOOR_NAME; dev.name
  const char* outdoor_entity_prefix;  // HA_OUTDOOR_ENTITY_PREFIX; NULL: none
  const char* op_prefix;              // what MQTT_OP_PREFIX adds to MQTT_PREFIX, "OpData/"
  const char* t_op_outdoor, *t_op_ct, *t_op_kwh, *t_op_comp, *t_op_defrost, *t_op_total_comp_run, *t_op_protection_no;
  const char* defrost_on, *defrost_off;  // PAYLOAD_OP_DEFROST_ON/OFF
  const char* t_frame_errors, *t_frame_timeouts;  // the error-code row reads t_errorcode, which is already there
  const char* fan[4];                 // PAYLOAD_FAN_1..4
};

// Home Assistant's climate accepts only its own mode names: off, auto, dry,
// cool, fan_only, heat. False means the climate row must not be published.
bool mhi_discovery_modes_valid(const MhiDiscoveryCtx* ctx);

// "<discovery_prefix>/<component>/<uniq_id>/config". Returns the length,
// 0 when it does not fit out_len.
size_t mhi_discovery_topic(MhiDiscoveryRow row, const MhiDiscoveryCtx* ctx, char* out, size_t out_len);

// Home Assistant's slug of an entity name: lower case, every run of characters
// outside a-z 0-9 becomes one "_", none at the ends. With entity_prefix set,
// every row's default_entity_id is <component>.<entity_prefix>_<slug(name)>
// (the climate's is climate.<entity_prefix>): the ID HA derives itself from
// "<device name> <entity name>" when the device has no area, pinned so an area
// or a lost registry never moves what automations read. Returns the length,
// 0 when the name is NULL, slugs to nothing or does not fit out_len.
size_t mhi_discovery_slug(const char* name, char* out, size_t out_len);

// Whether a row is part of this build: false for MHI_DISCOVERY_VANES_LR and
// MHI_DISCOVERY_3DAUTO when !has_lr, false for every OU_* row when
// !has_outdoor, false for a row that is not in the table, true otherwise.
// Callers must skip a disabled row entirely.
bool mhi_discovery_row_enabled(MhiDiscoveryRow row, const MhiDiscoveryCtx* ctx);

// One row's JSON. Returns the length, 0 (and an empty string) when it does
// not fit out_len. out_len should be MHI_DISCOVERY_BUF.
size_t mhi_discovery_build(MhiDiscoveryRow row, const MhiDiscoveryCtx* ctx, char* out, size_t out_len);
