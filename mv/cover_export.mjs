// 导出《谎话》封面：node cover_export.mjs [a|b|c] [边长=3000] [输出文件]
// 用无头 Chrome 打开 cover.html，等字体加载完，把画布存成 PNG
import { spawn } from 'node:child_process';
import { mkdtempSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
const here = dirname(fileURLToPath(import.meta.url));
const [v = 'a', s = '3000', out = `output/谎话_封面_${v}_${s}.png`] = process.argv.slice(2);
const CHROME = process.env.CHROME || ['/opt/pw-browsers/chromium-1194/chrome-linux/chrome', '/usr/bin/chromium', '/usr/bin/google-chrome'].find(p => { try { readFileSync(p); return true; } catch { return false; } });
const sleep = ms => new Promise(r => setTimeout(r, ms));
const profile = mkdtempSync(join(tmpdir(), 'cover-'));
const proc = spawn(CHROME, ['--headless=new', '--remote-debugging-port=0', '--remote-allow-origins=*', `--user-data-dir=${profile}`, '--allow-file-access-from-files',
  '--enable-unsafe-swiftshader', '--enable-webgl', '--ignore-gpu-blocklist', ...(process.getuid?.() === 0 ? ['--no-sandbox'] : []), 'about:blank'], { stdio: 'ignore' });
let port = 0, target;
for (let i = 0; i < 120 && !port; i++) { try { port = +readFileSync(join(profile, 'DevToolsActivePort'), 'utf8').split('\n')[0]; } catch {} if (!port) await sleep(250); }
for (let i = 0; i < 80 && !target; i++) { try { target = (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json()).find(t => t.type === 'page'); } catch {} if (!target) await sleep(250); }
const ws = new WebSocket(target.webSocketDebuggerUrl); await new Promise(r => ws.onopen = r);
let seq = 0; const pending = new Map();
ws.onmessage = ev => { const m = JSON.parse(ev.data); if (m.method === 'Runtime.exceptionThrown') console.error('页面报错：', m.params.exceptionDetails.exception?.description); const p = pending.get(m.id); if (p) { pending.delete(m.id); p(m.result); } };
const send = (method, params = {}) => new Promise(r => { const n = ++seq; pending.set(n, r); ws.send(JSON.stringify({ id: n, method, params })); });
const ev = async e => { const r = await send('Runtime.evaluate', { expression: e, returnByValue: true, awaitPromise: true }); if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || r.exceptionDetails.text); return r.result.value; };
await send('Runtime.enable'); await send('Page.enable');
await send('Page.navigate', { url: pathToFileURL(resolve(here, 'cover.html')).href + `?v=${v}&s=${s}` });
for (let i = 0; i < 200; i++) { await sleep(250); try { if (await ev("typeof window.__ready === 'function' && window.__ready()")) break; } catch {} }
const url = await ev('window.__png()');
writeFileSync(resolve(here, out), Buffer.from(url.slice(url.indexOf(',') + 1), 'base64'));
console.log('写出', out);
proc.kill(); await new Promise(r => { proc.once('exit', r); setTimeout(r, 3000); });
try { rmSync(profile, { recursive: true, force: true }); } catch {}
process.exit(0);
