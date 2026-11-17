# Phygital Sensor Hub

An ESP32 motion alarm. Two DFRobot C4002 radars track whether someone is coming closer or moving away, a third radar does simple presence detection, and a microphone runs an Edge Impulse model that listens for footsteps. An RGB LED and a speaker react locally, and readings go out over Wi-Fi/MQTT (currently to ThingsBoard). The alarm radius, volume, and speaker/LED on/off can be changed remotely.

| Path | What it is |
| --- | --- |
| `phygital/` | **The sketch to upload.** Includes the patched C4002 radar driver |
| `ei-project-v2-footsteps-arduino-1.0.2-impulse-#2.zip` | Edge Impulse footsteps model (Arduino library) |
| `dashboards/` | ThingsBoard dashboard exports |

## Hardware

ESP32 Dev Module (classic ESP32, not S3), 3× DFRobot C4002 radar (SEN0691), INMP441 I2S mic, MAX98357A I2S amp + small speaker, common-cathode RGB LED, 3× 220 Ω resistors.

| Part | Pin → ESP32 |
| --- | --- |
| Radar 1 | TX→35, RX→14, VCC→5V, GND |
| Radar 2 | TX→34, RX→26, VCC→5V, GND |
| Radar 3 | OUT→36 (VP), VCC→5V, GND |
| Mic | SCK→32, WS→25, SD→33, L/R→GND, VDD→3V3 |
| Amp | BCLK→4, LRC→16, DIN→17, Vin→5V, SD→5V, GAIN→100 kΩ→5V, GND |
| LED | R→220 Ω→21, G→220 Ω→18, B→220 Ω→19, common→GND |

Radars need 5 V (the VIN/5V pin), not 3V3.

## Getting started

1. **Arduino IDE 2.x + ESP32 boards.** In Boards Manager install **esp32 by Espressif** (3.x). Select **Tools → Board → ESP32 Dev Module**.
2. **Libraries.** In Library Manager install **arduinoFFT** (Enrique Condes, 2.x), **PubSubClient** (Nick O'Leary) and **ArduinoJson** (Benoit Blanchon, 7.x). Then **Sketch → Include Library → Add .ZIP Library** and pick `ei-project-v2-footsteps-arduino-1.0.2-impulse-#2.zip`.
   You do **not** need to install the DFRobot_C4002 library; the patched driver ships in `phygital/`.
3. **ThingsBoard device.** In ThingsBoard Cloud: **Entities → Devices → +**, then open the device → **Manage credentials** and copy the access token.
4. **Secrets.** Make your own copy of the secrets template. Run this from the repo folder, or duplicate the file in Finder and rename it:

   ```bash
   cp phygital/secrets.example.h phygital/secrets.h
   ```

   Open `phygital/secrets.h` (it also appears as a tab in Arduino IDE) and fill in `WIFI_SSID`, `WIFI_PASS` (2.4 GHz networks only) and your device token as `MQTT_USER`. The sketch reads `secrets.h`, and git ignores it, so your values never get committed.
   Never put real values in `secrets.example.h`. That file is committed, so it is public; it only shows which values are needed.
5. **Upload.** Open `phygital/phygital.ino`, click **Upload** (hold **BOOT** if it stalls at `Connecting...`), then open the Serial Monitor at **115200 baud**.

At boot you should see `Speaker ready`, `Sound AI ready`, `Mic ready`, `Radar 1/2 connected`, `Wi-Fi connected`, `MQTT: connecting... connected`, then a status line every 0.25 s.

### If you use the DFRobot_C4002 library instead

The stock library (v1.0.0) parses radar reports incorrectly on the ESP32. If you install it from [GitHub](https://github.com/DFRobot/DFRobot_C4002) rather than using the bundled copy, edit `Documents/Arduino/libraries/DFRobot_C4002/src/DFRobot_C4002.h`. At the end of the `sDetectResult_t` struct (around line 208), change

```cpp
} sDetectResult_t;
```

to

```cpp
} __attribute__((packed)) sDetectResult_t;
```

Re-apply this edit after any library update. (`phygital/DFRobot_C4002.h` is the patched header, if you'd rather copy it over.)

## Remote settings

Set these as **shared attributes** on the device (ThingsBoard: device → Attributes → Shared attributes → +). The device applies them right away, saves them so they survive a reboot, and reports what it's using as `active_*` client attributes.

| Key | Type | Default | Range |
| --- | --- | --- | --- |
| `perimeter_m` | number | 6.0 | 1.5–11 m alarm radius |
| `volume` | number | 0.02 | 0–0.5 (0.02 is very quiet) |
| `alarm_on` | boolean | true | speaker on/off |
| `led_on` | boolean | true | LED on/off |

Other tuning (trend threshold, AI confidence, beep speeds) lives at the top of `phygital.ino`.

## Telemetry

Values are sent when they change (checked every second), plus everything every 30 s.

| Key | Values |
| --- | --- |
| `r1_state`, `r2_state` | `moving`, `still`, `none`, `offline`, `no_data` |
| `r1_dir`, `r2_dir` | `approach`, `away`, `same`, `none` |
| `r1_dist_m`, `r1_speed_mps`, `r1_energy` (and `r2_`) | only while a target is present |
| `r3_detected` | `1` / `0` |
| `perimeter`, `nearest_m` | `clear` / `inside` / `leaving`; nearest distance while inside |
| `sound_label`, `sound_score`, `footsteps` | model's top label, confidence, `1` when footsteps ≥ 70 % |
| `mic_state`, `mic_level`, `mic_peak_hz` | `quiet` / `sound` / `loud`, raw level, peak frequency |
| `led` | `red` (closer), `green` (away), `blue` (same distance), `off` |

## Changing the MQTT service

Everything broker-specific (topics, login, how settings are requested) is in `phygital/mqtt_link.h`. Host, port and credentials are in `secrets.h`. To move off ThingsBoard, edit those two files; the main sketch doesn't change.

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| `Project_v2_footsteps_inferencing.h: No such file` | Add the Edge Impulse .zip library (step 2) |
| `secrets.h: No such file` | Copy `secrets.example.h` to `secrets.h` (step 4) |
| `Sketch too big` | **Tools → Partition Scheme → Huge APP** |
| Radar shows `no_data` or nonsense distances | Unpatched DFRobot library is being used; see the section above |
| `Radar N FAILED` | Radar TX goes to the ESP32's RX pin (35/34); VCC on 5V |
| Wi-Fi never connects | Use a 2.4 GHz network; check the name and password |
| `MQTT ... failed (state -2)` | Can't reach the host; check `MQTT_HOST` region, or the network blocks port 1883 |
| `failed (state 4/5)` | Wrong token in `MQTT_USER` |
| Footsteps never detected | Check `FOOTSTEP_LABEL` matches the model's label; try `MIC_SHIFT` 13 |
