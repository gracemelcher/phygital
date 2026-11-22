// Phygital motion alarm (ESP32-S3-DevKitC-1-N8R2)
//   3x DFRobot C4002 radar + INMP441 mic + Edge Impulse footsteps model
//   + RGB LED + MAX98357A speaker, reporting over Wi-Fi/MQTT (see mqtt_link.h).
//
// LED:     RED = someone coming closer, GREEN = going away, BLUE = same distance, OFF = nobody
// Speaker: entry beep when someone walks into the perimeter, then beeps that speed up as they approach
// Remote settings (MQTT topic phygital/<id>/settings, or USB): perimeter_m, volume, alarm_on, led_on
//
// Wiring (GPIO numbers; radar TX goes to the ESP32's RX pin and vice versa)
//   Radar 1: TX->4,  RX->5     Radar 2: TX->6, RX->7     Radar 3: TX->15, RX->16
//   All radars: VIN->5V rail, GND->GND rail (rails fed by a separate 5V supply; the S3 runs from USB)
//   Mic: SCK->9, WS->10, SD->11, L/R->GND, VDD->3V3
//   Amp: BCLK->47, LRC->39, DIN->21, Vin->5V rail, SD->Vin, GND->GND rail (not an S3 GND pin);
//        GAIN unconnected (9 dB, set loudness with volume)
//   LED: R->220R->1, G->220R->42, B->220R->41, common->GND
//
// Arduino IDE: Board "ESP32S3 Dev Module", USB Mode: Hardware CDC and JTAG, USB CDC On Boot: Enabled, Flash Size: 8MB,
//   Partition Scheme: 8M with spiffs, PSRAM: QSPI PSRAM. Plug into the port labelled "USB".
//   (Radar 3 uses UART0, so Serial must run over native USB instead.)
//
// The C4002 driver is bundled in this folder (with the required packed-struct fix).
#if !CONFIG_IDF_TARGET_ESP32S3
#error "This sketch is for the ESP32-S3: Tools > Board > ESP32S3 Dev Module"
#endif
#if !ARDUINO_USB_CDC_ON_BOOT
#error "Set Tools > USB CDC On Boot > Enabled (radar 3 needs UART0)"
#endif
#include <Project_v2_footsteps_inferencing.h>   // Edge Impulse model (Add .ZIP Library)
#include <ESP_I2S.h>
#include <arduinoFFT.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_system.h>
#include "DFRobot_C4002.h"
#include "mqtt_link.h"

const char* FW_VERSION = "0.5.0-s3";

// Types (must be at the top for Arduino IDE)
const int HIST = 8;               // distance history: 8 readings x 0.2 s = ~1.6 s
struct RadarInfo {
  bool  online = false;
  int   state = 0;                // 0 nothing, 1 still, 2 moving
  float dist = 0, speed = 0;
  int   dir = 1;                  // radar's own direction: 0 away, 1 none, 2 approaching
  int   energy = 0;
  unsigned long updated = 0;
  float histD[HIST], histT[HIST]; // recent distances (m) and times (s)
  int   histCount = 0, histPos = 0;
  unsigned long lastMoving = 0;
};
enum Motion { NONE, CLOSER, AWAY, SAME };
enum LedColor { OFF, RED, GREEN, BLUE };

// ================= REMOTE SETTINGS (defaults; MQTT can change them) =================
struct Config {
  float perimeterM = 6.0;         // perimeter radius for the speaker alarm (m)
  float volume     = 0.01;        // speaker loudness, 0-1.0 (0.01 is very quiet; each doubling is ~6 dB louder)
  bool  alarmOn    = true;        // speaker on/off
  bool  ledOn      = true;        // LED on/off
} cfg;

// ================= FIXED SETTINGS =================
const bool  INCLUDE_STILL = false;       // alarm: false = only MOVING people count
const unsigned long EXIT_DELAY_MS = 2000;
const int   BEEP_HZ = 1000, BEEP_MS = 80;
const unsigned long SLOWEST_MS = 1500;   // beep interval at the edge of the perimeter
const unsigned long FASTEST_MS = 200;    // beep interval at ~1 m
const float TREND_THRESHOLD = 0.25;      // m/s: slower distance change = "same distance"
const char* FOOTSTEP_LABEL = "human_footsteps";   // must match the model's label
const float AI_CONFIDENCE = 0.70;        // report footsteps only above 70 % sure
const int   MIC_SHIFT = 14;              // mic gain for the AI: 13 = louder, 15 = quieter
const int   QUIET_BELOW = 60, LOUD_ABOVE = 600;
const unsigned long PUBLISH_MS   = 1000;   // send changed values once per second
const unsigned long HEARTBEAT_MS = 30000;  // send everything every 30 s

// ================= LED =================
const int LED_RED = 1, LED_GREEN = 42, LED_BLUE = 41;
const unsigned long LED_HOLD_MS = 500;
LedColor currentColor = OFF;
unsigned long colorSince = 0;

void showColor(LedColor c) {
  digitalWrite(LED_RED,   c == RED   ? HIGH : LOW);
  digitalWrite(LED_GREEN, c == GREEN ? HIGH : LOW);
  digitalWrite(LED_BLUE,  c == BLUE  ? HIGH : LOW);
}

void setLed(LedColor c) {
  if (!cfg.ledOn) c = OFF;
  if (c == currentColor) return;
  if (millis() - colorSince < LED_HOLD_MS && cfg.ledOn) return;
  currentColor = c;
  colorSince = millis();
  showColor(c);
}

// ================= SPEAKER =================
const int AMP_BCLK = 47, AMP_LRC = 39, AMP_DIN = 21, AMP_RATE = 16000;
I2SClass amp;
bool ampOK = false;

void ampTone(float freq, int ms) {
  int16_t buf[256];
  int total = AMP_RATE * ms / 1000;
  static float phase = 0;
  float step = 2 * PI * freq / AMP_RATE;
  for (int done = 0; done < total; done += 256) {
    for (int i = 0; i < 256; i++) {
      buf[i] = (freq > 0) ? (int16_t)(sin(phase) * cfg.volume * 32767) : 0;
      phase += step;
      if (phase > 2 * PI) phase -= 2 * PI;
    }
    amp.write((uint8_t*)buf, sizeof(buf));
  }
}

void beep(int freq = BEEP_HZ, int ms = BEEP_MS) {
  if (!ampOK || !cfg.alarmOn) return;
  ampTone(freq, ms);
  ampTone(0, 100);                // flush with silence
}

// ================= RADARS =================
// The S3 has three UARTs; radar 3 takes UART0 (Serial0) because Serial runs over native USB.
const int NUM_RADARS = 3;
HardwareSerial* radarPort[NUM_RADARS] = { &Serial1, &Serial2, &Serial0 };
DFRobot_C4002 radars[NUM_RADARS] = {
  DFRobot_C4002(radarPort[0], 115200, 4, 5),      // ESP RX 4, TX 5
  DFRobot_C4002(radarPort[1], 115200, 6, 7),      // ESP RX 6, TX 7
  DFRobot_C4002(radarPort[2], 115200, 15, 16),    // ESP RX 15, TX 16
};
RadarInfo r[NUM_RADARS];

void addHistory(RadarInfo &r, float d) {
  r.histD[r.histPos] = d;
  r.histT[r.histPos] = millis() / 1000.0;
  r.histPos = (r.histPos + 1) % HIST;
  if (r.histCount < HIST) r.histCount++;
}

// Best-fit slope of distance over time (m/s). Negative = getting closer.
float distanceSlope(const RadarInfo &r) {
  if (r.histCount < 4) return 0;
  int first = (r.histPos - r.histCount + HIST) % HIST;
  float t0 = r.histT[first], sT = 0, sD = 0, sTT = 0, sTD = 0;
  for (int i = 0; i < r.histCount; i++) {
    int k = (first + i) % HIST;
    float t = r.histT[k] - t0, d = r.histD[k];
    sT += t; sD += d; sTT += t * t; sTD += t * d;
  }
  float n = r.histCount, den = n * sTT - sT * sT;
  return (den == 0) ? 0 : (n * sTD - sT * sD) / den;
}

void pollRadar(DFRobot_C4002 &radar, HardwareSerial &port, RadarInfo &info) {
  if (port.available() < 8) return;               // nothing new yet: don't wait
  sRetResult_t ret = radar.getNoteInfo();
  if (ret.noteType != eResult) return;

  eTargetState_t s = radar.getTargetState();
  info.updated = millis();
  if (s == eMotion) {
    sMotionTarget_t m = radar.getMotionTargetInfo();
    info.state = 2; info.dist = m.distance; info.speed = m.speed;
    info.dir = (int)m.direction; info.energy = m.energy;
    if (m.distance > 0) { addHistory(info, m.distance); info.lastMoving = millis(); }
  } else if (s == ePresence) {
    sPresenceTarget_t p = radar.getPresenceTargetInfo();
    info.state = 1; info.dist = p.distance; info.speed = 0;
    info.dir = 1; info.energy = p.energy;
  } else {
    info.state = 0; info.dist = 0; info.speed = 0; info.dir = 1; info.energy = 0;
  }
  if (millis() - info.lastMoving > 1500) { info.histCount = 0; info.histPos = 0; }   // stale trend
}

bool setupRadar(DFRobot_C4002 &radar, const char* name) {
  for (int tries = 0; tries < 5; tries++) {
    if (radar.begin()) {
      delay(50);
      radar.setRunLedState(eLedOn);              delay(50);
      radar.setOutLedState(eLedOn);              delay(50);
      radar.setResolutionMode(eResolution80Cm);  delay(50);   // up to ~11 m
      radar.setDetectRange(0, 1100);             delay(50);
      radar.setLightThresh(0);                   delay(50);
      uint8_t gates[15];
      for (int i = 0; i < 15; i++) gates[i] = C4002_ENABLE;
      radar.configureGate(eMotionDistGate, gates);   delay(50);
      radar.configureGate(ePresenceDistGate, gates); delay(50);
      radar.setTargetDisappearDelay(1);          delay(50);
      radar.setReportPeriod(2);                  // new reading every 0.2 s
      Serial.printf("%s connected.\n", name);
      return true;
    }
    Serial.printf("%s not found, retrying...\n", name);
    delay(1000);
  }
  Serial.printf("%s FAILED - check its TX/RX, VIN (5V), GND\n", name);
  return false;
}

bool fresh(const RadarInfo &r) { return r.online && millis() - r.updated <= 3000; }

Motion motionOf(const RadarInfo &r) {
  if (!fresh(r) || r.state == 0) return NONE;
  if (r.state == 1) return SAME;                  // standing still
  if (r.histCount >= 4) {                         // use the distance trend
    float slope = distanceSlope(r);
    return slope < -TREND_THRESHOLD ? CLOSER : slope > TREND_THRESHOLD ? AWAY : SAME;
  }
  return r.dir == 2 ? CLOSER : r.dir == 0 ? AWAY : SAME;   // not enough history yet
}

bool inPerimeter(const RadarInfo &r) {
  if (!fresh(r)) return false;
  bool counts = INCLUDE_STILL ? (r.state > 0) : (r.state == 2);
  return counts && r.dist > 0 && r.dist <= cfg.perimeterM;
}

// ================= MICROPHONE + SOUND AI =================
const int MIC_SCK = 9, MIC_WS = 10, MIC_SD = 11;
const int SAMPLE_RATE = EI_CLASSIFIER_FREQUENCY;     // 44100 Hz, what the model was trained on
const int N = 512;
I2SClass i2s;
int32_t micBuf[N * 2];                               // left + right interleaved
float vReal[N], vImag[N];
ArduinoFFT<float> FFT(vReal, vImag, N, SAMPLE_RATE);
int   micLevel = 0;  float micPeakHz = 0;            // loudest chunk since the last status line
int   pubMicLevel = 0; float pubMicHz = 0;           // loudest since the last publish
bool  micOK = false;

const int AI_WINDOW = EI_CLASSIFIER_RAW_SAMPLE_COUNT;  // 1 second of audio
int16_t *aiBuf = NULL;            // ring buffer of the last second
int   aiPos = 0, aiNewSamples = 0;
bool  aiOK = false;
const char* aiLabel = "waiting";
float aiScore = 0;
bool  aiHuman = false;

int aiGetData(size_t offset, size_t length, float *out) {
  for (size_t i = 0; i < length; i++) out[i] = (float)aiBuf[(aiPos + offset + i) % AI_WINDOW];
  return 0;
}

void runSoundAI() {
  if (!aiOK || aiNewSamples < AI_WINDOW) return;   // wait for a full fresh second
  aiNewSamples = 0;
  signal_t signal;
  signal.total_length = AI_WINDOW;
  signal.get_data = &aiGetData;
  ei_impulse_result_t result = { 0 };
  if (run_classifier(&signal, &result, false) != EI_IMPULSE_OK) { aiLabel = "ai_error"; return; }

  int best = 0;
  for (int i = 1; i < EI_CLASSIFIER_LABEL_COUNT; i++)
    if (result.classification[i].value > result.classification[best].value) best = i;
  aiLabel = result.classification[best].label;
  aiScore = result.classification[best].value;

  bool human = strcmp(aiLabel, FOOTSTEP_LABEL) == 0 && aiScore >= AI_CONFIDENCE;
  if (human && !aiHuman) Serial.printf(">>> SOUND AI: HUMAN FOOTSTEPS (%.0f%% sure)\n", aiScore * 100);
  aiHuman = human;
}

int channelRMS(int ch) {
  double mean = 0, e = 0;
  for (int i = 0; i < N; i++) mean += (micBuf[2 * i + ch] >> 14);
  mean /= N;
  for (int i = 0; i < N; i++) { double v = (micBuf[2 * i + ch] >> 14) - mean; e += v * v; }
  return (int)sqrt(e / N);
}

void readMic() {
  if (!micOK) return;
  if (i2s.readBytes((char*)micBuf, sizeof(micBuf)) < sizeof(micBuf)) return;
  int rL = channelRMS(0), rR = channelRMS(1);
  int ch = (rL >= rR) ? 0 : 1;
  int level = max(rL, rR);

  if (aiOK) {                                      // feed the AI's ring buffer
    for (int i = 0; i < N; i++) {
      aiBuf[aiPos] = (int16_t)constrain(micBuf[2 * i + ch] >> MIC_SHIFT, -32768, 32767);
      aiPos = (aiPos + 1) % AI_WINDOW;
    }
    aiNewSamples += N;
  }

  if (level <= micLevel) return;                   // keep the loudest chunk only
  micLevel = level;
  micPeakHz = 0;
  if (level < QUIET_BELOW) return;
  double mean = 0;
  for (int i = 0; i < N; i++) { vReal[i] = (float)(micBuf[2 * i + ch] >> 14); mean += vReal[i]; }
  mean /= N;
  for (int i = 0; i < N; i++) { vReal[i] -= mean; vImag[i] = 0; }
  FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
  FFT.compute(FFTDirection::Forward);
  FFT.complexToMagnitude();
  micPeakHz = FFT.majorPeak();
}

// ================= REMOTE CONFIG =================
Preferences prefs;                  // settings survive a reboot
volatile bool configChanged = false;

void loadConfig() {
  prefs.begin("phygital", true);
  cfg.perimeterM = prefs.getFloat("perimeter", cfg.perimeterM);
  cfg.volume     = prefs.getFloat("volume", cfg.volume);
  cfg.alarmOn    = prefs.getBool("alarm", cfg.alarmOn);
  cfg.ledOn      = prefs.getBool("led", cfg.ledOn);
  prefs.end();
}

void saveConfig() {
  prefs.begin("phygital", false);
  prefs.putFloat("perimeter", cfg.perimeterM);
  prefs.putFloat("volume", cfg.volume);
  prefs.putBool("alarm", cfg.alarmOn);
  prefs.putBool("led", cfg.ledOn);
  prefs.end();
}

bool takeFloat(JsonObjectConst c, const char* k, float &f, float lo, float hi) {
  if (!c[k].is<float>()) return false;
  float v = constrain(c[k].as<float>(), lo, hi);
  if (v == f) return false;
  f = v; return true;
}
bool takeBool(JsonObjectConst c, const char* k, bool &b) {
  if (!c[k].is<bool>() || c[k].as<bool>() == b) return false;
  b = c[k].as<bool>(); return true;
}

// Applies settings from MQTT or USB. Unknown keys are ignored.
void onRemoteConfig(JsonObjectConst c) {
  bool changed = takeFloat(c, "perimeter_m", cfg.perimeterM, 1.5, 11.0)
               | takeFloat(c, "volume",      cfg.volume,     0.0, 1.0)
               | takeBool (c, "alarm_on",    cfg.alarmOn)
               | takeBool (c, "led_on",      cfg.ledOn);
  if (changed) configChanged = true;               // saved + reported from loop()
}

// ================= TELEMETRY =================
JsonDocument lastSent;              // last value sent for each key
bool forceFull = true;

template <typename T>
void addIfChanged(JsonDocument &doc, const char* key, T value, bool full) {
  if (full || lastSent[key].isNull() || lastSent[key] != value) { doc[key] = value; lastSent[key] = value; }
}
float round2(float v) { return roundf(v * 100) / 100; }

const char* stateWord(const RadarInfo &r) {
  if (!r.online) return "offline";
  if (!fresh(r)) return "no_data";
  return r.state == 2 ? "moving" : r.state == 1 ? "still" : "none";
}
const char* motionWord(Motion m) { return m == CLOSER ? "approach" : m == AWAY ? "away" : m == SAME ? "same" : "none"; }
const char* ledWord(LedColor c)  { return c == RED ? "red" : c == GREEN ? "green" : c == BLUE ? "blue" : "off"; }
const char* micWord(int lvl)     { return lvl >= LOUD_ABOVE ? "loud" : lvl >= QUIET_BELOW ? "sound" : "quiet"; }

void addRadar(JsonDocument &doc, const char* p, const RadarInfo &r, bool full) {
  char k[24];
  const char* st = stateWord(r);
  snprintf(k, sizeof(k), "%s_state", p);     addIfChanged(doc, k, st, full);
  if (r.state == 0 || !fresh(r)) return;     // no target: skip numbers
  snprintf(k, sizeof(k), "%s_dist_m", p);    addIfChanged(doc, k, round2(r.dist), full);
  snprintf(k, sizeof(k), "%s_speed_mps", p); addIfChanged(doc, k, round2(r.speed), full);
  snprintf(k, sizeof(k), "%s_dir", p);       addIfChanged(doc, k, motionWord(motionOf(r)), full);
  if (full) { snprintf(k, sizeof(k), "%s_energy", p); doc[k] = r.energy; }
}

void publishTelemetry(const char* zone, float nearest, bool full) {
  JsonDocument doc;
  char p[4];
  for (int i = 0; i < NUM_RADARS; i++) { snprintf(p, sizeof(p), "r%d", i + 1); addRadar(doc, p, r[i], full); }
  addIfChanged(doc, "perimeter", zone, full);
  if (strcmp(zone, "inside") == 0) addIfChanged(doc, "nearest_m", round2(nearest), full);
  addIfChanged(doc, "mic_state", micWord(pubMicLevel), full);
  if (full || abs(pubMicLevel - (int)(lastSent["mic_level"] | 0)) >= 25) {   // raw RMS, not dB
    doc["mic_level"] = pubMicLevel;
    doc["mic_peak_hz"] = (int)pubMicHz;
    lastSent["mic_level"] = pubMicLevel;
  }
  bool labelChanged = full || lastSent["sound_label"] != aiLabel;
  addIfChanged(doc, "sound_label", aiLabel, full);
  if (labelChanged) doc["sound_score"] = round2(aiScore);
  addIfChanged(doc, "footsteps", aiHuman ? 1 : 0, full);
  addIfChanged(doc, "led", ledWord(currentColor), full);

  pubMicLevel = 0; pubMicHz = 0;
  if (doc.size() == 0) return;
  char buf[768];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  mqttSendTelemetry(buf, n);
  Serial.printf("@T %s\n", buf);   // same data over USB, for the web app
}

// Device info + the settings currently in use (prefixed "active_"), published retained on .../info
void sendDeviceInfo() {
  JsonDocument doc;
  doc["firmware"] = FW_VERSION;
  doc["board"] = "ESP32-S3-DevKitC-1-N8R2";
  doc["mac_address"] = wifiMac();
  doc["wifi_rssi_dbm"] = WiFi.RSSI();
  doc["radar1_online"] = r[0].online;
  doc["radar2_online"] = r[1].online;
  doc["radar3_online"] = r[2].online;
  doc["psram_ok"] = psramFound();
  doc["mic_ok"] = micOK;
  doc["speaker_ok"] = ampOK;
  doc["sound_ai_ok"] = aiOK;
  doc["active_perimeter_m"] = cfg.perimeterM;
  doc["active_volume"] = cfg.volume;
  doc["active_alarm_on"] = cfg.alarmOn;
  doc["active_led_on"] = cfg.ledOn;
  char buf[512];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  mqttSendInfo(buf, n);
  Serial.printf("@A %s\n", buf);   // same data over USB, for the web app
}

// ================= COMMANDS (from MQTT or USB) =================
// A JSON object of settings, e.g. {"perimeter_m":4,"volume":0.02}, and/or commands:
// {"hello":true} asks for a full snapshot; {"preview":true} plays one beep at the
// current volume (even when the alarm is off).
void handleCommand(JsonObjectConst c) {
  if (c["hello"].is<bool>()) { forceFull = true; sendDeviceInfo(); }
  if (c["preview"].is<bool>() && ampOK) { ampTone(BEEP_HZ, 300); ampTone(0, 100); }   // "Preview sensor volume"
  onRemoteConfig(c);
}

// The settings in use, published (retained) so the broker's copy matches the device
void sendSettings() {
  JsonDocument doc;
  doc["perimeter_m"] = cfg.perimeterM;
  doc["volume"]      = cfg.volume;
  doc["alarm_on"]    = cfg.alarmOn;
  doc["led_on"]      = cfg.ledOn;
  char buf[160];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  mqttSendSettings(buf, n);
}

// USB: one JSON object per line, same as MQTT messages (used by the web app over USB)
void readUsbCommands() {
  static char line[256];
  static size_t len = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch != '\n' && ch != '\r') { if (len < sizeof(line) - 1) line[len++] = ch; continue; }
    if (len == 0) continue;
    line[len] = 0; len = 0;
    JsonDocument doc;
    if (deserializeJson(doc, line) || !doc.is<JsonObject>()) continue;
    handleCommand(doc.as<JsonObjectConst>());
  }
}

void onMqttConnect() { sendDeviceInfo(); forceFull = true; }

void printConfig() {
  Serial.printf("Settings: perimeter %.1f m, volume %.3f, alarm %s, LED %s\n",
                cfg.perimeterM, cfg.volume, cfg.alarmOn ? "on" : "off", cfg.ledOn ? "on" : "off");
}

// Wi-Fi MAC read straight from the chip (WiFi.macAddress() can return zeros before Wi-Fi starts)
String wifiMac() {
  uint8_t m[6];
  esp_read_mac(m, ESP_MAC_WIFI_STA);
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(b);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);          // don't stall when no computer is reading the USB port
  delay(1500);                       // give the USB serial port time to appear
  {
    const char* why[] = {"unknown","power on","external pin","software restart","crash (panic)","interrupt watchdog",
                         "task watchdog","other watchdog","deep sleep","brownout (power dipped)","SDIO","USB","JTAG"};
    int rr = esp_reset_reason();
    Serial.printf("\nStarted after: %s\n", (rr >= 0 && rr <= 12) ? why[rr] : "unknown");
  }
  loadConfig();

  pinMode(LED_RED, OUTPUT); pinMode(LED_GREEN, OUTPUT); pinMode(LED_BLUE, OUTPUT);
  showColor(RED); delay(400); showColor(GREEN); delay(400); showColor(BLUE); delay(400); showColor(OFF);

  amp.setPins(AMP_BCLK, AMP_LRC, AMP_DIN);
  ampOK = amp.begin(I2S_MODE_STD, AMP_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  Serial.println(ampOK ? "Speaker ready." : "Speaker FAILED - check BCLK, LRC, DIN");
  if (ampOK) ampTone(0, 100);

  // Sound AI buffer (88 KB): in PSRAM if enabled, otherwise internal RAM
  aiBuf = psramFound() ? (int16_t*)ps_calloc(AI_WINDOW, sizeof(int16_t))
                       : (int16_t*)calloc(AI_WINDOW, sizeof(int16_t));
  aiOK = (aiBuf != NULL);
  Serial.printf("PSRAM: %s\n", psramFound() ? "found" : "NOT found (Tools > PSRAM > QSPI PSRAM)");
  Serial.println(aiOK ? "Sound AI ready." : "Sound AI FAILED - not enough memory");

  i2s.setPins(MIC_SCK, MIC_WS, -1, MIC_SD);
  micOK = i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH);
  if (micOK) for (int k = 0; k < 5; k++) i2s.readBytes((char*)micBuf, sizeof(micBuf));
  Serial.println(micOK ? "Mic ready." : "Mic FAILED - check SCK, WS, SD");

  for (int i = 0; i < NUM_RADARS; i++) {
    char name[10];
    snprintf(name, sizeof(name), "Radar %d", i + 1);
    radarPort[i]->setRxBufferSize(1024);
    r[i].online = setupRadar(radars[i], name);
  }

  WiFi.mode(WIFI_STA);
  Serial.printf("Wi-Fi MAC address: %s   <- register this one\n", wifiMac().c_str());
  mqttBegin(handleCommand, onMqttConnect);

  printConfig();
  beep(1500, 60);                    // "armed"
  Serial.println("LED: RED = closer, GREEN = away, BLUE = same distance");
  Serial.println("-------------------------------------------------------------------------------");
}

// ================= LOOP =================
unsigned long lastPrint = 0, lastBeep = 0, lastInside = 0, lastPublish = 0, lastHeartbeat = 0;
bool occupied = false;
float nearest = 0;

void loop() {
  readMic();
  runSoundAI();                      // about once per second, takes a moment
  for (int i = 0; i < NUM_RADARS; i++)
    if (r[i].online) pollRadar(radars[i], *radarPort[i], r[i]);

  // ---- LED: direction of movement, from any radar ----
  Motion m[NUM_RADARS];
  bool closer = false, away = false, same = false, insideNow = false;
  for (int i = 0; i < NUM_RADARS; i++) {
    m[i] = motionOf(r[i]);
    closer |= m[i] == CLOSER; away |= m[i] == AWAY; same |= m[i] == SAME;
  }
  setLed(closer ? RED : away ? GREEN : same ? BLUE : OFF);

  // ---- Speaker: perimeter alarm ----
  float nearNow = cfg.perimeterM;
  for (int i = 0; i < NUM_RADARS; i++)
    if (inPerimeter(r[i])) { insideNow = true; nearNow = min(nearNow, r[i].dist); }
  if (insideNow) { nearest = nearNow; lastInside = millis(); }

  if (insideNow && !occupied) {
    occupied = true;
    Serial.printf(">>> Someone ENTERED the perimeter at %.2f m\n", nearest);
    beep(800, 120); beep(1200, 120);
    lastBeep = millis();
  } else if (occupied && !insideNow && millis() - lastInside > EXIT_DELAY_MS) {
    occupied = false;
    Serial.println("<<< Perimeter clear");
  }
  if (occupied && insideNow) {       // beep faster as they get closer
    float t = (constrain(nearest, 1.0f, cfg.perimeterM) - 1.0f) / (cfg.perimeterM - 1.0f);
    if (millis() - lastBeep >= FASTEST_MS + (unsigned long)(t * (SLOWEST_MS - FASTEST_MS))) {
      beep();
      lastBeep = millis();
    }
  }
  const char* zone = (occupied && insideNow) ? "inside" : occupied ? "leaving" : "clear";

  // ---- Status line every 0.25 s ----
  if (millis() - lastPrint >= 250) {
    lastPrint = millis();
    Serial.printf("Perimeter: %-7s |", zone);
    for (int i = 0; i < NUM_RADARS; i++) Serial.printf(" R%d: %-8s %5.2fm |", i + 1, motionWord(m[i]), r[i].dist);
    Serial.printf(" Mic: %5d %-5s | Sound: %s %3.0f%%\n",
                  micLevel, micWord(micLevel), aiHuman ? "FOOTSTEPS" : aiLabel, aiScore * 100);
    if (micLevel > pubMicLevel) { pubMicLevel = micLevel; pubMicHz = micPeakHz; }
    micLevel = 0;
  }

  // ---- MQTT + USB ----
  readUsbCommands();
  mqttLoop();
  if (configChanged) {
    configChanged = false;
    saveConfig();
    printConfig();
    sendDeviceInfo();
    sendSettings();
  }
  if (millis() - lastPublish >= PUBLISH_MS) {
    lastPublish = millis();
    bool full = forceFull || millis() - lastHeartbeat >= HEARTBEAT_MS;
    if (full) { lastHeartbeat = millis(); forceFull = false; }
    publishTelemetry(zone, nearest, full);
  }
}
