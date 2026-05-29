/**
 * Sailboat 3D Regatta — Live WebSocket Relay Server
 *
 * Accepts device telemetry (JSON via HTTP POST) and broadcasts binary
 * packets to all connected Virtual Eye viewer clients.
 *
 * Usage:
 *   npm install
 *   node server.js
 *
 * Then configure your viewer's WebSocket URL to ws://localhost:8080
 * and POST device data to http://localhost:3001/telemetry
 *
 * Device JSON format (POST to /telemetry):
 * {
 *   "deviceId": "boat1",          // unique device identifier
 *   "teamId":   1,                // matches team_id in appconfig.json
 *   "yachtId":  0,                // 0-63, boat number
 *   "raceId":   1,                // race identifier
 *   "lat":      -36.845,          // WGS84 latitude
 *   "lon":      174.768,          // WGS84 longitude
 *   "headingDeg":  270.5,         // compass heading
 *   "heelDeg":    -5.2,           // IMU roll
 *   "pitchDeg":    1.1,           // IMU pitch
 *   "speedKnots":  8.4,           // GPS or log speed
 *   "elevationM":  0.0,           // GPS altitude (optional)
 *   "windDeg":     245.0,         // true wind direction (optional)
 *   "windKnots":   12.5           // true wind speed (optional)
 * }
 */

const http = require('http');
const { WebSocketServer } = require('ws');
const { encodeBoatPacket, encodeWindPacket } = require('./packet-encoder');

const WS_PORT   = 8080;   // Viewer connects here  (ws://localhost:8080)
const HTTP_PORT = 3001;   // Devices POST here     (http://localhost:3001/telemetry)

// ── WebSocket server (viewer connects here) ───────────────────────────────────

const wss = new WebSocketServer({ port: WS_PORT });
const clients = new Set();

wss.on('connection', (ws, req) => {
  clients.add(ws);
  console.log(`[WS] Viewer connected  (${clients.size} total) from ${req.socket.remoteAddress}`);
  ws.on('close', () => {
    clients.delete(ws);
    console.log(`[WS] Viewer disconnected (${clients.size} remaining)`);
  });
  ws.on('error', (err) => console.error('[WS] Client error:', err.message));
});

function broadcast(packet) {
  for (const ws of clients) {
    if (ws.readyState === 1 /* OPEN */) {
      ws.send(packet, { binary: true });
    }
  }
}

// ── State ─────────────────────────────────────────────────────────────────────

const lastPosition = new Map();  // deviceId → last packet for rank calculation
let   raceStartTime = null;      // seconds, set on first packet

function getRaceTime() {
  if (!raceStartTime) raceStartTime = Date.now() / 1000;
  return Date.now() / 1000 - raceStartTime;
}

// ── HTTP server (devices POST here) ──────────────────────────────────────────

const httpServer = http.createServer((req, res) => {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'POST, GET, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

  if (req.method === 'OPTIONS') { res.writeHead(204); res.end(); return; }

  if (req.method === 'GET' && req.url === '/status') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      viewers: clients.size,
      boats: lastPosition.size,
      uptime: Math.round(process.uptime())
    }));
    return;
  }

  if (req.method === 'POST' && req.url === '/telemetry') {
    let body = '';
    req.on('data', chunk => body += chunk);
    req.on('end', () => {
      try {
        const data = JSON.parse(body);
        handleTelemetry(data);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true }));
      } catch (e) {
        console.error('[HTTP] Bad JSON:', e.message);
        res.writeHead(400);
        res.end(JSON.stringify({ error: e.message }));
      }
    });
    return;
  }

  res.writeHead(404);
  res.end();
});

// ── Telemetry handler ─────────────────────────────────────────────────────────

function handleTelemetry(data) {
  const timeSeconds = data.timeSeconds || getRaceTime();

  // Calculate rank from DTL (distance to start/lead position)
  lastPosition.set(data.deviceId, { lat: data.lat, lon: data.lon, time: timeSeconds });

  // Encode and broadcast boat packet
  const boatPkt = encodeBoatPacket({
    raceId:      data.raceId      || 1,
    teamId:      data.teamId      || 1,
    yachtId:     data.yachtId     || 0,
    timeSeconds,
    lat:         data.lat,
    lon:         data.lon,
    headingDeg:  data.headingDeg  || 0,
    heelDeg:     data.heelDeg     || 0,
    pitchDeg:    data.pitchDeg    || 0,
    speedKnots:  data.speedKnots  || 0,
    elevationM:  data.elevationM  || 0,
    dtlMetres:   data.dtlMetres   || 0,
    rank:        data.rank        || 0,
    currentLeg:  data.currentLeg  || 0,
    foilState:   data.foilState   || 0,
    rudderDeg:   data.rudderDeg   || 0,
    trailColor:  data.trailColor  || 0,
  });
  broadcast(boatPkt);

  // Encode and broadcast wind packet (if wind data present)
  if (data.windDeg !== undefined && data.windKnots !== undefined) {
    const windPkt = encodeWindPacket({
      raceId:      data.raceId || 1,
      timeSeconds,
      windDeg:     data.windDeg,
      windKnots:   data.windKnots,
    });
    broadcast(windPkt);
  }

  console.log(`[DATA] boat${data.yachtId}@team${data.teamId} ` +
    `lat=${data.lat?.toFixed(5)} lon=${data.lon?.toFixed(5)} ` +
    `hdg=${data.headingDeg?.toFixed(1)}° spd=${data.speedKnots?.toFixed(1)}kn ` +
    `→ ${clients.size} viewer(s)`);
}

// ── Start ─────────────────────────────────────────────────────────────────────

httpServer.listen(HTTP_PORT, () => {
  console.log(`\n🚤 Sailboat 3D Relay Server`);
  console.log(`   WebSocket  (viewer)  → ws://localhost:${WS_PORT}`);
  console.log(`   HTTP POST  (devices) → http://localhost:${HTTP_PORT}/telemetry`);
  console.log(`   Status               → http://localhost:${HTTP_PORT}/status`);
  console.log(`\nWaiting for connections...\n`);
});

// ── Example: simulate a single test boat (remove in production) ───────────────

if (process.argv.includes('--demo')) {
  console.log('[DEMO] Simulating one boat at 1Hz...');
  let t = 0;
  const CENTER_LAT = -36.845;
  const CENTER_LON =  174.768;
  setInterval(() => {
    t++;
    handleTelemetry({
      deviceId:    'demo-boat-1',
      teamId:      1,
      yachtId:     0,
      raceId:      1,
      lat:         CENTER_LAT + Math.sin(t * 0.05) * 0.002,
      lon:         CENTER_LON + Math.cos(t * 0.05) * 0.002,
      headingDeg:  (t * 3) % 360,
      heelDeg:     Math.sin(t * 0.1) * 15,
      pitchDeg:    Math.sin(t * 0.07) * 3,
      speedKnots:  8 + Math.random() * 2,
      windDeg:     245,
      windKnots:   12,
    });
  }, 1000);
}
