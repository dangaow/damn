'use strict';
// ============================================================================
// 渲染：2D 层（场景）→ GL（背景 + 后期）→ 多个子帧累加成运动模糊 → 最上面画 HUD
// ============================================================================
const OUT = document.getElementById('cv'), ox = OUT.getContext('2d');
const LAY = mk(), lx = LAY.getContext('2d');
const Q = new URLSearchParams(location.search);
const EXPORT = Q.has('export');
let DEBUG = !EXPORT && Q.has('debug');

function drawLayer(t) {
  const s = sceneAt(t), lt = t - s.t0;
  lx.setTransform(1, 0, 0, 1, 0, 0); lx.globalAlpha = 1; lx.globalCompositeOperation = 'source-over'; lx.shadowBlur = 0;
  lx.clearRect(0, 0, W, H);
  if (s.weather) drawWorld(lx, t, s.weather.call(s, t, lt));                   // 同一场雨、同一条街
  s.draw.call(s, lx, t, lt);
  lx.setTransform(1, 0, 0, 1, 0, 0); lx.globalAlpha = 1;
  polygraph(lx, t, POLY);
  const fx = { ...FX0, ...(s.fx ? s.fx.call(s, t, lt) : {}) };
  for (const k of ['zoom', 'rot', 'barrel']) if (!Number.isFinite(fx[k])) fx[k] = FX0[k];
  // 镜头一直在呼吸：很轻很慢的整体漂移，像手持摄像机，切镜时画面不会"归零"
  fx.shift = [(fx.shift?.[0] ?? 0) + Math.sin(t * .21) * .003, (fx.shift?.[1] ?? 0) + Math.cos(t * .17) * .0025];
  fx.rot = fx.rot + Math.sin(t * .09) * .004;
  return { s, fx, bg: s.bg ? s.bg.call(s, t, lt) : { mode: 'solid' } };
}

// samples：子帧数（1 = 不做运动模糊）；shutter：快门占一帧的比例
function render(t, samples = 1, shutter = .5) {
  let last;
  for (let k = 0; k < samples; k++) {
    const ts = samples > 1 ? t + ((k + .5) / samples - .5) * shutter / 30 : t;
    last = drawLayer(ts);
    GLX.frame(LAY, ts, last.bg, last.fx, 1 / samples, k === 0);
  }
  ox.setTransform(1, 0, 0, 1, 0, 0); ox.globalAlpha = 1;
  ox.drawImage(GLX.present(), 0, 0);
  const fx = last.fx;
  if (fx.hud > 0 && fx.sq[1] > .5) hud(ox, t, { a: fx.hud, lb: fx.lb, tape: fx.tape, mode: fx.hudMode, tc: fx.tc ?? t });
  if (DEBUG) drawDebug(t, last.s);
}

function drawDebug(t, s) {
  const b = M.bar(t), { line } = M.lyric(t), k = M.last('kick', t), n = M.note(t);
  ox.save(); ox.fillStyle = 'rgba(0,0,0,.7)'; ox.fillRect(20, 20, 620, 230);
  ox.font = '22px monospace'; ox.fillStyle = '#9f9'; ox.textAlign = 'left'; ox.textBaseline = 'top';
  [`t ${t.toFixed(2)}s   94 BPM   小节 ${b.i} 第 ${Math.floor(b.phase * 16)} 格`, `分镜 ${s.name}  (${s.t0.toFixed(2)}–${Math.min(s.t1, D.duration).toFixed(2)}s)`,
   `底鼓 #${k.i} ${jerk(t).act ?? ''}  ${M.gap(t) ? '■ 静音' : ''}${b.info.fill ? ' 加花' : ''}${b.info.b808 ? '' : ' 无808'}`,
   `808 ${n ? n.midi.toFixed(1) + (n.glide ? ' 滑→' + n.glide : '') : '—'}   人声 ${M.vocal(t).toFixed(2)}`, `歌词 ${line ? line.t.toFixed(2) + 's ' + line.text : '—'}`]
    .forEach((s2, i) => ox.fillText(s2, 36, 34 + i * 38));
  const y0 = H - 54, sx = x => x / D.duration * W;
  SCENES.forEach((s2, i) => { ox.fillStyle = `hsla(${i * 37},60%,50%,.6)`; ox.fillRect(sx(s2.t0), y0, sx(Math.min(s2.t1, D.duration)) - sx(s2.t0) - 1, 10); });
  ox.fillStyle = '#fff'; D.kicks.forEach(x => ox.fillRect(sx(x), y0 + 12, 1, 8));
  ox.fillStyle = '#6cf'; D.snares.forEach(x => ox.fillRect(sx(x), y0 + 22, 1, 6));
  ox.fillStyle = '#f55'; D.gaps.forEach(g => ox.fillRect(sx(g[0]), y0 + 30, Math.max(2, sx(g[1]) - sx(g[0])), 5));
  ox.fillStyle = '#fc6'; D.lyrics.forEach(l => ox.fillRect(sx(l.t), y0 + 37, Math.max(2, sx(l.end) - sx(l.t) - 2), 5));
  ox.fillStyle = '#f44'; ox.fillRect(sx(t) - 1, y0 - 6, 3, 52);
  ox.restore();
}

// ---------------------------------------------------------------- 导出接口（export.mjs 调用）
const FONTS = [`400 20px "${SANS}"`, `900 20px "${SANS}"`, `400 20px "${SERIF}"`, `900 20px "${SERIF}"`, `20px "${GOTH}"`, `20px "${ANTON}"`, `20px "${MONO}"`, `700 20px "${PIX}"`];
const fontsReady = Promise.all(FONTS.map(f => document.fonts.load(f, '谎A'))).then(() => document.fonts.ready).then(() => true);
window.__ready = () => fontsReady;
window.__info = () => ({ duration: D.duration, title: D.title });
window.__frame = (t, samples = 3, png = false) => { render(t, samples); return png ? OUT.toDataURL('image/png') : OUT.toDataURL('image/jpeg', .92); };

// ---------------------------------------------------------------- 播放器
const au = document.getElementById('au'), pp = document.getElementById('pp'), seek = document.getElementById('seek'), tm = document.getElementById('time');
const fmt = s => `${Math.floor(s / 60)}:${(s % 60).toFixed(1).padStart(4, '0')}`;
if (EXPORT) document.body.classList.add('export');
else {
  const loop = () => {
    const t = au.currentTime;
    render(t, 1);
    seek.value = t / D.duration * 1000;
    tm.textContent = `${fmt(t)} / ${fmt(D.duration).slice(0, -2)}`;
    if (!au.paused) requestAnimationFrame(loop);
  };
  const toggle = () => au.paused ? au.play() : au.pause();
  au.onplay = () => { document.body.classList.remove('paused'); pp.textContent = '❚❚'; requestAnimationFrame(loop); };
  au.onpause = () => { document.body.classList.add('paused'); pp.textContent = '▶'; };
  pp.onclick = toggle; OUT.onclick = toggle;
  seek.oninput = () => { au.currentTime = seek.value / 1000 * D.duration; loop(); };
  document.getElementById('dbg').onclick = () => { DEBUG = !DEBUG; loop(); };
  addEventListener('keydown', e => {
    if (e.code === 'Space') { e.preventDefault(); toggle(); }
    else if (e.key === 'd' || e.key === 'D') { DEBUG = !DEBUG; loop(); }
    else if (e.key === 'ArrowLeft') { au.currentTime = Math.max(0, au.currentTime - (e.shiftKey ? 1 : 5)); loop(); }
    else if (e.key === 'ArrowRight') { au.currentTime = Math.min(D.duration, au.currentTime + (e.shiftKey ? 1 : 5)); loop(); }
    else if (e.key === ',') { au.currentTime = Math.max(0, au.currentTime - 1 / 30); loop(); }
    else if (e.key === '.') { au.currentTime = Math.min(D.duration, au.currentTime + 1 / 30); loop(); }
  });
  fontsReady.then(loop);
}
