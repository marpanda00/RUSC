/**
 * Simulate GPS devices for the RUSC backend (TCP :3001 + JSON WS :3002).
 * Powers /viewer/ (Babylon) and /index.html live map — not Virtual Eye binary.
 *
 * Prerequisite: backend running (npm start).
 *
 * Usage:
 *   node scripts/simulate-gps.js
 *   node scripts/simulate-gps.js --host localhost --port 3001 --boats 3
 */

const net = require('net');
const fs = require('fs');
const path = require('path');

const args = process.argv.slice(2);
function arg(name, fallback) {
  const i = args.indexOf(name);
  return i >= 0 && args[i + 1] ? args[i + 1] : fallback;
}

const HOST = arg('--host', '127.0.0.1');
const PORT = parseInt(arg('--port', '3001'), 10);
const BOAT_COUNT = Math.min(6, Math.max(1, parseInt(arg('--boats', '2'), 10)));
const HZ = parseFloat(arg('--hz', '1'));

let origin = { latitude: 41.125, longitude: 16.87 };
const liveCfg = path.join(__dirname, '..', 'live-config.json');
if (fs.existsSync(liveCfg)) {
  try {
    const cfg = JSON.parse(fs.readFileSync(liveCfg, 'utf8'));
    if (cfg.origin) origin = cfg.origin;
  } catch (_) { /* ignore */ }
}

const macs = Array.from({ length: BOAT_COUNT }, (_, i) =>
  `AA:BB:CC:DD:EE:${(10 + i).toString(16).toUpperCase().padStart(2, '0')}`
);

function send(client, obj) {
  client.write(JSON.stringify(obj) + '\n');
}

function connect() {
  const client = net.createConnection({ host: HOST, port: PORT }, () => {
    console.log(`[TCP] Connected to ${HOST}:${PORT}`);
    let tick = 0;
    setInterval(() => {
      tick++;
      const t = tick * 0.05;
      macs.forEach((mac, i) => {
        const phase = (i / macs.length) * Math.PI * 2;
        const angle = t + phase;
        const lat = origin.latitude + Math.sin(angle) * 0.0004;
        const lon = origin.longitude + Math.cos(angle) * 0.0005;
        send(client, {
          type: 'gps',
          device_id: `sim-boat-${i + 1}`,
          mac,
          lat,
          lon,
          alt: 0,
          speed: 4 + Math.sin(t) * 1.5,
          course: ((angle * 180 / Math.PI) + 90 + 360) % 360,
          heading: ((angle * 180 / Math.PI) + 90 + 360) % 360,
          quality: 4,
          sats: 12,
          roll: Math.sin(t + phase) * 10,
          pitch: Math.cos(t * 0.7) * 3,
          battery_mv: 4200,
          signal_strength: -55
        });
      });
      console.log(`[SIM] tick ${tick} → ${macs.length} boat(s) near ${origin.latitude}, ${origin.longitude}`);
    }, Math.max(200, Math.round(1000 / HZ)));
  });

  client.on('error', (err) => {
    console.error(`[TCP] ${err.message} — is the backend running? (cd backend && npm start)`);
    process.exit(1);
  });
  client.on('close', () => console.log('[TCP] Disconnected'));
}

console.log('\n📡 RUSC backend GPS simulator');
console.log(`   TCP target  ${HOST}:${PORT}`);
console.log(`   Origin      ${origin.latitude}, ${origin.longitude}`);
console.log(`   Devices     ${macs.join(', ')}`);
console.log('   Open http://localhost:3000/viewer/ or /index.html\n');

connect();
