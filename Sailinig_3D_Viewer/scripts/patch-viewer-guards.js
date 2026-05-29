/**
 * Guard showMenu when React menu ref is not mounted yet (avoids crash after loadMenu).
 *
 * Usage: node scripts/patch-viewer-guards.js [path-to-viewer-dir]
 */
const fs = require('fs');
const path = require('path');

const viewerDir = path.resolve(process.argv[2] || path.join(__dirname, '..', 'viewer'));
const bundlePath = path.join(viewerDir, 'bundle.js');

let bundle = fs.readFileSync(bundlePath, 'utf8');

const showMenuOld =
  'e.prototype.showMenu=function(){this.loading=!1,void 0!==this.menuScreen&&null!==this.menuScreen?this.menuScreen.setLoading(this.loading):Mt(this,this.playController,this.root)}';
const showMenuNew =
  'e.prototype.showMenu=function(){var e=this;e.loading=!1;var t=e.menuScreen;if(void 0!==t&&null!==t&&"function"==typeof t.setLoading)try{t.setLoading(e.loading)}catch(n){Mt(e,e.playController,e.root)}else Mt(e,e.playController,e.root)}';

if (bundle.includes(showMenuNew)) {
  console.log('patch-viewer-guards: already applied —', bundlePath);
} else if (bundle.includes(showMenuOld)) {
  bundle = bundle.replace(showMenuOld, showMenuNew);
  fs.writeFileSync(bundlePath, bundle);
  console.log('patch-viewer-guards: OK —', bundlePath);
} else {
  throw new Error('bundle.js: showMenu pattern not found');
}
