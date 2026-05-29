const fs = require('fs');
const b = fs.readFileSync(require('path').join(__dirname, '../viewer/bundle.js'), 'utf8');
const i = b.indexOf('PACKET_ID_WIND');
console.log(b.substring(i - 100, i + 600));
