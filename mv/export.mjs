// 把 mv.html 逐帧渲染出来，再和 song.mp3 合成 MP4。
//
//   node export.mjs                          整首导出 → mv.mp4
//   node export.mjs --from 20 --to 35        只导出一段（试看用），音频也会截取对应部分
//   node export.mjs --shots 5,22.5,45        只截几张图到 shots/，检查画面
//   可选：--workers 4  --fps 30  --crf 18  --out 名字.mp4
//
// 每个 worker 是一个无头 Chrome，页面以 ?export=1 打开，由脚本指定每一帧的精确时间（window.__frame(t)）。
// 帧按顺序写给 ffmpeg，所以结果与 worker 数量、机器快慢无关。
// 需要：Node 22+、Chrome / Edge / Chromium、PATH 中的 ffmpeg。
import { spawn } from 'node:child_process';
import { existsSync, mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { availableParallelism, tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const argv = process.argv.slice(2), args = {};
for (let i = 0; i < argv.length; i++) if (argv[i].startsWith('--')) args[argv[i].slice(2)] = argv[i + 1]?.startsWith('--') ? true : argv[++i] ?? true;
const FPS = +(args.fps || 30), CRF = +(args.crf || 18);
const WORKERS = args.shots ? 1 : Math.max(1, +(args.workers || Math.min(4, Math.floor(availableParallelism() / 2))));
const PAGE = resolve(here, args.page || 'mv.html');
const AUDIO = resolve(here, 'song.mp3');
const OUT = resolve(here, args.out || 'mv.mp4');
const CHROME = [process.env.CHROME,
  '/opt/pw-browsers/chromium-1194/chrome-linux/chrome', '/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser',
  'C:/Program Files/Google/Chrome/Application/chrome.exe', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
  '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'].find(p => p && existsSync(p));
if (!CHROME) { console.error('找不到 Chrome / Edge，请用环境变量 CHROME=路径 指定'); process.exit(1); }

const sleep = ms => new Promise(r => setTimeout(r, ms));
const procs = [], profiles = [];
process.on('exit', () => {
  for (const p of procs) try { p.kill(); } catch {}
  for (const d of profiles) try { rmSync(d, { recursive: true, force: true }); } catch {}
});
process.on('SIGINT', () => process.exit(130));
process.on('unhandledRejection', e => { console.error('导出失败：', e?.message || e); process.exit(1); });

// ---- 启动一个无头 Chrome，通过 DevTools 协议控制 ----
async function launch(id) {
  const profile = mkdtempSync(join(tmpdir(), 'mv-export-'));
  profiles.push(profile);
  const proc = spawn(CHROME, ['--headless=new', '--remote-debugging-port=0', '--remote-allow-origins=*', `--user-data-dir=${profile}`,
    '--window-size=1920,1080', '--hide-scrollbars', '--mute-audio', '--no-first-run', '--no-default-browser-check',
    '--allow-file-access-from-files', ...(process.getuid?.() === 0 ? ['--no-sandbox'] : []), 'about:blank'], { stdio: 'ignore' });
  procs.push(proc);
  let port = 0, target;
  for (let i = 0; i < 120 && !port; i++) { try { port = +readFileSync(join(profile, 'DevToolsActivePort'), 'utf8').split('\n')[0]; } catch {} if (!port) await sleep(250); }
  for (let i = 0; i < 80 && port && !target; i++) {
    try { target = (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json()).find(t => t.type === 'page'); } catch {}
    if (!target) await sleep(250);
  }
  if (!target) throw new Error(`worker ${id}：Chrome 没有启动`);
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
  let seq = 0; const pending = new Map();
  ws.onmessage = ev => {
    const m = JSON.parse(ev.data);
    if (m.method === 'Runtime.exceptionThrown') console.error(`worker ${id} 页面报错：`, m.params.exceptionDetails.exception?.description || m.params.exceptionDetails.text);
    const p = pending.get(m.id); if (!p) return;
    pending.delete(m.id); m.error ? p.rej(new Error(m.error.message)) : p.res(m.result);
  };
  const send = (method, params = {}) => new Promise((res, rej) => { const n = ++seq; pending.set(n, { res, rej }); ws.send(JSON.stringify({ id: n, method, params })); });
  const evaluate = async expr => {
    const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
    if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || r.exceptionDetails.text);
    return r.result.value;
  };
  await send('Runtime.enable'); await send('Page.enable');
  await send('Page.navigate', { url: pathToFileURL(PAGE).href + '?export=1' });
  let ready = false;
  for (let i = 0; i < 120 && !ready; i++) { await sleep(250); try { ready = await evaluate("typeof window.__frame === 'function' && window.__ready()"); } catch {} }
  if (!ready) throw new Error(`worker ${id}：页面没有加载完成`);
  return {
    evaluate,
    frame: async t => { const url = await evaluate(`__frame(${t})`); return Buffer.from(url.slice(url.indexOf(',') + 1), 'base64'); },
  };
}

const first = await launch(0);
const info = await first.evaluate('__info()');

// ---- 截图模式 ----
if (args.shots) {
  const dir = join(here, 'shots'); mkdirSync(dir, { recursive: true });
  for (const s of String(args.shots).split(',').map(Number)) {
    const f = join(dir, `shot_${s.toFixed(2).padStart(7, '0')}.jpg`);
    writeFileSync(f, await first.frame(s)); console.log('写出', f);
  }
  process.exit(0);
}

// ---- 视频模式 ----
const T0 = +(args.from || 0), T1 = Math.min(+(args.to || info.duration), info.duration);
const N = Math.round((T1 - T0) * FPS);
const workers = [first, ...await Promise.all(Array.from({ length: WORKERS - 1 }, (_, i) => launch(i + 1)))];
console.log(`导出 ${T0}s–${T1.toFixed(2)}s，${N} 帧，${workers.length} 个 worker → ${OUT}`);

const ff = spawn('ffmpeg', ['-y', '-loglevel', 'error', '-f', 'image2pipe', '-framerate', String(FPS), '-c:v', 'mjpeg', '-i', '-',
  '-ss', String(T0), '-t', String(T1 - T0), '-i', AUDIO,
  '-map', '0:v', '-map', '1:a', '-c:v', 'libx264', '-preset', 'medium', '-crf', String(CRF), '-pix_fmt', 'yuv420p',
  '-c:a', 'aac', '-b:a', '256k', '-shortest', '-movflags', '+faststart', OUT], { stdio: ['pipe', 'inherit', 'inherit'] });
const ffDone = new Promise((res, rej) => ff.on('close', c => c === 0 ? res() : rej(new Error(`ffmpeg 退出码 ${c}`))));

// 帧按序号分给空闲的 worker，渲染完的帧先放进 buffer，按顺序写给 ffmpeg
const done = new Map(); let next = 0, written = 0; const start = Date.now();
async function flush() {
  while (done.has(written)) {
    const buf = done.get(written); done.delete(written); written++;
    if (!ff.stdin.write(buf)) await new Promise(r => ff.stdin.once('drain', r));
  }
}
let flushing = Promise.resolve();
await Promise.all(workers.map(async w => {
  while (next < N) {
    const i = next++;
    done.set(i, await w.frame(T0 + i / FPS));
    flushing = flushing.then(flush);
    if (i % FPS === 0) {
      const sec = (Date.now() - start) / 1000;
      process.stdout.write(`\r  ${i}/${N} 帧  ${(i / sec || 0).toFixed(1)} fps  剩余约 ${Math.round((N - i) / (i / sec || 1))}s   `);
    }
  }
}));
await flushing;
ff.stdin.end();
await ffDone;
console.log(`\n完成：${OUT}（用时 ${Math.round((Date.now() - start) / 1000)}s）`);
process.exit(0);
