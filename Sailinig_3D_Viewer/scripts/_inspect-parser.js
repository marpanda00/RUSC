const fs = require('fs');
const b = fs.readFileSync(require('path').join(__dirname, '../viewer/bundle.js'), 'utf8');
for (const term of ['parseBoat', 'BoatPacket', 'PACKET_BOAT', 'case 179', '===8', '===7', 'version===', '0xB2', '0xB1', '0xB7', 'readBoat']) {
  const i = b.indexOf(term);
  if (i >= 0) console.log(term, i, b.substring(i, i + 120));
}
