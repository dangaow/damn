// 把 clawd.html 逐帧渲染出来，用 PNG（无损）交给 ffmpeg，再和配乐合成 MP4。
//
//   node export.mjs                          整片导出 → output/小克的一回合.mp4
//   node export.mjs --from 50 --to 74        只导出一段（试看用），音频也截取对应部分
//   node export.mjs --shots 5,22.5,45        只截几张 PNG 到 shots/，检查画面
//   node export.mjs --cues cues.json         导出音效点和段落表，给 music.py 编曲用
//   node export.mjs --frames                 同时把每一帧存成 output/frames/frame_00000.png（无损序列，很占空间）
//   node export.mjs --master                 同时输出无损母版 output/小克的一回合_master.mkv（FFV1，RGB 原样保留）
//   可选：--workers 4  --mb 3（运动模糊子帧数，1 = 关）  --crf 16  --preset slow  --jpeg（改用 JPEG 传帧，快一点但有损）  --out 名字.mp4
//
// 每个 worker 是一个无头 Chrome，页面以 ?export=1 打开，由脚本指定每一帧的精确时间（window.__frame(t)）。
// 帧按顺序写给 ffmpeg，所以结果与 worker 数量、机器快慢无关。
// 需要：Node 22+、Chrome / Edge / Chromium、PATH 中的 ffmpeg。
import { spawn } from 'node:child_process';
import { existsSync, mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync, statSync } from 'node:fs';
import { availableParallelism, tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const argv = process.argv.slice(2), args = {};
for (let i = 0; i < argv.length; i++) if (argv[i].startsWith('--')) args[argv[i].slice(2)] = argv[i + 1] === undefined || argv[i + 1].startsWith('--') ? true : argv[++i];
const FPS = 30, CRF = +(args.crf || 16), SAMPLES = +(args.mb || 3), PRESET = args.preset || 'medium';
const PNG = !args.jpeg;
const WORKERS = args.shots || args.cues ? 1 : Math.max(1, +(args.workers || Math.min(4, Math.floor(availableParallelism() / 2) || 1)));
const PAGE = resolve(here, args.page || 'clawd.html');
const OUTDIR = resolve(here, 'output'); mkdirSync(OUTDIR, { recursive: true });
const OUT = resolve(OUTDIR, args.out || '小克的一回合.mp4');
const MASTER = resolve(OUTDIR, '小克的一回合_master.mkv');
const WAV = resolve(OUTDIR, 'music.wav'), MP3 = resolve(here, 'music.mp3');
const AUDIO = args.audio ? resolve(here, args.audio) : existsSync(WAV) ? WAV : existsSync(MP3) ? MP3 : null;
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
  const profile = mkdtempSync(join(tmpdir(), 'clawd-export-'));
  profiles.push(profile);
  const proc = spawn(CHROME, ['--headless=new', '--remote-debugging-port=0', '--remote-allow-origins=*', `--user-data-dir=${profile}`,
    '--window-size=1920,1080', '--hide-scrollbars', '--mute-audio', '--no-first-run', '--no-default-browser-check',
    '--allow-file-access-from-files', '--force-color-profile=srgb',
    ...(process.getuid?.() === 0 ? ['--no-sandbox'] : []), 'about:blank'], { stdio: 'ignore' });
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
  ws.onclose = () => { for (const p of pending.values()) p.rej(new Error(`worker ${id}：Chrome 断开了`)); };
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
    frame: async t => { const url = await evaluate(`__frame(${t}, ${SAMPLES}, ${PNG})`); return Buffer.from(url.slice(url.indexOf(',') + 1), 'base64'); },
  };
}

const first = await launch(0);
const info = await first.evaluate('__info()');

// ---- 音效点 ----
if (args.cues) {
  const f = resolve(here, args.cues === true ? 'cues.json' : args.cues);
  writeFileSync(f, JSON.stringify(await first.evaluate('__cues()'), null, 1));
  console.log('写出', f);
  process.exit(0);
}
// ---- 截图模式 ----
if (args.shots) {
  const dir = join(here, 'shots'); mkdirSync(dir, { recursive: true });
  for (const s of String(args.shots).split(',').map(Number)) {
    const f = join(dir, `shot_${s.toFixed(2).padStart(7, '0')}.${PNG ? 'png' : 'jpg'}`);
    writeFileSync(f, await first.frame(s)); console.log('写出', f);
  }
  process.exit(0);
}

// ---- 视频模式 ----
const T0 = +(args.from || 0), T1 = Math.min(+(args.to || info.duration), info.duration);
const N = Math.round((T1 - T0) * FPS);
const workers = [first, ...await Promise.all(Array.from({ length: WORKERS - 1 }, (_, i) => launch(i + 1)))];
const FRAMES = args.frames ? resolve(OUTDIR, 'frames') : null;
if (FRAMES) mkdirSync(FRAMES, { recursive: true });
if (!AUDIO) console.warn('没有配乐（output/music.wav 或 music.mp3），先跑 python music.py；这次导出没有声音');
console.log(`导出 ${T0}s–${T1.toFixed(2)}s，${N} 帧，${workers.length} 个 worker，${PNG ? 'PNG 无损' : 'JPEG'}传帧，运动模糊 ×${SAMPLES} → ${OUT}`);

const ffArgs = ['-y', '-loglevel', 'error', '-f', 'image2pipe', '-framerate', String(FPS), '-c:v', PNG ? 'png' : 'mjpeg', '-i', '-'];
if (AUDIO) ffArgs.push('-ss', String(T0), '-t', String(T1 - T0), '-i', AUDIO);
const color = ['-colorspace', 'bt709', '-color_primaries', 'bt709', '-color_trc', 'bt709'];
ffArgs.push('-map', '0:v', ...(AUDIO ? ['-map', '1:a'] : []), '-c:v', 'libx264', '-preset', PRESET, '-crf', String(CRF), '-pix_fmt', 'yuv420p',
  '-vf', 'scale=out_color_matrix=bt709', ...color, ...(AUDIO ? ['-c:a', 'aac', '-b:a', '256k', '-shortest'] : []), '-movflags', '+faststart', OUT);
if (args.master) ffArgs.push('-map', '0:v', ...(AUDIO ? ['-map', '1:a'] : []), '-c:v', 'ffv1', '-level', '3', '-pix_fmt', 'bgr0', ...(AUDIO ? ['-c:a', 'flac', '-shortest'] : []), MASTER);
const ff = spawn('ffmpeg', ffArgs, { stdio: ['pipe', 'inherit', 'inherit'] });
const ffDone = new Promise((res, rej) => ff.on('close', c => c === 0 ? res() : rej(new Error(`ffmpeg 退出码 ${c}`))));

// 帧按序号分给空闲的 worker，渲染完的帧先放进 buffer，按顺序写给 ffmpeg
const done = new Map(); let next = 0, written = 0; const start = Date.now();
async function flush() {
  while (done.has(written)) {
    const buf = done.get(written); done.delete(written);
    if (FRAMES) writeFileSync(join(FRAMES, `frame_${String(Math.round(T0 * FPS) + written).padStart(5, '0')}.png`), buf);
    written++;
    if (!ff.stdin.write(buf)) await new Promise(r => ff.stdin.once('drain', r));
  }
}
let flushing = Promise.resolve();
await Promise.all(workers.map(async w => {
  while (next < N) {
    while (next - written > 16 * workers.length) await sleep(20);   // 别让渲染跑得比编码快太多（PNG 很大）
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
console.log(`\n完成：${OUT}（${(statSync(OUT).size / 1e6).toFixed(1)} MB，用时 ${Math.round((Date.now() - start) / 1000)}s）`);
if (args.master) console.log(`无损母版：${MASTER}（${(statSync(MASTER).size / 1e6).toFixed(1)} MB）`);
process.exit(0);
