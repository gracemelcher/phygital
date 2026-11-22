// Copy this file to secrets.h (same folder) and fill it in.
// secrets.h is gitignored, so your real passwords never get committed.
#pragma once

// Wi-Fi (2.4 GHz networks only). Harvard: "Harvard University" with an empty password,
// after registering the board's MAC address (see README).
#define WIFI_SSID      "YOUR_WIFI"
#define WIFI_PASS      "YOUR_PASSWORD"

// MQTT broker (see README "Broker setup"). Hosted brokers use TLS on port 8883.
#define MQTT_HOST      "your-deployment.emqxsl.com"
#define MQTT_PORT      8883
#define MQTT_TLS       1               // 0 for a plain local broker on port 1883
#define MQTT_USER      "phygital-device"
#define MQTT_PASS      "YOUR_DEVICE_PASSWORD"
#define MQTT_CLIENT_ID "phygital-01"   // also the device's name in the app and topics
