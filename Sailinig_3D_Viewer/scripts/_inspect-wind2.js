const fs = require('fs');
const b = fs.readFileSync(require('path').join(__dirname, '../viewer/bundle.js'), 'utf8');
const i = b.indexOf('case g.PACKET_ID_WIND');
console.log(b.substring(i, i + 400));
const j = b.indexOf('PACKET_ID_WIND:n=');
console.log('alt', j, b.substring(j, j + 500));
