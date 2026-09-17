#include "mhi_discovery.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// The templates below add up to about 1.5 KB. On the ESP8266 a plain string
// literal lives in RAM, so there they go to flash and are formatted with
// vsnprintf_P; on the host they are ordinary literals. This is the one place
// in lib/mhi_pure that knows about Arduino.
#if defined(ARDUINO)
#include <pgmspace.h>
#define FMT(s) PSTR(s)
#define MHI_VSNPRINTF vsnprintf_P
#else
#define FMT(s) s
#define MHI_VSNPRINTF vsnprintf
#endif

static const char* const kComponent[MHI_DISCOVERY_ROWS] = {
  "climate", "select", "switch", "binary_sensor", "binary_sensor", "sensor", "sensor", "sensor", "sensor", "sensor"};
static const char* const kSuffix[MHI_DISCOVERY_ROWS] = {
  "", "vanes", "silent", "problem", "wiring", "uptime", "free_heap", "rssi", "reset_reason", "wifi_phy"};
static const char* const kHaModes[6] = {"off", "auto", "dry", "cool", "fan_only", "heat"};

struct Out {
  char* buf;
  size_t len;
  size_t n;
  bool overflow;
};

static void put(Out* o, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

static void put(Out* o, const char* fmt, ...) {
  if (o->overflow) return;
  va_list ap;
  va_start(ap, fmt);
  const int r = MHI_VSNPRINTF(o->buf + o->n, o->len - o->n, fmt, ap);
  va_end(ap);
  if (r < 0 || (size_t)r >= o->len - o->n) {
    o->overflow = true;
    return;
  }
  o->n += (size_t)r;
}

static void put_char(Out* o, char c) {
  if (o->overflow) return;
  if (o->n + 1 >= o->len) {
    o->overflow = true;
    return;
  }
  o->buf[o->n++] = c;
  o->buf[o->n] = '\0';
}

// A JSON string: " and \ escaped. Names and templates carry no control characters.
static void put_str(Out* o, const char* s) {
  put_char(o, '"');
  for (; s && *s; s++) {
    if (*s == '"' || *s == '\\') put_char(o, '\\');
    put_char(o, *s);
  }
  put_char(o, '"');
}

static void put_list(Out* o, const char* key, const char* const* items, size_t count) {
  put(o, FMT("\"%s\":["), key);
  for (size_t i = 0; i < count; i++) {
    if (i) put_char(o, ',');
    put_str(o, items[i]);
  }
  put(o, FMT("],"));
}

static void head(Out* o, const MhiDiscoveryCtx* c, MhiDiscoveryRow row) {
  put(o, FMT("{\"~\":\"%s\","), c->base);
  if (row == MHI_DISCOVERY_CLIMATE) {
    // null: the climate is the device's main feature, HA names it after the device.
    put(o, FMT("\"name\":null,\"uniq_id\":\"%s\","), c->climate_id);
    if (c->entity_prefix) put(o, FMT("\"default_entity_id\":\"climate.%s\","), c->entity_prefix);
    return;
  }
  put(o, FMT("\"name\":"));
  put_str(o, c->names[row]);
  put(o, FMT(",\"uniq_id\":\"%s_%s\","), c->id_prefix, kSuffix[row]);
  if (c->entity_prefix) {
    // The ID HA derives itself for a device without an area, pinned.
    char slug[48];
    if (mhi_discovery_slug(c->names[row], slug, sizeof(slug)) == 0) {
      o->overflow = true;
      return;
    }
    put(o, FMT("\"default_entity_id\":\"%s.%s_%s\","), kComponent[row], c->entity_prefix, slug);
  }
}

static void tail(Out* o, const MhiDiscoveryCtx* c, bool diagnostic) {
  if (diagnostic) put(o, FMT("\"ent_cat\":\"diagnostic\","));
  put(o, FMT("\"avty_t\":\"~/%s\",\"pl_avail\":\"%s\",\"pl_not_avail\":\"%s\",\"dev\":{\"ids\":[\"%s\"],\"name\":"),
      c->t_connected, c->connected_on, c->connected_off, c->hostname);
  put_str(o, c->device_name);
  put(o, FMT(",\"mf\":\"Mitsubishi Heavy Industries\",\"mdl\":\"MHI-AC-Ctrl\",\"sw\":\"%s\"}}"), c->version);
}

static void state_topic(Out* o, const char* topic) {
  put(o, FMT("\"stat_t\":\"~/%s\","), topic);
}

size_t mhi_discovery_slug(const char* name, char* out, size_t out_len) {
  if (!out || out_len == 0) return 0;
  out[0] = '\0';
  if (!name) return 0;
  size_t n = 0;
  bool separator_pending = false;  // written before the next alphanumeric, never at the ends
  for (; *name; name++) {
    const unsigned char ch = (unsigned char)*name;
    const bool alnum = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
    if (!alnum) {
      if (n > 0) separator_pending = true;
      continue;
    }
    if (n + 1 + (separator_pending ? 1 : 0) >= out_len) {
      out[0] = '\0';
      return 0;
    }
    if (separator_pending) {
      out[n++] = '_';
      separator_pending = false;
    }
    out[n++] = (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : (char)ch;
  }
  out[n] = '\0';
  return n;
}

bool mhi_discovery_modes_valid(const MhiDiscoveryCtx* c) {
  for (size_t i = 0; i < 6; i++)
    if (!c->modes[i] || strcmp(c->modes[i], kHaModes[i]) != 0) return false;
  return true;
}

size_t mhi_discovery_topic(MhiDiscoveryRow row, const MhiDiscoveryCtx* c, char* out, size_t out_len) {
  if (!out || out_len == 0 || row >= MHI_DISCOVERY_ROWS) return 0;
  int r;
  if (row == MHI_DISCOVERY_CLIMATE)
    r = snprintf(out, out_len, "%s/%s/%s/config", c->discovery_prefix, kComponent[row], c->climate_id);
  else
    r = snprintf(out, out_len, "%s/%s/%s_%s/config", c->discovery_prefix, kComponent[row], c->id_prefix, kSuffix[row]);
  if (r < 0 || (size_t)r >= out_len) {
    out[0] = '\0';
    return 0;
  }
  return (size_t)r;
}

size_t mhi_discovery_build(MhiDiscoveryRow row, const MhiDiscoveryCtx* c, char* out, size_t out_len) {
  if (!out || out_len == 0 || row >= MHI_DISCOVERY_ROWS) return 0;
  Out o = {out, out_len, 0, false};
  out[0] = '\0';
  head(&o, c, row);
  bool diagnostic = true;
  switch (row) {
    case MHI_DISCOVERY_CLIMATE: {
      diagnostic = false;
      const char* fan_modes[5] = {"1", "2", "3", "4", c->fan_auto};
      put(&o, FMT("\"mode_cmd_t\":\"~/%s%s\",\"mode_stat_t\":\"~/%s\",\"temp_cmd_t\":\"~/%s%s\",\"temp_stat_t\":\"~/%s\","),
          c->set_prefix, c->t_mode, c->t_mode, c->set_prefix, c->t_tsetpoint, c->t_tsetpoint);
      put(&o, FMT("\"fan_mode_cmd_t\":\"~/%s%s\",\"fan_mode_stat_t\":\"~/%s\",\"swing_mode_cmd_t\":\"~/%s%s\",\"swing_mode_stat_t\":\"~/%s\","),
          c->set_prefix, c->t_fan, c->t_fan, c->set_prefix, c->t_vanes, c->t_vanes);
      put(&o, FMT("\"curr_temp_t\":\"~/%s\",\"act_t\":\"~/%s\","), c->t_troom, c->t_action);
      put_list(&o, "modes", c->modes, 6);
      put_list(&o, "fan_modes", fan_modes, 5);
      put_list(&o, "swing_modes", c->vanes, 6);
      put(&o, FMT("\"min_temp\":18,\"max_temp\":30,\"temp_step\":0.5,"));
      break;
    }
    case MHI_DISCOVERY_VANES:
      diagnostic = false;
      put(&o, FMT("\"stat_t\":\"~/%s\",\"cmd_t\":\"~/%s%s\","), c->t_vanes, c->set_prefix, c->t_vanes);
      put_list(&o, "ops", c->vanes, 6);
      break;
    case MHI_DISCOVERY_SILENT:
      diagnostic = false;
      put(&o, FMT("\"stat_t\":\"~/%s\",\"cmd_t\":\"~/%s%s\",\"pl_on\":\"%s\",\"pl_off\":\"%s\",\"ic\":\"mdi:volume-low\","),
          c->t_silent, c->set_prefix, c->t_silent, c->silent_on, c->silent_off);
      break;
    case MHI_DISCOVERY_PROBLEM:
      state_topic(&o, c->t_errorcode);
      put(&o, FMT("\"val_tpl\":\"{{ 'ON' if value|int(0) != 0 else 'OFF' }}\",\"dev_cla\":\"problem\","));
      break;
    case MHI_DISCOVERY_WIRING:
      state_topic(&o, c->t_wiring);
      put(&o, FMT("\"val_tpl\":\"{{ 'OFF' if value == '%s' else 'ON' }}\",\"dev_cla\":\"problem\","), c->wiring_ok);
      break;
    case MHI_DISCOVERY_UPTIME:
      state_topic(&o, c->t_uptime);
      // No state class: hass-config's reboot counter compares the raw seconds.
      put(&o, FMT("\"dev_cla\":\"duration\",\"unit_of_meas\":\"s\",\"sug_dsp_prc\":0,"));
      break;
    case MHI_DISCOVERY_FREE_HEAP:
      state_topic(&o, c->t_free_heap);
      put(&o, FMT("\"dev_cla\":\"data_size\",\"unit_of_meas\":\"B\",\"stat_cla\":\"measurement\","));
      break;
    case MHI_DISCOVERY_RSSI:
      state_topic(&o, c->t_rssi);
      put(&o, FMT("\"dev_cla\":\"signal_strength\",\"unit_of_meas\":\"dBm\",\"stat_cla\":\"measurement\","));
      break;
    case MHI_DISCOVERY_RESET_REASON:
      state_topic(&o, c->t_reset_reason);
      if (c->reset_reason_tpl) {
        put(&o, FMT("\"val_tpl\":"));
        put_str(&o, c->reset_reason_tpl);
        put_char(&o, ',');
      }
      put(&o, FMT("\"ic\":\"mdi:restart-alert\","));
      break;
    case MHI_DISCOVERY_WIFI_PHY:
      state_topic(&o, c->t_wifi_phy);
      put(&o, FMT("\"ic\":\"mdi:wifi-cog\","));
      break;
    case MHI_DISCOVERY_ROWS:  // excluded above; keeps -Wswitch exhaustive
      return 0;
  }
  tail(&o, c, diagnostic);
  if (o.overflow) {
    out[0] = '\0';
    return 0;
  }
  return o.n;
}
