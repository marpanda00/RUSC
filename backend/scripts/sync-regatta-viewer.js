/**
 * Copy Virtual Eye viewer from Sailinig_3D_Viewer into backend/public/regatta-viewer
 * and ensure hull-wake patches are applied.
 *
 * Usage: node scripts/sync-regatta-viewer.js [path-to-Sailinig_3D_Viewer]
 */
const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const backendRoot = path.join(__dirname, '..');
const defaultSource = path.join(backendRoot, '..', 'Sailinig_3D_Viewer', 'viewer');
const source = path.resolve(process.argv[2] || defaultSource);
const dest = path.join(backendRoot, 'public', 'regatta-viewer');

if (!fs.existsSync(source)) {
  console.error('Source viewer not found:', source);
  process.exit(1);
}

function copyRecursive(src, dst) {
  fs.mkdirSync(dst, { recursive: true });
  for (const name of fs.readdirSync(src)) {
    const from = path.join(src, name);
    const to = path.join(dst, name);
    if (fs.statSync(from).isDirectory()) {
      copyRecursive(from, to);
    } else {
      fs.copyFileSync(from, to);
    }
  }
}

if (fs.existsSync(dest)) {
  fs.rmSync(dest, { recursive: true, force: true });
}
copyRecursive(source, dest);

const localConfigDest = path.join(dest, 'localconfig.json');
const localConfigSrc = path.join(path.dirname(source), 'localconfig.json');
const localConfigTemplate = {
  _comment: 'Offline race menu — same shape as ac36 raceconfig.json',
  websocketUrl: 'ws://localhost:8080',
  eventIds: [],
  liveEventId: 'live',
  audioChannels: [],
  videoChannels: []
};
if (fs.existsSync(localConfigSrc)) {
  try {
    const parsed = JSON.parse(fs.readFileSync(localConfigSrc, 'utf8'));
    if (Array.isArray(parsed.eventIds) && !parsed.events) {
      fs.copyFileSync(localConfigSrc, localConfigDest);
    } else {
      fs.writeFileSync(localConfigDest, JSON.stringify(localConfigTemplate, null, 4) + '\n');
    }
  } catch {
    fs.writeFileSync(localConfigDest, JSON.stringify(localConfigTemplate, null, 4) + '\n');
  }
} else {
  fs.writeFileSync(localConfigDest, JSON.stringify(localConfigTemplate, null, 4) + '\n');
}

const appconfigPath = path.join(dest, 'appconfig.json');
const appconfig = JSON.parse(fs.readFileSync(appconfigPath, 'utf8'));
appconfig.raceConfigUrl = 'localconfig.json';
fs.writeFileSync(appconfigPath, JSON.stringify(appconfig, null, 4) + '\n');

execSync(`node "${path.join(__dirname, 'patch-hull-wakes.js')}" "${dest}"`, {
  stdio: 'inherit',
  cwd: backendRoot
});

const scriptsDir = path.join(path.dirname(source), 'scripts');
const patchAutoLive = path.join(scriptsDir, 'patch-auto-live.js');
if (fs.existsSync(patchAutoLive)) {
  execSync(`node "${patchAutoLive}" "${dest}"`, { stdio: 'inherit', cwd: backendRoot });
}
const patchViewerGuards = path.join(scriptsDir, 'patch-viewer-guards.js');
if (fs.existsSync(patchViewerGuards)) {
  execSync(`node "${patchViewerGuards}" "${dest}"`, { stdio: 'inherit', cwd: backendRoot });
}

const glb470 = path.join(dest, 'models', '470.glb');
const modelsDir = path.join(backendRoot, 'public', 'models');
if (fs.existsSync(glb470)) {
  fs.mkdirSync(modelsDir, { recursive: true });
  fs.copyFileSync(glb470, path.join(modelsDir, '470.glb'));
}

console.log('Synced regatta-viewer from', source);
