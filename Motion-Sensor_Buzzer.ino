// 3 Radars (DFRobot C4002) + Microphone (INMP441) + RGB LED
// LED shows WHICH radar senses motion:
//   Radar 1 only -> RED, Radar 2 only -> GREEN, both -> BLUE, none -> OFF
// Requires the packed-struct fix in DFRobot_C4002.h
//
// Radar 1: TX->D35, RX->D14   [UART1, full data]
// Radar 2: TX->D34, RX->D26   [UART2, full data]
// Radar 3: OUT->VP (GPIO36)   [detect only]
// All radars: VCC->+rail (5V), GND->-rail
//
// Mic: SCK->D32, SD->D33, WS->D25, L/R+GND->-rail
//      VDD-> 3V3 pin (or D13 - the code powers D13 either way)
//
// LED: Red   -> 220R -> D21
//      Green -> 220R -> D18
//      Blue  -> 220R -> D19
//      Common (longest leg) -> GND
#include <ESP_I2S.h>
#include <arduinoFFT.h>
#include "DFRobot_C4002.h"

// Radar data holder (must be at the top for Arduino IDE)
struct RadarInfo {
  bool  online = false;
  int   state = 0;          // 0 nothing, 1 still, 2 moving
  float dist = 0, speed = 0;
  int   dir = 1;            // 0 away, 1 none, 2 approaching
  int   energy = 0;
  unsigned long updated = 0;
};

// ================= LED =================
const int LED_RED   = 21;
const int LED_GREEN = 18;
const int LED_BLUE  = 19;
const unsigned long LED_HOLD_MS = 500;   // keep each color at least 0.5 s

enum LedColor { OFF, RED, GREEN, BLUE };
LedColor currentColor = OFF;
unsigned long colorSince = 0;

void showColor(LedColor c) {
  digitalWrite(LED_RED,   c == RED   ? HIGH : LOW);
  digitalWrite(LED_GREEN, c == GREEN ? HIGH : LOW);
  digitalWrite(LED_BLUE,  c == BLUE  ? HIGH : LOW);
}

void setLed(LedColor c) {
  if (c == currentColor) return;
  if (millis() - colorSince < LED_HOLD_MS) return;
  currentColor = c;
  colorSince = millis();
  showColor(c);
}

// ================= RADARS =================
DFRobot_C4002 radar1(&Serial1, 115200, 35, 14);   // RX 35, TX 14
DFRobot_C4002 radar2(&Serial2, 115200, 34, 26);   // RX 34, TX 26
const int RADAR3_OUT = 36;                         // VP

RadarInfo r1, r2;

// Read a new report from one radar, if one has arrived
void pollRadar(DFRobot_C4002 &radar, HardwareSerial &port, RadarInfo &info) {
  if (port.available() < 8) return;          // nothing new yet: don't wait
  sRetResult_t ret = radar.getNoteInfo();
  if (ret.noteType != eResult) return;

  eTargetState_t s = radar.getTargetState();
  info.updated = millis();
  if (s == eMotion) {
    sMotionTarget_t m = radar.getMotionTargetInfo();
    info.state = 2; info.dist = m.distance; info.speed = m.speed;
    info.dir = (int)m.direction; info.energy = m.energy;
  } else if (s == ePresence) {
    sPresenceTarget_t p = radar.getPresenceTargetInfo();
    info.state = 1; info.dist = p.distance; info.speed = 0;
    info.dir = 1; info.energy = p.energy;
  } else {
    info.state = 0; info.dist = 0; info.speed = 0; info.dir = 1; info.energy = 0;
  }
}

// Connect to one radar and apply settings (tries 5 times)
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
  Serial.printf("%s FAILED - check its TX/RX, VCC, GND\n", name);
  return false;
}

// Short text describing one radar's status
void radarText(const RadarInfo &r, char *out, size_t n) {
  if (!r.online)                   { snprintf(out, n, "offline          "); return; }
  if (millis() - r.updated > 3000) { snprintf(out, n, "no data!         "); return; }
  if (r.state == 0)                { snprintf(out, n, "nothing          "); return; }
  if (r.state == 1)                { snprintf(out, n, "still   %5.2fm   ", r.dist); return; }
  const char* d = (r.dir == 2) ? "APPROACH" : (r.dir == 0) ? "AWAY    " : "moving  ";
  snprintf(out, n, "%s %5.2fm   ", d, r.dist);
}

// ================= MICROPHONE =================
const int MIC_SCK = 32, MIC_SD = 33, MIC_WS = 25, MIC_PWR = 13;
const int SAMPLE_RATE = 16000;
const int N = 512;                  // ~32 ms of audio per chunk
const int QUIET_BELOW = 60;         // tune: room background level
const int LOUD_ABOVE  = 600;        // tune: claps, shouts, barks

I2SClass i2s;
int32_t micBuf[N * 2];              // left + right interleaved
float vReal[N], vImag[N];
ArduinoFFT<float> FFT(vReal, vImag, N, SAMPLE_RATE);
int   micLevel = 0;
float micPeakHz = 0;

int channelRMS(int ch) {
  double mean = 0, e = 0;
  for (int i = 0; i < N; i++) mean += (micBuf[2 * i + ch] >> 14);
  mean /= N;
  for (int i = 0; i < N; i++) { double v = (micBuf[2 * i + ch] >> 14) - mean; e += v * v; }
  return (int)sqrt(e / N);
}

void readMic() {
  size_t got = i2s.readBytes((char*)micBuf, sizeof(micBuf));
  if (got < sizeof(micBuf)) return;
  int rL = channelRMS(0), rR = channelRMS(1);
  int ch = (rL >= rR) ? 0 : 1;
  int level = max(rL, rR);
  if (level <= micLevel) return;               // keep the loudest chunk per line
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

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(500);

  // LED self-test: red -> green -> blue -> off
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  showColor(RED);   delay(500);
  showColor(GREEN); delay(500);
  showColor(BLUE);  delay(500);
  showColor(OFF);

  // Microphone
  pinMode(MIC_PWR, OUTPUT);
  digitalWrite(MIC_PWR, HIGH);       // powers the mic if VDD is on D13 (harmless otherwise)
  delay(200);
  i2s.setPins(MIC_SCK, MIC_WS, -1, MIC_SD);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_32BIT,
                 I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    Serial.println("Mic failed to start - check SCK, WS, SD");
  } else {
    for (int k = 0; k < 5; k++) i2s.readBytes((char*)micBuf, sizeof(micBuf));
    Serial.println("Mic ready.");
  }

  // Radars
  Serial1.setRxBufferSize(1024);
  Serial2.setRxBufferSize(1024);
  r1.online = setupRadar(radar1, "Radar 1");
  r2.online = setupRadar(radar2, "Radar 2");
  pinMode(RADAR3_OUT, INPUT);
  Serial.println("Radar 3 on OUT pin (detect only).");

  Serial.println();
  Serial.println("LED: Radar 1 = RED, Radar 2 = GREEN, both = BLUE");
  Serial.println("-------------------------------------------------------------------------------");
}

// ================= LOOP =================
unsigned long lastPrint = 0;

// true = only MOVING counts; false = moving OR standing still counts
const bool MOTION_ONLY = true;

void loop() {
  readMic();
  if (r1.online) pollRadar(radar1, Serial1, r1);
  if (r2.online) pollRadar(radar2, Serial2, r2);
  bool r3 = (digitalRead(RADAR3_OUT) == HIGH);

  // ---- Does each radar see something? ----
  bool r1Sees = MOTION_ONLY ? (r1.state == 2) : (r1.state > 0);
  bool r2Sees = MOTION_ONLY ? (r2.state == 2) : (r2.state > 0);

  // ---- LED: show WHICH radar is sensing ----
  if (r1Sees && r2Sees)  setLed(BLUE);    // both radars
  else if (r1Sees)       setLed(RED);     // radar 1 only
  else if (r2Sees)       setLed(GREEN);   // radar 2 only
  else                   setLed(OFF);     // neither

  // ---- Print one status line every 0.25 s ----
  if (millis() - lastPrint >= 250) {
    lastPrint = millis();
    char t1[40], t2[40], mic[40];
    radarText(r1, t1, sizeof(t1));
    radarText(r2, t2, sizeof(t2));
    if (micLevel < QUIET_BELOW) snprintf(mic, sizeof(mic), "%5d quiet", micLevel);
    else snprintf(mic, sizeof(mic), "%5d %s %5.0f Hz", micLevel,
                  micLevel >= LOUD_ABOVE ? "LOUD!" : "sound", micPeakHz);
    const char* sensing = (r1Sees && r2Sees) ? "BOTH     " :
                          r1Sees ? "RADAR 1  " :
                          r2Sees ? "RADAR 2  " : "none     ";
    Serial.printf("Sensing: %s | Radar 1: %s| Radar 2: %s| Radar 3: %s | Mic: %s\n",
                  sensing, t1, t2, r3 ? "DETECTED" : "nothing ", mic);
    micLevel = 0;
  }
}