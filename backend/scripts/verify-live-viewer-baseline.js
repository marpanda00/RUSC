/**
 * Sanity-check that the live 3D viewer baseline files exist and config parses.
 * Usage: node scripts/verify-live-viewer-baseline.js
 */

const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');

const requiredFiles = [
  'live-config.json',
  'boat-models.json',
  'boat-types.json',
  'public/viewer/index.html',
  'public/js/rusc-viewer-manifest.js',
  'public/js/rusc-scene.js',
  'public/js/rusc-osm-ground.js',
  'public/js/rusc-water.js',
  'public/js/rusc-device-ui.js',
  'public/js/rusc-wakes.js',
  'public/css/rusc-device-ui.css',
  'scripts/simulate-gps.js'
];

let failed = 0;

for (const rel of requiredFiles) {
  const p = path.join(root, rel);
  if (!fs.existsSync(p)) {
    console.error('MISSING', rel);
    failed++;
  }
}

function readJson(rel) {
  const p = path.join(root, rel);
  try {
    return JSON.parse(fs.readFileSync(p, 'utf8'));
  } catch (e) {
    console.error('INVALID JSON', rel, e.message);
    failed++;
    return null;
  }
}

const live = readJson('live-config.json');
if (live?.origin) {
  const lat = live.origin.latitude ?? live.origin.lat;
  const lon = live.origin.longitude ?? live.origin.lon;
  if (!Number.isFinite(lat) || !Number.isFinite(lon)) {
    console.error('live-config.json: origin needs latitude/longitude');
    failed++;
  } else {
    console.log('Origin OK:', lat, lon);
  }
  if (live.osmGround?.enabled && live.coastalBackdrop?.enabled) {
    console.warn('WARN: both osmGround and coastalBackdrop enabled — OSM takes precedence');
  }
}

const manifestPath = path.join(root, 'public/js/rusc-viewer-manifest.js');
const indexPath = path.join(root, 'public/viewer/index.html');
if (fs.existsSync(manifestPath)) {
  const text = fs.readFileSync(manifestPath, 'utf8');
  const m = text.match(/version:\s*'([^']+)'/);
  if (m) console.log('Viewer version:', m[1]);
  if (fs.existsSync(indexPath)) {
    const html = fs.readFileSync(indexPath, 'utf8');
    for (const key of ['wakes', 'deviceUi', 'osmGround', 'water', 'scene']) {
      const assetM = text.match(new RegExp(`${key}:\\s*(\\d+)`));
      if (assetM && !html.includes(`?v=${assetM[1]}`)) {
        console.warn(`WARN: index.html cache-bust may not match manifest assets.${key}=${assetM[1]}`);
      }
    }
  }
}

const models = readJson('boat-models.json');
if (models && !models['470']) {
  console.warn('WARN: boat-models.json has no "470" entry (procedural fallback still works)');
}

if (failed) {
  console.error(`\nverify-viewer: ${failed} problem(s)`);
  process.exit(1);
}

console.log('\nverify-viewer: baseline files OK');
