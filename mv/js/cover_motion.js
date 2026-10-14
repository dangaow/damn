'use strict';
// ============================================================================
// 《谎话》动态封面：j · 雨夜字幕，正方形无缝循环。cover_motion.html?s=边长（默认 1080）&p=周期秒（默认 10）
// 版式完全照 cover.js 的 coverJ（同样的随机种子、同样的字和角标），只让这些东西动起来：
//   雨（每根雨丝的速度取成「一个周期正好落整数圈」）、光斑呼吸和轻微飘动、一对路过的车灯、浮尘、
//   字幕的光晕起伏、「谎话」中途套色错开一下、REC 闪、时间码走字。
// 所有周期运动都是 P 的整数分之一，所以第 0 帧和第 P 秒那帧完全接得上；
// 时间码在循环点跳回去，这一下藏在一次很短的录像带跳帧里（循环点前后各两帧）。
// 导出：node cover_motion_export.mjs
// ============================================================================
const P = +Q.get('p') || 10, TAU = Math.PI * 2, K = H / 1080, FPS = 30;

// 循环的雨：同一套随机数，速度取整到「一个周期落 laps 圈」
function loopRain(c, t, o) {
  const r = rand(o.seed), span = H + 400 * K, v = o.v * K;
  c.save(); c.strokeStyle = o.col; c.lineCap = 'round';
  for (let i = 0; i < o.n; i++) {
    const x0 = r() * (W + 800 * K) - 400 * K, sp = .6 + r() * .7, len = o.len * (.5 + r()), ph = r(), a = o.alpha * (.3 + r() * .7), lw = o.lw * (.8 + r() * 1.8);
    const laps = Math.max(1, Math.round(P * v * sp / span));
    const y = mod(ph * span + t / P * laps * span, span) - 200 * K, x = x0 - y * o.wind;
    c.globalAlpha = a; c.lineWidth = lw; c.beginPath(); c.moveTo(x, y); c.lineTo(x - len * o.wind, y + len); c.stroke();
  }
  c.restore();
}
// 循环的浮尘：原地轻轻晃，闪烁频率取整到周期
function loopDust(c, t, n, seed, a, col) {
  const r = rand(seed); c.save(); c.fillStyle = col;
  for (let i = 0; i < n; i++) {
    const x = r() * W + Math.sin(TAU * t / P + r() * 6) * (6 + r() * 14) * K, y = r() * H + Math.sin(TAU * 2 * t / P + r() * 6) * (4 + r() * 10) * K, s = .6 + r() * 2.4;
    const m = 1 + Math.round(r() * 3), ph = r() * 6;
    c.globalAlpha = a * (.3 + .7 * (.5 + .5 * Math.sin(TAU * m * t / P + ph))); c.fillRect(x, y, s, s);
  }
  c.restore();
}
// 一对车灯从左往右滑过（周期中段出现，两头都在画外，所以循环不露馅）
function carLights(c, t, y0, y1) {
  const t0 = 1.6, t1 = 8.6; if (t < t0 || t > t1) return;
  const k = (t - t0) / (t1 - t0), x = -260 * S + k * (W + 520 * S), y = y0 + (y1 - y0) * .8, env = Math.sin(Math.PI * k);
  c.save(); c.globalCompositeOperation = 'lighter';
  [[0, 1], [150 * S, .85]].forEach(([dx, w]) => {
    const R = 130 * S, rg = c.createRadialGradient(x - dx, y, 0, x - dx, y, R), a = .17 * env * w;
    rg.addColorStop(0, `rgba(255,214,160,${a})`); rg.addColorStop(.55, `rgba(255,170,95,${a * .55})`); rg.addColorStop(1, 'rgba(255,150,80,0)');
    c.fillStyle = rg; c.beginPath(); c.arc(x - dx, y, R, 0, 7); c.fill();
  });
  c.restore();
}
const tcText = t => {                                   // 60 帧的时间码，从 00:01:18:42 往后走
  const f = 78 * 60 + 42 + Math.floor(t * 60), p2 = n => String(n).padStart(2, '0');
  return `TC 00:${p2(Math.floor(f / 3600))}:${p2(Math.floor(f / 60) % 60)}:${p2(f % 60)}`;
};

function coverJMotion(c, t) {
  const bar = 290 * S, y0 = bar, y1 = H - bar, phase = TAU * t / P;
  c.fillStyle = '#000'; c.fillRect(0, 0, W, H);
  const IMG = mk(), g = IMG.getContext('2d');
  const grd = g.createLinearGradient(0, y0, 0, y1); grd.addColorStop(0, '#040a1e'); grd.addColorStop(.6, '#0a1638'); grd.addColorStop(1, '#122150');
  g.fillStyle = grd; g.fillRect(0, 0, W, H);
  g.globalCompositeOperation = 'lighter';
  const r = rand(14), q = rand(1414);
  for (let i = 0; i < 46; i++) {
    const x = r() * W, y = y0 + r() * (y1 - y0), R = (40 + r() * r() * 170) * S, col = r() < .8 ? '90,124,255' : '200,220,255', a0 = .05 + r() * .18;
    const m = 1 + (q() * 2 | 0), ph = q() * TAU, ph2 = q() * TAU, dx = (8 + q() * 18) * S;
    const a = a0 * (1 + .32 * Math.sin(m * phase + ph)), xx = x + dx * Math.sin(phase + ph2);
    const rg = g.createRadialGradient(xx, y, 0, xx, y, R); rg.addColorStop(0, `rgba(${col},${a})`); rg.addColorStop(.65, `rgba(${col},${a * .6})`); rg.addColorStop(1, `rgba(${col},0)`);
    g.fillStyle = rg; g.beginPath(); g.arc(xx, y, R, 0, 7); g.fill();
  }
  g.globalCompositeOperation = 'source-over';
  carLights(g, t, y0, y1);
  loopRain(g, t, { n: 340, v: 1700, len: 150 * S, alpha: .32, wind: .2, seed: 9, lw: 1.8 * S, col: 'rgb(170,190,255)' });
  loopRain(g, t, { n: 90, v: 2300, len: 260 * S, alpha: .2, wind: .2, seed: 21, lw: 3.2 * S, col: 'rgb(200,214,255)' });
  loopDust(g, t, 260, 5, .35, '#cfe0ff');
  c.save(); c.beginPath(); c.rect(0, y0, W, y1 - y0); c.clip(); c.drawImage(IMG, 0, 0);

  // 字幕：光晕慢慢起伏；6.0–6.27 秒「谎话」套色错开、抖两下（像谎话被看穿的那一下）
  const sz = 74 * S, ym = (y0 + y1) / 2 + 10 * S, a1 = '就别再想着再跟我说', a2 = '谎话';
  c.font = fnt(sz, 400, SERIF); const w1 = c.measureText(a1).width; c.font = fnt(sz * 1.12, 900, SANS); const w2 = c.measureText(a2).width;
  const x0 = W / 2 - (w1 + w2 + 8 * S) / 2;
  const breathe = .5 + .5 * Math.sin(2 * phase - Math.PI / 2), hit = t >= 6 && t < 6.27, fi = Math.floor(t * FPS);
  c.shadowColor = 'rgba(125,147,255,.9)'; c.shadowBlur = 22 * S * (.85 + .3 * breathe);
  misText(c, a1, x0, ym, sz, { align: 'left', w: 400, fam: SERIF, fill: '#cfe0ff', d: 2, ga: .5, g1: 'rgba(255,60,90,1)', g2: 'rgba(80,200,255,1)' });
  c.shadowColor = 'rgba(220,230,255,.9)'; c.shadowBlur = 28 * S * (.8 + .45 * breathe);
  const jx = hit ? (rand(fi * 7 + 3)() - .5) * 10 * S : 0;
  misText(c, a2, x0 + w1 + 8 * S + jx, ym, sz * 1.12, { align: 'left', fill: '#ffffff', d: hit ? 9 + (fi % 3) * 3 : 3, ga: hit ? .9 : .55 });
  c.restore();
  // 下黑边：歌名 + 署名；上黑边：录像带角标（REC 每秒亮 0.6 秒，时间码在走）
  c.save(); c.textBaseline = 'alphabetic';
  misText(c, '谎话', 76 * S, y1 + 150 * S, 150 * S, { align: 'left', base: 'alphabetic', fill: '#f2f1ee', d: 4, ga: .6 });
  c.font = `${80 * S}px "${GOTH}"`; c.textAlign = 'right'; c.fillStyle = 'rgba(242,241,238,.9)'; c.fillText('dangao_w', W - 76 * S, y1 + 140 * S);
  c.restore();
  tapeHud(c, { col: '#e4e7ec', a: .8, fs: 31 * S, top: bar / 2, bot: H - 72 * S, tc: tcText(t), url: 'makestudio.cn', dot: mod(t, 1) < .6 ? C.red : 'rgba(0,0,0,0)' });
  // 循环点（最后两帧 + 最开头两帧）：一下录像带跳帧
  const fr = Math.round(t * FPS), N = Math.round(P * FPS), seam = fr < 2 || fr >= N - 2, sk = seam ? 1 : 0;
  return { grade: 'cold', gradeMix: .25, bloom: .35, bloomThr: .6, ca: 0, grain: .24, vig: .8, scan: .05,
    tear: sk * .55, glitch: sk * .06, gseed: fi % 7 + 1, split: sk * 1.1, shift: [0, sk * (fi % 2 ? -3 : 4) * K / H] };
}

function frameAt(t) {
  lx.setTransform(1, 0, 0, 1, 0, 0); lx.globalAlpha = 1; lx.globalCompositeOperation = 'source-over'; lx.filter = 'none';
  lx.clearRect(0, 0, W, H);
  const base = coverJMotion(lx, t);
  const fx = { zoom: 1, rot: 0, barrel: 0, shift: [0, 0], sq: [1, 1], split: 0, glitch: 0, gseed: 1, wave: 0, tear: 0, invert: 0, xerox: 0,
    flash: 0, flashCol: '#ffffff', dark: 0, scan: 0, lb: 0, ...base };
  GLX.frame(LAY, t, { mode: 'solid', a: '#000000' }, fx, 1, true);
  ox.setTransform(1, 0, 0, 1, 0, 0); ox.drawImage(GLX.present(), 0, 0);
}
window.__info = () => ({ P, FPS, W, H });
window.__frame = t => ready.then(() => { frameAt(t); return OUT.toDataURL('image/png'); });
// 直接在浏览器里打开就是实时预览
if (!Q.has('export')) ready.then(() => { const t0 = performance.now(); const loop = () => { frameAt(mod((performance.now() - t0) / 1000, P)); requestAnimationFrame(loop); }; loop(); });
