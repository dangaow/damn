// 导出《谎话》动态封面：node cover_motion_export.mjs [边长=1080] [周期秒=10] [输出文件]
// 用无头 Chrome 打开 cover_motion.html，一帧一帧取 PNG，直接喂给 ffmpeg 编成 H.264（无声，30fps，首尾无缝）
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { mkdtempSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
const here = dirname(fileURLToPath(import.meta.url));
const [s = '1080', p = '10', out = `output/谎话_动态封面_${s}.mp4`] = process.argv.slice(2);
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
await send('Page.navigate', { url: pathToFileURL(resolve(here, 'cover_motion.html')).href + `?v=j&s=${s}&p=${p}&export=1` });
for (let i = 0; i < 200; i++) { await sleep(250); try { if (await ev("typeof window.__frame === 'function' && typeof window.__ready === 'function' && window.__ready()")) break; } catch {} }
const { P, FPS } = await ev('window.__info()'), N = Math.round(P * FPS);
const ff = spawn('ffmpeg', ['-v', 'error', '-y', '-f', 'image2pipe', '-framerate', String(FPS), '-c:v', 'png', '-i', '-',
  '-c:v', 'libx264', '-preset', 'slow', '-crf', process.env.CRF || '20', '-pix_fmt', 'yuv420p', '-profile:v', 'high', '-g', String(FPS * 2),
  '-colorspace', 'bt709', '-color_primaries', 'bt709', '-color_trc', 'bt709', '-movflags', '+faststart', '-an', resolve(here, out)], { stdio: ['pipe', 'inherit', 'inherit'] });
for (let i = 0; i < N; i++) {
  const url = await ev(`window.__frame(${i / FPS})`);
  if (!ff.stdin.write(Buffer.from(url.slice(url.indexOf(',') + 1), 'base64'))) await once(ff.stdin, 'drain');
  if (i % 30 === 0) process.stdout.write(`\r${i}/${N}`);
}
ff.stdin.end(); const [code] = await once(ff, 'exit');
console.log(`\n写出 ${out}（${N} 帧，${P} 秒循环）`, code === 0 ? '' : `ffmpeg 退出码 ${code}`);
proc.kill(); await new Promise(r => { proc.once('exit', r); setTimeout(r, 3000); });
try { rmSync(profile, { recursive: true, force: true }); } catch {}
process.exit(code === 0 ? 0 : 1);
