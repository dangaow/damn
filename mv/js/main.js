'use strict';
// ============================================================================
// 渲染：2D 层（场景）→ GL（背景 + 后期）→ 多个子帧累加成运动模糊 → 整体调色 → 最上面画 HUD
// ============================================================================
const OUT = document.getElementById('cv'), ox = OUT.getContext('2d');
const LAY = mk(), lx = LAY.getContext('2d');
const Q = new URLSearchParams(location.search);
const EXPORT = Q.has('export');
let DEBUG = !EXPORT && Q.has('debug');

// 全片从 -PRE（片头警告）到 MV_END，循环播放时 MV_END 接回 -PRE
const T_A = -PRE, LOOP = MV_END + PRE;
const BREATH = [.21, .17, .09].map(w => Math.max(1, Math.round(w * LOOP / (2 * Math.PI))) * 2 * Math.PI / LOOP);

function drawLayer(t) {
  const s = sceneAt(t), lt = t - s.t0;
  lx.setTransform(1, 0, 0, 1, 0, 0); lx.globalAlpha = 1; lx.globalCompositeOperation = 'source-over'; lx.shadowBlur = 0;
  lx.clearRect(0, 0, W, H);
  const fx = { ...FX0, ...(s.fx ? s.fx.call(s, t, lt) : {}) };
  // 背景跟着鼓点微微跳：底鼓放大再弹回、往下沉一点，踩镲细抖；字不跳。bounce 越大跳得越狠（808 也算时 bounce808 = 1）
  const kb = Math.max(M.hit('kick', t, 9), fx.bounce808 ? M.hit('note', t, 9) : 0), hb = M.hit('hat', t, 28);
  fx.bz = 1 + fx.bounce * (.025 * kb + .005 * hb);
  fx.boff = [0, fx.bounce * .006 * kb];
  if (s.weather) {                                                                 // 同一场雨、同一条街（属于背景，也一起跳）
    lx.save(); lx.translate(W / 2, H / 2 + fx.boff[1] * H); lx.scale(fx.bz, fx.bz); lx.translate(-W / 2, -H / 2);
    drawWorld(lx, t, s.weather.call(s, t, lt)); lx.restore();
  }
  s.draw.call(s, lx, t, lt);
  lx.setTransform(1, 0, 0, 1, 0, 0); lx.globalAlpha = 1;
  polygraph(lx, t, POLY);
  for (const k of ['zoom', 'rot', 'barrel']) if (!Number.isFinite(fx[k])) fx[k] = FX0[k];
  // 镜头一直在呼吸：很轻很慢的整体漂移，像手持摄像机，切镜时画面不会"归零"（频率取成全片长度的整数倍，循环播放时首尾对得上）
  fx.shift = [(fx.shift?.[0] ?? 0) + Math.sin(t * BREATH[0]) * .003, (fx.shift?.[1] ?? 0) + Math.cos(t * BREATH[1]) * .0025];
  fx.rot = fx.rot + Math.sin(t * BREATH[2]) * .004;
  return { s, fx, bg: s.bg ? s.bg.call(s, t, lt) : { mode: 'solid' } };
}

// ---------------------------------------------------------------- 运动模糊
// 快门从这一帧开始往后开（shutter = 快门占一帧的比例，.5 = 180°），一帧里的子帧都落在同一个 1/30 秒的格子里：
// 按帧跳的东西（闪烁、雪花、卡帧、12 帧/秒的雨）在一帧里是同一个状态，不会被糊成两半。
// samples = 'auto'：先渲快门两头，估计这一帧里画面动了多少像素（2D 层缩略图上的梯度光流 + 镜头参数的变化），
// 动得越快补的子帧越多，相邻子帧最多差 MB_STEP 像素，拖影是连续的一条，不会变成几个重影；不动的帧只渲两头。
// 光流用两层：细的一格 6 像素（量小位移，亚像素也量得出），粗的一格 24 像素（量大位移）；整体变亮变暗不算动。
const MB_MIN = 2, MB_MAX = 14, MB_STEP = 4;
const PYR = [{ w: 320, h: 180, b: 8 }, { w: 80, h: 45, b: 4 }].map(L => ({ ...L, s: W / L.w, cv: mk(L.w, L.h) }));
PYR.forEach(L => { L.cx = L.cv.getContext('2d', { willReadFrequently: true }); });
function snap() {                                                                  // 2D 层两层缩略图的亮度（按不透明度预乘）
  return PYR.map(L => {
    L.cx.globalCompositeOperation = 'copy'; L.cx.imageSmoothingQuality = 'high'; L.cx.drawImage(L === PYR[0] ? LAY : PYR[0].cv, 0, 0, L.w, L.h);
    const d = L.cx.getImageData(0, 0, L.w, L.h).data, g = new Float32Array(L.w * L.h);
    for (let i = 0; i < g.length; i++) g[i] = (d[i * 4] * .3 + d[i * 4 + 1] * .59 + d[i * 4 + 2] * .11) * d[i * 4 + 3] / 255;
    return g;
  });
}
function flow(A, B, L) {                                                           // 每块的法向光流 sqrt(Σ It² / Σ |∇I|²)，取最大，单位：全分辨率像素
  let best = 0;
  for (let y0 = 1; y0 + L.b < L.h; y0 += L.b) for (let x0 = 1; x0 + L.b < L.w; x0 += L.b) {
    let n = 0, mt = 0;
    for (let y = y0; y < y0 + L.b; y++) for (let x = x0; x < x0 + L.b; x++) { const p = y * L.w + x; mt += B[p] - A[p]; n++; }
    mt /= n;
    let sg = 0, st = 0;
    for (let y = y0; y < y0 + L.b; y++) for (let x = x0; x < x0 + L.b; x++) {
      const p = y * L.w + x, gx = (A[p + 1] + B[p + 1] - A[p - 1] - B[p - 1]) / 4, gy = (A[p + L.w] + B[p + L.w] - A[p - L.w] - B[p - L.w]) / 4, it = B[p] - A[p] - mt;
      sg += gx * gx + gy * gy; st += it * it;
    }
    if (sg / n < 25) continue;                                                     // 没纹理的块量不出来
    best = Math.max(best, Math.sqrt(st / sg) * L.s);
  }
  return best;
}
function camMotion(a, b) {                                                         // 镜头参数（缩放、旋转、鱼眼、平移、CRT 压扁）造成的最大位移（像素）
  return Math.abs(a.zoom - b.zoom) / Math.max(a.zoom, .01) * 1100 + Math.abs(a.rot - b.rot) * 1100 + Math.abs(a.barrel - b.barrel) * 1150
    + Math.abs(a.shift[0] - b.shift[0]) * W + Math.abs(a.shift[1] - b.shift[1]) * H
    + Math.abs(a.sq[0] - b.sq[0]) / Math.max(a.sq[0], .01) * W / 2 + Math.abs(a.sq[1] - b.sq[1]) / Math.max(a.sq[1], .01) * H / 2;
}
function motionPx(A, B, a, b) {
  if (a.s !== b.s) return 0;                                                       // 快门里正好切镜：两头各一个子帧就够了
  const fine = flow(A[0], B[0], PYR[0]), coarse = flow(A[1], B[1], PYR[1]);
  MB_STAT = { fine: Math.round(fine), coarse: Math.round(coarse) };
  return Math.max(Math.min(fine, 9), coarse, camMotion(a.fx, b.fx));              // 细的一层超过约 1.5 格就不准了，交给粗的一层
}
let MB_LAST = { n: 1, px: 0 }, MB_STAT = null;
// 调色：默认用 LOOK，?look={...} 可以临时换一套（export.mjs --look '{...}'），见 gl.js 的 present()
// 调色：冷调黑色电影——黑更干净、暗部一点蓝黑，高光中性银色，整体略收饱和，血红不褪，青和洋红收一点
const LOOK_DEFAULT = { con: 1.03, curve: .24, shoulder: .14, sat: .83, redKeep: 1, tame: .55, gain: [.99, 1, 1.01], shadow: [0, .004, .016], high: [0, .001, .003] };
const LOOK = Q.has('look') ? JSON.parse(Q.get('look')) : LOOK_DEFAULT;

function render(t, samples = 1, shutter = .5, fps = 30) {
  const span = shutter / fps;
  let head, n;
  const sub = (ts, first) => { const r = drawLayer(ts); GLX.frame(LAY, ts, r.bg, r.fx, 1, first, t); return r; };
  if (samples === 'auto') {
    head = sub(t, true); const A = snap(), tail = sub(t + span, false), B = snap();
    const px = motionPx(A, B, head, tail);
    n = clamp(1 + Math.ceil(px / MB_STEP), MB_MIN, MB_MAX);
    for (let k = 1; k < n - 1; k++) sub(t + span * k / (n - 1), false);
    MB_LAST = { n, px: Math.round(px) };
  } else {
    n = Math.max(1, samples | 0);
    for (let k = 0; k < n; k++) { const r = sub(n > 1 ? t + span * k / (n - 1) : t, k === 0); if (!k) head = r; }
    MB_LAST = { n, px: 0 };
  }
  ox.setTransform(1, 0, 0, 1, 0, 0); ox.globalAlpha = 1;
  const fx = head.fx;
  ox.drawImage(GLX.present(1 / n, fx.lb, LOOK), 0, 0);
  if (fx.hud > 0 && fx.sq[1] > .5) hud(ox, t, { a: fx.hud, lb: fx.lb, tape: fx.tape, mode: fx.hudMode, label: fx.hudLabel, tc: fx.tc ?? t });
  if (DEBUG) drawDebug(t, head.s);
}

function drawDebug(t, s) {
  const b = M.bar(t), { line } = M.lyric(t), k = M.last('kick', t), n = M.note(t);
  ox.save(); ox.fillStyle = 'rgba(0,0,0,.7)'; ox.fillRect(20, 20, 620, 230);
  ox.font = '22px monospace'; ox.fillStyle = '#9f9'; ox.textAlign = 'left'; ox.textBaseline = 'top';
  [`t ${t.toFixed(2)}s   94 BPM   小节 ${b.i} 第 ${Math.floor(b.phase * 16)} 格`, `分镜 ${s.name}  (${s.t0.toFixed(2)}–${Math.min(s.t1, MV_END).toFixed(2)}s)`,
   `底鼓 #${k.i} ${jerk(t).act ?? ''}  ${M.gap(t) ? '■ 静音' : ''}${b.info.fill ? ' 加花' : ''}${b.info.b808 ? '' : ' 无808'}`,
   `808 ${n ? n.midi.toFixed(1) + (n.glide ? ' 滑→' + n.glide : '') : '—'}   人声 ${M.vocal(t).toFixed(2)}`, `歌词 ${line ? line.t.toFixed(2) + 's ' + line.text : '—'}`]
    .forEach((s2, i) => ox.fillText(s2, 36, 34 + i * 38));
  const y0 = H - 54, sx = x => (x - T_A) / LOOP * W;
  SCENES.forEach((s2, i) => { ox.fillStyle = `hsla(${i * 37},60%,50%,.6)`; ox.fillRect(sx(s2.t0), y0, sx(Math.min(s2.t1, MV_END)) - sx(s2.t0) - 1, 10); });
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
window.__info = () => ({ start: T_A, duration: MV_END, title: D.title });           // start < 0：片头警告，那几秒没有声音（export.mjs 把音轨往后推）
// samples：子帧数或 'auto'；fps：输出帧率（决定快门时长）
window.__frame = (t, samples = 'auto', png = false, fps = 30, shutter = .5, q = .92) => { render(t, samples, shutter, fps); return png ? OUT.toDataURL('image/png') : OUT.toDataURL('image/jpeg', q); };
window.__mb = () => MB_LAST;

// ---------------------------------------------------------------- 播放器
// 时间从 -PRE 开始：片头警告那几秒还没有声音，用自己的钟走，走到 0 再放音轨；放到结尾接回片头继续放（首尾是接上的）。
// 进度条和时间显示的是视频时间（从片头算起）。
const au = document.getElementById('au'), pp = document.getElementById('pp'), seek = document.getElementById('seek'), tm = document.getElementById('time');
const fmt = s => `${Math.floor(s / 60)}:${(s % 60).toFixed(1).padStart(4, '0')}`;
if (EXPORT) document.body.classList.add('export');
else {
  let playing = false, pre = T_A, preAt = 0, ticking = false;                     // pre < 0：还在片头，pre 是上次起步（或暂停）时的时间
  const now = () => pre < 0 ? (playing ? pre + (performance.now() - preAt) / 1000 : pre) : au.currentTime;
  const go = t => {                                                                // 跳到 t
    t = clamp(t, T_A, MV_END);
    if (t < 0) { pre = t; preAt = performance.now(); if (!au.paused) au.pause(); au.currentTime = 0; }
    else { pre = 0; au.currentTime = t; if (playing && au.paused) au.play(); }
  };
  const draw = () => {
    let t = now();
    if (pre < 0 && t >= 0) { go(t); t = now(); }                                   // 片头走完：开始放音轨
    if (t >= MV_END) { go(T_A + (t - MV_END)); t = now(); }                         // 放到结尾：接回片头
    render(t, 1);
    seek.value = (t - T_A) / LOOP * 1000;
    tm.textContent = `${fmt(t - T_A)} / ${fmt(LOOP).slice(0, -2)}`;
  };
  const tick = () => { ticking = false; draw(); if (playing) kick(); };
  const kick = () => { if (!ticking) { ticking = true; requestAnimationFrame(tick); } };
  const ui = () => { document.body.classList.toggle('paused', !playing); pp.textContent = playing ? '❚❚' : '▶'; };
  const play = () => { if (pre < 0) preAt = performance.now(); else au.play(); playing = true; ui(); kick(); };
  const pause = () => { if (pre < 0) pre = now(); playing = false; au.pause(); ui(); draw(); };
  const toggle = () => playing ? pause() : play();
  au.onended = () => { if (playing) { go(T_A); kick(); } };                        // 音轨比 MV_END 短一点点时也接回片头
  pp.onclick = toggle; OUT.onclick = toggle;
  seek.oninput = () => { go(T_A + seek.value / 1000 * LOOP); draw(); };
  document.getElementById('dbg').onclick = () => { DEBUG = !DEBUG; draw(); };
  addEventListener('keydown', e => {
    if (e.code === 'Space') { e.preventDefault(); toggle(); }
    else if (e.key === 'd' || e.key === 'D') { DEBUG = !DEBUG; draw(); }
    else if (e.key === 'ArrowLeft') { go(now() - (e.shiftKey ? 1 : 5)); draw(); }
    else if (e.key === 'ArrowRight') { go(now() + (e.shiftKey ? 1 : 5)); draw(); }
    else if (e.key === ',') { go(now() - 1 / 30); draw(); }
    else if (e.key === '.') { go(now() + 1 / 30); draw(); }
  });
  fontsReady.then(draw);
}
