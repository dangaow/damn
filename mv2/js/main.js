'use strict';
// ============================================================================
// 渲染：2D 层（场景）→ GL（镜头 + 网点 + 纸）→ 多个子帧累加成运动模糊
// ============================================================================
const OUT = document.getElementById('cv'), ox = OUT.getContext('2d');
const LAY = mk(), lx = LAY.getContext('2d');
const Q = new URLSearchParams(location.search);
const EXPORT = Q.has('export');
let DEBUG = !EXPORT && Q.has('debug');

function drawLayer(t) {
  const s = sceneAt(t), lt = t - s.t0;
  lx.setTransform(1, 0, 0, 1, 0, 0); lx.globalAlpha = 1; lx.globalCompositeOperation = 'source-over';
  lx.fillStyle = C.paper; lx.fillRect(0, 0, W, H);
  const fx = { ...FX0, ...(s.fx ? s.fx.call(s, t, lt) : {}) };
  // 底鼓：整页往下顿一下（像漫画的"定格"），越到后面越轻
  const kj = fx.jolt * M.hit('kick', t, 16);
  lx.save(); lx.translate(0, kj * 7);
  s.draw.call(s, lx, t, lt);
  lx.restore();
  // 镜头一直在呼吸：很轻很慢的漂移，像读者的视线
  fx.shift = [(fx.shift?.[0] ?? 0) + Math.sin(t * .21) * .002, (fx.shift?.[1] ?? 0) + Math.cos(t * .17) * .0018];
  fx.rot = fx.rot + Math.sin(t * .09) * .003;
  return { s, fx };
}

// samples：子帧数（1 = 不做运动模糊）；shutter：快门占一帧的比例
function render(t, samples = 1, shutter = .5) {
  let last;
  for (let k = 0; k < samples; k++) {
    const ts = samples > 1 ? t + ((k + .5) / samples - .5) * shutter / 30 : t;
    last = drawLayer(ts);
    GLX.frame(LAY, ts, last.fx, 1 / samples, k === 0);
  }
  ox.setTransform(1, 0, 0, 1, 0, 0); ox.globalAlpha = 1;
  ox.drawImage(GLX.present(), 0, 0);
  if (DEBUG) drawDebug(t, last.s);
}

function drawDebug(t, s) {
  const b = M.bar(t), { line } = M.lyric(t), k = M.last('kick', t);
  ox.save(); ox.fillStyle = 'rgba(0,0,0,.7)'; ox.fillRect(20, 20, 640, 192);
  ox.font = '22px monospace'; ox.fillStyle = '#9f9'; ox.textAlign = 'left'; ox.textBaseline = 'top';
  [`t ${t.toFixed(2)}s   86 BPM   小节 ${b.i} 第 ${Math.floor(b.phase * 16)} 格`, `分镜 ${s.name}  (${s.t0.toFixed(2)}–${Math.min(s.t1, MV_END).toFixed(2)}s)`,
   `底鼓 #${k.i}  ${M.gap(t) ? '■ 静音' : ''}  底鼓/军鼓 ${b.info.kicks ?? '-'}/${b.info.snares ?? '-'}${b.info.bass ? '' : ' 无贝斯'}`,
   `歌词 ${line ? line.t.toFixed(2) + 's ' + line.text : '—'}`]
    .forEach((s2, i) => ox.fillText(s2, 36, 34 + i * 38));
  const y0 = H - 54, sx = x => x / MV_END * W;
  SCENES.forEach((s2, i) => { ox.fillStyle = `hsla(${i * 37},60%,50%,.6)`; ox.fillRect(sx(s2.t0), y0, sx(Math.min(s2.t1, MV_END)) - sx(s2.t0) - 1, 10); });
  ox.fillStyle = '#000'; D.kicks.forEach(x => ox.fillRect(sx(x), y0 + 12, 1, 8));
  ox.fillStyle = '#06c'; D.snares.forEach(x => ox.fillRect(sx(x), y0 + 22, 1, 6));
  ox.fillStyle = '#f55'; D.gaps.forEach(g => ox.fillRect(sx(g[0]), y0 + 30, Math.max(2, sx(g[1]) - sx(g[0])), 5));
  ox.fillStyle = '#c80'; D.lyrics.forEach(l => ox.fillRect(sx(l.t), y0 + 37, Math.max(2, sx(l.end) - sx(l.t) - 2), 5));
  ox.fillStyle = '#f00'; ox.fillRect(sx(t) - 1, y0 - 6, 3, 52);
  ox.restore();
}

// ---------------------------------------------------------------- 导出接口（export.mjs 调用）
const FONTS = [`500 20px "${SANS}"`, `900 20px "${SANS}"`, `300 20px "${SERIF}"`, `900 20px "${SERIF}"`, `700 20px "${COMIC}"`, `20px "${BANG}"`, `20px "${BRUSH}"`, `600 20px "${HAND}"`, `20px "${HANDZH}"`];
const fontsReady = Promise.all([coverReady, ...FONTS.map(f => document.fonts.load(f, '我A'))]).then(() => document.fonts.ready).then(() => true);
window.__ready = () => fontsReady;
window.__info = () => ({ duration: MV_END, title: D.title });
window.__frame = (t, samples = 3, png = false) => { render(t, samples); return png ? OUT.toDataURL('image/png') : OUT.toDataURL('image/jpeg', .93); };

// ---------------------------------------------------------------- 播放器
const au = document.getElementById('au'), pp = document.getElementById('pp'), seek = document.getElementById('seek'), tm = document.getElementById('time');
const fmt = s => `${Math.floor(s / 60)}:${(s % 60).toFixed(1).padStart(4, '0')}`;
if (EXPORT) document.body.classList.add('export');
else {
  const loop = () => {
    const t = au.currentTime;
    render(t, 1);
    seek.value = t / MV_END * 1000;
    tm.textContent = `${fmt(t)} / ${fmt(MV_END).slice(0, -2)}`;
    if (!au.paused) requestAnimationFrame(loop);
  };
  const toggle = () => au.paused ? au.play() : au.pause();
  au.onplay = () => { document.body.classList.remove('paused'); pp.textContent = '❚❚'; requestAnimationFrame(loop); };
  au.onpause = () => { document.body.classList.add('paused'); pp.textContent = '▶'; };
  pp.onclick = toggle; OUT.onclick = toggle;
  seek.oninput = () => { au.currentTime = seek.value / 1000 * MV_END; loop(); };
  document.getElementById('dbg').onclick = () => { DEBUG = !DEBUG; loop(); };
  addEventListener('keydown', e => {
    if (e.code === 'Space') { e.preventDefault(); toggle(); }
    else if (e.key === 'd' || e.key === 'D') { DEBUG = !DEBUG; loop(); }
    else if (e.key === 'ArrowLeft') { au.currentTime = Math.max(0, au.currentTime - (e.shiftKey ? 1 : 5)); loop(); }
    else if (e.key === 'ArrowRight') { au.currentTime = Math.min(MV_END, au.currentTime + (e.shiftKey ? 1 : 5)); loop(); }
    else if (e.key === ',') { au.currentTime = Math.max(0, au.currentTime - 1 / 30); loop(); }
    else if (e.key === '.') { au.currentTime = Math.min(MV_END, au.currentTime + 1 / 30); loop(); }
  });
  fontsReady.then(loop);
}
