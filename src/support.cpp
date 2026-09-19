#include "support.h"
#include <Arduino.h>

#include "group.h"
#include "mhi_diag.h"
#include "mhi_frame_stats.h"
#include "mhi_group.h"
#include "mhi_link.h"
#include "mhi_phy.h"
#include "mhi_temp.h"
#include "mhi_uptime.h"

WiFiClient espClient;
PubSubClient MQTTclient(espClient);
int WIFI_lost = 0;
int MQTT_lost = 0;

struct rising_edge_cnt_struct{
  volatile uint32_t SCK = 0;
  volatile uint32_t MOSI = 0;
  volatile uint32_t MISO = 0;
} rising_edge_cnt;

IRAM_ATTR void handleInterrupt_SCK() {
  rising_edge_cnt.SCK++;
}

IRAM_ATTR void handleInterrupt_MOSI() {
  rising_edge_cnt.MOSI++;
}

IRAM_ATTR void handleInterrupt_MISO() {
  rising_edge_cnt.MISO++;
}

uint8_t wiring_faults = 0;

void MeasureFrequency() {  // measure the frequency on the pins
  pinMode(SCK_PIN, INPUT);
  pinMode(MOSI_PIN, INPUT);
  pinMode(MISO_PIN, INPUT);
  Serial.println(F("Measure frequency for SCK, MOSI and MISO pin"));
  attachInterrupt(digitalPinToInterrupt(SCK_PIN), handleInterrupt_SCK, RISING);
  attachInterrupt(digitalPinToInterrupt(MOSI_PIN), handleInterrupt_MOSI, RISING);
  attachInterrupt(digitalPinToInterrupt(MISO_PIN), handleInterrupt_MISO, RISING);
  unsigned long starttimeMicros = micros();
  while (micros() - starttimeMicros < 1000000)
    yield();  // one second is a long time to hold off the SDK
  detachInterrupt(SCK_PIN);
  detachInterrupt(MOSI_PIN);
  detachInterrupt(MISO_PIN);

  wiring_faults = mhi_wiring_faults(rising_edge_cnt.SCK, rising_edge_cnt.MOSI, rising_edge_cnt.MISO);

  Serial.printf_P(PSTR("SCK frequency=%iHz (expected: >3000Hz) %s\n"), rising_edge_cnt.SCK,
                  (wiring_faults & MHI_WIRING_FAULT_SCK) ? "out of range!" : "o.k.");
  Serial.printf_P(PSTR("MOSI frequency=%iHz (expected: <SCK frequency) %s\n"), rising_edge_cnt.MOSI,
                  (wiring_faults & MHI_WIRING_FAULT_MOSI) ? "out of range!" : "o.k.");
  Serial.printf_P(PSTR("MISO frequency=%iHz (expected: ~0Hz) %s\n"), rising_edge_cnt.MISO,
                  (wiring_faults & MHI_WIRING_FAULT_MISO) ? "out of range!" : "o.k.");

  if (wiring_faults != 0) {
    // Deliberately not fatal. This used to be `while (1);` on a bad MISO
    // reading, which meant the hardware watchdog rebooted into the same check
    // forever - before setupOTA() had run, so the only fix was a screwdriver.
    char faults[32];
    mhi_wiring_fault_text(wiring_faults, faults, sizeof(faults));
    Serial.printf_P(PSTR("Wiring check failed for: %s\n"), faults);
    Serial.println(F("Continuing in degraded mode so the unit stays reachable over OTA."));
  }
}

void initWiFi(){
  WiFi.persistent(false);
  WiFi.disconnect(true);    // Delete SDK wifi config
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME); 
  WiFi.setAutoReconnect(false);
}

int WiFiStatus = WIFI_CONNECT_TIMEOUT;
uint networksFound = 0;
unsigned long WiFiTimeoutMillis;
unsigned long WiFiScanStartMillis;
// A full scan takes a few seconds. Well past that, the callback is not coming.
static const unsigned long kWiFiScanDeadlineMs = 30000;
// A broker that refuses at once must not use up the ten failed attempts that
// reset Wi-Fi within milliseconds. Paced, that reset needs about 50 s of
// outage. OTA is served in between; SPI only with CONTINUE_WITHOUT_MQTT, since
// without it loop() skips the SPI core whenever MQTT is down.
static const unsigned long kMqttRetryIntervalMs = 5000;
// Upstream #224: a router with 802.11ax on 2.4 GHz can refuse the default 11n
// join and look like a wrong password, and only 11g gets in. A unit that is
// off the network cannot be told to change, so it falls back on its own after
// this long without a link, and alternates back in case the router refuses
// 11g. Five minutes outlasts a router reboot, so a normal outage keeps 11n.
static const unsigned long kWiFiPhyFallbackMs = 5 * 60 * 1000;
static MhiPhyFallback wifi_phy = {0, MHI_PHY_11N};

// The SDK keeps the PHY mode in flash, so it is only written when it changes.
static void applyPhyModeForJoin() {
  const WiFiPhyMode_t wanted = (WiFiPhyMode_t)mhi_phy_mode_for_join(&wifi_phy, millis(), kWiFiPhyFallbackMs);
  if (WiFi.getPhyMode() == wanted) return;
  Serial.printf_P(PSTR("WiFi: joining in PHY mode %s\n"), mhi_phy_mode_text(wanted));
  WiFi.setPhyMode(wanted);
}

void handleWiFiScanResult(int WifinetworksFound) {  // Handles async WiFi scan result
  int max_rssi = -999;
  int strongest_AP = -1;

  networksFound = WifinetworksFound;  // will be used other places
 
  Serial.printf_P(PSTR("handleWiFiScanResult(): %i access points available\n"), networksFound);
  for (uint i = 0; i < networksFound; i++)
  {
    Serial.printf("%2d %25s %2d %ddBm %s %s %02x\n", i + 1, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.BSSIDstr(i).c_str(), WiFi.encryptionType(i) == ENC_TYPE_NONE ? "open" : "secured", (uint)WiFi.encryptionType(i));
    if((strcmp(WiFi.SSID(i).c_str(), WIFI_SSID) == 0) && (WiFi.RSSI(i)>max_rssi)){
        max_rssi = WiFi.RSSI(i);
        strongest_AP = i;
    }
  }
  Serial.printf_P(PSTR("current BSSID: %s, strongest BSSID: %s\n"), WiFi.BSSIDstr().c_str(), WiFi.BSSIDstr(strongest_AP).c_str());
  if((WiFi.status() != WL_CONNECTED) || ((max_rssi > WiFi.RSSI() + 10) && (strcmp(WiFi.BSSIDstr().c_str(), WiFi.BSSIDstr(strongest_AP).c_str()) != 0))) {
    if (WiFi.status() != WL_CONNECTED)  // a roam keeps the mode the link has
      applyPhyModeForJoin();
    if(strongest_AP != -1) {
      Serial.printf_P(PSTR("Connecting from bssid:%s to bssid:%s, channel:%i\n"), WiFi.BSSIDstr().c_str(), WiFi.BSSIDstr(strongest_AP).c_str(), WiFi.channel(strongest_AP));
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WiFi.channel(strongest_AP), WiFi.BSSID(strongest_AP), true);
    }
    else {
      Serial.println(F("No matching AP found (maybe hidden SSID), however try to connect."));
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    WiFiStatus = WIFI_CONNECT_ONGOING;
    Serial.println(F("WIFI_CONNECT_ONGOING"));
    WiFiTimeoutMillis = millis();
  }
  else {  // scanning is started for WiFI_SEARCHStrongestAP and WiFi was already connected
    WiFiStatus = WIFI_CONNECT_SCANNING_DONE;
    Serial.println(F("WIFI_CONNECT_SCANNING_DONE"));
  }
}

void setupWiFi(int& WiFiStatusParam) {

  if (mhi_wifi_link_lost(WiFiStatus == WIFI_CONNECT_OK, WiFi.status() == WL_CONNECTED))
    WIFI_lost++;
  if (WiFi.status() == WL_CONNECTED)
    mhi_phy_link_up(&wifi_phy, WiFi.getPhyMode(), millis());

  if(WiFiStatus != WIFI_CONNECT_ONGOING) {   // WIFI_CONNECT_OK or WIFI_CONNECT_TIMEOUT or WIFI_CONNECT_SCANNING or WIFI_CONNECT_SCANNING_DONE
    if (WiFiStatus == WIFI_CONNECT_OK || WiFiStatus == WIFI_CONNECT_TIMEOUT){  // Start scanning async if not in already in progress 
      WiFi.scanDelete();
      Serial.println(F("setupWiFi: Start async scanNetworks"));
      WiFi.scanNetworksAsync(handleWiFiScanResult);
      WiFiStatus = WIFI_CONNECT_SCANNING;
      WiFiScanStartMillis = millis();
      Serial.println(F("WIFI_CONNECT_SCANNING"));
    }
    else if (WiFiStatus == WIFI_CONNECT_SCANNING) {
      // scanNetworksAsync() does not report a scan the SDK refused to start,
      // and then handleWiFiScanResult() never runs. Without this, the state
      // machine sat here forever: no MQTT, no OTA, until a power cycle.
      // Treat it like a connection attempt: the SDK may well be connecting,
      // which is why it refused; if that does not come up, the usual timeout
      // leads to a fresh scan.
      if (mhi_scan_gave_up(WiFi.scanComplete(), millis() - WiFiScanStartMillis, kWiFiScanDeadlineMs)) {
        Serial.println(F("setupWiFi: scan did not start or finish, connecting without it"));
        // A slow scan on a unit that is still connected must not be disturbed;
        // a unit that is not connected gets the plain connect attempt that
        // handleWiFiScanResult() also falls back to when no AP was found.
        if (WiFi.status() != WL_CONNECTED) {
          applyPhyModeForJoin();
          WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
        WiFiStatus = WIFI_CONNECT_ONGOING;
        WiFiTimeoutMillis = millis();
      }
    }

    if (WiFiStatus == WIFI_CONNECT_SCANNING_DONE){ // after scanning for WiFI_SEARCHStrongestAP. Should be still connected
        WiFiStatus = WIFI_CONNECT_OK;
        Serial.println(F("WIFI_CONNECT_OK"));
    }
  }
  else { // WiFiStatus == WIFI_CONNECT_ONGOING
    if(WiFi.status() == WL_CONNECTED){
      Serial.printf_P(PSTR(" connected to %s, IP address: %s (%ddBm)\n"), WIFI_SSID, WiFi.localIP().toString().c_str(), WiFi.RSSI());
      WiFiStatus = WIFI_CONNECT_OK;
      Serial.println(F("WIFI_CONNECT_OK"));
    }
    else if(millis() - WiFiTimeoutMillis > 10*1000) {  // timeout after 10 seconds
      WiFiStatus = WIFI_CONNECT_TIMEOUT;
      Serial.println(F("WIFI_CONNECT_TIMEOUT"));
    }
  }
  WiFiStatusParam = WiFiStatus; // return WiFiStatus to caller
}

// RSSI, Uptime and FreeHeap used to be one connect-time snapshot (RSSI) or
// nothing at all, so the health check could not tell "no drops" from
// "rebooted and started counting again". Published at connect and every
// TELEMETRY_PERIOD seconds after it (#18). The uptime counter is advanced on
// every pass while connected, so it never misses the millis() wrap.
static MhiUptime uptime_counter = {0, 0, 0};
static MhiRetryPacer telemetry_pacer = {0, false};
static MhiFrameStats frame_stats = {0, 0};

static void publishTelemetryNow(uint32_t uptime_s) {
  char strtmp[12];
  itoa(WiFi.RSSI(), strtmp, 10);
  output_P((ACStatus)type_status, PSTR(TOPIC_RSSI), strtmp);
  ultoa(uptime_s, strtmp, 10);
  output_P((ACStatus)type_status, PSTR(TOPIC_UPTIME), strtmp);
  ultoa(ESP.getFreeHeap(), strtmp, 10);
  output_P((ACStatus)type_status, PSTR(TOPIC_FREE_HEAP), strtmp);
  ultoa(frame_stats.errors, strtmp, 10);
  output_P((ACStatus)type_status, PSTR(TOPIC_FRAME_ERRORS), strtmp);
  ultoa(frame_stats.timeouts, strtmp, 10);
  output_P((ACStatus)type_status, PSTR(TOPIC_FRAME_TIMEOUTS), strtmp);
}

// mhi_frame_stats classifies loop()'s return by value, because lib/mhi_pure
// cannot include MHI-AC-Ctrl-core.h (it pulls in Arduino.h). support.cpp sees
// both, so this is where the two spellings are tied together.
static_assert(err_msg_invalid_signature == -1 && err_msg_invalid_checksum == -2 && err_msg_timeout_SCK_low == -3 &&
                  err_msg_timeout_SCK_high == -4,
              "mhi_frame_stats classifies loop()'s ErrMsg by value");

void note_frame_result(int ret) {
  mhi_frame_stats_count(&frame_stats, ret);
}

// Derived once, on first use: every member of a group derives the same ID from
// the same GROUP_ROOT (fork #22 spec §2). support.h checks both at compile time.
const char* outdoor_id() {
  static char id[MHI_GROUP_ID_MAX + 1];
  if (id[0] == '\0') {
#ifdef HA_OUTDOOR_ID
    strncpy(id, HA_OUTDOOR_ID, sizeof(id) - 1);
#else
    mhi_group_default_outdoor_id(GROUP_ROOT, id, sizeof(id));
#endif
  }
  return id;
}

uint32_t uptime_seconds() {
  return mhi_uptime_advance(&uptime_counter, millis());
}

// Called on every loop() pass, connected or not: an outage longer than the
// millis() wrap must not cost the counter a wrap.
void publishTelemetry() {
  const unsigned long now = millis();
  const uint32_t uptime_s = mhi_uptime_advance(&uptime_counter, now);
  if (MQTTclient.connected() && mhi_retry_due(&telemetry_pacer, now, TELEMETRY_PERIOD * 1000UL))
    publishTelemetryNow(uptime_s);
}

int MQTTreconnect() {
  char strtmp[50];
  static int reconnect_trials=0;
  static bool mqtt_was_up = false;
  static MhiRetryPacer mqtt_retry = {0, false};
  //Serial.printf("MQTTreconnect(): (MQTTclient.state=%i), WiFi.status()=%i networksFound=%i ...\n", MQTTclient.state(), WiFi.status(), networksFound);
  if (mhi_link_dropped(&mqtt_was_up, MQTTclient.connected()))
    MQTT_lost++;
  if(!MQTTclient.connected()) {
    if (!mhi_retry_due(&mqtt_retry, millis(), kMqttRetryIntervalMs))
      return MQTT_NOT_CONNECTED;
    Serial.printf("MQTTreconnect(): Attempting MQTT connection (MQTTclient.state=%i), WiFi.status()=%i ...\n", MQTTclient.state(), WiFi.status());  // state(), see https://pubsubclient.knolleary.net/api#state
    if(reconnect_trials++>9){                                                                                                                       // WiFi.status()=3=connected, see https://realglitch.com/2018/07/arduino-wifi-status-codes/
      Serial.printf("MQTTreconnect(): reconnect_trials=%i\n", reconnect_trials);
      WiFi.disconnect(); // work around for https://github.com/esp8266/Arduino/issues/7432
      reconnect_trials=0;
    }

    if (MQTTclient.connect(HOSTNAME, MQTT_USER, MQTT_PASSWORD, MQTT_PREFIX TOPIC_CONNECTED, 0, true, PAYLOAD_CONNECTED_FALSE)) {
      Serial.println(F(" connected"));
      Serial.printf("MQTTclient.connected=%i\n", MQTTclient.connected());
      reconnect_trials=0;
      output_P((ACStatus)type_status, PSTR(TOPIC_CONNECTED), PSTR(PAYLOAD_CONNECTED_TRUE));
      output_P((ACStatus)type_status, PSTR(TOPIC_VERSION), PSTR(VERSION));
      output_P((ACStatus)type_status, PSTR(TOPIC_RESET_REASON), ESP.getResetReason().c_str());
      publishTelemetryNow(mhi_uptime_advance(&uptime_counter, millis()));
      telemetry_pacer.last_ms = millis();  // the first periodic publish is one period after this one
      telemetry_pacer.attempted = true;
      itoa(WIFI_lost, strtmp, 10);
      output_P((ACStatus)type_status, PSTR(TOPIC_WIFI_LOST), strtmp);
      itoa(MQTT_lost, strtmp, 10);
      output_P((ACStatus)type_status, PSTR(TOPIC_MQTT_LOST), strtmp);
      WiFi.BSSIDstr().toCharArray(strtmp, 20);
      output_P((ACStatus)type_status, PSTR(TOPIC_WIFI_BSSID), strtmp);
      output_P((ACStatus)type_status, PSTR(TOPIC_WIFI_PHY), mhi_phy_mode_text(WiFi.getPhyMode()));

      // for testing publish list of access points with the expected SSID 
      Serial.printf("MQTTreconnect(): %i access points available\n", networksFound);         
      for (uint i = 0; i < networksFound; i++)
      {
        if(strcmp(WiFi.SSID(i).c_str(), WIFI_SSID) == 0){
          strcpy(strtmp, "BSSID:");
          strcat(strtmp, WiFi.BSSIDstr(i).c_str());
          char strtmp2[20];
          strcat(strtmp, " RSSI:");
          itoa(WiFi.RSSI(i), strtmp2, 10);
          strcat(strtmp, strtmp2);
          MQTTclient.publish(MQTT_PREFIX "APs", strtmp, true);
        }
      }

      itoa(rising_edge_cnt.SCK, strtmp, 10);
      output_P((ACStatus)type_status, PSTR(TOPIC_FSCK), strtmp);
      itoa(rising_edge_cnt.MOSI, strtmp, 10);
      output_P((ACStatus)type_status, PSTR(TOPIC_FMOSI), strtmp);
      itoa(rising_edge_cnt.MISO, strtmp, 10);
      output_P((ACStatus)type_status, PSTR(TOPIC_FMISO), strtmp);
      mhi_wiring_fault_text(wiring_faults, strtmp, sizeof(strtmp));
      output_P((ACStatus)type_status, PSTR(TOPIC_WIRING), strtmp);


      MQTTclient.subscribe(MQTT_SET_PREFIX "#");
      MQTTclient.subscribe(GROUP_ROOT "members/+");  // every unit's record, this one's included (fork #22)
      return MQTT_RECONNECTED;
    }
    else {
      Serial.print(F(" reconnect failed, reason "));
      itoa(MQTTclient.state(), strtmp, 10);
      Serial.print(strtmp);
      Serial.print(", WiFi status: ");
      Serial.println(WiFi.status());
      return MQTT_NOT_CONNECTED;
    }
  }
  mhi_retry_reset(&mqtt_retry);
  MQTTclient.loop();
  return MQTT_CONNECT_OK;  // ours, not PubSubClient's MQTT_CONNECTED; both are 0
}

void publish_cmd_ok() {
  output_P((ACStatus)type_status, PSTR(TOPIC_CMD_RECEIVED), PSTR(PAYLOAD_CMD_OK));
}
void publish_cmd_unknown() {
  output_P((ACStatus)type_status, PSTR(TOPIC_CMD_RECEIVED), PSTR(PAYLOAD_CMD_UNKNOWN));
}
void publish_cmd_invalidparameter() {
  output_P((ACStatus)type_status, PSTR(TOPIC_CMD_RECEIVED), PSTR(PAYLOAD_CMD_INVALID_PARAMETER));
}

// The outdoor unit's values (fork #22 spec §4.1): the same on every indoor
// unit, so only the group's publisher writes them, under the group root. Only
// their opdata_* statuses: the erropdata_* ones sharing their case labels in
// main.cpp are this unit's own error snapshot and stay under
// MQTT_ERR_OP_PREFIX.
static bool is_system_value(ACStatus status) {
  switch (status) {
    case opdata_outdoor:
    case opdata_ct:
    case opdata_comp:
    case opdata_defrost:
    case opdata_total_comp_run:
    case opdata_protection_no:
    case opdata_td:
    case opdata_tdsh:
    case opdata_tho_r1:
    case opdata_thi_r2:
    case opdata_ou_fanspeed:
      return true;
    default:  // every other status, KWH and OU-EEV1 included: this unit's own
      return false;
  }
}

void output_P(const ACStatus status, PGM_P topic, PGM_P payload) {
  const int mqtt_topic_size = 100;
  char mqtt_topic[mqtt_topic_size];
  
  Serial.printf_P(PSTR("status=%i topic=%s payload=%s\n"), status, topic, payload);
  
  PGM_P prefix;
  if ((status & 0xc0) == type_status)
    prefix = PSTR(MQTT_PREFIX);
  else if ((status & 0xc0) == type_opdata && is_system_value(status)) {
    if (!group_may_publish_system())
      return;  // not the publisher, or still in the grace period: dropped (spec §6.4)
    prefix = PSTR(GROUP_OP_PREFIX);
  }
  else if ((status & 0xc0) == type_opdata)
    prefix = PSTR(MQTT_OP_PREFIX);
  else if ((status & 0xc0) == type_erropdata)
    prefix = PSTR(MQTT_ERR_OP_PREFIX);
  else {
    // Previously mqtt_topic was left uninitialised here and then appended to.
    Serial.printf_P(PSTR("output_P: status 0x%02x has no known type, not publishing\n"), status);
    return;
  }

  strncpy_P(mqtt_topic, prefix, mqtt_topic_size - 1);
  mqtt_topic[mqtt_topic_size - 1] = '\0';  // strncpy does not terminate on overflow
  const size_t prefix_len = strlen(mqtt_topic);
  // strncat appends n characters *plus* a NUL, so the limit is one below the
  // remaining space. Passing the remaining space, as this used to, writes one
  // byte past the buffer.
  strncat_P(mqtt_topic, topic, mqtt_topic_size - prefix_len - 1);

  if (strlen(mqtt_topic) != prefix_len + strlen_P(topic)) {
    Serial.printf_P(PSTR("output_P: topic does not fit in %i bytes, not publishing\n"), mqtt_topic_size);
    return;
  }
  MQTTclient.publish_P(mqtt_topic, payload, true);
}

#if TEMP_MEASURE_PERIOD > 0
OneWire oneWire(ONE_WIRE_BUS);       // Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
DallasTemperature sensors(&oneWire); // Pass our oneWire reference to Dallas Temperature.
DeviceAddress insideThermometer;     // arrays to hold device address

// DallasTemperature 4.x reports these instead of a temperature. The 85 degC
// power-on-reset value in particular used to reach us as a plausible-looking
// reading and was only rejected by the > 48 degC sanity clamp further down.
static bool ds18x20_reading_is_fault(int16_t raw) {
  return raw == DEVICE_DISCONNECTED_RAW
      || raw == DEVICE_FAULT_OPEN_RAW
      || raw == DEVICE_FAULT_SHORTGND_RAW
      || raw == DEVICE_FAULT_SHORTVDD_RAW
      || raw == DEVICE_POWER_ON_RESET_RAW
      || raw == DEVICE_INSUFFICIENT_POWER_RAW;
}

byte getDs18x20Temperature(int temp_hysterese) {
  static unsigned long DS1820Millis = millis();
  static int16_t tempR_old = 0;
  // "No reading yet" used to be encoded as tempR_old = 0xffff, which worked
  // only because the conversion below returned 0 for anything negative and the
  // caller then dropped the 0. Now that sub-zero temperatures encode properly,
  // the absence of a reading has to be tracked explicitly.
  static bool have_reading = false;

  if (millis() - DS1820Millis > TEMP_MEASURE_PERIOD * 1000) {
    int16_t tempR = sensors.getTemp(insideThermometer);
    DS1820Millis = millis();

    if (ds18x20_reading_is_fault(tempR)) {
      Serial.printf_P(PSTR("DS18x20 fault, raw=%i\n"), tempR);
      have_reading = false;
      sensors.requestTemperatures();
      return DS18X20_NOT_CONNECTED;
    }

    tempR += ROOM_TEMP_DS18X20_OFFSET*128;
    if (!mhi_ds18x20_raw_plausible(tempR)) {    // skip onrealistic values
      if (!have_reading) {                      // nothing sane to fall back to
        sensors.requestTemperatures();
        return DS18X20_NOT_CONNECTED;
      }
      tempR = tempR_old;    // use previous value
    }

    int16_t tempR_diff = tempR - tempR_old; // avoid using other functions inside the brackets of abs, see https://www.arduino.cc/reference/en/language/functions/math/abs/
    if (!have_reading || abs(tempR_diff) > temp_hysterese) {
      tempR_old = tempR;
      have_reading = true;
      char strtmp[10];
      dtostrf(sensors.rawToCelsius(tempR), 0, 2, strtmp);
      //Serial.printf_P(PSTR("new DS18x20 temperature=%s°C\n"), strtmp);
      output_P((ACStatus)type_status, PSTR(TOPIC_TDS1820), strtmp);
    }
    sensors.requestTemperatures();
  }
  //Serial.printf_P(PSTR("temp DS18x20 tempR_old=%i %i\n"), tempR_old, mhi_troom_from_ds18x20_raw(tempR_old));
  if (!have_reading) return DS18X20_NOT_CONNECTED;
  return mhi_troom_from_ds18x20_raw(tempR_old);
}

void printAddress(DeviceAddress deviceAddress) {
  for (uint8_t i = 0; i < 8; i++)
  {
    if (deviceAddress[i] < 16)
      Serial.print(F("0"));
    Serial.print(deviceAddress[i], HEX);
  }
}

void setup_ds18x20() {
  sensors.begin();
  Serial.printf_P(PSTR("Found %i DS18xxx family devices.\n"), sensors.getDS18Count());
  if (!sensors.getAddress(insideThermometer, 0))
    Serial.println(F("Unable to find address for Device 0"));
  else {
    Serial.print(F("Device 0 Address: 0x"));
    printAddress(insideThermometer);  // %02x on the array printed the pointer
    Serial.println();
  }
  sensors.setResolution(insideThermometer, 9); // set the resolution to 9 bit
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures(); // Send the command to get temperatures
}
#endif

void setupOTA() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  if (strcmp(OTA_PASSWORD, "") != 0)
    ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";
    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println(F("\nEnd"));
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf_P(PSTR("Progress: %u%%\n"), (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf_P(PSTR("Error[%u]\n"), error);
    if (error == OTA_AUTH_ERROR)
      Serial.println(F("Auth Failed"));
    else if (error == OTA_BEGIN_ERROR)
      Serial.println(F("Begin Failed"));
    else if (error == OTA_CONNECT_ERROR)
      Serial.println(F("Connect Failed"));
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println(F("Receive Failed"));
    else if (error == OTA_END_ERROR)
      Serial.println(F("End Failed"));
  });
  ArduinoOTA.begin();
  Serial.println(F("OTA Ready"));
}
