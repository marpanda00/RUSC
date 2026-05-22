/**
 * Copy boat 3D assets into backend/public/models for the live viewer (/viewer/).
 *
 * Sources (first found wins):
 *   1. backend/public/regatta-viewer/models
 *   2. ../Sailinig_3D_Viewer/viewer/models
 *
 * Usage:
 *   node scripts/sync-live-models.js
 *   node scripts/sync-live-models.js --source C:\path\to\models
 *
 * License: AC / Virtual Eye models are proprietary — personal / dev use only
 * (see Sailinig_3D_Viewer/README.md). Do not redistribute commercially.
 */

const fs = require('fs');
const path = require('path');

const backendRoot = path.join(__dirname, '..');
const defaultSources = [
  path.join(backendRoot, 'public', 'regatta-viewer', 'models'),
  path.join(backendRoot, '..', 'Sailinig_3D_Viewer', 'viewer', 'models')
];

const args = process.argv.slice(2);
function arg(name) {
  const i = args.indexOf(name);
  return i >= 0 && args[i + 1] ? args[i + 1] : null;
}

const sourceArg = arg('--source');
const dest = path.join(backendRoot, 'public', 'models');

/** @type {{ type: 'file' | 'dir', from: string }[]} */
const MANIFEST = [
  { type: 'file', from: '470.glb' },
  { type: 'file', from: '470.gltf' },
  { type: 'file', from: '470.bin' },
  { type: 'file', from: 'AC75_gen.gltf' },
  { type: 'file', from: 'AC75_gen.bin' },
  { type: 'file', from: 'AC75_N.png' },
  { type: 'file', from: 'AC75_RM.png' },
  { type: 'dir', from: 'ETNZ' },
  { type: 'dir', from: 'American_Magic' },
  { type: 'dir', from: 'Ineos_Team_UK' },
  { type: 'dir', from: 'Luna_Rossa' }
];

function resolveSource() {
  if (sourceArg) {
    const p = path.resolve(sourceArg);
    if (!fs.existsSync(p)) {
      console.error('Source not found:', p);
      process.exit(1);
    }
    return p;
  }
  for (const s of defaultSources) {
    if (fs.existsSync(s)) return s;
  }
  console.error('No model source found. Run download-regatta-assets or sync-regatta-viewer first.');
  process.exit(1);
}

function copyFile(src, dst) {
  fs.mkdirSync(path.dirname(dst), { recursive: true });
  fs.copyFileSync(src, dst);
}

function copyDir(src, dst) {
  fs.mkdirSync(dst, { recursive: true });
  for (const name of fs.readdirSync(src)) {
    const from = path.join(src, name);
    const to = path.join(dst, name);
    if (fs.statSync(from).isDirectory()) copyDir(from, to);
    else copyFile(from, to);
  }
}

function checkGltfDeps(gltfPath) {
  const dir = path.dirname(gltfPath);
  const text = fs.readFileSync(gltfPath, 'utf8');
  const missing = [];
  const uris = [];
  const re = /"uri"\s*:\s*"([^"]+)"/g;
  let m;
  while ((m = re.exec(text)) !== null) {
    const uri = m[1];
    if (uri.startsWith('data:')) continue;
    if (uri.endsWith('.bin') || /\.(png|jpe?g)$/i.test(uri)) uris.push(uri);
  }
  for (const uri of uris) {
    if (!fs.existsSync(path.join(dir, uri))) missing.push(uri);
  }
  return missing;
}

function main() {
  const source = resolveSource();
  console.log('Source:', source);
  console.log('Dest:  ', dest);
  console.log('(AC/Virtual Eye assets — personal dev use only)\n');

  fs.mkdirSync(dest, { recursive: true });
  let copied = 0;
  let skipped = 0;
  const warnings = [];

  for (const item of MANIFEST) {
    const srcPath = path.join(source, item.from);
    const dstPath = path.join(dest, item.from);
    if (!fs.existsSync(srcPath)) {
      skipped++;
      continue;
    }
    if (item.type === 'file') {
      copyFile(srcPath, dstPath);
      copied++;
      if (item.from.endsWith('.gltf')) {
        const missing = checkGltfDeps(dstPath);
        if (missing.length) warnings.push(`${item.from} missing: ${missing.join(', ')}`);
      }
    } else {
      copyDir(srcPath, dstPath);
      copied++;
      for (const name of fs.readdirSync(dstPath)) {
        if (!name.endsWith('.gltf')) continue;
        const missing = checkGltfDeps(path.join(dstPath, name));
        if (missing.length) warnings.push(`${item.from}/${name} missing: ${missing.join(', ')}`);
      }
    }
    console.log('  OK', item.from);
  }

  console.log(`\nDone: ${copied} copied, ${skipped} not in source (skipped).`);
  if (warnings.length) {
    console.log('\nWarnings — incomplete GLTF folders (run npm run download-regatta-assets):');
    warnings.forEach((w) => console.log('  -', w));
  } else {
    console.log('\nAll copied GLTF sidecars (.bin, textures) present.');
  }
  console.log('\nRestart backend and hard-refresh /viewer/');
}

main();
