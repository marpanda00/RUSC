/**
 * Simulate GPS devices for the RUSC backend (TCP :3001 + JSON WS :3002).
 * Powers /viewer/ (Babylon) and /index.html live map — not Virtual Eye binary.
 *
 * Positions orbit live-config.json origin (default: sailing area 41.282284, 13.212244).
 *
 * Prerequisite: backend running (npm start).
 *
 * Usage:
 *   node scripts/simulate-gps.js
 *   node scripts/simulate-gps.js --boats 3
 *   node scripts/simulate-gps.js --lat 41.282284 --lon 13.212244
 */

const net = require('net');
const fs = require('fs');
const path = require('path');

const SAILING_ORIGIN = { latitude: 41.282284, longitude: 13.212244 };

const args = process.argv.slice(2);
function arg(name, fallback) {
  const i = args.indexOf(name);
  return i >= 0 && args[i + 1] ? args[i + 1] : fallback;
}

const HOST = arg('--host', '127.0.0.1');
const PORT = parseInt(arg('--port', '3001'), 10);
const BOAT_COUNT = Math.min(6, Math.max(1, parseInt(arg('--boats', '2'), 10)));
const HZ = parseFloat(arg('--hz', '1'));

function normalizeOrigin(o) {
  if (!o) return { ...SAILING_ORIGIN };
  return {
    latitude: Number(o.latitude ?? o.lat ?? SAILING_ORIGIN.latitude),
    longitude: Number(o.longitude ?? o.lon ?? SAILING_ORIGIN.longitude)
  };
}

function loadOrigin() {
  const liveCfg = path.join(__dirname, '..', 'live-config.json');
  if (fs.existsSync(liveCfg)) {
    try {
      const cfg = JSON.parse(fs.readFileSync(liveCfg, 'utf8'));
      if (cfg.origin) return normalizeOrigin(cfg.origin);
    } catch (e) {
      console.warn('[SIM] live-config.json parse failed, using defaults:', e.message);
    }
  }
  return { ...SAILING_ORIGIN };
}

let origin = loadOrigin();

const cliLat = arg('--lat', null);
const cliLon = arg('--lon', null);
if (cliLat != null && cliLon != null) {
  origin = {
    latitude: parseFloat(cliLat),
    longitude: parseFloat(cliLon)
  };
}

/** Orbit radius in degrees (~45 m lat, ~40 m lon at this latitude). */
const ORBIT_DLAT = 0.0004;
const ORBIT_DLON = 0.00035;

const macs = Array.from({ length: BOAT_COUNT }, (_, i) =>
  `AA:BB:CC:DD:EE:${(10 + i).toString(16).toUpperCase().padStart(2, '0')}`
);

function send(client, obj) {
  client.write(JSON.stringify(obj) + '\n');
}

const COS_LAT = Math.cos(origin.latitude * Math.PI / 180);

function samplePosition(i, t) {
  const phase = (i / macs.length) * Math.PI * 2;
  const angle = t + phase;

  // Elliptical orbit around the origin.
  const lat = origin.latitude + Math.sin(angle) * ORBIT_DLAT;
  const lon = origin.longitude + Math.cos(angle) * ORBIT_DLON;

  // Velocity tangent to the path (derivative wrt angle), converted to meters so the
  // heading is the true direction of travel for this elliptical, anisotropic orbit.
  const vNorth = Math.cos(angle) * ORBIT_DLAT * 110540;
  const vEast = -Math.sin(angle) * ORBIT_DLON * 111320 * COS_LAT;

  // Compass course: 0° = North, 90° = East, clockwise (true direction of travel).
  const course = (Math.atan2(vEast, vNorth) * 180 / Math.PI + 360) % 360;

  return { lat, lon, course };
}

function connect() {
  const client = net.createConnection({ host: HOST, port: PORT }, () => {
    console.log(`[TCP] Connected to ${HOST}:${PORT}`);
    let tick = 0;
    setInterval(() => {
      tick++;
      const t = tick * 0.05;
      macs.forEach((mac, i) => {
        const pos = samplePosition(i, t);
        send(client, {
          type: 'gps',
          device_id: `sim-boat-${i + 1}`,
          mac,
          lat: pos.lat,
          lon: pos.lon,
          alt: 0,
          speed: 4 + Math.sin(t) * 1.5,
          course: pos.course,
          heading: pos.course,
          quality: 4,
          sats: 12,
          roll: 0, /**Math.sin(t + (i / macs.length) * Math.PI * 2) * 10,*/
          pitch: 0, /**Math.cos(t * 0.7) * 3,*/
          battery_mv: 4200,
          signal_strength: -55
        });
      });
      if (tick === 1 || tick % 10 === 0) {
        const p0 = samplePosition(0, t);
        console.log(
          `[SIM] tick ${tick} — ${macs.length} boat(s) orbiting ` +
          `${origin.latitude.toFixed(6)}, ${origin.longitude.toFixed(6)} ` +
          `(e.g. ${p0.lat.toFixed(6)}, ${p0.lon.toFixed(6)})`
        );
      }
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
console.log(`   Center      ${origin.latitude}, ${origin.longitude}`);
console.log(`   Orbit       ±${(ORBIT_DLAT * 110540).toFixed(0)} m (lat), ±${(ORBIT_DLON * 111320 * Math.cos(origin.latitude * Math.PI / 180)).toFixed(0)} m (lon)`);
console.log(`   Devices     ${macs.join(', ')}`);
console.log('   Open http://localhost:3000/viewer/ or /index.html\n');

connect();
