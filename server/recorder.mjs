// Recorder: listens to every device on the broker, stores readings and events in
// SQLite, and answers history requests from the app over MQTT.
//
//   node recorder.mjs            (settings in config.json; copy config.example.json)
//
// Topics (ROOT = topicRoot, default "phygital"):
//   in   ROOT/+/telemetry, ROOT/+/info, ROOT/+/status, ROOT/+/settings   (from devices)
//   in   ROOT/_history/req          {"id":"abc","device":"phygital-01","kind":"points"|"events"|"raw","since":ms,"until":ms}
//   out  ROOT/_history/res/<id>     {"id":"abc","kind":...,"rows":[...]}
//   out  ROOT/_recorder/status      retained "online" / "offline" (so the app knows history is available)
import mqtt from 'mqtt';
import { DatabaseSync } from 'node:sqlite';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const cfg = {
  mqttUrl: 'mqtt://localhost:1883', username: '', password: '', topicRoot: 'phygital',
  db: 'phygital.db', keepDays: 30, keepRawDays: 2, maxPoints: 720,
  ...JSON.parse(readFileSync(resolve(here, process.env.PHYGITAL_CONFIG || 'config.json'), 'utf8')),
};
const ROOT = cfg.topicRoot;

// ---------------- storage ----------------
const db = new DatabaseSync(resolve(here, cfg.db));
db.exec(`
  PRAGMA journal_mode = WAL;
  CREATE TABLE IF NOT EXISTS raw    (device TEXT, t INTEGER, topic TEXT, json TEXT);
  CREATE TABLE IF NOT EXISTS points (device TEXT, t INTEGER, r1 REAL, r2 REAL, r3 REAL, nearest REAL,
                                     mic INTEGER, sound_score REAL /* footstep confidence 0-1 */, footsteps INTEGER, zone TEXT, perimeter REAL);
  CREATE TABLE IF NOT EXISTS events (device TEXT, t INTEGER, kind TEXT, text TEXT);
  CREATE INDEX IF NOT EXISTS raw_dt    ON raw(device, t);
  CREATE INDEX IF NOT EXISTS points_dt ON points(device, t);
  CREATE INDEX IF NOT EXISTS events_dt ON events(device, t);
`);
const insRaw = db.prepare('INSERT INTO raw VALUES (?, ?, ?, ?)');
const insPoint = db.prepare('INSERT INTO points VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)');
const insEvent = db.prepare('INSERT INTO events VALUES (?, ?, ?, ?)');

function prune() {
  const cutoff = Date.now() - cfg.keepDays * 86400e3;
  for (const t of ['points', 'events']) db.prepare(`DELETE FROM ${t} WHERE t < ?`).run(cutoff);
  db.prepare('DELETE FROM raw WHERE t < ?').run(Date.now() - cfg.keepRawDays * 86400e3);   // raw messages pile up fast
}
prune();
setInterval(prune, 3600e3);

// ---------------- per-device state (merged telemetry, for points + events) ----------------
const devices = new Map();   // id -> { state, info, status, lastSteps }
const dev = id => devices.get(id) ?? devices.set(id, { state: {}, info: {}, status: null, lastSteps: 0 }).get(id);
const num = v => (typeof v === 'number' && isFinite(v) ? v : null);

function event(id, kind, text, t = Date.now()) {
  insEvent.run(id, t, kind, text);
  console.log(new Date(t).toLocaleTimeString(), id, text);
}

function onTelemetry(id, delta, t) {
  const d = dev(id), prev = { ...d.state }, s = Object.assign(d.state, delta);
  for (let i = 1; i <= 3; i++)
    if (!['moving', 'still'].includes(s[`r${i}_state`])) for (const k of ['dist_m', 'speed_mps', 'dir']) delete s[`r${i}_${k}`];
  if (s.perimeter !== 'inside') delete s.nearest_m;

  const r = [1, 2, 3].map(i => num(s[`r${i}_dist_m`]));
  const present = r.filter(v => v != null);
  // footstep confidence: the model reports its top label's score; with two labels the other is 1 - score
  const score = num(s.sound_score), steps = score == null ? null : s.sound_label === 'human_footsteps' ? score : 1 - score;
  insPoint.run(id, t, r[0], r[1], r[2], present.length ? Math.min(...present) : null, num(s.mic_level),
               steps, s.footsteps === 1 ? 1 : 0, s.perimeter ?? null, num(d.info.active_perimeter_m));

  if (prev.perimeter !== s.perimeter && s.perimeter === 'inside')
    event(id, 'in', `Someone entered the perimeter${s.nearest_m != null ? ' at ' + s.nearest_m.toFixed(1) + ' m' : ''}${d.info.active_alarm_on === false ? ' (disarmed)' : ''}`, t);
  if (prev.perimeter && prev.perimeter !== 'clear' && s.perimeter === 'clear') event(id, '', 'Perimeter clear', t);
  if (s.footsteps === 1 && prev.footsteps !== 1 && t - d.lastSteps > 15000) {
    d.lastSteps = t;
    event(id, 'steps', `Footsteps heard (${Math.round((s.sound_score ?? 0) * 100)}% sure)`, t);
  }
  for (let i = 1; i <= 3; i++) {
    const st = s[`r${i}_state`];
    if (prev[`r${i}_state`] !== st && (st === 'offline' || st === 'no_data')) event(id, 'warn', `Sensor ${i} stopped responding`, t);
  }
}

function onInfo(id, info, t) {
  const d = dev(id), old = d.info;
  if (old.active_alarm_on != null && old.active_alarm_on !== info.active_alarm_on)
    event(id, 'set', info.active_alarm_on ? 'Armed' : 'Disarmed', t);
  else {
    const changes = [];
    if (old.active_perimeter_m != null && old.active_perimeter_m !== info.active_perimeter_m) changes.push(`radius ${info.active_perimeter_m} m`);
    if (old.active_volume != null && old.active_volume !== info.active_volume) changes.push(`volume ${(+info.active_volume).toFixed(3)}`);
    if (old.active_led_on != null && old.active_led_on !== info.active_led_on) changes.push(`LED ${info.active_led_on ? 'on' : 'off'}`);
    if (changes.length) event(id, 'set', 'Settings changed: ' + changes.join(', '), t);
  }
  d.info = info;
}

function onStatus(id, status, t, retained) {
  const d = dev(id);
  if (!retained && d.status !== status) event(id, status === 'online' ? 'set' : 'warn', status === 'online' ? 'Device online' : 'Device went offline', t);
  d.status = status;
}

// ---------------- history requests ----------------
function history({ device, kind = 'points', since = Date.now() - 3600e3, until = Date.now() }) {
  if (kind === 'events')
    return db.prepare('SELECT t, kind, text FROM events WHERE device = ? AND t BETWEEN ? AND ? ORDER BY t DESC LIMIT 500').all(device, since, until);
  if (kind === 'raw')
    return db.prepare('SELECT t, topic, json FROM raw WHERE device = ? AND t BETWEEN ? AND ? ORDER BY t DESC LIMIT 500').all(device, since, until);
  // points: averaged into at most maxPoints buckets so long ranges stay small
  const bucket = Math.max(1000, Math.ceil((until - since) / cfg.maxPoints));
  return db.prepare(`
    SELECT (t / ${bucket}) * ${bucket} AS t, MIN(r1) AS r1, MIN(r2) AS r2, MIN(r3) AS r3, MIN(nearest) AS nearest,
           CAST(AVG(mic) AS INTEGER) AS mic, MAX(sound_score) AS sound_score, MAX(footsteps) AS footsteps,
           MAX(zone = 'inside') AS inside, MAX(perimeter) AS perimeter
    FROM points WHERE device = ? AND t BETWEEN ? AND ? GROUP BY t / ${bucket} ORDER BY t`).all(device, since, until);
}

// ---------------- MQTT ----------------
const client = mqtt.connect(cfg.mqttUrl, {
  username: cfg.username || undefined, password: cfg.password || undefined,
  clientId: `phygital-recorder-${Math.random().toString(16).slice(2, 8)}`,
  will: { topic: `${ROOT}/_recorder/status`, payload: 'offline', retain: true, qos: 1 },
  reconnectPeriod: 5000,
});
client.on('connect', () => {
  console.log(`Recorder connected to ${cfg.mqttUrl}, storing in ${cfg.db}`);
  client.publish(`${ROOT}/_recorder/status`, 'online', { retain: true, qos: 1 });
  client.subscribe([`${ROOT}/+/telemetry`, `${ROOT}/+/info`, `${ROOT}/+/status`, `${ROOT}/+/settings`, `${ROOT}/_history/req`], { qos: 1 });
});
client.on('error', e => console.error('MQTT error:', e.message));
client.on('reconnect', () => console.log('MQTT: reconnecting...'));

client.on('message', (topic, buf, packet) => {
  const parts = topic.split('/');
  if (parts.length !== 3 || parts[0] !== ROOT) return;
  const [, id, kind] = parts, t = Date.now(), text = buf.toString();
  try {
    if (id === '_history' && kind === 'req') {
      const req = JSON.parse(text);
      if (!req.id || !req.device) return;
      const rows = history(req);
      client.publish(`${ROOT}/_history/res/${req.id}`, JSON.stringify({ id: req.id, kind: req.kind, rows }));
      return;
    }
    if (id.startsWith('_')) return;
    if (kind === 'status') return onStatus(id, text, t, packet.retain);
    const json = JSON.parse(text);
    insRaw.run(id, t, kind, text);
    if (kind === 'telemetry') onTelemetry(id, json, t);
    else if (kind === 'info') onInfo(id, json, t);
  } catch (e) {
    console.error(`Bad message on ${topic}: ${e.message}`);
  }
});

process.on('SIGINT', () => {
  client.publish(`${ROOT}/_recorder/status`, 'offline', { retain: true, qos: 1 }, () => { client.end(); db.close(); process.exit(0); });
});
