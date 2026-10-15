// Serves the wizard folder on the LAN so a phone can open it.
//   node wizard/tools/serve.mjs [port]
// Then open http://<this machine>:<port>/ on the phone and enter the sensor's
// address in the bar that opens (or add ?host=<address>).
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const port = +(process.argv[2] || 8080);
const types = {'.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.mjs': 'text/javascript', '.json': 'application/json',
  '.css': 'text/css', '.woff2': 'font/woff2', '.txt': 'text/plain; charset=utf-8'};

http.createServer((req, res) => {
  const p = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  const f = path.join(root, p === '/' ? 'wizard.html' : p);
  if (!f.startsWith(root + path.sep)) { res.writeHead(403).end(); return; }
  fs.readFile(f, (err, data) => {
    if (err) { res.writeHead(404).end('not found'); return; }
    res.writeHead(200, {'Content-Type': types[path.extname(f)] || 'application/octet-stream', 'Cache-Control': 'no-store'}).end(data);
    console.log(new Date().toLocaleTimeString(), req.socket.remoteAddress, p);
  });
}).listen(port, '0.0.0.0', () => console.log(`serving ${root} on port ${port}`));
