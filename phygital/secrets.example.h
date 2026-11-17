// Copy this file to secrets.h (same folder) and fill it in.
// secrets.h is gitignored, so your real passwords never get committed.
#pragma once

// Wi-Fi (2.4 GHz networks only)
#define WIFI_SSID      "YOUR_WIFI"
#define WIFI_PASS      "YOUR_PASSWORD"

// MQTT broker
// ThingsBoard Cloud: host "mqtt.thingsboard.cloud" (Europe: "mqtt.eu.thingsboard.cloud"),
// user = the device's access token, password = "" (empty).
#define MQTT_HOST      "mqtt.thingsboard.cloud"
#define MQTT_PORT      1883
#define MQTT_USER      "YOUR_DEVICE_ACCESS_TOKEN"
#define MQTT_PASS      ""
#define MQTT_CLIENT_ID "phygital-01"
