/**
 * After race metadata loads: show Live/Recorded menu when eventIds exist,
 * otherwise skip straight to live 3D (RUSC live viewer).
 *
 * Usage: node scripts/patch-auto-live.js [path-to-viewer-dir]
 */
const fs = require('fs');
const path = require('path');

const viewerDir = path.resolve(process.argv[2] || path.join(__dirname, '..', 'viewer'));
const bundlePath = path.join(viewerDir, 'bundle.js');

let bundle = fs.readFileSync(bundlePath, 'utf8');

const menuTimeoutVanilla =
  't.uiEngine.updatePlaybackStatus(!0),t.uiEngine.showMenu()}),1e3)';
const menuTimeoutPatched =
  't.uiEngine.updatePlaybackStatus(!0),t.uiEngine.onPlayLiveClicked()}),1e3)';
const menuTimeoutSmartBroken =
  't.uiEngine.updatePlaybackStatus(!0),(0===n?t.uiEngine.onPlayLiveClicked:t.uiEngine.showMenu)()}),1e3)';
const menuTimeoutSmart =
  't.uiEngine.updatePlaybackStatus(!0),0===n?t.uiEngine.onPlayLiveClicked():t.uiEngine.showMenu()}),1e3)';

if (bundle.includes(menuTimeoutSmart)) {
  console.log('patch-auto-live: already applied —', bundlePath);
} else if (bundle.includes(menuTimeoutSmartBroken)) {
  bundle = bundle.replace(menuTimeoutSmartBroken, menuTimeoutSmart);
  fs.writeFileSync(bundlePath, bundle);
  console.log('patch-auto-live: fixed method binding —', bundlePath);
} else if (bundle.includes(menuTimeoutPatched)) {
  bundle = bundle.replace(menuTimeoutPatched, menuTimeoutSmart);
  fs.writeFileSync(bundlePath, bundle);
  console.log('patch-auto-live: OK —', bundlePath);
} else if (bundle.includes(menuTimeoutVanilla)) {
  bundle = bundle.replace(menuTimeoutVanilla, menuTimeoutSmart);
  fs.writeFileSync(bundlePath, bundle);
  console.log('patch-auto-live: OK —', bundlePath);
} else {
  throw new Error('bundle.js: menu setTimeout pattern not found');
}

const eventLoopOld = 'l<c.length;l++){a(c[l])}this.postInMenuMessage()';
const eventLoopNew = 'l<c.length;l++){a(c[l])}0===c.length&&o(),this.postInMenuMessage()';
if (!bundle.includes(eventLoopNew)) {
  if (bundle.includes(eventLoopOld)) {
    bundle = bundle.replace(eventLoopOld, eventLoopNew);
    fs.writeFileSync(bundlePath, bundle);
    console.log('patch-auto-live: eventIds empty shortcut applied');
  }
}
