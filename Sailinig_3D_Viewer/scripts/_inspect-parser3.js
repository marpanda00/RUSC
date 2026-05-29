const fs = require('fs');
const b = fs.readFileSync(require('path').join(__dirname, '../viewer/bundle.js'), 'utf8');
const start = b.indexOf('n=function(e){var t=new o.d;e.skip(1)');
console.log(b.substring(start, start + 1200));
