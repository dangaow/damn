// Render mv.html frame by frame in headless Chrome and encode the PNG frames with Isolated.mp3.
//
//   node export.mjs                          -> Isolated_MV.mp4 (H.264 master, CRF 16, from lossless PNG frames)
//   node export.mjs --share                  (also Isolated_MV_share.mp4, CRF 24, encoded from the same PNG frames)
//   node export.mjs --lossless               -> Isolated_MV_lossless.mkv (RGB, bit-exact to the PNG frames; very large)
//   node export.mjs --frames frames          (also keep every PNG frame in frames/)
//   node export.mjs --shots 7.2,30.5,40.4    (write single PNG frames to shots/ for checking)
//   options: --workers 4  --fps 30  --from 0  --to <end>  --crf 16  --share-crf 24  --preset slow  --out <file>
//
// Every worker is its own headless Chrome; frame i is drawn at exactly t = from + i / fps through the page's
// ?export=1 hooks and returned as a PNG (canvas.toDataURL('image/png'), lossless). Frames are handed out one
// at a time and written to FFmpeg strictly in order, so the result does not depend on the worker count.
// Needs Node 22+, FFmpeg on PATH, and Chrome / Edge / Chromium (or set CHROME=path). No network needed:
// fonts, cover and audio analysis are embedded in assets.js.
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { EventEmitter, once } from 'node:events';
import { availableParallelism, tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const argv = process.argv.slice(2), args = {};
for (let i = 0; i < argv.length; i++) if (argv[i].startsWith('--')) args[argv[i].slice(2)] = argv[i + 1] && !argv[i + 1].startsWith('--') ? argv[++i] : true;
const LOSSLESS = !!args.lossless, FPS = +(args.fps || 30), CRF = +(args.crf ?? 16), PRESET = args.preset || (LOSSLESS ? 'medium' : 'slow');
const WORKERS = args.shots ? 1 : Math.max(1, +(args.workers || Math.min(6, availableParallelism())));
const PAGE = resolve(here, args.page || 'mv.html'), AUDIO = join(here, 'Isolated.mp3');
const OUT = resolve(here, args.out || (LOSSLESS ? 'Isolated_MV_lossless.mkv' : 'Isolated_MV.mp4'));
const FRAMES = args.frames ? resolve(here, args.frames) : null;
const SHARE = args.share && !LOSSLESS ? resolve(here, typeof args.share === 'string' ? args.share : 'Isolated_MV_share.mp4') : null;
const CHROME = [process.env.CHROME,
  'C:/Program Files/Google/Chrome/Application/chrome.exe', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
  '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', '/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser',
  '/opt/pw-browsers/chromium'].find(p => p && existsSync(p));
if (!CHROME) throw new Error('Chrome / Edge / Chromium not found (set CHROME=path)');

const sleep = ms => new Promise(r => setTimeout(r, ms));
const browsers = [];
let ff = null, ffError = null;
function cleanup() {
  for (const b of browsers) try { b.proc.kill(); } catch {}
  try { if (ff && ff.exitCode === null) ff.kill(); } catch {}
  for (const b of browsers) try { rmSync(b.profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 }); } catch {}
}
process.on('exit', cleanup);
process.on('SIGINT', () => process.exit(130));
process.on('SIGTERM', () => process.exit(143));
process.on('uncaughtException', e => { console.error(`export failed: ${e.message}`); process.exit(1); });
process.on('unhandledRejection', e => { console.error(`export failed: ${e && e.message}`); process.exit(1); });

// ---- one headless Chrome over the DevTools protocol
async function launch(id) {
  const profile = mkdtempSync(join(tmpdir(), 'isolated-mv-'));
  const flags = ['--headless=new', '--remote-debugging-port=0', '--remote-allow-origins=*', `--user-data-dir=${profile}`,
    '--window-size=1920,1080', '--hide-scrollbars', '--mute-audio', '--no-first-run', '--no-default-browser-check',
    '--ignore-gpu-blocklist', '--enable-gpu-rasterization', '--enable-unsafe-swiftshader', '--allow-file-access-from-files',
    '--disable-background-networking', '--disable-component-update', '--disable-sync', '--no-pings'];
  if (process.getuid && process.getuid() === 0) flags.push('--no-sandbox');      // Chrome refuses to sandbox as root
  const proc = spawn(CHROME, [...flags, 'about:blank'], { stdio: 'ignore' });
  browsers.push({ proc, profile });
  let port = 0, target = null;
  for (let i = 0; i < 120 && !port; i++) { try { port = +readFileSync(join(profile, 'DevToolsActivePort'), 'utf8').split('\n')[0]; } catch {} if (!port) await sleep(250); }
  for (let i = 0; i < 80 && port && !target; i++) {
    try { target = (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json()).find(t => t.type === 'page'); } catch {}
    if (!target) await sleep(250);
  }
  if (!target) throw new Error(`worker ${id}: Chrome did not start`);
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
  let seq = 0; const pending = new Map();
  ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.method === 'Runtime.exceptionThrown') console.error(`worker ${id} page error:`, m.params.exceptionDetails.exception?.description || m.params.exceptionDetails.text);
    if (m.method === 'Runtime.consoleAPICalled' && (m.params.type === 'error' || m.params.type === 'warning'))
      console.error(`worker ${id} console.${m.params.type}:`, m.params.args.map(a => a.value ?? a.description).join(' '));
    const p = pending.get(m.id); if (!p) return;
    pending.delete(m.id); m.error ? p.rej(new Error(m.error.message)) : p.res(m.result);
  };
  ws.onclose = () => { for (const p of pending.values()) p.rej(new Error(`worker ${id}: Chrome closed the connection`)); };
  const send = (method, params = {}) => new Promise((res, rej) => { const k = ++seq; pending.set(k, { res, rej }); ws.send(JSON.stringify({ id: k, method, params })); });
  const evaluate = async expression => {
    const r = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
    if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || r.exceptionDetails.text);
    return r.result.value;
  };
  await send('Page.enable'); await send('Runtime.enable');
  await send('Page.navigate', { url: pathToFileURL(PAGE).href + '?export=1' });
  let ready = false;
  for (let i = 0; i < 120 && !ready; i++) { await sleep(250); try { ready = await evaluate("typeof window.__frame === 'function' && window.__ready()"); } catch {} }
  if (!ready) throw new Error(`worker ${id}: the page did not get ready`);
  const frame = async t => {
    const url = await evaluate(`__frame(${t})`);
    return Buffer.from(url.slice(url.indexOf(',') + 1), 'base64');
  };
  return { id, evaluate, frame };
}

const tLoad = Date.now();
const workers = await Promise.all(Array.from({ length: WORKERS }, (_, i) => launch(i)));
const gpu = await workers[0].evaluate(`(() => { const g = document.createElement('canvas').getContext('webgl2'), d = g && g.getExtension('WEBGL_debug_renderer_info'); return d ? g.getParameter(d.UNMASKED_RENDERER_WEBGL) : 'unknown'; })()`);
console.log(`${WORKERS} worker${WORKERS > 1 ? 's' : ''} ready in ${((Date.now() - tLoad) / 1000).toFixed(1)}s   renderer: ${gpu}`);
const problems = await workers[0].evaluate('window.__check ? window.__check() : []');
for (const p of problems) console.warn(`check: ${p}`);

if (args.shots) {
  const dir = join(here, 'shots'); mkdirSync(dir, { recursive: true });
  const ts = String(args.shots).split(',').map(Number);
  for (const t of ts) writeFileSync(join(dir, `t${t.toFixed(2).padStart(6, '0')}.png`), await workers[0].frame(t));
  console.log(`wrote ${ts.length} PNG shot${ts.length > 1 ? 's' : ''} to ${dir}`);
  process.exit(0);
}

const DUR = await workers[0].evaluate('window.__duration');
const FROM = +(args.from || 0), TO = Math.min(+(args.to || DUR), DUR), N = Math.round((TO - FROM) * FPS);
if (FRAMES) mkdirSync(FRAMES, { recursive: true });
console.log(`rendering ${N} PNG frames (${FROM.toFixed(2)}s -> ${TO.toFixed(2)}s @ ${FPS} fps) -> ${OUT}${SHARE ? ` + ${SHARE}` : ''}${FRAMES ? `  + ${FRAMES}/` : ''}`);
const h264 = crf => ['-map', '0:v:0', '-map', '1:a:0', '-c:v', 'libx264', '-preset', PRESET, '-crf', String(crf), '-pix_fmt', 'yuv420p', '-profile:v', 'high',
  '-colorspace', 'bt709', '-color_primaries', 'bt709', '-color_trc', 'bt709', '-c:a', 'aac', '-b:a', '320k', '-movflags', '+faststart', '-r', String(FPS), '-shortest'];
const outputs = LOSSLESS
  ? ['-map', '0:v:0', '-map', '1:a:0', '-c:v', 'libx264rgb', '-qp', '0', '-preset', PRESET, '-pix_fmt', 'rgb24', '-c:a', 'copy', '-r', String(FPS), '-shortest', OUT]
  : [...h264(CRF), OUT, ...(SHARE ? [...h264(+(args['share-crf'] ?? 24)), SHARE] : [])];
ff = spawn('ffmpeg', ['-y', '-loglevel', 'error', '-f', 'image2pipe', '-framerate', String(FPS), '-c:v', 'png', '-i', '-',
  '-ss', String(FROM), '-t', String(TO - FROM), '-i', AUDIO, ...outputs], { stdio: ['pipe', 'inherit', 'inherit'] });
const ffDone = once(ff, 'exit');
ff.stdin.on('error', e => { ffError = e; });
ff.on('exit', c => { if (c !== 0) ffError = ffError || new Error(`ffmpeg exited with ${c}`); });

// ---- whichever worker is free takes the next frame; frames are written in order
const AHEAD = 8 * WORKERS;
const ev = new EventEmitter(); ev.setMaxListeners(0);
const done = new Map();
let nextTake = 0, nextWrite = 0;
async function work(w) {
  while (nextTake < N) {
    if (nextTake >= nextWrite + AHEAD) { await once(ev, 'written'); continue; }
    const i = nextTake++;
    done.set(i, await w.frame(FROM + i / FPS));
    ev.emit('rendered');
  }
}
const t0 = Date.now(); let lastLog = 0;
async function write() {
  while (nextWrite < N) {
    if (ffError) throw ffError;
    const png = done.get(nextWrite);
    if (!png) { await once(ev, 'rendered'); continue; }
    done.delete(nextWrite);
    if (FRAMES) writeFileSync(join(FRAMES, `frame_${String(nextWrite).padStart(5, '0')}.png`), png);
    if (!ff.stdin.write(png)) await once(ff.stdin, 'drain');
    nextWrite++; ev.emit('written');
    const now = Date.now();
    if (now - lastLog > 10000 || nextWrite === N) {
      lastLog = now; const rate = nextWrite / ((now - t0) / 1000);
      console.log(`frame ${nextWrite}/${N}  ${(100 * nextWrite / N).toFixed(1)}%  ${rate.toFixed(2)} fps  eta ${Math.round((N - nextWrite) / rate)}s`);
    }
  }
}
await Promise.all([write(), ...workers.map(work)]);
ff.stdin.end();
const [code] = await ffDone;
if (code !== 0) throw new Error(`ffmpeg exited with ${code}`);
for (const f of [OUT, SHARE].filter(Boolean)) console.log(`done: ${f}  (${(statSync(f).size / 1e6).toFixed(1)} MB)`);
console.log(`${((Date.now() - t0) / 60000).toFixed(1)} min`);
process.exit(0);
