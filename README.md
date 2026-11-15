# README: Phygital Sensor Hub (ESP32 + C4002 Radars + Mic + ThingsBoard)

## What this does

`Motion-Sensor_Buzzer.ino` runs on an **ESP32 Dev Module**. It reads three DFRobot C4002 radars and an INMP441 microphone, and lights an RGB LED to show which radar senses motion. It also sends the readings to a ThingsBoard dashboard over Wi-Fi.

- **Radar 1 and 2:** full data over UART (state, distance, speed, direction, energy)
- **Radar 3:** detect-only, through its OUT pin
- **Microphone:** loudness level and peak frequency
- **LED:** red = Radar 1, green = Radar 2, blue = both, off = none

**Parts**

| Qty | Part |
| --- | --- |
| 1 | ESP32 Dev Module (classic ESP32) |
| 3 | DFRobot C4002 mmWave radar (SEN0691) |
| 1 | INMP441 I2S microphone |
| 1 | RGB LED, common cathode |
| 3 | 220 Ω resistors |
| 1 | Breadboard, jumper wires, USB data cable |

You also need a computer with Arduino IDE 2.x, a 2.4 GHz Wi-Fi network, and a free ThingsBoard Cloud account.

## Wiring

Wire everything with the board unplugged. The radars need 5 V: on most ESP32 Dev Module boards that is the pin labelled **VIN** or **5V** (USB power). Do not use 3V3 for them.

**Radars**

| Radar pin | Radar 1 | Radar 2 | Radar 3 |
| --- | --- | --- | --- |
| TX | GPIO 35 | GPIO 34 | not used |
| RX | GPIO 14 | GPIO 26 | not used |
| OUT | not used | not used | GPIO 36 (VP) |
| VCC | 5 V rail | 5 V rail | 5 V rail |
| GND | GND rail | GND rail | GND rail |

**Microphone (INMP441)**

| Mic pin | ESP32 |
| --- | --- |
| SCK | GPIO 32 |
| WS | GPIO 25 |
| SD | GPIO 33 |
| L/R | GND |
| GND | GND |
| VDD | 3V3 (or GPIO 13; the sketch powers GPIO 13 either way) |

**RGB LED (common cathode)**

| LED leg | Connect to |
| --- | --- |
| Red | 220 Ω → GPIO 21 |
| Green | 220 Ω → GPIO 18 |
| Blue | 220 Ω → GPIO 19 |
| Common (longest leg) | GND |

These pins are for the classic ESP32 only. Several of them (25, 26, 32–36) don't exist or are reserved on an ESP32-S3, so the S3 needs a new pin map.

## Install the libraries

Install the first three from **Sketch → Include Library → Manage Libraries**. Match the author names exactly; several libraries share similar names.

| Library | Author | Version | Used for |
| --- | --- | --- | --- |
| arduinoFFT | Enrique Condes | 2.x | Finding the mic's peak frequency |
| PubSubClient | Nick O'Leary | latest | MQTT connection to ThingsBoard |
| ArduinoJson | Benoit Blanchon | 7.x | Building the data messages |
| DFRobot_C4002 | DFRobot | from GitHub | Talking to the radars |

`ESP_I2S.h` and `WiFi.h` come with the ESP32 board package; don't install them separately.

**DFRobot_C4002 (manual install)**

1. Go to [github.com/DFRobot/DFRobot_C4002](https://github.com/DFRobot/DFRobot_C4002), click **Code → Download ZIP**.
2. In Arduino IDE: **Sketch → Include Library → Add .ZIP Library**, and pick the downloaded file.

**Apply the packed-struct fix (required)**

The sketch needs a change in the library's `DFRobot_C4002.h`; without it, radar readings come out wrong. The file is in your Arduino libraries folder: `Documents/Arduino/libraries/DFRobot_C4002/`.

- [ ] Open question: write the exact change here (which struct, and the line added), so new team members can repeat it.

Re-apply the fix any time you update or reinstall the library.

## Set up ThingsBoard

You need a device and its access token before uploading. Full detail, including dashboards, is in the [ThingsBoard Setup Guide](https://claude.ai/artifact/CuWVqiZicmtK9KFfhbCjEx).

1. Sign up for the Free plan at [thingsboard.io/pricing](https://thingsboard.io/pricing) (ThingsBoard Cloud → Start Free). Note your region: North America or Europe.
2. **Entities → Devices → + → Add new device**. Name it (for example `phygital-devmodule`) and click **Add**.
3. Open the device → **Manage credentials** → copy the **access token**. Keep it private; it works like a password.

The Free plan allows about 1 million data points a month. The sketch only sends values when they change, plus a full update every 30 s, to stay well inside that.

## Configure and upload

1. Put `phygital_thingsboard.ino` in a folder with the same name: `phygital_thingsboard/phygital_thingsboard.ino`. Arduino IDE requires this.
2. Open it in Arduino IDE.
3. Edit the settings block near the top:

```cpp
const char* WIFI_SSID = "YOUR_WIFI";                // 2.4 GHz network only
const char* WIFI_PASS = "YOUR_PASSWORD";
const char* TB_HOST   = "mqtt.thingsboard.cloud";   // Europe: "mqtt.eu.thingsboard.cloud"
const char* TB_TOKEN  = "YOUR_ACCESS_TOKEN";
```

4. Click **Verify** (check mark). Fix any "No such file" errors by installing the missing library from the table above.
5. Click **Upload** (arrow). If it stalls at `Connecting....`, hold the board's **BOOT** button until uploading starts.
6. Open **Tools → Serial Monitor** and set it to **115200 baud**.

Don't commit the sketch to a shared repository with the real password or token in it.

## Check it works

**1. At power-up**, the LED flashes red, green, blue, then turns off. The Serial Monitor should show:

```text
Mic ready.
Radar 1 connected.
Radar 2 connected.
Radar 3 on OUT pin (detect only).
Wi-Fi: connecting in the background...
Wi-Fi connected, IP 192.168.x.x, signal -58 dBm
ThingsBoard: connecting... connected.
```

**2. While running**, one status line prints every 0.25 s:

```text
Sensing: RADAR 1   | Radar 1: APPROACH  2.41m   | Radar 2: nothing          | Radar 3: nothing  | Mic:   143 sound   420 Hz
```

Walk in front of each radar. The LED shows red for Radar 1, green for Radar 2 and blue for both. By default only movement counts; standing still does not (see `MOTION_ONLY`).

**3. In ThingsBoard**, open the device → **Latest telemetry**. These keys should appear and update:

| Key | Values |
| --- | --- |
| `r1_state`, `r2_state` | `moving`, `still`, `none`, `offline`, `no_data` |
| `r1_dist_m`, `r1_speed_mps`, `r1_dir`, `r1_energy` | Only while a target is present; same for `r2_` |
| `r3_detected` | `1` or `0` |
| `mic_state` | `quiet`, `sound`, `loud` |
| `mic_level`, `mic_peak_hz` | Raw loudness (not decibels) and peak frequency |
| `led` | `red`, `green`, `blue`, `off` |

Under **Attributes → Client attributes** you should also see `firmware`, `board`, `wifi_rssi_dbm` and which radars are online.

## Settings you can tune

All are constants in the sketch; change, then re-upload.

| Setting | Default | What it does |
| --- | --- | --- |
| `MOTION_ONLY` | `true` | `true`: only moving people light the LED. `false`: standing still counts too |
| `QUIET_BELOW` | `60` | Mic level below this is "quiet". Raise it if a silent room reads as "sound" |
| `LOUD_ABOVE` | `600` | Mic level above this is "loud" (claps, shouts) |
| `LED_HOLD_MS` | `500` | Minimum time the LED holds a colour, to stop flicker |
| `PUBLISH_MS` | `1000` | How often the sketch checks for changes to send |
| `HEARTBEAT_MS` | `30000` | How often it sends every value, changed or not |
| `setDetectRange(0, 1100)` | 0–11 m | Radar detection range, in cm (inside `setupRadar`) |
| `setReportPeriod(2)` | 0.2 s | How often each radar reports (units of 0.1 s) |

To tune the mic thresholds, watch the `Mic:` number in the Serial Monitor in a quiet room, then while clapping, and set the two values between those readings.

## Troubleshooting

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| `fatal error: ESP_I2S.h: No such file` | ESP32 board package older than 3.x | Update esp32 by Espressif to 3.x in Boards Manager |
| `ArduinoFFT does not name a template` | Old arduinoFFT (1.x) | Update arduinoFFT to 2.x |
| Upload stalls at `Connecting....` | Board not in boot mode | Hold **BOOT** until upload starts |
| `Radar N FAILED` | Wiring or power | Radar TX goes to the ESP32's RX pin (35 or 34); VCC on 5 V, not 3V3 |
| Radar shows `no data!` | Library fix missing, or loose wire | Re-apply the packed-struct fix; reseat TX/RX |
| Radar 3 always `DETECTED` or always `nothing` | OUT pin not wired, or radar needs calibration | Check GPIO 36 wire; run the library's calibration example with the room empty |
| `Mic failed to start` | Mic wiring | Check SCK 32, WS 25, SD 33, and L/R to GND |
| Mic level never drops below `QUIET_BELOW` | Room noise or threshold too low | Raise `QUIET_BELOW` (see tuning) |
| Wi-Fi never connects | 5 GHz network or wrong password | Use 2.4 GHz; recheck name and password (case-sensitive) |
| `ThingsBoard: connecting... failed (state -2)` | Can't reach the server | Check `TB_HOST` matches your region; some school or office networks block port 1883 |
| `failed (state 4)` or `(state 5)` | Wrong access token | Copy the token again from the device's credentials |
| Connected, but no telemetry in ThingsBoard | Token belongs to a different device | Confirm you're looking at the device whose token is in the sketch |
