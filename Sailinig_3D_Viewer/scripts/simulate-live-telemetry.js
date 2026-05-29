/**
 * Simulate live boat telemetry for the Virtual Eye 3D viewer.
 *
 * Broadcasts DLE-framed binary packets (same protocol as ws-relay) on a WebSocket.
 * No hardware or backend required for regatta-viewer testing.
 *
 * Usage:
 *   node scripts/simulate-live-telemetry.js
 *   node scripts/simulate-live-telemetry.js --port 8080 --hz 2
 *   node scripts/simulate-live-telemetry.js --center -36.845,174.768
 *
 * Viewer localconfig.json must use: "websocketUrl": "ws://localhost:8080"
 * (or match --port). Open the viewer, hard-refresh, wait for the 3D scene.
 */

const path = require('path');
const moduleRoots = [
  path.join(__dirname, '..', 'ws-relay', 'node_modules'),
  path.join(__dirname, '..', '..', 'backend', 'node_modules')
];
for (const root of moduleRoots) {
  try {
    require.resolve('ws', { paths: [root] });
    module.paths.unshift(root);
    break;
  } catch (_) { /* try next */ }
}
const { WebSocketServer } = require('ws');
const { encodeBoatPacket, encodeWindPacket } = require(path.join(__dirname, '..', 'ws-relay', 'packet-encoder'));

const args = process.argv.slice(2);
function arg(name, fallback) {
  const i = args.indexOf(name);
  return i >= 0 && args[i + 1] ? args[i + 1] : fallback;
}

const WS_PORT = parseInt(arg('--port', process.env.WS_PORT || '8080'), 10);
const HTTP_PORT = parseInt(arg('--http', process.env.HTTP_PORT || '0'), 10);
const HZ = parseFloat(arg('--hz', '1'));
const centerArg = arg('--center', '-36.845,174.768');
const [CENTER_LAT, CENTER_LON] = centerArg.split(',').map(Number);

if (!Number.isFinite(CENTER_LAT) || !Number.isFinite(CENTER_LON)) {
  console.error('Invalid --center lat,lon');
  process.exit(1);
}

const BOATS = [
  { deviceId: 'sim-etnz', teamId: 1, yachtId: 0, phase: 0, radius: 0.0018, color: 0 },
  { deviceId: 'sim-lrpp', teamId: 2, yachtId: 0, phase: Math.PI * 0.66, radius: 0.0015, color: 0 },
  { deviceId: 'sim-am', teamId: 3, yachtId: 0, phase: Math.PI * 1.33, radius: 0.0012, color: 1 }
];

const WIND_DEG = 245;
const WIND_KNOTS = 12;

const wss = new WebSocketServer({ port: WS_PORT });
const clients = new Set();
let raceStart = Date.now() / 1000;
let tick = 0;

wss.on('connection', (ws, req) => {
  clients.add(ws);
  console.log(`[WS] Viewer connected (${clients.size}) from ${req.socket.remoteAddress || '?'}`);
  ws.on('close', () => {
    clients.delete(ws);
    console.log(`[WS] Viewer disconnected (${clients.size} left)`);
  });
});

function broadcast(buf) {
  for (const ws of clients) {
    if (ws.readyState === 1) ws.send(buf, { binary: true });
  }
}

function raceTimeSec() {
  return Date.now() / 1000 - raceStart;
}

function step() {
  tick++;
  const t = raceTimeSec();
  const windPkt = encodeWindPacket({ raceId: 1, timeSeconds: t, windDeg: WIND_DEG, windKnots: WIND_KNOTS });
  broadcast(windPkt);

  for (const boat of BOATS) {
    const angle = tick * 0.04 + boat.phase;
    const lat = CENTER_LAT + Math.sin(angle) * boat.radius;
    const lon = CENTER_LON + Math.cos(angle) * boat.radius * 1.2;
    const headingDeg = ((angle * 180 / Math.PI) + 90 + 360) % 360;
    const pkt = encodeBoatPacket({
      raceId: 1,
      teamId: boat.teamId,
      yachtId: boat.yachtId,
      trailColor: boat.color,
      timeSeconds: t,
      lat,
      lon,
      headingDeg,
      heelDeg: Math.sin(t * 0.5 + boat.phase) * 12,
      pitchDeg: Math.sin(t * 0.35) * 4,
      speedKnots: 7 + Math.sin(t * 0.2 + boat.phase) * 2,
      elevationM: 0,
      rank: BOATS.indexOf(boat) + 1,
      currentLeg: 1,
      foilState: tick % 40 > 30 ? 1 : 0,
      rudderDeg: Math.sin(t) * 15
    });
    broadcast(pkt);
  }

  if (clients.size > 0) {
    console.log(`[SIM] t=${t.toFixed(1)}s → ${clients.size} viewer(s), ${BOATS.length} boats @ ${CENTER_LAT.toFixed(4)}, ${CENTER_LON.toFixed(4)}`);
  }
}

if (HTTP_PORT > 0) {
  const relay = path.join(__dirname, '..', 'ws-relay', 'server.js');
  console.log(`[HTTP] Optional POST relay on :${HTTP_PORT} — use ws-relay/server.js for full HTTP API`);
}

console.log('\n⛵ RUSC live telemetry simulator (Virtual Eye binary WS)');
console.log(`   WebSocket  ws://localhost:${WS_PORT}`);
console.log(`   Center     ${CENTER_LAT}, ${CENTER_LON}  (${centerArg})`);
console.log(`   Boats      ${BOATS.map((b) => `team ${b.teamId}`).join(', ')} @ ${HZ} Hz`);
console.log(`   Set viewer localconfig: "websocketUrl": "ws://localhost:${WS_PORT}"`);
console.log('\nWaiting for viewer connection…\n');

setInterval(step, Math.max(100, Math.round(1000 / HZ)));
step();
