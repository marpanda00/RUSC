const fs = require('fs');
const b = fs.readFileSync('../public/regatta-viewer/bundle.js', 'utf8');
const idx = b.indexOf('a.p=');
console.log(b.slice(idx, idx + 40));
