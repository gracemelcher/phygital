// MQTT link: the only part of the sketch that knows about the network and broker.
// Works with any standard MQTT broker (hosted EMQX Cloud / HiveMQ, or a local Mosquitto).
// The main sketch only calls these functions:
//
//   mqttBegin(onMessage, onConnect)   once, in setup()
//   mqttLoop()                        every loop(); never blocks for long
//   mqttSendTelemetry(json, len)      sensor readings (changed values)
//   mqttSendInfo(json, len)           device info + settings in use (retained)
//   mqttSendSettings(json, len)       the settings in use, so the broker's copy stays current (retained)
//
// Topics, with ID = MQTT_CLIENT_ID (e.g. "phygital-01") and ROOT = MQTT_TOPIC_ROOT ("phygital"):
//   ROOT/ID/telemetry   device -> app    changed sensor values
//   ROOT/ID/info        device -> app    retained: firmware, sensor health, active_* settings
//   ROOT/ID/status      device -> app    retained: "online", or "offline" (set by the broker if the device drops)
//   ROOT/ID/settings    app <-> device   retained: {"perimeter_m":6,"volume":0.01,"alarm_on":true,"led_on":true}
//   ROOT/ID/cmd         app -> device    one-off commands: {"hello":true}, {"preview":true}
// onMessage(obj) gets the JSON object from settings or cmd.
#pragma once
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "secrets.h"   // copy secrets.example.h -> secrets.h
#include "mqtt_ca.h"

#ifndef MQTT_TOPIC_ROOT
#define MQTT_TOPIC_ROOT "phygital"
#endif
#ifndef MQTT_TLS
#define MQTT_TLS 1
#endif

typedef void (*MessageHandler)(JsonObjectConst msg);
typedef void (*ConnectHandler)();

#if MQTT_TLS
WiFiClientSecure mqttNet;
#else
WiFiClient mqttNet;
#endif
PubSubClient   mqtt(mqttNet);
MessageHandler msgHandler  = nullptr;
ConnectHandler connHandler = nullptr;
char tTelemetry[64], tInfo[64], tStatus[64], tSettings[64], tCmd[64];

void mqttOnMessage(char* topic, byte* payload, unsigned int len) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, len) || !doc.is<JsonObject>()) return;
  if (msgHandler) msgHandler(doc.as<JsonObjectConst>());
}

void mqttBegin(MessageHandler onMessage, ConnectHandler onConnect) {
  msgHandler = onMessage; connHandler = onConnect;
  const char* r = MQTT_TOPIC_ROOT; const char* id = MQTT_CLIENT_ID;
  snprintf(tTelemetry, sizeof(tTelemetry), "%s/%s/telemetry", r, id);
  snprintf(tInfo,      sizeof(tInfo),      "%s/%s/info",      r, id);
  snprintf(tStatus,    sizeof(tStatus),    "%s/%s/status",    r, id);
  snprintf(tSettings,  sizeof(tSettings),  "%s/%s/settings",  r, id);
  snprintf(tCmd,       sizeof(tCmd),       "%s/%s/cmd",       r, id);
  WiFi.setAutoReconnect(true);

  // Diagnostics: show whether our network is visible, and why the access point refuses us.
  int n = WiFi.scanNetworks();
  int seen = 0;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) != WIFI_SSID) continue;
    seen++;
    Serial.printf("Wi-Fi: sees \"%s\" on channel %d, signal %d dBm, %s\n", WIFI_SSID, WiFi.channel(i), WiFi.RSSI(i),
                  WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open (no password)" : "needs a password");
  }
  if (!seen) Serial.printf("Wi-Fi: \"%s\" not visible here (%d other networks seen)\n", WIFI_SSID, n);
  WiFi.scanDelete();
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    static int lastReason = -1;
    int reason = info.wifi_sta_disconnected.reason;
    if (reason != lastReason) {
      lastReason = reason;
      Serial.printf("Wi-Fi: refused, reason %d (%s)\n", reason, WiFi.disconnectReasonName((wifi_err_reason_t)reason));
    }
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  // Some ESP32-S3 clone boards can hear access points but can't complete the
  // handshake at full transmit power (shows as AUTH_EXPIRE). Lower power fixes it.
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
#if MQTT_TLS
  mqttNet.setCACert(MQTT_CA_CERT);
#endif
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttOnMessage);
  mqtt.setBufferSize(1024);     // default 256 bytes is too small
  mqtt.setSocketTimeout(4);     // TLS handshakes need a little longer
  mqtt.setKeepAlive(30);        // broker marks us offline ~45 s after we vanish
  Serial.printf("Wi-Fi: connecting in the background... (MQTT topics %s/%s/...)\n", MQTT_TOPIC_ROOT, MQTT_CLIENT_ID);
}

static bool pub(const char* topic, const char* json, size_t len, bool retained) {
  return mqtt.connected() && mqtt.publish(topic, (const uint8_t*)json, len, retained);
}
bool mqttSendTelemetry(const char* json, size_t len) { return pub(tTelemetry, json, len, false); }
bool mqttSendInfo(const char* json, size_t len)      { return pub(tInfo, json, len, true); }
bool mqttSendSettings(const char* json, size_t len)  { return pub(tSettings, json, len, true); }

void mqttLoop() {
  static bool wifiAnnounced = false;
  static unsigned long lastWifiMsg = 0, lastTry = 0;

  if (WiFi.status() != WL_CONNECTED) {
    wifiAnnounced = false;
    if (millis() - lastWifiMsg > 5000) {
      lastWifiMsg = millis();
      int s = WiFi.status();
      Serial.printf("Wi-Fi: not connected (status %d)%s\n", s,
                    s == WL_NO_SSID_AVAIL ? " - network not found (wrong name or 5 GHz?)" :
                    s == WL_CONNECT_FAILED ? " - wrong password?" : "");
    }
    return;
  }
  if (!wifiAnnounced) {
    wifiAnnounced = true;
    Serial.printf("Wi-Fi connected, IP %s, signal %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }
  if (mqtt.connected()) { mqtt.loop(); return; }
  if (millis() - lastTry < 5000) return;   // retry every 5 s
  lastTry = millis();

  Serial.printf("MQTT: connecting to %s:%d... ", MQTT_HOST, MQTT_PORT);
  const char* user = strlen(MQTT_USER) ? MQTT_USER : nullptr;
  const char* pass = strlen(MQTT_PASS) ? MQTT_PASS : nullptr;
  // Last will: if we drop off, the broker sets status to "offline" for us.
  if (!mqtt.connect(MQTT_CLIENT_ID, user, pass, tStatus, 1, true, "offline")) {
    int st = mqtt.state();
    Serial.printf("failed (state %d)%s\n", st,
                  st == 4 || st == 5 ? " - wrong MQTT_USER / MQTT_PASS" :
                  st == -2 ? " - can't reach the broker (host, port, TLS certificate, or network blocks the port)" : "");
    return;
  }
  Serial.println("connected.");
  mqtt.publish(tStatus, "online", true);
  mqtt.subscribe(tSettings, 1);           // the broker sends the retained settings straight away
  mqtt.subscribe(tCmd);
  if (connHandler) connHandler();
}
