// Data sources shared by the phone app (index.html) and the admin dashboard (admin.html).
// ================= DATA SOURCES =================
// Every source calls the same handlers with the same keys (README "Telemetry"),
// and offers connect / disconnect / sendConfig / preview. MqttSource also offers history().
//   h.telemetry(obj)  changed values (r1_state, r1_dist_m, perimeter, mic_level, ...)
//   h.attributes(obj) device info + active_* settings (+ device_online, recorder_online for MQTT)
//   h.log(line)       raw text from the device
//   h.status(text, connected)

class UsbSource {
  name = 'USB';
  async connect(h) {
    if (!('serial' in navigator)) throw new Error('This browser has no USB support. Use Chrome or Edge on localhost.');
    this.h = h;
    this.port = await navigator.serial.requestPort();
    await this.port.open({ baudRate: 115200 });
    h.status('Connected over USB', true);
    this.readLoop();
    await this.send({ hello: true });                          // ask for a full snapshot
    this.helloTimer = setTimeout(() => this.send({ hello: true }), 3000);
  }
  async readLoop() {
    const dec = new TextDecoderStream();
    this.piped = this.port.readable.pipeTo(dec.writable).catch(() => {});
    this.reader = dec.readable.getReader();
    let buf = '';
    try {
      for (;;) {
        const { value, done } = await this.reader.read();
        if (done) break;
        buf += value;
        let i;
        while ((i = buf.indexOf('\n')) >= 0) { this.line(buf.slice(0, i).replace(/\r$/, '')); buf = buf.slice(i + 1); }
      }
    } catch (e) { /* unplugged or closed */ }
    if (!this.closing) this.h.status('Disconnected', false);
  }
  line(l) {
    try {
      if (l.startsWith('@T ')) return this.h.telemetry(JSON.parse(l.slice(3)));
      if (l.startsWith('@A ')) return this.h.attributes(JSON.parse(l.slice(3)));
    } catch (e) { /* partial line */ }
    if (!l.startsWith('Perimeter:')) this.h.log(l);            // skip the 4-per-second status lines
  }
  async send(obj) {
    if (!this.port?.writable) return;
    const w = this.port.writable.getWriter();
    try { await w.write(new TextEncoder().encode(JSON.stringify(obj) + '\n')); } finally { w.releaseLock(); }
  }
  sendConfig(obj) { return this.send(obj); }
  preview() { return this.send({ preview: true }); }
  async disconnect() {
    this.closing = true;
    clearTimeout(this.helloTimer);
    try { await this.reader?.cancel(); } catch (e) {}
    try { await this.piped; } catch (e) {}
    try { await this.port?.close(); } catch (e) {}
  }
}

class DemoSource {
  name = 'Demo';
  connect(h) {
    this.h = h;
    this.cfg = { perimeter_m: 6, volume: 0.01, alarm_on: false, led_on: true };
    this.t0 = Date.now(); this.prev = [null, null, null]; this.occupied = false; this.lastInside = 0;
    h.status('Demo data', true);
    h.log('Demo mode: a simulated person walks around the device every 75 s.');
    this.sendInfo();
    this.tick();
    this.timer = setInterval(() => this.tick(), 1000);
  }
  sendInfo() {
    const c = this.cfg;
    this.h.attributes({ firmware: 'demo', board: 'simulated',
      radar1_online: true, radar2_online: true, radar3_online: true, mic_ok: true, speaker_ok: true, sound_ai_ok: true,
      active_perimeter_m: c.perimeter_m, active_volume: c.volume, active_alarm_on: c.alarm_on, active_led_on: c.led_on });
  }
  tick() {
    const t = (Date.now() - this.t0) / 1000;
    const cycle = (t % 75) / 75;
    const d = Math.max(1.1, 10.5 - 11 * Math.sin(Math.PI * cycle));
    const angle = (t * 4) % 360;
    const out = {}, dirs = []; let inside = false, nearest = 99;
    for (let i = 0; i < 3; i++) {
      const diff = Math.abs(((angle - i * 120 + 540) % 360) - 180), p = `r${i + 1}`;
      if (diff < 70 && d < 10.5) {
        const dist = +(d + 0.25 * i + (Math.random() - 0.5) * 0.08).toFixed(2);
        const delta = this.prev[i] == null ? 0 : dist - this.prev[i];
        const dir = delta < -0.05 ? 'approach' : delta > 0.05 ? 'away' : 'same';
        Object.assign(out, { [`${p}_state`]: Math.abs(delta) > 0.05 ? 'moving' : 'still', [`${p}_dist_m`]: dist,
          [`${p}_speed_mps`]: +Math.abs(delta).toFixed(2), [`${p}_dir`]: dir });
        this.prev[i] = dist; dirs.push(dir);
        if (dist <= this.cfg.perimeter_m) { inside = true; nearest = Math.min(nearest, dist); }
      } else { out[`${p}_state`] = 'none'; this.prev[i] = null; }
    }
    if (inside) { this.occupied = true; this.lastInside = Date.now(); }
    else if (this.occupied && Date.now() - this.lastInside > 3000) this.occupied = false;
    out.perimeter = inside ? 'inside' : this.occupied ? 'leaving' : 'clear';
    if (inside) out.nearest_m = +nearest.toFixed(2);
    const steps = d < 5 && dirs.some(x => x !== 'same') && Math.random() < 0.6;
    out.sound_label = steps ? 'human_footsteps' : 'Background_noise';
    out.sound_score = +(steps ? 0.7 + Math.random() * 0.25 : 0.9 + Math.random() * 0.1).toFixed(2);
    out.footsteps = steps ? 1 : 0;
    out.mic_level = Math.round(300 + Math.random() * 400 + (steps ? 900 : 0));
    const led = dirs.includes('approach') ? 'red' : dirs.includes('away') ? 'green' : dirs.includes('same') ? 'blue' : 'off';
    out.led = this.cfg.led_on ? led : 'off';
    this.h.telemetry(out);
  }
  sendConfig(obj) {
    const c = this.cfg, clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
    if (typeof obj.perimeter_m === 'number') c.perimeter_m = clamp(obj.perimeter_m, 1.5, 11);
    if (typeof obj.volume === 'number') c.volume = clamp(obj.volume, 0, 1);
    if (typeof obj.alarm_on === 'boolean') c.alarm_on = obj.alarm_on;
    if (typeof obj.led_on === 'boolean') c.led_on = obj.led_on;
    this.h.log('Settings: ' + JSON.stringify(c));
    setTimeout(() => this.sendInfo(), 250);
  }
  preview() {                                                   // play the beep on this computer instead
    try {
      const ctx = new AudioContext(), o = ctx.createOscillator(), g = ctx.createGain();
      o.frequency.value = 1000; g.gain.value = Math.min(1, this.cfg.volume * 2);
      o.connect(g).connect(ctx.destination); o.start(); o.stop(ctx.currentTime + 0.3);
      setTimeout(() => ctx.close(), 600);
    } catch (e) {}
  }
  disconnect() { clearInterval(this.timer); }
}

// ================= INTERNET (MQTT broker) =================
// Talks to the device through the same broker it publishes to (README "Broker setup").
// Topics: ROOT/ID/{telemetry,info,status,settings,cmd}; history via the recorder.
const BROKER_KEY = 'phygital.broker.v1';
// Pre-filled in the connect forms. Change these if the broker deployment is recreated.
const BROKER_DEFAULTS = {
  url: 'wss://y941182f.ala.us-east-1.emqxsl.com:8084/mqtt', username: 'phygital-app', password: '',
  device: 'phygital-01', root: 'phygital',
};
function loadBroker() {
  let saved = {};
  try { saved = JSON.parse(localStorage.getItem(BROKER_KEY)) || {}; } catch (e) {}
  const b = { ...BROKER_DEFAULTS, ...saved };
  for (const k of ['url', 'username', 'device', 'root']) if (!b[k]) b[k] = BROKER_DEFAULTS[k];   // blank → default
  return b;
}
function saveBroker(b) { try { localStorage.setItem(BROKER_KEY, JSON.stringify(b)); } catch (e) {} }

class MqttSource {
  name = 'Internet';
  constructor(opts) {
    this.o = { root: 'phygital', ...opts };
    this.active = {};                       // settings in use, from the device's info message
    this.pending = new Map(); this.n = 0;
    this.rid = Math.random().toString(16).slice(2, 10);
    this.devices = new Map();               // discovered devices (admin): id -> { status, info, seen }
  }
  topic(k, id = this.o.device) { return `${this.o.root}/${id}/${k}`; }
  connect(h) {
    this.h = h;
    return new Promise((resolve, reject) => {
      if (!window.mqtt) return reject(new Error('The MQTT library did not load. Check the internet connection.'));
      if (!this.o.url) return reject(new Error('Enter the broker address first.'));
      const c = this.c = mqtt.connect(this.o.url, {
        username: this.o.username || undefined, password: this.o.password || undefined,
        clientId: 'phygital-app-' + this.rid, reconnectPeriod: 5000, connectTimeout: 10000,
      });
      let settled = false;
      const fail = msg => { if (!settled) { settled = true; c.end(true); reject(new Error(msg)); } };
      const timer = setTimeout(() => fail('Could not reach the broker. Check the address (wss://…:8084/mqtt).'), 12000);
      c.on('connect', () => {
        const R = this.o.root;
        c.subscribe([`${R}/+/telemetry`, `${R}/+/info`, `${R}/+/status`, `${R}/_recorder/status`, `${R}/_history/res/${this.rid}/#`], { qos: 1 });
        this.hello();
        h.status(`${this.o.device}: waiting for device…`, true);
        if (!settled) { settled = true; clearTimeout(timer); resolve(); }
      });
      c.on('error', e => { if (!settled) { clearTimeout(timer); fail(/auth/i.test(e.message) ? 'Wrong username or password.' : e.message); } else h.log('MQTT: ' + e.message); });
      c.on('offline', () => { if (settled) h.status('Reconnecting to broker…', true); });
      c.on('message', (topic, buf) => this.message(topic, buf.toString()));
    });
  }
  message(topic, text) {
    const [root, id, kind, reqId] = topic.split('/');
    if (root !== this.o.root) return;
    if (id === '_recorder') { this.recorder = text === 'online'; return this.h.attributes({ recorder_online: this.recorder }); }
    if (id === '_history' && kind === 'res') {
      let res; try { res = JSON.parse(text); } catch (e) { return; }
      const p = this.pending.get(res.id); if (p) { this.pending.delete(res.id); clearTimeout(p.timer); p.resolve(res.rows); }
      return;
    }
    if (id.startsWith('_')) return;
    const isNew = !this.devices.has(id);
    const d = this.devices.get(id) || { id, status: null, info: {}, seen: 0 };
    d.seen = Date.now(); this.devices.set(id, d);
    let json = null;
    if (kind !== 'status') { try { json = JSON.parse(text); } catch (e) { return; } }
    const statusChanged = kind === 'status' && d.status !== text;
    if (kind === 'status') d.status = text; else if (kind === 'info') d.info = json;
    if (isNew || statusChanged) this.h.devices?.(this.devices);
    if (id !== this.o.device) return;
    if (kind === 'telemetry') this.h.telemetry(json);
    else if (kind === 'info') { this.active = json; this.h.attributes(json); }
    else if (kind === 'status') {
      const online = text === 'online';
      this.h.status(`${id} ${online ? 'online' : 'offline'}`, true);
      this.h.attributes({ device_online: online });
    }
    this.h.raw?.(topic, text);
  }
  hello() { this.c?.publish(this.topic('cmd'), JSON.stringify({ hello: true })); }
  setDevice(id) {                       // admin: switch which device the handlers follow
    this.o.device = id; this.active = this.devices.get(id)?.info || {};
    const d = this.devices.get(id);
    if (d?.info) this.h.attributes(d.info);
    if (d?.status) this.h.status(`${id} ${d.status}`, true);
    this.hello();
  }
  // The device keeps the latest settings message (retained), so always send all four values.
  sendConfig(obj) {
    const a = this.active, full = {
      perimeter_m: a.active_perimeter_m ?? 6, volume: a.active_volume ?? 0.01,
      alarm_on: a.active_alarm_on ?? true, led_on: a.active_led_on ?? true, ...obj,
    };
    this.c?.publish(this.topic('settings'), JSON.stringify(full), { retain: true, qos: 1 });
  }
  preview() { this.c?.publish(this.topic('cmd'), JSON.stringify({ preview: true }), { qos: 1 }); }
  // Ask the recorder for stored history. kind: 'points' | 'events' | 'raw'
  history(kind, since, until = Date.now()) {
    if (!this.c?.connected) return Promise.reject(new Error('not connected'));
    const id = `${this.rid}/${++this.n}`;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.pending.delete(id); reject(new Error('The recorder did not answer. Is it running?')); }, 8000);
      this.pending.set(id, { resolve, timer });
      this.c.publish(`${this.o.root}/_history/req`, JSON.stringify({ id, device: this.o.device, kind, since, until }), { qos: 1 });
    });
  }
  disconnect() { this.c?.end(true); }
}
