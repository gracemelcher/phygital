// Simulated device: publishes exactly what the ESP32 publishes, so the app and
// recorder can be tested without hardware. A person walks around it every 75 s.
//
//   node simulate.mjs [device-id]       (broker settings from config.json, or PHYGITAL_CONFIG=config.local.json)
import mqtt from 'mqtt';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const cfg = { mqttUrl: 'mqtt://localhost:1883', username: '', password: '', topicRoot: 'phygital',
  ...JSON.parse(readFileSync(resolve(here, process.env.PHYGITAL_CONFIG || 'config.json'), 'utf8')) };
const ID = process.argv[2] || 'phygital-sim';
const T = k => `${cfg.topicRoot}/${ID}/${k}`;

const settings = { perimeter_m: 6, volume: 0.01, alarm_on: true, led_on: true };
const client = mqtt.connect(cfg.mqttUrl, {
  username: cfg.username || undefined, password: cfg.password || undefined, clientId: ID,
  will: { topic: T('status'), payload: 'offline', retain: true, qos: 1 },
});

function sendInfo() {
  client.publish(T('info'), JSON.stringify({
    firmware: 'simulator', board: 'simulated', mac_address: '00:00:00:00:00:00', wifi_rssi_dbm: -60,
    radar1_online: true, radar2_online: true, radar3_online: true, psram_ok: true, mic_ok: true, speaker_ok: true, sound_ai_ok: true,
    active_perimeter_m: settings.perimeter_m, active_volume: settings.volume, active_alarm_on: settings.alarm_on, active_led_on: settings.led_on,
  }), { retain: true });
}

client.on('connect', () => {
  console.log(`Simulated device "${ID}" connected to ${cfg.mqttUrl}`);
  client.publish(T('status'), 'online', { retain: true, qos: 1 });
  client.subscribe([T('settings'), T('cmd')], { qos: 1 });
  sendInfo();
});
client.on('error', e => console.error('MQTT error:', e.message));

client.on('message', (topic, buf) => {
  let m; try { m = JSON.parse(buf.toString()); } catch { return; }
  if (m.hello) sendInfo();
  if (m.preview) console.log('beep (preview)');
  const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
  let changed = false;
  for (const [k, lo, hi] of [['perimeter_m', 1.5, 11], ['volume', 0, 1]])
    if (typeof m[k] === 'number' && clamp(m[k], lo, hi) !== settings[k]) { settings[k] = clamp(m[k], lo, hi); changed = true; }
  for (const k of ['alarm_on', 'led_on'])
    if (typeof m[k] === 'boolean' && m[k] !== settings[k]) { settings[k] = m[k]; changed = true; }
  if (changed) {
    console.log('Settings:', JSON.stringify(settings));
    sendInfo();
    client.publish(T('settings'), JSON.stringify(settings), { retain: true, qos: 1 });
  }
});

// ---- the walking person (same model as the app's demo mode) ----
const t0 = Date.now(), prev = [null, null, null];
let occupied = false, lastInside = 0;
setInterval(() => {
  if (!client.connected) return;
  const t = (Date.now() - t0) / 1000, cycle = (t % 75) / 75;
  const d = Math.max(1.1, 10.5 - 11 * Math.sin(Math.PI * cycle)), angle = (t * 4) % 360;
  const out = {}, dirs = []; let inside = false, nearest = 99;
  for (let i = 0; i < 3; i++) {
    const diff = Math.abs(((angle - i * 120 + 540) % 360) - 180), p = `r${i + 1}`;
    if (diff < 70 && d < 10.5) {
      const dist = +(d + 0.25 * i + (Math.random() - 0.5) * 0.08).toFixed(2);
      const delta = prev[i] == null ? 0 : dist - prev[i];
      const dir = delta < -0.05 ? 'approach' : delta > 0.05 ? 'away' : 'same';
      Object.assign(out, { [`${p}_state`]: Math.abs(delta) > 0.05 ? 'moving' : 'still', [`${p}_dist_m`]: dist,
        [`${p}_speed_mps`]: +Math.abs(delta).toFixed(2), [`${p}_dir`]: dir, [`${p}_energy`]: Math.round(40 + Math.random() * 40) });
      prev[i] = dist; dirs.push(dir);
      if (dist <= settings.perimeter_m) { inside = true; nearest = Math.min(nearest, dist); }
    } else { out[`${p}_state`] = 'none'; prev[i] = null; }
  }
  if (inside) { occupied = true; lastInside = Date.now(); }
  else if (occupied && Date.now() - lastInside > 3000) occupied = false;
  out.perimeter = inside ? 'inside' : occupied ? 'leaving' : 'clear';
  if (inside) out.nearest_m = +nearest.toFixed(2);
  const steps = d < 5 && dirs.some(x => x !== 'same') && Math.random() < 0.6;
  out.sound_label = steps ? 'human_footsteps' : 'Background_noise';
  out.sound_score = +(steps ? 0.7 + Math.random() * 0.25 : 0.9 + Math.random() * 0.1).toFixed(2);
  out.footsteps = steps ? 1 : 0;
  out.mic_level = Math.round(300 + Math.random() * 400 + (steps ? 900 : 0));
  out.mic_peak_hz = Math.round(steps ? 150 + Math.random() * 300 : 60 + Math.random() * 2000);
  out.mic_state = out.mic_level >= 1200 ? 'loud' : 'sound';
  const led = dirs.includes('approach') ? 'red' : dirs.includes('away') ? 'green' : dirs.includes('same') ? 'blue' : 'off';
  out.led = settings.led_on ? led : 'off';
  client.publish(T('telemetry'), JSON.stringify(out));
}, 1000);
