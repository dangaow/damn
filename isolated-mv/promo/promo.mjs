// 《Isolated》竖屏短视频（1080×1920 · 30 fps）：从 MV 成片里截最抓人的几段，竖着排版。
//
//   node promo/promo.mjs              全部导出 → promo/out/*.mp4
//   node promo/promo.mjs 01_onemore   只导出一条
//
// 画面：MV 原样放在中间（1080×608，不裁），背景是同一段画面放大、糊开、压暗铺满（只取上面 70%，MV 自己的字幕不会被放大糊在背景里）；
// 叠字层（promo.html，透明 PNG）每帧一张：上面片名署名，下面跟着歌词走的大字幕，最下面 MV OUT NOW。
// 声音直接用成片里的那一段。开头不淡入、结尾硬切：第一帧就要抓人，循环播放时接得上。
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const MASTER = resolve(here, '..', 'Isolated_MV.mp4');
const FPS = 30, VY = 656, VH = 608;
// 每条从一个"砸下来"的点切进去（鼓进、鼓抽空、换段），时间是歌曲时间（秒）
export const CLIPS = [
  { id: '01_onemore', t0: 34.55, t1: 45.85, note: '鼓抽空 → 副歌一：可是 i want u know that … u love me onemore，停在 ONE MORE 字卡上' },
  { id: '02_rain', t0: 4.12, t1: 12.15, note: '第一记底鼓 → 我心里下起 rain → ISOLATED 片名卡 → 停在 would u know that（问句，循环播放接得上）' },
  { id: '03_dangao_w', t0: 68.38, t1: 79.55, note: 'dangao_w：鼓回来的那一下（铁笼砸碎栏杆）→ 面对没有你的未来 → 风铃 → 我只是不懂我真的不懂' },
  { id: '04_rose', t0: 82.22, t1: 90.50, freeze: 89.95, note: 'dangao_w：纪念的花束被你丢掉 … 删掉曾经的唯一 … 定格在「下段 back to you, yuki」，声音走到副歌的第一个字再切' },
];
const only = process.argv.slice(2).filter(a => !a.startsWith('--'));
const todo = CLIPS.filter(c => !only.length || only.includes(c.id));

const CHROME = [process.env.CHROME, '/opt/pw-browsers/chromium-1194/chrome-linux/chrome', '/usr/bin/google-chrome', '/usr/bin/chromium'].find(p => p && existsSync(p));
const sleep = ms => new Promise(r => setTimeout(r, ms));
const procs = [], profiles = [];
process.on('exit', () => { for (const p of procs) try { p.kill(); } catch {} for (const d of profiles) try { rmSync(d, { recursive: true, force: true }); } catch {} });

async function launch() {
  const profile = mkdtempSync(join(tmpdir(), 'promo-')); profiles.push(profile);
  const proc = spawn(CHROME, ['--headless=new', '--remote-debugging-port=0', '--remote-allow-origins=*', `--user-data-dir=${profile}`, '--window-size=1080,1920',
    '--hide-scrollbars', '--mute-audio', '--no-first-run', '--allow-file-access-from-files', ...(process.getuid?.() === 0 ? ['--no-sandbox'] : []), 'about:blank'], { stdio: 'ignore' });
  procs.push(proc);
  let port = 0, target;
  for (let i = 0; i < 120 && !port; i++) { try { port = +readFileSync(join(profile, 'DevToolsActivePort'), 'utf8').split('\n')[0]; } catch {} if (!port) await sleep(250); }
  for (let i = 0; i < 80 && !target; i++) { try { target = (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json()).find(t => t.type === 'page'); } catch {} if (!target) await sleep(250); }
  const ws = new WebSocket(target.webSocketDebuggerUrl); await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
  let seq = 0; const pending = new Map();
  ws.onmessage = ev => { const m = JSON.parse(ev.data); if (m.method === 'Runtime.exceptionThrown') console.error('页面报错：', m.params.exceptionDetails.exception?.description);
    const p = pending.get(m.id); if (p) { pending.delete(m.id); m.error ? p.rej(new Error(m.error.message)) : p.res(m.result); } };
  const send = (method, params = {}) => new Promise((res, rej) => { const n = ++seq; pending.set(n, { res, rej }); ws.send(JSON.stringify({ id: n, method, params })); });
  const evaluate = async expr => { const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true }); if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || r.exceptionDetails.text); return r.result.value; };
  await send('Runtime.enable'); await send('Page.navigate', { url: pathToFileURL(join(here, 'promo.html')).href });
  for (let i = 0; i < 120; i++) { await sleep(250); try { if (await evaluate("typeof __frame === 'function' && __ready()")) break; } catch {} }
  return { overlay: async (clip, t) => Buffer.from((await evaluate(`__frame(${JSON.stringify(clip)}, ${t})`)).split(',')[1], 'base64') };
}

const page = await launch();
for (const clip of todo) {
  const N = Math.round((clip.t1 - clip.t0) * FPS), D = N / FPS;                   // 时长取整到帧
  const Dv = clip.freeze ? Math.round((clip.freeze - clip.t0) * FPS) / FPS : D;     // 定格：画面停在 freeze，声音继续走到 t1
  const outDir = join(here, 'out'); mkdirSync(outDir, { recursive: true });
  // 结尾硬切（平台会自动循环播放，淡出到黑像是"结束了"），声音只留 80 ms 的淡出防爆音；限幅到 -2 dBFS（AAC 编码后真峰值约 -1 dBTP），平台转码不会削波
  const graph = `[0:v]split[a][b];[a]crop=iw:ih*0.7:0:0,scale=1080:1920:force_original_aspect_ratio=increase,crop=1080:1920,boxblur=60:3,eq=brightness=-0.34:saturation=0.7[bg];` +
    `[b]scale=1080:${VH}:flags=lanczos[fg];[bg][fg]overlay=0:${VY}${clip.freeze ? `,tpad=stop_mode=clone:stop_duration=${(D - Dv).toFixed(4)}` : ''}[base];[base][1:v]overlay=0:0,format=yuv420p[v];` +
    `[2:a]alimiter=limit=0.8:level=false,afade=t=in:st=0:d=0.03,afade=t=out:st=${(D - .08).toFixed(3)}:d=0.08[a]`;
  const out = join(outDir, `Isolated_${clip.id}.mp4`);
  const args = ['-y', '-v', 'error', '-ss', String(clip.t0), '-t', String(Dv), '-i', MASTER, '-f', 'image2pipe', '-framerate', String(FPS), '-c:v', 'png', '-i', '-',
    '-ss', String(clip.t0), '-t', String(D), '-i', MASTER,
    '-filter_complex', graph, '-map', '[v]', '-map', '[a]', '-frames:v', String(N),
    '-c:v', 'libx264', '-preset', 'slow', '-crf', '18', '-pix_fmt', 'yuv420p', '-r', String(FPS), '-colorspace', 'bt709', '-color_primaries', 'bt709', '-color_trc', 'bt709',
    '-c:a', 'aac', '-b:a', '192k', '-movflags', '+faststart', out];
  const ff = spawn('ffmpeg', args, { stdio: ['pipe', 'inherit', 'inherit'] });
  const done = new Promise((res, rej) => ff.on('close', code => code === 0 ? res() : rej(new Error('ffmpeg ' + code))));
  for (let i = 0; i < N; i++) {
    const buf = await page.overlay(clip, clip.t0 + i / FPS);
    if (!ff.stdin.write(buf)) await new Promise(r => ff.stdin.once('drain', r));
  }
  ff.stdin.end(); await done;
  console.log(`${clip.id}: ${D}s, ${N} 帧 → ${out}`);
}
process.exit(0);
