// MQTT link: the only part of the sketch that knows which broker we use.
// Currently set up for ThingsBoard's device MQTT API. To move to another
// broker, change the topics and the connect/config details in this file;
// the main sketch only calls these four functions:
//
//   mqttBegin(configKeys, onConfig, onConnect)   once, in setup()
//   mqttLoop()                                   every loop(); never blocks for long
//   mqttSendTelemetry(json, len)                 sensor readings
//   mqttSendAttributes(json, len)                device info / current settings
//
// onConfig(obj) gets a flat JSON object of settings, e.g. {"perimeter_m":4,"alarm_on":false}.
#pragma once
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "secrets.h"   // copy secrets.example.h -> secrets.h

// ---- ThingsBoard topics ----
#define TOPIC_TELEMETRY  "v1/devices/me/telemetry"
#define TOPIC_ATTRIBUTES "v1/devices/me/attributes"            // out: client attributes, in: shared-attribute updates
#define TOPIC_ATTR_REQ   "v1/devices/me/attributes/request/1"  // ask for current shared attributes
#define TOPIC_ATTR_RESP  "v1/devices/me/attributes/response/+"

typedef void (*ConfigHandler)(JsonObjectConst settings);
typedef void (*ConnectHandler)();

WiFiClient   mqttNet;
PubSubClient mqtt(mqttNet);
ConfigHandler  cfgHandler  = nullptr;
ConnectHandler connHandler = nullptr;
const char*    cfgKeys     = "";

void mqttOnMessage(char* topic, byte* payload, unsigned int len) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, len)) return;
  // A reply to our request wraps settings in {"shared": {...}}; live updates are flat.
  JsonObjectConst settings = doc["shared"].is<JsonObjectConst>() ? doc["shared"].as<JsonObjectConst>()
                                                                  : doc.as<JsonObjectConst>();
  if (cfgHandler && !settings.isNull()) cfgHandler(settings);
}

void mqttBegin(const char* configKeys, ConfigHandler onConfig, ConnectHandler onConnect) {
  cfgKeys = configKeys; cfgHandler = onConfig; connHandler = onConnect;
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttOnMessage);
  mqtt.setBufferSize(1024);     // default 256 bytes is too small
  mqtt.setSocketTimeout(2);     // don't stall the sensors for long
  Serial.println("Wi-Fi: connecting in the background...");
}

bool mqttSendTelemetry(const char* json, size_t len)  { return mqtt.connected() && mqtt.publish(TOPIC_TELEMETRY,  (const uint8_t*)json, len); }
bool mqttSendAttributes(const char* json, size_t len) { return mqtt.connected() && mqtt.publish(TOPIC_ATTRIBUTES, (const uint8_t*)json, len); }

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

  Serial.print("MQTT: connecting... ");
  const char* pass = strlen(MQTT_PASS) ? MQTT_PASS : nullptr;
  if (!mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, pass)) {
    Serial.printf("failed (state %d). Check host, port and token.\n", mqtt.state());
    return;
  }
  Serial.println("connected.");
  mqtt.subscribe(TOPIC_ATTRIBUTES);
  mqtt.subscribe(TOPIC_ATTR_RESP);
  char req[160];
  snprintf(req, sizeof(req), "{\"sharedKeys\":\"%s\"}", cfgKeys);
  mqtt.publish(TOPIC_ATTR_REQ, req);      // fetch current settings
  if (connHandler) connHandler();
}
