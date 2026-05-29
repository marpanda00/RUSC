const fs = require('fs');
const b = fs.readFileSync(require('path').join(__dirname, '../viewer/bundle.js'), 'utf8');
const markers = ['processBoatPacket', 'addBoatPacket', 'function(e){var t=new', 'readUInt16BE'];
for (const m of ['processBoat', 'addBoatPacket', 'parsePacket=function']) {
  const i = b.indexOf(m);
  if (i >= 0) console.log('\n===', m, '===\n', b.substring(i, i + 800));
}
