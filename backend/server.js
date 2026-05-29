/**
 * RUSC GPS Gateway Backend
 * Receives GPS data from Device 2 and provides API for web frontend
 */

const express = require('express');
const cors = require('cors');
const net = require('net');
const https = require('https');
const fs = require('fs');
const http = require('http');
const { WebSocketServer } = require('ws');
require('dotenv').config();

const app = express();
const API_PORT = process.env.API_PORT || 3000;
const HTTPS_PORT = process.env.HTTPS_PORT || 8443;
const TCP_PORT = process.env.TCP_PORT || 3001;
const LIVE_WS_PORT = process.env.LIVE_WS_PORT || 3002;

// Create separate app for HTTPS website
const websiteApp = express();

// API Middleware
app.use(cors());
app.use(express.json());
app.use(express.static('public')); // Serve website pages on the HTTP endpoint too.

// Website App Middleware (HTTPS)
websiteApp.use(cors());
websiteApp.use(express.json());
websiteApp.use(express.static('public')); // Serve static files on HTTPS

// In-memory data storage. Devices are keyed by stable hardware identity when available.
let gpsData = {};

let gatewayStatus = {
  device_id: 'device2',
  last_update: null,
  battery_mv: 0,
  battery: 0
};

const MAX_HISTORY = 100;
const ACTIVE_DEVICE_MS = 30000;
const DEVICE_NAMES_FILE = 'device-names.json';
const BOAT_PROFILES_FILE = 'boat-profiles.json';
const BOAT_MODELS_FILE = 'boat-models.json';
const BOAT_TYPES_FILE = 'boat-types.json';
const LIVE_CONFIG_FILE = 'live-config.json';

let gatewaySocket = null;
let nextCommandId = 1;
let calibrationCommands = {};
let deviceNames = {};
let boatProfiles = {};
let boatModels = {};
let boatTypes = {};
let liveConfig = { origin: { latitude: 41.282284, longitude: 13.212244 } };

const wsClients = new Set();

const OSM_TILE_BASE = 'https://tile.openstreetmap.org';
const OSM_TILE_USER_AGENT = 'RUSC-Regatta-Viewer/1.0 (local development)';
const TILE_CACHE_TTL_MS = 7 * 24 * 60 * 60 * 1000;
const TILE_CACHE_DIR = require('path').join(__dirname, 'cache', 'osm-tiles');
const tileCache = new Map();

function tileDiskPath(z, x, y) {
  return require('path').join(TILE_CACHE_DIR, String(z), String(x), `${y}.png`);
}

// Memory → disk → network. Disk cache survives restarts so we don't re-hammer
// (and get blocked by) the OSM tile server every time the server restarts.
function fetchOsmTile(z, x, y) {
  const key = `${z}/${x}/${y}`;
  const cached = tileCache.get(key);
  if (cached && cached.expires > Date.now()) {
    return Promise.resolve(cached.buf);
  }
  const diskPath = tileDiskPath(z, x, y);
  try {
    const stat = fs.statSync(diskPath);
    if (Date.now() - stat.mtimeMs < TILE_CACHE_TTL_MS) {
      const buf = fs.readFileSync(diskPath);
      tileCache.set(key, { buf, expires: Date.now() + TILE_CACHE_TTL_MS });
      return Promise.resolve(buf);
    }
  } catch (_) {
    // not on disk yet
  }
  return new Promise((resolve, reject) => {
    const url = `${OSM_TILE_BASE}/${z}/${x}/${y}.png`;
    https.get(url, {
      headers: { 'User-Agent': OSM_TILE_USER_AGENT }
    }, (res) => {
      if (res.statusCode !== 200) {
        reject(new Error(`OSM HTTP ${res.statusCode}`));
        res.resume();
        return;
      }
      const chunks = [];
      res.on('data', (chunk) => chunks.push(chunk));
      res.on('end', () => {
        const buf = Buffer.concat(chunks);
        tileCache.set(key, { buf, expires: Date.now() + TILE_CACHE_TTL_MS });
        try {
          fs.mkdirSync(require('path').dirname(diskPath), { recursive: true });
          fs.writeFileSync(diskPath, buf);
        } catch (e) {
          console.warn('[map/tiles] disk cache write failed:', e.message);
        }
        resolve(buf);
      });
    }).on('error', reject);
  });
}

function loadJsonFile(path, fallback) {
  try {
    return JSON.parse(fs.readFileSync(path, 'utf8'));
  } catch (error) {
    console.warn(`[config] failed to load ${path}: ${error.message} — using fallback`);
    return fallback;
  }
}

function loadDeviceNames() {
  deviceNames = loadJsonFile(DEVICE_NAMES_FILE, {});
}

function saveDeviceNames() {
  fs.writeFileSync(DEVICE_NAMES_FILE, JSON.stringify(deviceNames, null, 2));
}

function loadBoatCatalog() {
  boatProfiles = loadJsonFile(BOAT_PROFILES_FILE, {});
  boatModels = loadJsonFile(BOAT_MODELS_FILE, {});
  boatTypes = loadJsonFile(BOAT_TYPES_FILE, {});
  liveConfig = loadJsonFile(LIVE_CONFIG_FILE, liveConfig);
}

function saveBoatProfiles() {
  fs.writeFileSync(BOAT_PROFILES_FILE, JSON.stringify(boatProfiles, null, 2));
}

function normalizeDeviceKey(value) {
  return String(value || '').trim().toLowerCase();
}

function macSuffix(mac) {
  const parts = String(mac || '').split(':').filter(Boolean);
  return parts.length >= 2 ? parts.slice(-2).join(':') : 'new';
}

function hullTypeForModel(modelId) {
  const model = boatModels[modelId];
  if (model && model.hullType) {
    return model.hullType;
  }
  return modelId === 'catamaran' ? 'multihull' : 'monohull';
}

function defaultProfileForMac(mac) {
  const type = boatTypes.default || { modelId: '470', color: '#2563eb' };
  const modelId = type.modelId || '470';
  return {
    displayName: `Boat ${macSuffix(mac)}`,
    boatType: 'default',
    modelId,
    color: type.color || '#2563eb',
    hullType: type.hullType || hullTypeForModel(modelId),
    scale: 1.0,
    configured: false
  };
}

function ensureBoatProfile(deviceKey) {
  if (!deviceKey || boatProfiles[deviceKey]) {
    return boatProfiles[deviceKey];
  }
  const legacyName = deviceNames[deviceKey];
  boatProfiles[deviceKey] = {
    ...defaultProfileForMac(deviceKey),
    ...(legacyName ? { displayName: legacyName, configured: true } : {})
  };
  saveBoatProfiles();
  return boatProfiles[deviceKey];
}

function getBoatProfile(deviceKey) {
  return boatProfiles[deviceKey] || ensureBoatProfile(deviceKey);
}

function resolveDisplayName(deviceKey, fallbackId) {
  const profile = boatProfiles[deviceKey];
  if (profile && profile.displayName) {
    return profile.displayName;
  }
  return deviceNames[deviceKey] || fallbackId || deviceKey;
}

function deviceToApiPayload(device) {
  const key = device.device_id;
  const profile = getBoatProfile(key);
  return {
    device_id: device.device_id,
    source_device_id: device.source_device_id,
    mac: device.mac,
    display_name: resolveDisplayName(key, device.source_device_id),
    boatType: profile.boatType,
    modelId: profile.modelId,
    color: profile.color,
    hullType: profile.hullType || hullTypeForModel(profile.modelId),
    scale: profile.scale ?? 1,
    configured: Boolean(profile.configured),
    last_update: device.last_update,
    position: device.position,
    altitude_m: device.altitude_m ?? 0,
    speed_knots: device.speed_knots,
    course: device.course,
    quality: device.quality,
    signal_strength: device.signal_strength,
    battery_mv: device.battery_mv,
    battery: device.battery,
    halow_status: device.halow_status,
    roll: device.roll || 0,
    pitch: device.pitch || 0,
    yaw_rate: device.yaw_rate || 0,
    calibration_state: device.calibration_state || 0,
    calibration_progress: device.calibration_progress || 0,
    last_command_id: device.last_command_id || 0,
    home_point: device.home_point,
    current_distance: device.current_distance,
    max_distance: device.max_distance
  };
}

function broadcastBoatUpdate(deviceKey) {
  const device = gpsData[deviceKey];
  if (!device || !isDeviceActive(device)) {
    return;
  }
  const payload = deviceToApiPayload(device);
  const message = JSON.stringify({
    type: 'boat_update',
    ...payload,
    heading_deg: payload.course,
    lat: payload.position.latitude,
    lon: payload.position.longitude,
    ts: device.last_update ? device.last_update.toISOString() : new Date().toISOString()
  });
  for (const ws of wsClients) {
    if (ws.readyState === 1) {
      ws.send(message);
    }
  }
}

function isDeviceActive(device) {
  return Boolean(device.last_update && (Date.now() - new Date(device.last_update)) < ACTIVE_DEVICE_MS);
}

loadDeviceNames();
loadBoatCatalog();

// Hot-reload live-config.json on edit so viewer changes (origin, water, osmGround,
// boatFloatLiftM, …) apply on the next browser refresh — no server restart needed.
try {
  fs.watchFile(LIVE_CONFIG_FILE, { interval: 1000 }, () => {
    const next = loadJsonFile(LIVE_CONFIG_FILE, null);
    if (next) {
      liveConfig = next;
      console.log(`[config] reloaded ${LIVE_CONFIG_FILE} (boatFloatLiftM=${liveConfig.boatFloatLiftM})`);
    }
  });
} catch (e) {
  console.warn('[config] could not watch live-config.json:', e.message);
}

// ============ Haversine Distance Calculator ============

function calculateDistance(lat1, lon1, lat2, lon2) {
  const R = 6371; // Earth's radius in km
  const dLat = (lat2 - lat1) * Math.PI / 180;
  const dLon = (lon2 - lon1) * Math.PI / 180;
  const a = Math.sin(dLat / 2) * Math.sin(dLat / 2) +
    Math.cos(lat1 * Math.PI / 180) * Math.cos(lat2 * Math.PI / 180) *
    Math.sin(dLon / 2) * Math.sin(dLon / 2);
  const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
  return R * c;
}

const tcpServer = net.createServer((socket) => {
  console.log(`[TCP] Client connected from ${socket.remoteAddress}`);
  gatewaySocket = socket;
  
  let buffer = '';
  
  socket.on('data', (chunk) => {
    try {
      buffer += chunk.toString('utf-8');
      
      // Split by newlines and process complete messages
      const lines = buffer.split('\n');
      buffer = lines.pop(); // Keep incomplete line in buffer
      
      for (const line of lines) {
        if (line.trim()) {
          const data = JSON.parse(line);
          if (data.type === 'gateway_status') {
            processGatewayStatus(data);
          } else {
            processGPSData(data);
          }
        }
      }
    } catch (error) {
      console.error('[TCP] Parse error:', error.message);
    }
  });
  
  socket.on('end', () => {
    console.log('[TCP] Client disconnected');
    if (gatewaySocket === socket) {
      gatewaySocket = null;
    }
  });
  
  socket.on('error', (error) => {
    console.error('[TCP] Socket error:', error.message);
    if (gatewaySocket === socket) {
      gatewaySocket = null;
    }
  });
});

tcpServer.listen(TCP_PORT, '0.0.0.0', () => {
  console.log(`TCP server listening on port ${TCP_PORT}`);
});

// ============ Process GPS Data ============

function processGatewayStatus(data) {
  gatewayStatus = {
    device_id: data.device_id || 'device2',
    last_update: new Date(),
    battery_mv: data.battery_mv || 0,
    battery: data.battery || 0
  };
}

function processGPSData(data) {
  // Support both flat format (device1) and nested format
  // Device1 format: {device_id, lat, lon, alt, speed, sats, quality}
  // Expected format: {device_id, position: {latitude, longitude}, ...}
  
  if (!data.device_id) {
    console.warn('[Data] Missing device_id');
    return;
  }
  
  // Convert device1 flat format to expected format
  let position, speed, quality, alt, sats, course;
  
  if (data.position) {
    // Already in expected format
    position = data.position;
    speed = data.speed_knots || 0;
    quality = data.quality || 0;
    alt = data.altitude || 0;
    sats = data.satellites || 0;
    course = data.course ?? data.heading ?? 0;
  } else if (data.lat !== undefined && data.lon !== undefined) {
    // Device1 flat format
    position = { latitude: data.lat, longitude: data.lon };
    speed = data.speed || 0;
    quality = data.quality || 0;
    alt = data.alt || 0;
    sats = data.sats || 0;
    course = data.course ?? data.heading ?? 0;
    console.log(`[Data] Device1 GPS received: lat=${data.lat}, lon=${data.lon}, course=${course} (raw)`);
  } else {
    console.warn('[Data] Invalid GPS data format - missing position or lat/lon');
    console.warn('[Data] Received:', JSON.stringify(data));
    return;
  }
  
  const sourceDeviceId = data.device_id;
  const deviceKey = normalizeDeviceKey(data.mac || sourceDeviceId);
  
  ensureBoatProfile(deviceKey);

  if (!gpsData[deviceKey]) {
    gpsData[deviceKey] = {
      device_id: deviceKey,
      source_device_id: sourceDeviceId,
      mac: data.mac || null,
      display_name: resolveDisplayName(deviceKey, sourceDeviceId),
      last_update: null,
      position: { latitude: 0, longitude: 0 },
      altitude_m: 0,
      speed_knots: 0,
      course: 0,
      quality: 0,
      signal_strength: 0,
      battery_mv: 0,
      battery: 0,
      halow_status: 0,
      roll: 0,
      pitch: 0,
      yaw_rate: 0,
      calibration_state: 0,
      calibration_progress: 0,
      last_command_id: 0,
      history: [],
      home_point: null,
      current_distance: 0,
      max_distance: 0
    };
  }
  
  // Set home point on first valid fix
  if (!gpsData[deviceKey].home_point) {
    gpsData[deviceKey].home_point = { ...position };
    console.log(`[${deviceKey}] Home point set: ${position.latitude.toFixed(6)}, ${position.longitude.toFixed(6)}`);
  }
  
  // Update current position
  gpsData[deviceKey].source_device_id = sourceDeviceId;
  gpsData[deviceKey].mac = data.mac || gpsData[deviceKey].mac || null;
  gpsData[deviceKey].display_name = resolveDisplayName(deviceKey, sourceDeviceId);
  gpsData[deviceKey].last_update = new Date();
  gpsData[deviceKey].position = position;
  gpsData[deviceKey].altitude_m = alt;
  gpsData[deviceKey].speed_knots = speed;
  gpsData[deviceKey].course = course;
  gpsData[deviceKey].quality = quality;
  gpsData[deviceKey].signal_strength = data.signal_strength || 0;
  gpsData[deviceKey].battery_mv = data.battery_mv || 0;
  gpsData[deviceKey].battery = data.battery || 0;
  gpsData[deviceKey].halow_status = data.halow_status || 0;
  gpsData[deviceKey].roll = data.roll ?? gpsData[deviceKey].roll ?? 0;
  gpsData[deviceKey].pitch = data.pitch ?? gpsData[deviceKey].pitch ?? 0;
  gpsData[deviceKey].yaw_rate = data.yaw_rate ?? gpsData[deviceKey].yaw_rate ?? 0;
  gpsData[deviceKey].calibration_state = data.calibration_state ?? gpsData[deviceKey].calibration_state ?? 0;
  gpsData[deviceKey].calibration_progress = data.calibration_progress ?? gpsData[deviceKey].calibration_progress ?? 0;
  gpsData[deviceKey].last_command_id = data.last_command_id ?? gpsData[deviceKey].last_command_id ?? 0;

  if (gpsData[deviceKey].last_command_id && calibrationCommands[gpsData[deviceKey].last_command_id]) {
    calibrationCommands[gpsData[deviceKey].last_command_id].last_update = new Date();
    calibrationCommands[gpsData[deviceKey].last_command_id].state = gpsData[deviceKey].calibration_state;
    calibrationCommands[gpsData[deviceKey].last_command_id].progress = gpsData[deviceKey].calibration_progress;
  }
  
  // Calculate distance from home point
  if (gpsData[deviceKey].home_point) {
    const dist = calculateDistance(
      gpsData[deviceKey].home_point.latitude,
      gpsData[deviceKey].home_point.longitude,
      position.latitude,
      position.longitude
    );
    gpsData[deviceKey].current_distance = parseFloat(dist.toFixed(3));
    
    // Update max distance if current is greater
    if (gpsData[deviceKey].current_distance > gpsData[deviceKey].max_distance) {
      gpsData[deviceKey].max_distance = gpsData[deviceKey].current_distance;
    }
  }
  
  // Add to history
  gpsData[deviceKey].history.push({
    timestamp: gpsData[deviceKey].last_update,
    position: { ...position },
    speed_knots: speed,
    course,
    distance_from_home: gpsData[deviceKey].current_distance,
    signal_strength: gpsData[deviceKey].signal_strength,
    battery_mv: gpsData[deviceKey].battery_mv,
    battery: gpsData[deviceKey].battery,
    halow_status: gpsData[deviceKey].halow_status,
    roll: gpsData[deviceKey].roll,
    pitch: gpsData[deviceKey].pitch,
    yaw_rate: gpsData[deviceKey].yaw_rate,
    calibration_state: gpsData[deviceKey].calibration_state,
    calibration_progress: gpsData[deviceKey].calibration_progress,
    last_command_id: gpsData[deviceKey].last_command_id
  });
  
  // Keep only last MAX_HISTORY entries
  if (gpsData[deviceKey].history.length > MAX_HISTORY) {
    gpsData[deviceKey].history.shift();
  }

  broadcastBoatUpdate(deviceKey);
  
  //console.log(`[${deviceId}] Lat: ${data.position.latitude.toFixed(6)}, ` +
   //           `Lon: ${data.position.longitude.toFixed(6)}, ` +
   //           `Speed: ${data.speed_knots.toFixed(2)}kt, ` +
   //           `Course: ${data.course.toFixed(1)}°`);
}

// ============ REST API Endpoints - Shared by both HTTP and HTTPS ============

// Helper function to register API routes on an app instance
function registerAPIRoutes(expressApp) {
  // Get current position of all devices
  expressApp.get('/api/positions', (req, res) => {
    const response = {};
    for (const [key, device] of Object.entries(gpsData)) {
      if (!isDeviceActive(device)) {
        continue;
      }
      response[key] = deviceToApiPayload(device);
    }
    res.json(response);
  });

  // Get position of specific device
  expressApp.get('/api/positions/:device_id', (req, res) => {
    const deviceId = normalizeDeviceKey(req.params.device_id);
    const device = gpsData[deviceId];
    
    if (!device) {
      return res.status(404).json({ error: 'Device not found' });
    }
    
    res.json(deviceToApiPayload(device));
  });

  // Get position history of device
  expressApp.get('/api/history/:device_id', (req, res) => {
    const deviceId = normalizeDeviceKey(req.params.device_id);
    const device = gpsData[deviceId];
    
    if (!device) {
      return res.status(404).json({ error: 'Device not found' });
    }
    
    res.json({
      device_id: deviceId,
      history: device.history
    });
  });

  // Get all devices
  expressApp.get('/api/devices', (req, res) => {
    const devices = Object.values(gpsData).filter(isDeviceActive).map(device => ({
      device_id: device.device_id,
      source_device_id: device.source_device_id,
      mac: device.mac,
      display_name: device.display_name || device.device_id,
      last_update: device.last_update,
      is_active: true
    }));
    
    res.json(devices);
  });

  expressApp.get('/api/gateway/status', (req, res) => {
    const isActive = gatewayStatus.last_update &&
      (Date.now() - new Date(gatewayStatus.last_update)) < 120000;

    res.json({
      ...gatewayStatus,
      is_active: Boolean(isActive)
    });
  });

  expressApp.post('/api/devices/:device_id/calibration', (req, res) => {
    const deviceId = normalizeDeviceKey(req.params.device_id);
    const { command, duration_ms } = req.body || {};
    const allowedCommands = new Set(['calibrate_gyro', 'set_level']);
    const device = gpsData[deviceId];

    if (!allowedCommands.has(command)) {
      return res.status(400).json({ error: 'Unsupported calibration command' });
    }

    if (!device) {
      return res.status(404).json({ error: 'Device not found' });
    }

    if (!gatewaySocket || gatewaySocket.destroyed) {
      return res.status(503).json({ error: 'Gateway is not connected' });
    }

    const commandId = nextCommandId++;
    const payload = {
      type: 'calibration_command',
      command_id: commandId,
      device_id: device.source_device_id || deviceId,
      command,
      duration_ms: Number.isFinite(duration_ms) ? duration_ms : 5000
    };

    calibrationCommands[commandId] = {
      ...payload,
      queued_at: new Date(),
      last_update: null,
      state: null,
      progress: 0
    };

    device.calibration_state = command === 'calibrate_gyro' ? 1 : 0;
    device.calibration_progress = 0;
    device.last_command_id = commandId;

    gatewaySocket.write(`${JSON.stringify(payload)}\n`, (error) => {
      if (error) {
        calibrationCommands[commandId].error = error.message;
      }
    });

    res.json({
      accepted: true,
      command_id: commandId,
      command,
      device_id: deviceId
    });
  });

  expressApp.get('/api/calibration/:command_id', (req, res) => {
    const command = calibrationCommands[req.params.command_id];
    if (!command) {
      return res.status(404).json({ error: 'Calibration command not found' });
    }
    res.json(command);
  });

  expressApp.patch('/api/devices/:device_id/name', (req, res) => {
    const deviceId = normalizeDeviceKey(req.params.device_id);
    const device = gpsData[deviceId];
    const name = String((req.body && req.body.name) || '').trim();

    if (!device) {
      return res.status(404).json({ error: 'Device not found' });
    }

    if (!name || name.length > 40) {
      return res.status(400).json({ error: 'Name must be 1-40 characters' });
    }

    deviceNames[deviceId] = name;
    saveDeviceNames();
    const profile = ensureBoatProfile(deviceId);
    profile.displayName = name;
    profile.configured = true;
    saveBoatProfiles();
    device.display_name = name;
    res.json({ device_id: deviceId, display_name: name });
  });

  expressApp.get('/api/boat-models', (req, res) => {
    res.json(boatModels);
  });

  expressApp.get('/api/boat-types', (req, res) => {
    res.json(boatTypes);
  });

  expressApp.get('/api/boats', (req, res) => {
    const boats = {};
    for (const [key, device] of Object.entries(gpsData)) {
      boats[key] = {
        profile: getBoatProfile(key),
        live: isDeviceActive(device) ? deviceToApiPayload(device) : null
      };
    }
    for (const mac of Object.keys(boatProfiles)) {
      if (!boats[mac]) {
        boats[mac] = { profile: boatProfiles[mac], live: null };
      }
    }
    res.json(boats);
  });

  expressApp.get('/api/boats/:mac', (req, res) => {
    const deviceId = normalizeDeviceKey(req.params.mac);
    const device = gpsData[deviceId];
    res.json({
      profile: getBoatProfile(deviceId),
      live: device ? deviceToApiPayload(device) : null
    });
  });

  expressApp.patch('/api/boats/:mac', (req, res) => {
    const deviceId = normalizeDeviceKey(req.params.mac);
    const body = req.body || {};
    const profile = ensureBoatProfile(deviceId);

    if (body.displayName !== undefined) {
      const name = String(body.displayName).trim();
      if (!name || name.length > 40) {
        return res.status(400).json({ error: 'displayName must be 1-40 characters' });
      }
      profile.displayName = name;
      deviceNames[deviceId] = name;
      saveDeviceNames();
      if (gpsData[deviceId]) {
        gpsData[deviceId].display_name = name;
      }
    }
    if (body.boatType !== undefined) {
      profile.boatType = String(body.boatType);
      const typePreset = boatTypes[profile.boatType];
      if (typePreset && body.modelId === undefined) {
        profile.modelId = typePreset.modelId;
      }
      if (typePreset && body.color === undefined) {
        profile.color = typePreset.color;
      }
      if (typePreset && typePreset.hullType) {
        profile.hullType = typePreset.hullType;
      }
    }
    if (body.modelId !== undefined) {
      if (!boatModels[body.modelId]) {
        return res.status(400).json({ error: 'Unknown modelId' });
      }
      profile.modelId = body.modelId;
      profile.hullType = hullTypeForModel(profile.modelId);
    }
    if (body.hullType !== undefined) {
      const ht = String(body.hullType).toLowerCase();
      if (ht === 'monohull' || ht === 'multihull') {
        profile.hullType = ht;
      }
    }
    if (body.color !== undefined) {
      profile.color = String(body.color);
    }
    if (body.scale !== undefined) {
      profile.scale = Number(body.scale) || 1;
    }
    profile.configured = true;
    saveBoatProfiles();

    if (gpsData[deviceId]) {
      broadcastBoatUpdate(deviceId);
    }

    res.json({ device_id: deviceId, profile });
  });

  expressApp.get('/api/live/config', (req, res) => {
    res.set('Cache-Control', 'no-store, no-cache, must-revalidate');
    res.json({
      viewerVersion: '1.0.0-baseline',
      origin: liveConfig.origin,
      coastalBackdrop: liveConfig.coastalBackdrop,
      osmGround: liveConfig.osmGround,
      water: liveConfig.water,
      boatFloatLiftM: liveConfig.boatFloatLiftM,
      seaExtentM: liveConfig.seaExtentM,
      buoys: liveConfig.buoys,
      boatModels,
      boatTypes,
      profiles: boatProfiles
    });
  });

  expressApp.get('/api/map/tiles/:z/:x/:y.png', async (req, res) => {
    const z = parseInt(req.params.z, 10);
    const x = parseInt(req.params.x, 10);
    const y = parseInt(req.params.y, 10);
    if (!Number.isFinite(z) || z < 10 || z > 19) {
      return res.status(400).json({ error: 'zoom must be 10–19' });
    }
    if (!Number.isFinite(x) || !Number.isFinite(y) || x < 0 || y < 0) {
      return res.status(400).json({ error: 'invalid tile coordinates' });
    }
    const maxIndex = Math.pow(2, z);
    if (x >= maxIndex || y >= maxIndex) {
      return res.status(400).json({ error: 'tile out of range' });
    }
    try {
      const buf = await fetchOsmTile(z, x, y);
      res.set('Content-Type', 'image/png');
      res.set('Cache-Control', 'public, max-age=604800');
      res.send(buf);
    } catch (err) {
      console.warn('[map/tiles]', z, x, y, err.message);
      res.status(502).json({ error: 'tile fetch failed' });
    }
  });

  // Get statistics
  expressApp.get('/api/stats', (req, res) => {
    const stats = {
      timestamp: new Date(),
      devices: Object.keys(gpsData).length,
      devices_active: Object.values(gpsData).filter(d => 
        d.last_update && (Date.now() - new Date(d.last_update)) < 10000
      ).length,
      total_data_points: Object.values(gpsData).reduce((sum, d) => sum + d.history.length, 0)
    };
    
    res.json(stats);
  });

  // Reset home point and max distance for a device
  expressApp.post('/api/reset/:device_id', (req, res) => {
    const deviceId = normalizeDeviceKey(req.params.device_id);
    const device = gpsData[deviceId];
    
    if (!device) {
      return res.status(404).json({ error: 'Device not found' });
    }
    
    device.home_point = null;
    device.current_distance = 0;
    device.max_distance = 0;
    
    console.log(`[${deviceId}] Home point and distance tracking reset`);
    
    res.json({
      message: `Reset home point for ${deviceId}`,
      device_id: deviceId
    });
  });

  // Health check
  expressApp.get('/api/health', (req, res) => {
    res.json({
      status: 'ok',
      timestamp: new Date(),
      uptime: process.uptime()
    });
  });
}

// ============ OLD: REST API Endpoints (for backward compatibility - kept for reference) ============

// ============ Start HTTP API Server (port 3000) ============

// Register API routes on HTTP server
registerAPIRoutes(app);

const httpServer = http.createServer(app);
httpServer.listen(API_PORT, '0.0.0.0', () => {
  console.log(`HTTP API server listening on port ${API_PORT}`);
});

// ============ Live WebSocket (same HTTP server in Docker/ECS, or separate port locally) ============

const attachLiveWsToHttp = process.env.LIVE_WS_ATTACH_HTTP !== '0';
const wss = attachLiveWsToHttp
  ? new WebSocketServer({ server: httpServer })
  : new WebSocketServer({ port: LIVE_WS_PORT });
wss.on('connection', (ws) => {
  wsClients.add(ws);
  for (const [key, device] of Object.entries(gpsData)) {
    if (isDeviceActive(device)) {
      const payload = deviceToApiPayload(device);
      ws.send(JSON.stringify({
        type: 'boat_update',
        ...payload,
        heading_deg: payload.course,
        lat: payload.position.latitude,
        lon: payload.position.longitude,
        ts: device.last_update.toISOString()
      }));
    }
  }
  ws.on('close', () => wsClients.delete(ws));
  ws.on('error', () => wsClients.delete(ws));
});

console.log(`Live WebSocket listening on port ${LIVE_WS_PORT}`);

// ============ Start HTTPS Website + API Server (port 8443) ============

registerAPIRoutes(websiteApp);

try {
  const options = {
    key: fs.readFileSync('private.key'),
    cert: fs.readFileSync('certificate.crt')
  };
  https.createServer(options, websiteApp).listen(HTTPS_PORT, '0.0.0.0', () => {
    console.log(`HTTPS website + API server listening on port ${HTTPS_PORT}`);
  });
} catch (error) {
  console.warn(`HTTPS not started (${error.message}). HTTP and WS still available.`);
}

// ============ Graceful Shutdown ============

process.on('SIGINT', () => {
  console.log('\nShutting down...');
  tcpServer.close();
  process.exit(0);
});

console.log('RUSC GPS Backend Server started');
console.log(`HTTP API: http://0.0.0.0:${API_PORT}`);
console.log(`HTTPS Website: https://0.0.0.0:${HTTPS_PORT}`);
console.log(`TCP Data: 0.0.0.0:${TCP_PORT}`);
if (attachLiveWsToHttp) {
  console.log(`Live WS: attached to HTTP server on port ${API_PORT}`);
} else {
  console.log(`Live WS: ws://0.0.0.0:${LIVE_WS_PORT}`);
}
console.log('\nAvailable Endpoints:');
console.log(`  GET  /api/positions - All devices with home point & distances`);
console.log(`  GET  /api/positions/:device_id - Specific device`);
console.log(`  GET  /api/boats /api/boats/:mac - Boat profiles + live data`);
console.log(`  PATCH /api/boats/:mac - Associate device (name, type, model, color)`);
console.log(`  GET  /api/boat-models /api/boat-types /api/live/config`);
console.log(`  GET  /api/history/:device_id - Position history with distances`);
console.log(`  POST /api/reset/:device_id - Reset home point and max distance`);
