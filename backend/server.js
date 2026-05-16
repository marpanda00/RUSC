/**
 * RUSC GPS Gateway Backend
 * Receives GPS data from Device 2 and provides API for web frontend
 */

const express = require('express');
const cors = require('cors');
const net = require('net');
const https = require('https');
const fs = require('fs');
require('dotenv').config();

const app = express();
const API_PORT = process.env.API_PORT || 3000;
const HTTPS_PORT = process.env.HTTPS_PORT || 8443;
const TCP_PORT = process.env.TCP_PORT || 3001;

// Create separate app for HTTPS website
const websiteApp = express();

// API Middleware
app.use(cors());
app.use(express.json());

// Website App Middleware (HTTPS)
websiteApp.use(cors());
websiteApp.use(express.json());
websiteApp.use(express.static('public')); // Serve static files on HTTPS

// In-memory data storage
let gpsData = {
  device1: {
    device_id: "gps_device_1",
    last_update: null,
    position: { latitude: 0, longitude: 0 },
    speed_knots: 0,
    course: 0,
    quality: 0,
    signal_strength: 0,  // WiFi signal in dBm
    history: [], // Keep last 100 points
    home_point: null, // Set on first GPS fix
    current_distance: 0, // Distance from home in km
    max_distance: 0 // Max distance reached from home
  }
};

const MAX_HISTORY = 100;

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
          processGPSData(data);
        }
      }
    } catch (error) {
      console.error('[TCP] Parse error:', error.message);
    }
  });
  
  socket.on('end', () => {
    console.log('[TCP] Client disconnected');
  });
  
  socket.on('error', (error) => {
    console.error('[TCP] Socket error:', error.message);
  });
});

tcpServer.listen(TCP_PORT, '0.0.0.0', () => {
  console.log(`TCP server listening on port ${TCP_PORT}`);
});

// ============ Process GPS Data ============

function processGPSData(data) {
  // Support both flat format (device1) and nested format
  // Device1 format: {device_id, lat, lon, alt, speed, sats, quality}
  // Expected format: {device_id, position: {latitude, longitude}, ...}
  
  if (!data.device_id) {
    console.warn('[Data] Missing device_id');
    return;
  }
  
  // Convert device1 flat format to expected format
  let position, speed, quality, alt, sats;
  
  if (data.position) {
    // Already in expected format
    position = data.position;
    speed = data.speed_knots || 0;
    quality = data.quality || 0;
    alt = data.altitude || 0;
    sats = data.satellites || 0;
  } else if (data.lat !== undefined && data.lon !== undefined) {
    // Device1 flat format
    position = { latitude: data.lat, longitude: data.lon };
    speed = data.speed || 0;
    quality = data.quality || 0;
    alt = data.alt || 0;
    sats = data.sats || 0;
    console.log(`[Data] Device1 GPS received: lat=${data.lat}, lon=${data.lon} (raw)`);
  } else {
    console.warn('[Data] Invalid GPS data format - missing position or lat/lon');
    console.warn('[Data] Received:', JSON.stringify(data));
    return;
  }
  
  const deviceId = data.device_id;
  
  if (!gpsData[deviceId]) {
    gpsData[deviceId] = {
      device_id: deviceId,
      last_update: null,
      position: { latitude: 0, longitude: 0 },
      speed_knots: 0,
      course: 0,
      quality: 0,
      signal_strength: 0,
      history: [],
      home_point: null,
      current_distance: 0,
      max_distance: 0
    };
  }
  
  // Set home point on first valid fix
  if (!gpsData[deviceId].home_point) {
    gpsData[deviceId].home_point = { ...position };
    console.log(`[${deviceId}] Home point set: ${position.latitude.toFixed(6)}, ${position.longitude.toFixed(6)}`);
  }
  
  // Update current position
  gpsData[deviceId].last_update = new Date();
  gpsData[deviceId].position = position;
  gpsData[deviceId].speed_knots = speed;
  gpsData[deviceId].quality = quality;
  gpsData[deviceId].signal_strength = data.signal_strength || 0;
  
  // Calculate distance from home point
  if (gpsData[deviceId].home_point) {
    const dist = calculateDistance(
      gpsData[deviceId].home_point.latitude,
      gpsData[deviceId].home_point.longitude,
      position.latitude,
      position.longitude
    );
    gpsData[deviceId].current_distance = parseFloat(dist.toFixed(3));
    
    // Update max distance if current is greater
    if (gpsData[deviceId].current_distance > gpsData[deviceId].max_distance) {
      gpsData[deviceId].max_distance = gpsData[deviceId].current_distance;
    }
  }
  
  // Add to history
  gpsData[deviceId].history.push({
    timestamp: gpsData[deviceId].last_update,
    position: { ...position },
    speed_knots: speed,
    course: data.course || 0,
    distance_from_home: gpsData[deviceId].current_distance,
    signal_strength: gpsData[deviceId].signal_strength
  });
  
  // Keep only last MAX_HISTORY entries
  if (gpsData[deviceId].history.length > MAX_HISTORY) {
    gpsData[deviceId].history.shift();
  }
  
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
      response[key] = {
        device_id: device.device_id,
        last_update: device.last_update,
        position: device.position,
        speed_knots: device.speed_knots,
        course: 0,
        quality: device.quality,
        signal_strength: device.signal_strength,
        home_point: device.home_point,
        current_distance: device.current_distance,
        max_distance: device.max_distance
      };
    }
    res.json(response);
  });

  // Get position of specific device
  expressApp.get('/api/positions/:device_id', (req, res) => {
    const deviceId = req.params.device_id;
    const device = gpsData[deviceId];
    
    if (!device) {
      return res.status(404).json({ error: 'Device not found' });
    }
    
    res.json({
      device_id: device.device_id,
      last_update: device.last_update,
      position: device.position,
      speed_knots: device.speed_knots,
      course: device.course,
      quality: device.quality,
      signal_strength: device.signal_strength,
      home_point: device.home_point,
      current_distance: device.current_distance,
      max_distance: device.max_distance
    });
  });

  // Get position history of device
  expressApp.get('/api/history/:device_id', (req, res) => {
    const deviceId = req.params.device_id;
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
    const devices = Object.values(gpsData).map(device => ({
      device_id: device.device_id,
      last_update: device.last_update,
      is_active: device.last_update && (Date.now() - new Date(device.last_update)) < 10000
    }));
    
    res.json(devices);
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
    const deviceId = req.params.device_id;
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

app.listen(API_PORT, '0.0.0.0', () => {
  console.log(`HTTP API server listening on port ${API_PORT}`);
});

// ============ Start HTTPS Website + API Server (port 8443) ============

// Register API routes on HTTPS server (for secure frontend access)
registerAPIRoutes(websiteApp);

const options = {
  key: fs.readFileSync('private.key'),
  cert: fs.readFileSync('certificate.crt')
};

https.createServer(options, websiteApp).listen(HTTPS_PORT, '0.0.0.0', () => {
  console.log(`HTTPS website + API server listening on port ${HTTPS_PORT}`);
});

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
console.log('\nAvailable Endpoints:');
console.log(`  GET  /api/positions - All devices with home point & distances`);
console.log(`  GET  /api/positions/:device_id - Specific device`);
console.log(`  GET  /api/history/:device_id - Position history with distances`);
console.log(`  POST /api/reset/:device_id - Reset home point and max distance`);
