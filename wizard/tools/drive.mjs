// Drives the wizard in headless Chrome over the DevTools protocol and saves
// screenshots, for checking changes without a phone or a person on the stairs.
//   node wizard/tools/drive.mjs <url> <width> <height> <steps.json>
// steps.json is a list run in order; each entry may have:
//   eval  JavaScript run in the page (its value is printed)
//   wait  milliseconds to wait afterwards
//   shot  PNG path to save; with h, captures that many pixels tall
// Use ?sim=1 in the url for the simulated walker. Real waits are needed:
// Chrome's --virtual-time-budget hangs on the page's open event stream.
// Needs Node 22+ (built-in WebSocket) and Google Chrome.
import {spawn} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const CHROME = process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const [url, w, h, stepsFile] = process.argv.slice(2);
if (!stepsFile) { console.error('usage: drive.mjs <url> <width> <height> <steps.json>'); process.exit(2); }
const steps = JSON.parse(fs.readFileSync(stepsFile, 'utf8'));
const port = 9300 + Math.floor(Math.random() * 600);
const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'wizard-drive-'));
const chrome = spawn(CHROME, ['--headless=new', `--remote-debugging-port=${port}`, `--user-data-dir=${profile}`, 'about:blank'], {stdio: 'ignore'});
const sleep = ms => new Promise(r => setTimeout(r, ms));

let tabs;
for (let i = 0; i < 50 && !tabs; i++) { try { tabs = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); } catch { await sleep(200); } }
const ws = new WebSocket(tabs.find(t => t.type === 'page').webSocketDebuggerUrl);
await new Promise(r => { ws.onopen = r; });

let id = 0;
const pending = {}, errors = [];
ws.onmessage = e => {
  const m = JSON.parse(e.data);
  if (m.method === 'Runtime.exceptionThrown') errors.push(m.params.exceptionDetails.exception?.description || m.params.exceptionDetails.text);
  if (pending[m.id]) { pending[m.id](m); delete pending[m.id]; }
};
const send = (method, params = {}) => new Promise(r => { pending[++id] = r; ws.send(JSON.stringify({id, method, params})); });

await send('Runtime.enable');
await send('Emulation.setDeviceMetricsOverride', {width: +w, height: +h, deviceScaleFactor: 2, mobile: +w < 600});
await send('Page.enable');
await send('Page.navigate', {url});
await sleep(2500);

for (const s of steps) {
  if (s.eval) {
    const r = await send('Runtime.evaluate', {expression: s.eval, returnByValue: true, awaitPromise: true});
    if (r.result?.exceptionDetails) console.log('eval error:', r.result.exceptionDetails.exception?.description);
    else if (r.result?.result?.value !== undefined) console.log('eval:', JSON.stringify(r.result.result.value));
  }
  if (s.wait) await sleep(s.wait);
  if (s.shot) {
    const r = await send('Page.captureScreenshot', {format: 'png', clip: {x: 0, y: 0, width: +w, height: +(s.h || h), scale: 1}, captureBeyondViewport: !!s.h});
    fs.writeFileSync(s.shot, Buffer.from(r.result.data, 'base64'));
    console.log('shot', s.shot);
  }
}
console.log(errors.length ? errors.map(e => 'page error: ' + e).join('\n') : 'no page errors');
chrome.kill();
await sleep(300);
fs.rmSync(profile, {recursive: true, force: true, maxRetries: 3});
process.exit(0);
