'use strict';
// ============================================================================
// 分镜（见 STORYBOARD.md）。每个场景：
//   t0..t1  起止秒数
//   bg(t)   GL 动态背景参数 { mode, a, b, c, amt, speed, pulse, mirror }
//   draw(c, t, lt)  2D 层：先画"字后面"的特效，再画字，最后画"字前面"的特效
//   fx(t, lt)       后期参数（覆盖 FX0）
// ============================================================================
const FX0 = {
  zoom: 1, rot: 0, barrel: .03, shift: [0, 0], sq: [1, 1], ca: 1, split: 0, glitch: 0, gseed: 0, wave: 0, tear: 0,
  invert: 0, xerox: 0, grade: 'none', gradeMix: 0, bloom: .4, bloomThr: .62, flash: 0, flashCol: '#ffffff', dark: 0,
  grain: .08, scan: .14, vig: .75, lb: 0, hud: 0, tape: 'TAPE 01', hudMode: '▶ PLAY', tc: null,
  bounce: 0, bounce808: false,                                                  // 背景跟着鼓点跳的幅度（只在最后鼓点回来那段打开）
};
const SCENES = [];
const scene = o => (SCENES.push(o), o);
function sceneAt(t) { for (let i = SCENES.length - 1; i >= 0; i--) if (t >= SCENES[i].t0) return SCENES[i]; return SCENES[0]; }
const BARS = D.bars.map(b => b.t);
const barT = i => D.bar0 + i * BAR;

// jerk 镜头：猛推（放大 + 鱼眼 + 轻微旋转）
function cam(t, s = 1) {
  const j = jerk(t); if (j.i < 0) return {};
  const p = j.act === 'punch' ? Math.exp(-j.since * 13) : j.act === 'cut' ? Math.exp(-j.since * 22) * .35 : 0;
  return { zoom: 1 + .1 * p * s, barrel: .03 + .28 * p * s, rot: (hash(j.i, 4) - .5) * .05 * p * s, shift: [(hash(j.i, 5) - .5) * .012 * p * s, (hash(j.i, 6) - .5) * .012 * p * s] };
}
// ---------------------------------------------------------------- 世界：整支 MV 是同一个下雨的夜晚
// 同一场雨（同一个种子、同样的速度和角度，切镜时雨滴位置是连续的），同一条街（路灯光斑位置固定）。
// 每个场景用 weather(t, lt) 决定这场雨此刻的样子：{ rain 透明度, col 颜色, len 长度倍数, n 雨滴数, rt 雨的时间（冻住时传固定值）, wind, street 路灯透明度 }
const STREET_COLS = ['90,124,255', '90,124,255', '130,160,255', '255,170,90'];
function street(c, t, a) { if (a > .005) bokeh(c, t, { n: 16, seed: 5, cols: STREET_COLS, alpha: a }); }
function worldRain(c, t, w) {
  if (!w || !(w.rain > .005)) return;
  rain(c, w.rt ?? t, { n: w.n ?? 260, spd: 1700, len: 60 * (w.len ?? 1), alpha: w.rain, wind: w.wind ?? .22, seed: 1, col: `rgb(${w.col ?? '170,190,255'})`, lw: w.lw ?? 1 });
}
function drawWorld(c, t, w) { if (!w) return; street(c, t, w.street ?? 0); worldRain(c, t, w); }

// 黑边从上下滑进来
const lbIn = (lt, d = .35) => easeOut(lt / d);
// 文字特效：切片错位（在 fn 画的东西上横切 n 条，各自错开）
function sliced(c, n, amt, seed, top, h, fn) {
  if (amt <= .01) { fn(); return; }
  for (let i = 0; i < n; i++) {
    c.save(); c.beginPath(); c.rect(-W, top + h * i / n, W * 3, h / n + 1); c.clip();
    c.translate((hash(i, seed) - .5) * 2 * amt, 0); fn(); c.restore();
  }
}
// 硬切出现：在鼓点上直接出现，只有 3 帧极轻的落定和一点点抖；冲击力交给镜头猛推和曝光闪
function cutIn(c, t, t0, seed, fn, o = {}) {
  const k = Math.max(0, t - t0), s = 1 + .035 * Math.exp(-k * 30), sh = (o.shake ?? 12) * Math.exp(-k * 18);
  c.save(); c.translate(o.x ?? W / 2, o.y ?? H / 2); c.translate((hash(seed, 1) - .5) * sh, (hash(seed, 2) - .5) * sh); c.scale(s, s); fn(); c.restore();
}
// 闪 n 帧
const blink = (since, n = 2) => since >= 0 && since < n / 30 ? 1 : 0;
// CRT 关机：k 0..1，先压成横线再缩成点
const crt = k => k <= 0 ? [1, 1] : [lerp(1, .002, ease((k - .55) / .45)), lerp(1, .004, ease(k / .55))];

// ---------------------------------------------------------------- 测谎仪状态
const WOAH_T = D.lyrics.filter(l => l.text === 'woah').map(l => l.t);
function POLY(t) {
  if (t < .6) return { a: 0, amp: 0, lie: 0, spike: 0 };
  const s = { a: .85, amp: 1, lie: 0, spike: 0 };
  if (t < 1.2) s.a = (t - .6) / .6 * .85;
  if (t >= 41.88 && t < 44.26) { s.lie = 1; s.amp = 1.8; }                     // "你总说……"
  if (t >= 62.9 && t < 73.6) s.a = .85 * clamp(1 - (t - 62.9) / .4) + .85 * clamp((t - 73.2) / .4);   // 回忆里没有测谎仪
  if (t >= 80.0 && t < 80.4) s.amp = 0;                                          // 鼓停，针也停
  if (t >= 80.4) s.spike = Math.exp(-(t - 80.4) * 2.2);                          // 点题：冲顶
  for (const w of WOAH_T) if (t >= w && t < w + .8) { s.spike = Math.max(s.spike, .8 * Math.exp(-(t - w) * 2.5)); s.lie = .6; }   // woah：针冲上去写字
  if (t >= 88.38 && t < 95.8) { s.lie = .6; s.amp = 1.4; }
  if (t >= 95.8) s.amp = clamp(1 - (t - 95.8) / 8);
  if (t >= T_ZERO) { s.amp = 0; s.a = .85 * clamp(1 - (t - T_OFF) / .5); }      // 数到 0：归零、拉平
  return s;
}

// ================================================================ 0 · 开机
scene({ name: '开机', t0: 0, t1: D.bar0,
  bg: (t, lt) => ({ mode: 'static', a: '#000000', amt: lt > 1.08 ? .55 : 0 }),
  draw(c, t, lt) {
    if (lt < 1.08) {
      const w = W * (.15 + .85 * easeOut(lt / .35)), h = 2 + hash(frameNo(t), 3) * 2;
      c.fillStyle = `rgba(255,255,255,${.6 + .4 * hash(frameNo(t), 4)})`; c.fillRect((W - w) / 2, H / 2 - h / 2, w, h);
      if (lt > .3 && frameNo(t) % 5) text(c, `TAPE 01  ·  ${D.artist}`, W / 2, H / 2 - 40, 24, { w: 400, fam: MONO, fill: 'rgba(255,255,255,.7)' });
    }
  },
  fx: (t, lt) => ({ grain: .25, scan: .3, hud: .7, flash: lt > 1.08 && lt < 1.16 ? .7 : 0, glitch: lt > 1.08 ? .3 : 0, gseed: frameNo(t) }),
});

// ================================================================ 1 · 入场：铬金属「谎话」
const T_MELT = D.notes808.find(n => n.t > 8.5 && n.midi > 36.5).t;                  // 第一次加花：808 连砸 D
const T_UP = D.notes808.find(n => n.t > T_MELT && n.midi > 43).t;                    // 跳高八度
scene({ name: '谎话', t0: D.bar0, t1: T_MELT,
  weather: t => ({ rain: M.gap(t) ? 0 : .13, col: '205,212,230', rt: frozen(t) }),
  bg: t => ({ mode: 'tunnel', a: '#020309', b: '#2c3f96', c: '#0d1638', amt: .9, speed: 1 + M.low(t) * 1.4, pulse: M.hit('kick', t, 8) }),
  draw(c, t, lt) {
    if (M.gap(t)) return;
    const te = frozen(t), bp = M.bar(te).phase, sweep = bp * 2.2 - .1;
    scrollRows(c, te, D.artist, { size: 150, rows: 5, spd: 70, col: 'rgba(160,180,255,.06)', fam: GOTH, w: 400 });
    if (lt < .4) {                                                                  // 转场：开机的白线炸开成一圈方框
      const k = easeOut(lt / .4), w = W * (.55 + k * .75), h = lerp(3, H * 1.3, k);
      c.strokeStyle = `rgba(255,255,255,${1 - k})`; c.lineWidth = 6 * (1 - k) + 1; c.strokeRect((W - w) / 2, (H - h) / 2, w, h);
    }
    const v = cuts(te, this.t0) % 5;
    c.save();
    if (v === 0) chrome(c, D.title, W / 2, H / 2, 600, { sweep });
    else if (v === 1) chrome(c, D.title, W * .36, H / 2 + 40, 900, { sweep });
    else if (v === 2) chrome(c, '谎', W * .62, H * .58, 1500, {});                                  // 极近特写：只露半个谎字
    else if (v === 3) { chrome(c, '谎', W * .3, H * .34, 470, { sweep }); chrome(c, '话', W * .66, H * .66, 470, { sweep: sweep - .3 }); }
    else { text(c, D.title, W / 2, H / 2, 1000, { fill: null, stroke: 'rgba(255,255,255,.16)', lw: 3 }); chrome(c, D.title, W / 2, H / 2, 300, { sweep }); }
    c.restore();
    // dangao_w 比"谎话"慢一帧跟上
    const k = M.last('kick', te), lag = k.since < 1 / 30 ? 0 : 1;
    if (lag) chrome(c, D.artist, W - 120, H - 190, 74, { fam: GOTH, w: 400, align: 'right' });
    // 字前面：踩镲 = 横向扫描条
    if (M.last('hat', t).since < .05) bars(c, 3, M.last('hat', t).i, '#fff', .35);
  },
  fx(t) {
    return { ...cam(frozen(t)), ca: 1.5 + M.hit('kick', t, 9) * 6, grade: 'silver', gradeMix: .45, bloom: .55, xerox: .12,
      split: M.hit('hat', t, 30) * 1.5, dark: M.gap(t) ? .92 : 0, hud: .7 };
  },
});

scene({ name: '融化', t0: T_MELT, t1: D.notes808.find(n => n.t > T_UP).t,
  weather: t => ({ rain: M.gap(t) ? 0 : .13, col: '205,212,230', rt: frozen(t) }),
  bg: t => ({ mode: 'tunnel', a: '#020309', b: '#4a62d8', c: '#16204a', amt: 1, speed: 2.2 + M.hit('note', t, 4) * 3, pulse: M.hit('note', t, 7) }),
  draw(c, t, lt) {
    // 先把铬金属字画到 SNAP，再按列往下拖（融化）
    const b = chromeBase(D.title, 600, SANS, 900);
    snx.setTransform(1, 0, 0, 1, 0, 0); snx.clearRect(0, 0, W, H);
    chrome(snx, D.title, W / 2, H / 2, 600, { sweep: lt / 1.5 });
    const x0 = (W - b.w) / 2, y0 = (H - b.h) / 2;
    const k = clamp(lt / (T_UP - T_MELT)) * .9 + M.count('note', this.t0, t) * .05;
    const up = t > T_UP ? easeIn((t - T_UP) / .3) * H * 1.3 : 0;
    c.save(); c.translate(0, -up); melt(c, SNAP, x0, y0, b.w, b.h, k, 3); c.restore();
    if (M.last('note', t).since < .06) bars(c, 4, M.last('note', t).i + 50, '#cfe0ff', .4);
  },
  fx(t) {
    const n = M.hit('note', t, 12);
    return { zoom: 1 + n * .1, barrel: .05 + n * .3, ca: 2 + n * 8, grade: 'silver', gradeMix: .5, bloom: .7, hud: .7,
      flash: t > T_UP ? Math.exp(-(t - T_UP) * 10) * .8 : Math.exp(-(t - T_MELT) * 18) * .6, glitch: n * .3, gseed: M.last('note', t).i };
  },
});

// 意识流：前奏后半段不出现字。脑子里乱成一团：一堆物件（手机、钟、月亮、眼睛、镜子、戒指、钥匙、门、打火机、烟、面具、小丑牌、伞）
// 大的大、小的小、倒着的、翻着的、叠在一起乱飞，有的一闪一闪；镜头也是乱的（甩、翻滚、急推急拉、倒过来、手持乱晃），
// 每次切镜换一个不同的背景（蓝隧道、雪花、红色放射线、纯红、网点、纯白、烟、夜雨……）。
// 底鼓「猛推」「硬切」= 切到下一团（卡帧的那一下停住 3 帧）；踩镲 = 闪一下（反色 / 白闪 / 黑一帧）。
// 14.10 那次 808 往下滑：镜子裂开，所有东西往下掉。第 6 小节东西越来越多；静音之后时间倒流（东西往回飞、雨往天上飞、◀◀）。
// 最后一小节每个踩镲切一次，最后所有东西被吸进画面中心 → 白点 → TAPE 01 → CRT 关机（这个点变成雨夜的第一滴雨）。
const T_THINK = D.notes808.find(n => n.t > T_UP).t;
const T_STACK = barT(7), T_MORE = barT(6);
const T_CRT1 = D.lyrics[0].t - .24, T_SUCK = T_CRT1 - .62;
const GLIDE1 = D.notes808.find(n => n.glide && n.t > T_THINK && n.t < T_STACK);
const REV = (() => { const g = D.gaps.find(g => g[0] > T_MORE && g[1] < T_STACK); return g ? [g[1], T_STACK] : [T_STACK, T_STACK]; })();
const PALS = [
  { bg: { mode: 'tunnel', a: '#020309', b: '#2c3f96', c: '#0d1638', amt: .95, speed: 2.6 } },
  { bg: { mode: 'static', a: '#000000', amt: .4 } },
  { bg: { mode: 'smoke', a: '#0a0003', b: '#6e0a1a', c: '#ff3a55', amt: 1, speed: 4 }, acc: '#ffffff' },          // 暗红流动烟雾（和「连击」同一个世界）
  { bg: { mode: 'solid', a: '#e0112b' }, ink: '#05060a', acc: '#ffffff', glow: null },
  { bg: { mode: 'halftone', a: '#05060a', b: '#8d99b8', amt: 1, speed: 4 } },
  { bg: { mode: 'solid', a: '#e9edf5' }, ink: '#05060a', acc: '#e0112b', glow: null },
  { bg: { mode: 'smoke', a: '#020309', b: '#1d2f78', c: '#8aa0ff', amt: 1, speed: 4 } },
  { bg: { mode: 'tunnel', a: '#0a0002', b: '#9a0c22', c: '#24000a', amt: 1, speed: 4 }, ink: '#ffe3e7' },
  { bg: { mode: 'night', a: '#010309', b: '#0c1a3c', c: '#4a6cff', amt: 1, speed: 4 } },
];
const PAL_GLIDE = { bg: { mode: 'smoke', a: '#0d0003', b: '#8a0c20', c: '#ff5a70', amt: 1.1, speed: 3, mirror: true }, acc: '#ffffff' };   // 镜子裂开：左右对称的红烟，像墨迹测试（提前呼应「病态」）
const MOVES = ['whip', 'roll', 'crash', 'spin', 'shake', 'upside', 'push', 'pull', 'whipV'];
// 每一团（一个镜头）：从哪一刻开始、用哪个背景、什么运镜、哪些物件
const MESS = (() => {
  const g = GLIDE1 ? GLIDE1.t : -1, ts = [[T_THINK, 'start']];
  D.kicks.forEach((k, i) => { if (k > T_THINK + .05 && k < T_STACK - .05 && JERK[i % 3] !== 'freeze' && Math.abs(k - g) > .05) ts.push([k, 'kick']); });
  if (g > 0) ts.push([g, 'glide']);
  if (REV[0] < REV[1]) ts.push([REV[0], 'kick']);
  D.hats.forEach(h => { if (h >= T_STACK - .02 && h < T_SUCK) ts.push([h, 'hat']); });
  ts.sort((a, b) => a[0] - b[0]);
  const out = []; let lastPal = -1;
  ts.forEach(([t, kind]) => { if (out.length && t - out[out.length - 1].t < .06) return;
    const i = out.length, r = rand(i * 131 + 7);
    let pal = Math.floor(r() * PALS.length); if (pal === lastPal) pal = (pal + 1 + Math.floor(r() * 3)) % PALS.length; lastPal = pal;
    const many = t >= T_MORE, rush = t >= T_STACK - .02, objs = [];
    const pick = () => OBJ_NAMES[Math.floor(r() * OBJ_NAMES.length)];
    const add = (o) => objs.push({ r: (r() - .5) * 2.4, spin: (r() - .5) * (rush ? 6 : 3), vx: (r() - .5) * 500, vy: (r() - .5) * 350,
      flip: r() < .3 ? 2 + r() * 6 : 0, upside: r() < .25, far: false, flick: r() < .3, echo: r() < .3, seed: Math.floor(r() * 999), ...o });
    if (kind !== 'glide' && r() < .6) add({ name: pick(), x: r() < .5 ? W * (.05 + r() * .2) : W * (.75 + r() * .2), y: H * (.2 + r() * .6), s: 2.6 + r() * 1.6,
      far: true, flick: false, echo: false, vx: (r() - .5) * 120, vy: (r() - .5) * 80, spin: (r() - .5) * .6 });   // 背后一个巨大的、糊掉的
    add({ name: kind === 'glide' ? 'mirror' : pick(), x: W * (.3 + r() * .4), y: H * (.32 + r() * .36), s: kind === 'glide' ? 1.3 : 1.1 + r() * 1.1,
      vx: (r() - .5) * 200, vy: (r() - .5) * 140, spin: kind === 'glide' ? 0 : (r() - .5) * 1.5, r: kind === 'glide' ? 0 : (r() - .5) * 1.4, flick: false, upside: kind !== 'glide' && r() < .25 });
    const sw = pick(), nsw = rush ? 6 + Math.floor(r() * 5) : many ? 8 + Math.floor(r() * 6) : 4 + Math.floor(r() * 4);   // 一群一样的小东西乱飞
    for (let j = 0; j < nsw; j++) add({ name: sw, x: W * (.04 + r() * .92), y: H * (.08 + r() * .84), s: .1 + r() * .22, spin: (r() - .5) * 8, vx: (r() - .5) * 1000, vy: (r() - .5) * 600, echo: r() < .5 });
    const nmid = many || rush ? 3 + Math.floor(r() * 3) : 2 + Math.floor(r() * 3);
    for (let j = 0; j < nmid; j++) add({ name: pick(), x: W * (.08 + r() * .84), y: H * (.12 + r() * .76), s: .32 + r() * .55, far: r() < .3 });
    out.push({ t, kind, i, pal: kind === 'glide' ? PAL_GLIDE : PALS[pal], move: kind === 'start' ? 'rise' : MOVES[Math.floor(r() * MOVES.length)],
      dir: r() < .5 ? -1 : 1, split: kind !== 'glide' && PALS[pal].glow !== null && r() < .2 ? Math.floor(r() * 3) : -1, objs });
  });
  return out;
})();
function messAt(t) { const i = Math.max(0, lastAt(MESS, t, m => m.t)); return MESS[i]; }
// 混乱的运镜：返回这一刻物件层的位移 / 旋转 / 缩放
function messCam(m, k, t) {
  const d = m.dir, e = (dur) => 1 - easeOut(clamp(k / dur));
  switch (m.move) {
    case 'rise': return { y: H * 1.1 * e(.22), r: 0, s: 1 };
    case 'whip': return { x: d * W * .95 * e(.16) - d * 80 * k, r: d * .04, s: 1.05 };
    case 'whipV': return { y: d * H * .9 * e(.16) - d * 60 * k, r: -d * .06, s: 1.05 };
    case 'roll': return { r: d * (.25 + k * .8), s: 1.12 };
    case 'crash': return { s: lerp(2.8, 1, 1 - e(.2)) + k * .06, r: d * .05 };
    case 'spin': return { r: d * Math.PI * e(.3), s: 1 + .4 * e(.3) };
    case 'shake': { const f = frameNo(t); return { x: (hash(f, 71) - .5) * 70, y: (hash(f, 72) - .5) * 50, r: (hash(f, 73) - .5) * .09, s: 1.08 }; }
    case 'upside': return { r: Math.PI + d * k * .2, s: 1.05 };
    case 'push': return { s: 1 + k * .5, r: d * k * .1 };
    case 'pull': return { s: 1.7 - Math.min(k, 1.2) * .55, r: -d * k * .06 };
  }
  return {};
}
function drawMess(c, t, te) {
  const m = messAt(te); let k = te - m.t;
  const rev = te >= REV[0] && te < REV[1], km = rev ? Math.max(0, 1.1 - (te - REV[0]) * 1.6) : k;   // 倒流：东西往回飞
  const suck = clamp((te - T_SUCK) / .5), su = easeIn(suck);
  const cm = messCam(m, k, t), pal = m.pal;
  if (m.split >= 0) {                                                            // 背景也是乱的：一半换成别的颜色
    c.save(); c.fillStyle = m.split === 1 ? '#000000' : '#e0112b'; c.beginPath();
    if (m.split === 0) c.rect(0, 0, W / 2 + Math.sin(t * 3) * 40, H);
    else if (m.split === 1) { c.moveTo(W * .3, 0); c.lineTo(W, 0); c.lineTo(W, H); c.lineTo(W * .7, H); }
    else c.rect(0, H * .5, W, H * .5);
    c.fill(); c.restore();
  }
  c.save();
  c.translate(W / 2 + (cm.x ?? 0), H / 2 + (cm.y ?? 0)); c.rotate((cm.r ?? 0) + su * 4); c.scale((cm.s ?? 1) * (1 - su * .2), (cm.s ?? 1) * (1 - su * .2)); c.translate(-W / 2, -H / 2);
  const f = frameNo(t);
  m.objs.forEach(o => {
    if (o.flick && hash(f, o.seed) < .45) return;                               // 一闪一闪
    const at = kk => {
      let x = o.x + o.vx * kk, y = o.y + o.vy * kk + (m.kind === 'glide' ? 2600 * kk * kk * .5 * (o.name === 'mirror' ? .25 : 1) : 0);   // 808 往下滑：东西往下掉
      x = lerp(x, W / 2, su); y = lerp(y, H / 2, su);
      return { x, y, r: o.r + o.spin * kk, s: o.s * (1 - su), sx: o.flip ? Math.sign(Math.cos(kk * o.flip) || 1) * Math.max(.06, Math.abs(Math.cos(kk * o.flip))) : 1,
        sy: (o.upside ? -1 : 1) * (m.kind === 'glide' ? 1 + Math.min(km, .5) * 1.4 : 1) };
    };
    const base = { ink: pal.ink, acc: pal.acc, glow: pal.glow, crack: o.name === 'mirror' && m.kind === 'glide' ? clamp(km / .22) : 0 };
    if (o.far) { c.save(); c.filter = 'blur(5px)'; }
    if (o.echo) for (let e = 3; e >= 1; e--) drawObj(c, o.name, t, km, { ...base, ...at(Math.max(0, km - e * .06)), a: (o.far ? .45 : 1) * .22 / e });
    drawObj(c, o.name, t, km, { ...base, ...at(km), a: o.far ? .45 : 1 });
    if (o.far) c.restore();
  });
  c.restore();
  return { m, k, cm, su };
}
const messWeather = t => {
  const te = frozen(t);
  if (te >= REV[0] && te < REV[1]) return { rain: .13, col: '205,212,230', rt: REV[0] - (te - REV[0]) * 1.4 };   // 时间倒流：雨往天上飞
  return { rain: M.gap(t) ? 0 : .13, col: '205,212,230', rt: te };
};
const messBG = t => {
  const te = frozen(t), m = messAt(te), b = m.pal.bg, su = easeIn(clamp((te - T_SUCK) / .5));
  return { ...b, amt: (b.amt ?? 1) * (1 - su), a: su > 0 ? '#000000' : b.a, pulse: M.hit('kick', t, 9) * .8 + M.hit('hat', t, 25) * .3 };
};
const messFX = (t, lt) => {
  const te = frozen(t), m = messAt(te), k = te - m.t, h = M.last('hat', t), cm = messCam(m, k, t);
  const hatFlash = h.since < 1 / 30 && Math.abs(h.t - m.t) > .03 && t < T_SUCK ? Math.floor(hash(h.i, 17) * 3) : -1;   // 踩镲：0 反色 1 白闪 2 黑一帧
  const start = k < 1 / 30 && m.kind !== 'start';
  return { ...cam(te), shift: [(cm.x ?? 0) / W * .04, -(cm.y ?? 0) / H * .04], ca: 2.5 + M.hit('kick', t, 9) * 6 + (start ? 6 : 0), split: M.hit('hat', t, 25) * 2,
    bloom: .6, bloomThr: .55, grade: 'silver', gradeMix: .2, hud: .7, gseed: frameNo(t),
    glitch: start ? .45 : GLIDE1 && t > GLIDE1.t && t < GLIDE1.t + .2 ? .5 : 0,
    invert: hatFlash === 0 && m.pal.glow !== null ? 1 : 0, flash: hatFlash === 1 ? .55 : start ? .4 : lt < 2 / 30 && m.kind === 'start' ? .35 : 0,
    flashCol: m.kind === 'glide' && start ? '#e0112b' : '#ffffff',
    dark: M.gap(t) ? .95 : hatFlash === 2 || (hatFlash === 0 && m.pal.glow === null) ? .9 : 0, hudMode: te >= REV[0] && te < REV[1] ? '◀◀ REW' : '▶ PLAY' };
};
scene({ name: '念头', t0: T_THINK, t1: T_STACK,
  weather: messWeather, bg: messBG,
  draw(c, t) { if (!M.gap(t)) drawMess(c, t, frozen(t)); },
  fx: messFX,
});
// 念头乱窜：每个踩镲切一次；最后所有东西被吸进中心 → 白点 → TAPE 01 → CRT 关机
scene({ name: '念头乱窜', t0: T_STACK, t1: D.lyrics[0].t,
  weather: messWeather, bg: messBG,
  draw(c, t) {
    const { su } = drawMess(c, t, frozen(t));
    if (su >= 1) { c.fillStyle = '#fff'; c.beginPath(); c.arc(W / 2, H / 2, 6, 0, 7); c.fill(); }
    if (t > T_CRT1 - .55 && frameNo(t) % 3) pixel(c, 'TAPE 01', W / 2, H / 2 + 230, 110, { shadow: C.red });
  },
  fx: (t, lt) => ({ ...messFX(t, lt), ca: 4, sq: crt((t - T_CRT1) / .24) }),
});

// ================================================================ 2 · 雨夜（主歌一）
const RAIN_FREEZE1 = [barT(11), D.kicks.find(k => k > barT(11) + .05)];   // 加花小节第一拍 808 空掉：雨冻住一拍
const L_KONGBAI = lineAt(25.58), L_JUESAI = lineAt(30.84);
const VS = 130;                                                                       // 主歌字号（76 → 100 → 130，手机上看得清）
// 右边很淡的竖排宋体：每句占自己的一列（从右往左排，像竖着写的笔录），唱到哪个字哪个字才显出来；
// 换句时整页往右挪一列，新的一列留在固定位置，旧的往右退出画面；每列从出现起一直慢慢往上飘，不会跳回原位
const VCOL = { x: W - 250, gap: 132, size: 104, top: 160, drift: 28 };
function vcols(c, t, i) {
  let page = 0; for (let j = 1; j <= i; j++) page += ease((t - D.lyrics[j].t) / .5);
  for (let j = Math.max(0, i - 2); j <= i; j++) {
    const l = D.lyrics[j], age = page - j, x = VCOL.x + age * VCOL.gap, a = .075 * clamp(1 - (age - .6) / 1.4);
    if (a <= 0) continue;
    const y0 = VCOL.top - (t - l.t) * VCOL.drift;
    l.words.forEach((w, k) => {
      const kk = clamp((t - w.t + .04) / .3); if (kk <= 0) return;
      c.save(); c.globalAlpha = a * kk; if (kk < 1) c.filter = `blur(${((1 - kk) * 6).toFixed(1)}px)`;
      text(c, w.w, x, y0 + k * VCOL.size * 1.02 - (1 - easeOut(kk)) * 14, VCOL.size, { fam: SERIF, fill: '#c8d4ff', base: 'top' });
      c.restore();
    });
  }
}
// 手机锁屏亮了一下（"我试着不去等待"）：在画面深处、虚焦，黑暗里只看得到屏幕的光和被照亮的机身边缘；
// 屏幕慢慢亮起 → 弹出"你"的消息 → 自动变暗 → 熄灭
const PH_CV = mk(300, 460), PH_T = 23.14, PH_X = 1400, PH_Y = 555;
function phone(c, t) {
  const ph = t - PH_T; if (ph < 0 || ph > 1.55) return;
  const on = easeOut(ph / .22), off = 1 - ease((ph - 1.2) / .32), a = on * off * (1 - .45 * ease((ph - .9) / .15));
  if (a <= .002) return;
  const g = c.createRadialGradient(PH_X, PH_Y, 0, PH_X, PH_Y, 430);                // 屏幕的光照到雾里
  g.addColorStop(0, `rgba(150,175,255,${.16 * a})`); g.addColorStop(1, 'rgba(150,175,255,0)');
  c.fillStyle = g; c.fillRect(PH_X - 430, PH_Y - 430, 860, 860);
  const p = PH_CV.getContext('2d'), cx = 150, cy = 230, w = 150, h = 290;
  p.setTransform(1, 0, 0, 1, 0, 0); p.clearRect(0, 0, 300, 460);
  p.globalAlpha = .85 * clamp(on * off * 1.3);                                      // 机身：几乎看不见，只有被屏幕照亮的一圈边
  p.fillStyle = '#04060e'; p.beginPath(); p.roundRect(cx - w / 2 - 8, cy - h / 2 - 8, w + 16, h + 16, 24); p.fill();
  p.globalAlpha = a; p.strokeStyle = 'rgba(150,175,255,.35)'; p.lineWidth = 1.5; p.stroke();
  const sg = p.createLinearGradient(0, cy - h / 2, 0, cy + h / 2); sg.addColorStop(0, '#2c3d78'); sg.addColorStop(1, '#0e1530');
  p.fillStyle = sg; p.beginPath(); p.roundRect(cx - w / 2, cy - h / 2, w, h, 18); p.fill();
  text(p, '23:47', cx, cy - h * .28, 36, { w: 400, fam: MONO, fill: 'rgba(235,240,255,.95)' });
  const nk = easeOut((ph - .35) / .25);
  if (nk > 0) { const ny = cy + 10 + (1 - nk) * 18; p.globalAlpha = a * nk;
    p.fillStyle = 'rgba(225,232,255,.72)'; p.beginPath(); p.roundRect(cx - 64, ny - 24, 128, 48, 11); p.fill();
    text(p, '你', cx - 50, ny, 18, { align: 'left', fill: '#111' }); text(p, '新消息', cx - 26, ny, 15, { w: 400, align: 'left', fill: '#444' }); }
  c.save(); c.translate(PH_X, PH_Y); c.rotate(-.06); c.filter = 'blur(3.4px)'; c.globalAlpha = .82; c.drawImage(PH_CV, -cx, -cy); c.restore();
}
// 玻璃上的雨滴（前景）：每句开头先对焦在玻璃上（雨滴清楚、字还虚），然后焦点拉到字上（雨滴虚掉）
const GLASS = mk();
function glass(c, t, line, f = 1) {
  const g = GLASS.getContext('2d'); g.setTransform(1, 0, 0, 1, 0, 0); g.clearRect(0, 0, W, H);
  const r = rand(31);
  for (let i = 0; i < 70; i++) {                                                    // 每滴：亮的折射边 + 左上一点高光 + 右下一点暗边；少数往下滑，拖一道水痕
    const x = r() * W, y0 = r() * H, rr = 7 + r() * r() * 22, sl = r() < .25 ? 18 + r() * 40 : 0, y = mod(y0 + sl * t, H + 60) - 30;
    if (sl) { const tg = g.createLinearGradient(0, y - rr * 9, 0, y); tg.addColorStop(0, 'rgba(200,215,255,0)'); tg.addColorStop(1, 'rgba(200,215,255,.22)');
      g.fillStyle = tg; g.fillRect(x - rr * .35, y - rr * 9, rr * .7, rr * 9); }
    const rx = rr * (.85 + r() * .35), ry = rr * (.7 + r() * .25), rot = (r() - .5) * .5;     // 玻璃上的水是扁的、不规则的
    g.save(); g.translate(x, y); g.rotate(rot); g.scale(rx / rr, ry / rr);
    const gr = g.createRadialGradient(0, rr * .35, 0, 0, rr * .1, rr);                    // 折射：下半部透出亮光，上半部偏暗
    gr.addColorStop(0, 'rgba(205,220,255,.42)'); gr.addColorStop(.55, 'rgba(120,150,230,.16)'); gr.addColorStop(.92, 'rgba(10,18,45,.30)'); gr.addColorStop(1, 'rgba(10,18,45,0)');
    g.fillStyle = gr; g.beginPath(); g.arc(0, 0, rr, 0, 7); g.fill();
    g.fillStyle = 'rgba(255,255,255,.75)'; g.beginPath(); g.arc(-rr * .32, -rr * .4, rr * .11, 0, 7); g.fill();
    g.restore();
  }
  const k = line ? clamp((t - line.t - .08) / .55) : 1, blur = .4 + ease(k) * 5;
  c.save(); c.filter = `blur(${blur.toFixed(1)}px)`; c.globalAlpha = (.95 - .4 * ease(k)) * f; c.drawImage(GLASS, 0, 0); c.restore();
}
function rainTime(t) {
  const g = M.gap(t); if (g) return g[0];
  const k = M.last('kick', t); if (k.since < 2 / 30) return k.t;                  // 底鼓 = 雨停一帧
  if (t >= RAIN_FREEZE1[0] && t < RAIN_FREEZE1[1]) return RAIN_FREEZE1[0];
  return t;
}
function verseLine(c, t, line, idx) {
  const a = clamp((t - line.t) / .14), x = 150, y = H - 290;
  c.save();
  c.beginPath(); c.rect(0, y - 260, W, 260 * a + 120); c.clip();
  text(c, String(idx).padStart(2, '0'), x - 8, y - 150, 150, { fam: ANTON, w: 400, align: 'left', fill: null, stroke: 'rgba(200,215,255,.45)', lw: 2 });
  const dev = clamp((t - line.t) / .28);                                             // 显影：从过曝发虚沉淀成清晰的字
  c.save(); c.filter = dev < 1 ? `blur(${(1 - dev) * 7}px)` : 'none';
  c.shadowColor = `rgba(220,230,255,${1 - dev})`; c.shadowBlur = 40 * (1 - dev);
  text(c, line.text, x, y, VS, { align: 'left', fill: `rgb(${lerp(255, 242, dev) | 0},${lerp(255, 241, dev) | 0},${lerp(255, 238, dev) | 0})` });
  c.restore();
  const s = line.t, ts = `[${String(s / 60 | 0).padStart(2, '0')}:${(s % 60).toFixed(2).padStart(5, '0')}]  STATEMENT ${String(idx).padStart(2, '0')}/08`;
  text(c, ts, x, y + 102, 22, { w: 400, fam: MONO, align: 'left', fill: C.blue });
  c.restore();
  if (a < 1) { c.fillStyle = 'rgba(200,220,255,.8)'; c.fillRect(0, y - 260 + 260 * a + 118, W * .6, 2); }
}
scene({ name: '雨夜', t0: D.lyrics[0].t, t1: lineAt(32.44).t,
  weather: (t, lt) => { const t12 = Math.floor(t * 12) / 12;
    return { rain: .26 * clamp((lt - .2) / .6), rt: rainTime(t12), street: .14, wind: .22 + (t > barT(11) ? (t - barT(11)) * .12 : 0) }; },
  bg: t => ({ mode: 'night', a: '#010309', b: '#0c1a3c', c: '#4a6cff', amt: .9, speed: 1, pulse: M.hit('kick', t, 10) * .4 }),
  draw(c, t, lt) {
    const t12 = Math.floor(t * 12) / 12, rf = rainTime(t12);
    const { line, i } = M.lyric(t);
    if (line) vcols(c, t, i);
    phone(c, t);
    if (lt < .7) {                                                                  // 转场：CRT 关机缩成的那个点，变成第一滴雨落下
      const k = lt / .7, y = lerp(H / 2, H + 100, easeIn(k));
      c.strokeStyle = 'rgba(230,238,255,.95)'; c.lineWidth = 3; c.beginPath(); c.moveTo(W / 2, y - 120 * k); c.lineTo(W / 2, y); c.stroke();
      c.fillStyle = '#fff'; c.beginPath(); c.arc(W / 2, y, 4, 0, 7); c.fill();
    }
    if (line && i > 0 && t - line.t < .3) {                                         // 上一句往上飘着淡出
      const k = (t - line.t) / .3; c.save(); c.globalAlpha = 1 - k;
      text(c, D.lyrics[i - 1].text, 150, H - 290 - k * VS * .8, VS, { align: 'left', fill: C.white }); c.restore();
    }
    if (line) {
      verseLine(c, t, line, i + 1);
      if (line === L_KONGBAI) {                                                   // "空白"被涂黑
        c.font = fnt(VS, 900); const x = 150 + c.measureText('你故意清除的').width, w = c.measureText('空白').width;
        redact(c, x - 6, H - 290 - VS * .6, w + 12, VS * 1.2, (t - wordT(line, '空')) / .25);
      }
      if (line === L_JUESAI && t > wordT(line, '决')) {                           // "决赛"变成铬金属
        c.font = fnt(VS, 900); const x = 150 + c.measureText('我和你最后').width;
        c.fillStyle = '#000'; c.fillRect(x - 4, H - 290 - VS * .6, c.measureText('决赛').width + 8, VS * 1.2);
        chrome(c, '决赛', x - 20, H - 290, VS * 1.26, { align: 'left', sweep: (t - wordT(line, '决')) * 1.5 });
      }
    }
    rain(c, rf, { n: 36, spd: 2100, len: 150, alpha: .32, wind: .22, seed: 9, lw: 2.4 });   // 字前面的大雨丝
    if (lt > .5) glass(c, t, line, clamp((lt - .5) / .8));                         // 最前面：玻璃上的雨滴（开头那滴雨落完再慢慢出现）
  },
  fx(t) {
    const fill = t > barT(11), k = M.last('kick', t), end = this.t1 - t;
    const strobe = t > L_JUESAI.t && k.t > L_JUESAI.t ? Math.exp(-k.since * 18) * .75 : 0;
    return { ...cam(t, .35), zoom: (cam(t, .35).zoom ?? 1) + (t - this.t0) * .004, lb: lbIn(t - this.t0), grade: 'cold', gradeMix: .7, ca: 1.2 + M.hit('kick', t, 10) * 2, bloom: .5, hud: .75,
      glitch: fill ? .08 + M.hit('kick', t, 12) * .2 : 0, tear: fill ? .6 : 0, gseed: frameNo(t) >> 1, dark: M.gap(t) ? .45 : 0,
      flash: end < .45 ? (frameNo(t) % 2 ? .55 : 0) : strobe, flashCol: k.i % 2 ? '#ff1a3a' : '#ffffff' };
  },
});

// ================================================================ 3 / 6 · 副歌：连击（ROUND 2 更狠：镜像、倒影、病态、碎玻璃）
const KO = [lineAt(36.12), lineAt(56.46)].map(l => wordT(l, '倒'));
const GLIDES = D.notes808.filter(n => n.glide).map(n => n.t);
const COLS = [lineAt(37.38), lineAt(38.64), lineAt(39.92)];
const ROWS2 = [lineAt(59.1), lineAt(59.88)];                                        // 副歌二：上下两条横栏（副歌一是竖排三栏）
const L_FACE2 = lineAt(52.86);                                                      // 副歌二第一句：超大字特写，镜头横扫
const SNARE_DROP = barT(15) + 4 * STEP;                                              // 小节 15 第 2 拍军鼓空掉
const T_SHATTER = barT(24), T_POP = T_SHATTER + .36;
// 霓虹灭掉时迸出的火星：从 woooah 的灯管上掉下来，慢慢飘落、变暗，进入桥段后变成暖色灰尘
function embers(c, t) {
  const k = t - T_POP; if (k < 0 || k > 3.2) return;
  const pts = textPoints('wooooooah', 300, { fam: GOTH, w: 400, step: 26 });
  c.save();
  pts.forEach((p, i) => {
    if (hash(i, 91) > .45) return;
    const d = Math.max(0, k - hash(i, 92) * .25), vx = (hash(i, 93) - .5) * 120, vy = 40 + hash(i, 94) * 90;
    const x = W / 2 + p[0] * 1.45 + vx * d + Math.sin(d * 2 + i) * 14, y = H / 2 + p[1] * 1.45 + vy * d + 22 * d * d;
    const a = clamp(1 - d / (2 + hash(i, 95))) * (d < .05 ? d / .05 : 1);
    const hot = clamp(1 - d / .8);
    c.globalAlpha = a * .9; c.fillStyle = `rgb(255,${lerp(150, 230, hot) | 0},${lerp(80, 200, hot) | 0})`;
    c.beginPath(); c.arc(x, y, 1.6 + hot * 1.6, 0, 7); c.fill();
  });
  c.restore();
}
function hookLine(c, t, line, R2) {
  const te = gapHold(t), txt = line.text, idx = D.lyrics.indexOf(line);
  const size = fit(c, txt, W * .84, 250);
  const shadowed = (cc, a = 1) => { cc.globalAlpha = a; text(cc, txt, 7, 7, size, { fill: C.red }); text(cc, txt, 0, 0, size, { fill: C.white }); cc.globalAlpha = 1; };   // 红色套印错位
  // 背后：这句话的巨大空心字在滚
  scrollRows(c, te, txt, { size: 260, rows: 3, spd: R2 ? 260 : 140, col: 'rgba(255,255,255,.06)', lw: 2 });
  if (R2) { c.save(); c.globalAlpha = .45; vtext(c, txt, W / 2, 40, Math.min(150, 1000 / [...txt].length), { fam: SERIF, fill: C.red }); c.restore(); }
  // 失态 / 病态：鼓停时冻住；808 滑音时字一个个垂直往下沉、被拉长，像融化（不旋转）
  if (txt === '就失态' || txt === '变病态') {
    if (txt === '就失态') { c.fillStyle = 'rgba(150,6,22,.85)'; c.fillRect(0, 0, W, H); }
    const g = GLIDES.find(x => x > line.t && x < line.end + .2), k = g ? clamp((t - g) / .55) : 0;
    const draw = cc => { c.font = fnt(size, 900); let x = -c.measureText(txt).width / 2;
      [...txt].forEach((ch, j) => { const w = c.measureText(ch).width, d = easeIn(k - j * .12);
        cc.save(); cc.globalAlpha = 1 - d * .7; cc.translate(x + w / 2, d * (240 + hash(j, idx) * 160)); cc.scale(1, 1 + d * .9);
        text(cc, ch, 7, 7, size, { fill: C.red }); text(cc, ch, 0, 0, size); cc.restore(); x += w; }); };
    if (R2 && txt === '变病态') { c.save(); c.translate(W / 2, H / 2); draw(c); c.restore(); return; }
    cutIn(c, te, line.t, idx, () => draw(c)); return;
  }
  // 副歌二第一句：极近特写，字大到一屏只放得下两三个字，镜头从句首扫到句尾；硬切时横切错开
  if (R2 && line === L_FACE2) {
    const big = 430, k = clamp((te - line.t) / (line.end - line.t)), j = jerk(te);
    c.font = fnt(big, 900); const w = c.measureText(txt).width;
    const cut = j.act === 'cut' && j.since < .1 ? (1 - j.since / .1) * 90 : 0;
    c.save(); c.translate(lerp(80 + w / 2, W - 80 - w / 2, ease(k)), H / 2 + 20);
    sliced(c, 5, cut, j.i, -big * .6, big * 1.2, () => { text(c, txt, 10, 10, big, { fill: C.red }); text(c, txt, 0, 0, big, { fill: C.white }); });
    c.restore();
    return;
  }
  // 副歌二「不要这样就 / 不要躲着我」：上下两条横栏，上面红底黑字从左边甩进来，下面白底红字从右边甩进来
  if (R2 && ROWS2.includes(line)) {
    const tones = [[C.red, C.black], [C.white, C.red]];
    ROWS2.forEach((l, i) => {
      if (t < l.t) return;
      const jj = jerk(t), fz = (jj.i + i) % 3 === 2 && jj.since < .1, tt = fz ? jj.t : gapHold(t);
      const k = easeOut((tt - l.t) / .12), y0 = i * H / 2;
      c.save(); c.beginPath(); c.rect(0, y0, W, H / 2); c.clip();
      c.translate((1 - k) * (i ? W : -W), 0);
      c.fillStyle = tones[i][0]; c.fillRect(0, y0, W, H / 2);
      const n = [...l.text].length, sz = Math.min(330, W * .86 / n), sh = (hash(jj.i * 3 + i, 5) - .5) * 40 * Math.exp(-jj.since * 10);
      text(c, l.text, W / 2 + sh, y0 + H / 4, sz, { fill: tones[i][1] });
      text(c, `0${i + 1}`, 40, y0 + 46, 24, { w: 400, fam: MONO, align: 'left', fill: tones[i][1] });
      c.restore();
    });
    c.fillStyle = C.black; c.fillRect(0, H / 2 - 2, W, 4);
    return;
  }
  // 连击：每个底鼓叠一层残影
  if (txt.includes('连击')) {
    const n = M.count('kick', line.t, te) * (R2 ? 2 : 1);
    for (let j = Math.min(n, 10); j > 0; j--) { c.save(); c.translate(W / 2 + j * 26, H / 2 - j * 18); text(c, txt, 0, 0, size, { fill: null, stroke: j % 2 ? C.red : 'rgba(255,255,255,.7)', lw: 3 }); c.restore(); }
  }
  // 击倒：字被一拳打出画面（往下甩出），K.O. 硬切出现；第二次 K.O. 之后硬切成铬金属「谎话」
  const ko = KO.find(x => x > line.t && x < line.end + .1);
  if (ko && t >= ko) {
    const k = t - ko;
    if (R2 && k > .17) { chrome(c, D.title, W / 2, H / 2, 380, {}); return; }
    const fall = Math.pow(k / .22, 2) * H;
    if (fall < H) { c.save(); c.translate(W / 2 + k * 260, H / 2 + fall); c.rotate(k * .5); shadowed(c); c.restore(); }
    c.save(); c.font = fnt(820, 400, ANTON); c.textAlign = 'center'; c.textBaseline = 'middle';
    c.fillStyle = C.white; c.fillText('K.O.', W / 2, H / 2 + 40);
    c.fillStyle = C.red; c.fillRect(W / 2 - 520, H / 2 + 330, 1040, 12); c.restore();
    return;
  }
  // 不要让我救（ROUND 2）：像录像带来回搓带，字在前后几帧之间来回跳几下再停住
  let scrub = 0;
  if (R2 && txt === '不要让我救' && t - line.t < .3) scrub = [0, -46, 28, -14, 36, -8][frameNo(t) % 6];
  const j = jerk(te), cut = j.act === 'cut' && j.since < .1 ? (1 - j.since / .1) * 70 : 0;
  cutIn(c, te, line.t, idx, () => {
    if (scrub) { c.save(); c.translate(-scrub * 1.6, 0); shadowed(c, .35); c.restore(); c.translate(scrub, 0); }
    sliced(c, 5, cut, j.i, -size * .6, size * 1.2, () => shadowed(c));
  });
  if (R2) { c.save(); c.translate(W / 2, H / 2 + size * 1.15); c.scale(1, -1); shadowed(c, .16); c.restore(); }   // 倒影
}
// woah：测谎仪的针从右下角冲上来，像一支笔，用红色霓虹灯管一笔一笔"写"出哥特体 woah；
// 写完后灯管跟着人声抖、闪。ROUND 2 的 woah 更长：中间的 o 跟着拖长音不断复制（woah → wooooah）。
const NEON = mk(W, H), nx = NEON.getContext('2d');
function woahShot(c, t, t0, R2 = false) {
  const k = t - t0, draw = clamp(k / (R2 ? .42 : .3)), v = M.vocal(t);
  const n = R2 ? Math.min(9, Math.floor(Math.max(0, k - .45) / .1)) : 0;
  const word = 'wo' + 'o'.repeat(n) + 'ah', size = fit(c, word, W * .86, 430, 400, GOTH);
  const head = [W - 170, H - 92];                                                  // 测谎仪的针头
  // 灯管：先画到 NEON（描边 + 虚线偏移 = 一笔一笔写出来），再叠上发光
  nx.setTransform(1, 0, 0, 1, 0, 0); nx.clearRect(0, 0, W, H);
  nx.font = fnt(size, 400, GOTH); nx.textAlign = 'center'; nx.textBaseline = 'middle'; nx.lineJoin = 'round'; nx.lineCap = 'round';
  const L = 2600; nx.setLineDash([L, L]); nx.lineDashOffset = L * (1 - draw);
  const jx = (hash(frameNo(t), 3) - .5) * 14 * v * draw, jy = (hash(frameNo(t), 4) - .5) * 10 * v * draw;
  nx.strokeStyle = '#ff1f3d'; nx.lineWidth = 9; nx.shadowColor = '#ff1f3d'; nx.shadowBlur = 34; nx.strokeText(word, W / 2 + jx, H / 2 + jy);
  nx.shadowBlur = 0; nx.strokeStyle = '#ffd6dc'; nx.lineWidth = 2.6; nx.strokeText(word, W / 2 + jx, H / 2 + jy);
  nx.setLineDash([]);
  const flick = draw < 1 ? 1 : hash(frameNo(t), 8) < .12 ? .25 : .85 + .15 * v;   // 写完后像接触不良的霓虹招牌
  c.save(); c.globalAlpha = flick; c.drawImage(NEON, 0, 0); c.globalCompositeOperation = 'lighter'; c.globalAlpha = .35 * flick; c.drawImage(NEON, 0, 0); c.restore();
  // 针：从测谎仪的针头冲上来，连到正在写的那一笔
  if (draw < 1) {
    c.font = fnt(size, 400, GOTH); const w = c.measureText(word).width;
    const px = W / 2 - w / 2 + w * draw, py = H / 2 + size * .25 * Math.sin(draw * 25);
    c.save(); c.strokeStyle = 'rgba(255,60,80,.9)'; c.lineWidth = 2; c.shadowColor = '#ff1f3d'; c.shadowBlur = 12;
    c.beginPath(); c.moveTo(head[0], head[1]); c.lineTo(px, py); c.stroke();
    c.fillStyle = '#fff'; c.beginPath(); c.arc(px, py, 6, 0, 7); c.fill(); c.restore();
  }
}
function hookDraw(c, t, lt, R2) {
  const { line } = M.lyric(t);
  if (!line) return;
  if (line.text === 'woah') {
    if (R2 && t >= T_SHATTER) {                                                     // 霓虹短路：闪几下，啪地一下灭掉，火星慢慢飘落
      const k = t - T_SHATTER, on = k < T_POP - T_SHATTER ? [1, 0, 1, .25, 1, 0, 0, .9, 0, .4][frameNo(t) % 10] : 0;
      if (on > 0) { c.save(); c.globalAlpha = on; woahShot(c, T_SHATTER - .01, line.t, true); c.restore(); }
      embers(c, t); return;
    }
    woahShot(c, t, line.t, R2); return;
  }
  if (!R2 && COLS.includes(line)) {                                                 // 三栏分屏
    const cw = W / 3, tones = [[C.black, C.white], [C.silver, C.black], ['#9a0718', C.black]];
    COLS.forEach((l, i) => {
      if (t < l.t) return;
      const j = jerk(t), fz = (j.i + i) % 3 === 2 && j.since < .1, te = fz ? j.t : gapHold(t);
      const k = easeOut((te - l.t) / .12);
      c.save(); c.beginPath(); c.rect(i * cw, 0, cw, H); c.clip();
      c.translate(0, (1 - k) * -H);
      c.fillStyle = tones[i][0]; c.fillRect(i * cw, 0, cw, H);
      const n = [...l.text].length, s = Math.min(170, 980 / n), sh = (hash(j.i * 3 + i, 5) - .5) * 30 * Math.exp(-j.since * 10);
      vtext(c, l.text, i * cw + cw / 2 + sh, (H - n * s * 1.02) / 2, s, { fill: tones[i][1] });
      text(c, `0${i + 1}`, i * cw + 30, 50, 24, { w: 400, fam: MONO, align: 'left', fill: tones[i][1] });
      c.restore();
    });
    c.fillStyle = C.black; c.fillRect(cw - 2, 0, 4, H); c.fillRect(cw * 2 - 2, 0, 4, H);
    if (t > SNARE_DROP && t < SNARE_DROP + .32) { c.fillStyle = C.black; c.fillRect(0, 0, W, H); }   // 军鼓空一拍 = 三栏同时黑掉
    return;
  }
  hookLine(c, t, line, R2);
}
const HOOK_T0 = [lineAt(32.44).t, lineAt(52.86).t];                                // 副歌开头一帧红闪（替代粒子 / 速度线转场）
function hookFx(R2) {
  return function (t) {
    const k = M.last('kick', t), { line } = M.lyric(t), ko = KO.find(x => t >= x && t < x + .5);
    const sick = R2 && line && line.text === '变病态';
    const inv = R2 ? (t > 56 && t < 61.5 ? blink(k.since, 2) : k.i % 2 ? blink(k.since) : 0) : (k.i % 2 ? blink(k.since) : 0);
    const shatter = R2 && t >= T_SHATTER, sk = shatter ? clamp((t - T_SHATTER) / (lineAt(63.36).t - T_SHATTER)) : 0;
    return { ...cam(gapHold(t), R2 ? 1.3 : 1.1), ca: (R2 ? 3 : 2.2) + M.hit('kick', t, 9) * 8, xerox: .18, bloom: .55,
      grade: shatter ? 'warm' : sick ? 'sick' : M.gap(t) ? 'silver' : 'red', gradeMix: shatter ? .6 * sk : sick ? 1 : M.gap(t) ? 1 : (R2 ? .25 : .35), wave: sick ? .6 + M.hit('kick', t, 6) : 0,
      invert: shatter ? 0 : inv, glitch: hash(k.i, 13) < (R2 ? .45 : .28) ? M.hit('kick', t, 12) * .5 : 0, gseed: k.i,
      flash: ko != null ? (t - ko < 2 / 30 ? 1 : 0) : R2 && t >= T_POP && t < T_POP + 2 / 30 ? .8
        : line && t - line.t < 2 / 30 && line.text !== 'woah' ? .3 : M.hit('snare', t, 25) * (R2 ? .3 : .2),   // 每句出现时一帧曝光闪
      flashCol: R2 && t >= T_POP && t < T_POP + .1 ? '#ffb070' : t - HOOK_T0[+R2] < 2 / 30 ? '#e0112b' : '#ffffff',
      zoom: ko != null ? 1 - .14 * Math.exp(-(t - ko) * 6) : cam(gapHold(t)).zoom ?? 1, rot: ko != null ? .05 * Math.exp(-(t - ko) * 5) : cam(gapHold(t)).rot ?? 0,
      sq: R2 ? [1, 1] : crt((t - (lineAt(41.22).t + .5)) / .16), hud: .5 * (1 - sk), lb: sk * .6 };
  };
}
scene({ name: '连击', t0: lineAt(32.44).t, t1: lineAt(41.88).t,
  weather: t => ({ rain: M.gap(t) ? .07 : .24, col: '255,70,90', len: 2.8, rt: gapHold(t), lw: 1.7 }),
  bg: t => ({ mode: 'smoke', a: '#040002', b: '#4a0712', c: '#ff2a48', amt: .95, speed: 1.6, pulse: M.hit('kick', t, 8) }),
  draw: (c, t, lt) => hookDraw(c, t, lt, false), fx: hookFx(false) });

// ================================================================ 4 · 对质：聊天记录
const CHAT = [{ l: lineAt(41.88), who: 'L', t: lineAt(41.88).t }, { l: lineAt(43.46), who: 'L', t: lineAt(43.46).t },
  { l: lineAt(44.9), who: 'R', t: D.kicks.find(k => k > 44.5 && k < 45) }, { l: lineAt(45.96), who: 'R', t: M.gap(46)?.[1] ?? 46.17 }];
const DEL = D.hats.filter(h => h > 46.45).slice(0, 4);                               // 一条一条删除
const PHONE = { x: (W - 720) / 2, y: 26, w: 720, h: 900 };
scene({ name: '对质', t0: lineAt(41.88).t, t1: D.kicks.find(k => k > 47),
  weather: t => ({ rain: .16, street: .1, rt: M.gap(t)?.[0] ?? t }),
  bg: t => ({ mode: 'smoke', a: '#04060b', b: '#121a2c', c: '#3a4a7a', amt: .7, speed: .6, pulse: M.hit('kick', t, 6) }),
  draw(c, t, lt) {
    scrollRows(c, t * .5, '你总说', { size: 220, rows: 4, spd: 40, col: 'rgba(255,255,255,.04)', fam: SERIF });
    const P = PHONE, open = easeOut(lt / .28);
    if (open < 1) {                                                                 // 转场：CRT 关机的横线，变成手机屏幕"开机"展开
      c.fillStyle = `rgba(255,255,255,${1 - open})`; c.fillRect(P.x - 200 * (1 - open), H / 2 - 2, P.w + 400 * (1 - open), 4);
      c.save(); c.beginPath(); c.rect(0, H / 2 - P.h / 2 * open, W, P.h * open); c.clip();
    } else c.save();
    const glow = c.createRadialGradient(W / 2, P.y + P.h * .55, P.w * .3, W / 2, P.y + P.h * .55, P.w * 1.15);   // 屏幕照出来的冷光，让手机从背景里浮出来
    glow.addColorStop(0, 'rgba(110,140,255,.16)'); glow.addColorStop(1, 'rgba(110,140,255,0)'); c.fillStyle = glow; c.fillRect(0, 0, W, H);
    c.save();
    c.fillStyle = '#0f1119'; c.beginPath(); c.roundRect(P.x, P.y, P.w, P.h, 54); c.fill();
    c.strokeStyle = 'rgba(205,215,255,.38)'; c.lineWidth = 3; c.stroke();
    const rim = c.createLinearGradient(0, P.y, 0, P.y + 220); rim.addColorStop(0, 'rgba(160,180,255,.10)'); rim.addColorStop(1, 'rgba(160,180,255,0)');
    c.fillStyle = rim; c.beginPath(); c.roundRect(P.x, P.y, P.w, P.h, 54); c.fill();
    c.beginPath(); c.roundRect(P.x, P.y, P.w, P.h, 54); c.clip();
    if (M.gap(t)) { c.fillStyle = '#000'; c.fillRect(P.x, P.y, P.w, P.h); c.restore(); return; }   // 鼓停 = 屏幕黑掉
    text(c, '你', W / 2, P.y + 80, 34);
    const typing = t > 43.0 && t < CHAT[1].t;
    text(c, typing ? '对方正在输入…' : '在线', W / 2, P.y + 124, 22, { w: 400, fill: 'rgba(255,255,255,.45)' });
    c.fillStyle = 'rgba(255,255,255,.08)'; c.fillRect(P.x, P.y + 160, P.w, 2);
    const items = CHAT.filter(m => t >= m.t).map((m, k) => ({ ...m, k }));
    if (typing) items.push({ typing: true, who: 'L', t: 43.0, k: 9 });
    c.fillStyle = 'rgba(255,255,255,.08)'; c.beginPath(); c.roundRect(W / 2 - 80, P.y + 196, 160, 36, 18); c.fill();   // 时间分隔
    text(c, '昨天 23:47', W / 2, P.y + 214, 18, { w: 400, fill: 'rgba(255,255,255,.45)' });
    let y = P.y + P.h - 70;
    for (let n = items.length - 1; n >= 0; n--) {
      const m = items[n], del = m.typing ? -1 : DEL[m.k] ?? 1e9, gone = clamp((t - del) / .25);
      const txt = m.typing ? '·  ·  ·' : m.l.text, size = 54;
      c.font = fnt(size, 900); const bw = c.measureText(txt).width + 76, bh = 104;
      const app = m.typing ? 1 : easeOut((t - m.t) / .14), s = .8 + .2 * app;
      const bx = m.who === 'L' ? P.x + 40 : P.x + P.w - 40 - bw, by = y - bh - gone * 260;
      c.save(); c.globalAlpha = app * (1 - gone); c.translate(bx + bw / 2, by + bh / 2); c.scale(s, s);
      if (m.who === 'R' && t - m.t < .2) c.translate((hash(frameNo(t), 3) - .5) * 30, (hash(frameNo(t), 4) - .5) * 20);
      c.fillStyle = m.who === 'L' ? '#272830' : C.red; c.beginPath(); c.roundRect(-bw / 2, -bh / 2, bw, bh, 28); c.fill();
      if (m.typing) { [...'···'].forEach((_, j) => { c.fillStyle = '#ddd'; c.beginPath(); c.arc(-24 + j * 24, -10 * M.hit('hat', t - j * .05, 14), 7, 0, 7); c.fill(); }); }
      else {
        text(c, txt, 0, 2, size);
        if (txt.includes('bullshxt')) { const full = c.measureText(txt).width, cut = c.measureText(txt.replace('shxt', '')).width; mosaic(c, -full / 2 + cut, -size * .45, full - cut, size * .9, t, { cell: 11 }); }
        if (m.who === 'R' && n === items.length - 1) { c.globalAlpha = .45 * app; text(c, '已读', -bw / 2 - 40, bh / 2 - 14, 18, { w: 400 }); }
        const ts = `[${String(m.l.t / 60 | 0).padStart(2, '0')}:${(m.l.t % 60).toFixed(2).padStart(5, '0')}]`;   // 和雨夜的证词同一个格式：聊天记录像呈堂证据
        c.globalAlpha = .8 * app; text(c, ts, m.who === 'L' ? -bw / 2 + 6 : bw / 2 - 6, bh / 2 + 16, 16, { w: 400, fam: MONO, align: m.who === 'L' ? 'left' : 'right', fill: C.blue });
      }
      c.restore();
      y -= (bh + 30) * (1 - gone);
    }
    c.restore();
    // 屏幕反光（字前面）
    const g = c.createLinearGradient(P.x, P.y, P.x + P.w, P.y + P.h); g.addColorStop(0, 'rgba(255,255,255,.06)'); g.addColorStop(.35, 'rgba(255,255,255,0)'); g.addColorStop(.6, 'rgba(255,255,255,.03)'); g.addColorStop(1, 'rgba(255,255,255,0)');
    c.fillStyle = g; c.fillRect(P.x, P.y, P.w, P.h);
    c.restore();
  },
  fx(t) { const k = M.last('kick', t), dive = easeIn((t - (this.t1 - .3)) / .3);  // 转场：最后 0.3 秒镜头扎进手机屏幕
    return { zoom: 1 + .08 * Math.exp(-k.since * 12) + dive * 2.4, barrel: .05 + .3 * Math.exp(-k.since * 12) + dive * .4, dark: dive * .6, grade: 'cold', gradeMix: .5, ca: .8 + M.hit('kick', t, 8) * 6 + dive * 10,
    glitch: M.hit('kick', t, 10) * .6, gseed: k.i, flash: M.hit('kick', t, 14) * .35, flashCol: '#ff1a3a', hud: .6, scan: .2 }; },
});

// ================================================================ 5 · 都是我
const STACK = [47.4, 48.52, 49.82, 51.1].map(lineAt);
const T_FILL3 = barT(19), T_UP3 = D.notes808.find(n => n.t > T_FILL3 && n.midi > 43).t;
scene({ name: '都是我', t0: D.kicks.find(k => k > 47), t1: lineAt(52.86).t,
  weather: t => ({ rain: M.gap(t) ? 0 : .08, col: '255,80,100', len: 1.6 }),
  bg: t => ({ mode: 'halftone', a: '#040106', b: '#6a1222', amt: 1.15, speed: 1.2, pulse: M.hit('kick', t, 8) }),
  draw(c, t, lt) {
    if (M.gap(t)) return;
    const kc = M.count('kick', this.t0 - .01, t), step = kc * .21 + easeOut(clamp(M.last('kick', t).since / .1)) * .0;
    c.save(); c.translate(W - 430, H / 2 + 30); c.rotate(step);
    text(c, '我', 0, 0, 900, { fam: SERIF, fill: null, stroke: `rgba(224,17,43,${.5 + M.hit('kick', t, 6) * .4})`, lw: 5 }); c.restore();
    if (t >= T_UP3) {                                                               // 跳八度：硬切成一个巨大的红"我"，之后按十六分音符红黑互换
      const alt = t > T_UP3 + .12 && Math.floor((t - T_UP3) / STEP) % 2;
      if (alt) { c.fillStyle = C.red; c.fillRect(0, 0, W, H); }
      cutIn(c, t, T_UP3, 3, () => text(c, '我', 0, 0, 640, { fill: alt ? '#050003' : C.red })); return;
    }
    const collapse = 0, smear = t > T_FILL3 ? clamp((t - T_FILL3) / 2.1) : 0;
    const cur = STACK.filter(l => t >= l.t).length - 1, pop = 1 + .35 * M.hit('kick', t, 9);
    STACK.forEach((l, k) => {
      if (t < l.t) return;
      const x = 150, y = 230 + k * 190;
      const cx = lerp(x, W / 2, collapse), cy = lerp(y, H / 2, collapse), sc = 1 - collapse;
      c.save(); c.translate(cx, cy); c.scale(sc, sc);
      c.font = fnt(120, 900); const pre = l.text.slice(0, -1), pw = c.measureText(pre).width;
      for (let j = Math.round(smear * 8); j >= 0; j--) {                            // 像素拖尾
        c.globalAlpha = (k === cur ? 1 : .32) * (j ? .14 : 1);
        text(c, pre, j * smear * 30, 0, 120, { align: 'left' });
      }
      c.globalAlpha = 1;
      c.save(); c.translate(pw + 60, 0); c.scale(pop, pop); text(c, '我', 0, 0, 120, { fill: C.red }); c.restore();
      c.restore();
    });
  },
  fx(t) {
    return { ...cam(t), xerox: .35, grade: 'red', gradeMix: .4, ca: 2 + M.hit('kick', t, 9) * 5, bloom: .5, flashCol: '#e0112b',
      flash: t - this.t0 < 2 / 30 ? .9 : 0,
      glitch: hash(M.last('kick', t).i, 14) < .3 ? M.hit('kick', t, 12) * .4 : 0, gseed: M.last('kick', t).i,
      zoom: 1 + M.hit('note', t, 10) * (t > T_FILL3 ? .1 : 0) + (cam(t).zoom ?? 1) - 1,
      dark: M.gap(t) ? .9 : 0, hud: .5 };
  },
});

scene({ name: '病态', t0: lineAt(52.86).t, t1: lineAt(63.36).t,
  weather: t => t >= T_SHATTER ? null : ({ rain: M.gap(t) ? .07 : .24, col: '255,70,90', len: 2.8, rt: gapHold(t), lw: 1.7 }),
  bg: t => t >= T_SHATTER ? { mode: 'leak', a: '#05060a', b: '#ff7418', c: '#ffd59a', amt: .5 * easeIn((t - T_SHATTER) / (lineAt(63.36).t - T_SHATTER)), speed: .5, seed: 3 }   // 转场：暖光从黑里渗出来
    : ({ mode: 'smoke', a: '#030002', b: '#560a16', c: '#ff2a50', amt: 1, speed: 1.8, pulse: M.hit('kick', t, 7), mirror: true }),   // 左右镜像：像墨迹测试
  draw: (c, t, lt) => hookDraw(c, t, lt, true), fx: hookFx(true) });

// ================================================================ 7 · 面具掉了（桥段）
const L_HURT = lineAt(67.86), T_HURT = wordT(L_HURT, '伤');
// 「伤害」留在句子里：唱到"伤"开始变红，之后跟着鼓点一下下亮（军鼓最亮，踩镲小亮），不位移、不缩放
function hurtRed(t) { return Math.max(M.hit('snare', t, 6), .55 * M.hit('hat', t, 9)); }
const warmth = t => t < T_HURT ? 1 : lerp(1, .12, ease((t - T_HURT) / 4.5));
const T_PRERAIN = lineAt(73.7).t - 1.1;
scene({ name: '面具掉了', t0: lineAt(63.36).t, t1: lineAt(73.7).t,
  weather: t => t < T_PRERAIN ? null : ({ rain: .3 * easeIn((t - T_PRERAIN) / 1.1), n: Math.round(lerp(40, 600, easeIn((t - T_PRERAIN) / 1.1))), len: 1.2 }),   // 转场：雨开始落进回忆里
  bg: function (t) { return { mode: 'leak', a: '#05060a', b: '#ff7418', c: '#ffd59a', amt: warmth(t) * (.85 + M.hit('snare', t, 5) * .25) * lerp(.5, 1, ease((t - this.t0) / 1.2)), speed: .5, seed: 3 }; },
  draw(c, t, lt) {
    const sp = M.hit('snare', t, 4);
    embers(c, t);                                                                   // 转场：霓虹灭掉时的火星继续飘落，变成暖色灰尘
    dust(c, t, 140, 21, (.3 + sp * .25) * clamp(lt / 1.2), '#ffe6c8');
    const dist = t < T_HURT ? lerp(900, 160, ease((t - this.t0) / (T_HURT - .6 - this.t0))) : lerp(160, 1250, ease((t - T_HURT) / 4.6));
    const cy = H * .37 + Math.sin(t * .6) * 10, ax = W / 2 - dist / 2, bx = W / 2 + dist / 2;
    if (t > lineAt(65.46).t && t < T_HURT) { c.strokeStyle = `rgba(255,236,210,${.55 * clamp((t - 65.46) / 1.2)})`; c.lineWidth = 1.6; c.beginPath(); c.moveTo(ax, cy); c.lineTo(bx, cy); c.stroke(); }
    if (t >= T_HURT && t < T_HURT + 1.4) {                                          // 连线断成红色裂痕
      const r = rand(31), a = (t - T_HURT < .5 ? frameNo(t) % 2 : 1) * clamp(1 - (t - T_HURT - .6) / .8);
      c.strokeStyle = `rgba(230,20,45,${a})`; c.lineWidth = 2.4; c.beginPath(); c.moveTo(ax, cy);
      for (let i = 1; i < 14; i++) c.lineTo(lerp(ax, bx, i / 14), cy + (r() - .5) * 36); c.lineTo(bx, cy); c.stroke();
    }
    const outB = t > 71.6 ? (hash(M.last('hat', t).i, 4) < .5 ? .25 : 1) * clamp(1 - (t - 71.6) / 1.2) : 1;
    [[ax, 1], [bx, t >= T_HURT ? outB : 1]].forEach(([x, a]) => {
      const R = 70 * (1 + sp * .3), g = c.createRadialGradient(x, cy, 0, x, cy, R);
      g.addColorStop(0, `rgba(255,248,235,${a})`); g.addColorStop(.14, `rgba(255,210,160,${.7 * a})`); g.addColorStop(1, 'rgba(255,150,80,0)');
      c.fillStyle = g; c.fillRect(x - R, cy - R, 2 * R, 2 * R);
    });
    const { line } = M.lyric(t);
    if (!line) return;
    const size = fit(c, line.text, W * .78, 84, 400, SERIF), a = clamp((t - line.t) / .45) * clamp((line.end - t) / .35);
    c.globalAlpha = a;
    if (line === L_HURT) {                                                          // 「伤害」唱到时变红，跟着鼓点亮
      c.font = fnt(size, 400, SERIF);
      const pre = '可我们还要', post = '彼此就像将士', wp = c.measureText(pre).width, wh = c.measureText('伤害').width, wq = c.measureText(post).width;
      let x = W / 2 - (wp + wh + wq) / 2;
      text(c, pre, x, H * .7, size, { w: 400, fam: SERIF, align: 'left', fill: '#ece6dc' });
      if (t >= T_HURT) {
        const p = hurtRed(t), col = [lerp(150, 255, p), lerp(14, 60, p), lerp(28, 72, p)].map(v => v | 0).join(',');
        c.save(); c.shadowColor = `rgba(255,30,60,${.35 + .55 * p})`; c.shadowBlur = 10 + 26 * p;
        text(c, '伤害', x + wp, H * .7, size, { w: 400, fam: SERIF, align: 'left', fill: `rgb(${col})` }); c.restore();
      } else text(c, '伤害', x + wp, H * .7, size, { w: 400, fam: SERIF, align: 'left', fill: '#ece6dc' });
      text(c, post, x + wp + wh, H * .7, size, { w: 400, fam: SERIF, align: 'left', fill: '#ece6dc' });
    } else {
      c.save(); c.shadowColor = `rgba(255,160,80,${.55 * warmth(t)})`; c.shadowBlur = 26;              // 墨晕：暖色的一圈晕
      text(c, line.text, W / 2, H * .7, size, { w: 400, fam: SERIF, fill: '#ece6dc' }); c.restore();
    }
    c.globalAlpha = 1;
  },
  fx: function (t) { return { lb: lerp(.6, 1, lbIn(t - this.t0, .6)), grade: 'warm', gradeMix: .6 * warmth(t), grain: .14, bloom: .65, bloomThr: .5, ca: .5, vig: .95, scan: .08,
    flash: t >= T_HURT && t < T_HURT + 2 / 30 ? .25 : 0, flashCol: '#ff3040', dark: M.gap(t) ? .15 : 0 }; },
});

// ================================================================ 8 · 大雨
const L_RAIN = lineAt(73.7), L_TRUE = lineAt(75.86), L_LAST = lineAt(78.42);
const T_DROP = M.gap(80.2)?.[1] ?? 80.4;                                             // 点题：鼓停之后鼓回来的那一下
const LIGHT = [L_RAIN.t, M.gap(75)?.[1] ?? 75.37, M.gap(76.6)?.[1] ?? 76.81];
function lightning(t) {
  let v = 0;
  for (const l of LIGHT) { const k = t - l; if (k >= 0 && k < 1) v = Math.max(v, k < .05 ? 1 : k < .1 ? .15 : k < .17 ? .8 : Math.exp(-(k - .17) * 7) * .5); }
  return v;
}
const T_WASH = L_LAST.end;
scene({ name: '大雨', t0: L_RAIN.t, t1: T_WASH + .8,
  weather: (t, lt) => ({ rain: .34, n: Math.round(600 * (t < T_WASH ? 1 : 1 - clamp((t - T_WASH) / 1.5) * .5) * clamp(.3 + lt / .6)), len: 1.2, rt: M.gap(t)?.[0] ?? t, street: .12 }),
  bg: t => ({ mode: 'night', a: '#010309', b: '#0a1430', c: '#5f7dff', amt: .9, speed: .8, pulse: lightning(t) * 1.5 }),
  draw(c, t, lt) {
    const g = M.gap(t), rf = g ? g[0] : t, heavy = t < T_WASH ? 1 : 1 - clamp((t - T_WASH) / 1.5) * .5;
    if (lt < 2) {                                                                   // 转场：桥段剩下的那个光点，留在原位变成路灯
      const k = lt / 2, x = W / 2 - 625, y = H * .37, R = lerp(70, 150, k);
      const col = [lerp(255, 120, k), lerp(220, 160, k), lerp(170, 255, k)].map(v => v | 0).join(',');
      const gg = c.createRadialGradient(x, y, 0, x, y, R); gg.addColorStop(0, `rgba(${col},${.9 * (1 - k * .6)})`); gg.addColorStop(1, `rgba(${col},0)`);
      c.fillStyle = gg; c.fillRect(x - R, y - R, 2 * R, 2 * R);
    }
    const { line } = M.lyric(t);
    const wash = t > T_WASH ? easeIn((t - T_WASH) / .7) : 0;
    if (line === L_RAIN) {                                                          // 字像雨痕一样出现在窗玻璃上
      const tf = M.gap(t)?.[0] ?? t;                                                // 鼓停时字和雨一起冻住
      const size = 96, y0 = H * .52; c.font = fnt(size, 400, SERIF);
      const ws = line.words.map(q => c.measureText(q.w).width); let x = W / 2 - ws.reduce((a, b) => a + b, 0) / 2;
      const out = clamp((line.end - tf) / .45), wash = 1 - out;                      // 退场：往下拉长、淡掉，像被雨冲掉
      line.words.forEach((q, k) => {
        const dt = tf - q.t + .06, cx = x + ws[k] / 2; x += ws[k];
        if (dt <= 0) return;
        const settle = easeOut(dt / .22), streak = 1 - settle;
        c.save(); c.translate(cx, y0 + wash * 50);
        for (let j = 6; j >= 1; j--) {                                              // 出现：一道竖向拉长、带残影的雨丝，收拢成字
          const a = streak * .22 * (1 - j / 7) + wash * .12 * (1 - j / 7);
          if (a < .01) continue;
          c.save(); c.globalAlpha = a * out; c.translate(0, -j * (30 * streak) + j * 18 * wash); c.scale(1, 1 + .5 * streak + .4 * wash);
          text(c, q.w, 0, 0, size, { w: 400, fam: SERIF, fill: '#cfdcff' }); c.restore();
        }
        c.globalAlpha = settle * out; c.scale(1, 1 + .5 * streak + .35 * wash);
        c.shadowColor = 'rgba(160,190,255,.55)'; c.shadowBlur = 14;
        text(c, q.w, 0, -38 * streak, size, { w: 400, fam: SERIF, fill: '#e9efff' });
        c.restore();
        const r = rand(k * 37 + 5), nd = 1 + (r() < .5 ? 1 : 0);                     // 停留：底部慢慢淌下一两道细水痕
        for (let d = 0; d < nd; d++) {
          const dx = (r() - .5) * ws[k] * .7, len = easeOut((dt - .3 - r() * .4) / 2.6) * (30 + r() * 90), top = y0 + size * .32;
          if (len <= 1) continue;
          const g = c.createLinearGradient(0, top, 0, top + len);
          g.addColorStop(0, `rgba(210,225,255,${.45 * out})`); g.addColorStop(1, 'rgba(210,225,255,0)');
          c.fillStyle = g; c.fillRect(cx + dx - 1, top, 2, len);
          c.fillStyle = `rgba(225,235,255,${.55 * out})`; c.beginPath(); c.arc(cx + dx, top + len, 2.6, 0, 7); c.fill();
        }
      });
    } else if (line === L_TRUE) {
      const size = 82; c.font = fnt(size, 400, SERIF); const a = clamp((t - line.t) / .35) * clamp((line.end - t) / .3);
      const ws = line.words.map(q => c.measureText(q.w).width); let x = W / 2 - ws.reduce((p, q) => p + q, 0) / 2;
      c.globalAlpha = a;
      line.words.forEach((q, k) => {
        const lie = q.w === '假' && t > q.t, alt = lie && frameNo(t) % 4 < 2;
        c.save(); if (lie) c.translate((hash(frameNo(t), 7) - .5) * 18, (hash(frameNo(t), 8) - .5) * 10);
        text(c, q.w, x + ws[k] / 2, H * .52, size, { w: alt ? 900 : 400, fam: alt ? SANS : SERIF, fill: lie ? C.red : q.w === '真' ? '#ffffff' : '#dfe6f5' }); c.restore();
        x += ws[k];
      });
      c.globalAlpha = 1;
    } else if (line === L_LAST || (t >= T_WASH && t < T_WASH + .8)) {
      const l = L_LAST, size = 78; c.font = fnt(size, 400, SERIF);
      const pre = '就别再想着再跟我说', wp = c.measureText(pre).width, wl = c.measureText('谎话').width, x = W / 2 - (wp + wl) / 2, y = H * .52;
      const a = clamp((t - l.t) / .5);
      c.save(); c.globalAlpha = a * (1 - wash); c.translate(0, wash * 220); text(c, pre, x, y, size, { w: 400, fam: SERIF, align: 'left', fill: '#dfe6f5' }); c.restore();
      if (t < T_DROP) { c.globalAlpha = a; text(c, '谎话', x + wp, y, size, { w: 400, fam: SERIF, align: 'left', fill: '#dfe6f5' }); c.globalAlpha = 1; }
      else {
        const k = t - T_DROP, s = 1.35 + .3 * Math.exp(-k * 10), sink = t > T_WASH ? easeIn((t - T_WASH) / 1) * 160 : 0;
        chrome(c, '谎话', x + wp - 10, y + sink, size * s, { align: 'left', sweep: k * .9 });
        flare(c, x + wp + wl * .7, y + sink, Math.exp(-k * 2.5) * 1.2);
      }
    }
    rain(c, rf, { n: 70, spd: 2400, len: 170, alpha: .3, wind: .27, seed: 12, lw: 2.6 });   // 字前面
    droplets(c, t, 34, 44, .9);
  },
  fx: t => { const L = lightning(t), g = M.gap(t), drop = t >= T_DROP ? Math.exp(-(t - T_DROP) * 8) : 0;
    return { lb: 1, grade: g && t > 79.9 && t < T_DROP ? 'silver' : 'cold', gradeMix: g && t > 79.9 && t < T_DROP ? 1 : .7, ca: 1 + L * 4 + drop * 6,
      flash: Math.max(L * .75, drop * .7), flashCol: drop > L ? '#ffffff' : '#cfe0ff', bloom: .55 + drop, zoom: 1 + drop * .06, barrel: .03 + drop * .2 }; },
});

// ================================================================ 9 · 余震：镜头拉远，雨在一台旧电视里
const TVC = mk(), tvx = TVC.getContext('2d');
const NOISE = Array.from({ length: 4 }, (_, k) => { const c = mk(320, 180), x = c.getContext('2d'), im = x.createImageData(320, 180), r = rand(200 + k);
  for (let i = 0; i < im.data.length; i += 4) { const v = r() * 255 | 0; im.data[i] = im.data[i + 1] = im.data[i + 2] = v; im.data[i + 3] = 255; } x.putImageData(im, 0, 0); return c; });
const T_TV = T_WASH + .8, T_BACK = M.gap(88.5)?.[0] ?? 88.38, T_SLAM = D.kicks.find(k => k > 88.4);
function tvRect(s) { const w = W * s, h = H * s; return { x: (W - w) / 2, y: (H - h) / 2 - 40 * (1 - s) / .58, w, h }; }
// 余震里两下孤立的底鼓（心跳回来）：电视里闪过 0.22 秒的回忆——先是聊天记录，再是那两团光（给后面的「回放」埋伏笔）
const TV_MEM = D.kicks.filter(k => k > T_TV && k < T_BACK).map((k, i) => ({ k, mt: [46.4, 66.0][i % 2], bg: [['#04060b', '#121a2c'], ['#1a0904', '#5a2a10']][i % 2] }));
function tvOSD(t) {                                                                 // 录像机屏显：左上频道，右下 23:47（冒号一秒闪一次）
  pixel(tvx, 'CH 03', 110, 110, 64, { align: 'left', fill: 'rgba(220,255,225,.85)', shadow: 'rgba(0,0,0,.7)' });
  pixel(tvx, mod(t, 1) < .5 ? '23:47' : '23 47', W - 110, H - 110, 64, { align: 'right', fill: 'rgba(220,255,225,.85)', shadow: 'rgba(0,0,0,.7)' });
}
function tvContent(t, staticOn) {
  tvx.setTransform(1, 0, 0, 1, 0, 0);
  const mem = TV_MEM.find(m => t >= m.k && t < m.k + .22);
  if (mem) {
    const s = sceneAt(mem.mt), mt = mem.mt - (t - mem.k) * 1.5;
    const g = tvx.createRadialGradient(W / 2, H / 2, 0, W / 2, H / 2, W * .7); g.addColorStop(0, mem.bg[1]); g.addColorStop(1, mem.bg[0]);
    tvx.fillStyle = g; tvx.fillRect(0, 0, W, H);
    tvx.save(); s.draw.call(s, tvx, mt, mt - s.t0); tvx.restore();
    tvOSD(t); return TVC;
  }
  const g = tvx.createLinearGradient(0, 0, 0, H); g.addColorStop(0, '#0a1430'); g.addColorStop(1, '#22356e'); tvx.fillStyle = g; tvx.fillRect(0, 0, W, H);
  if (staticOn) { tvx.imageSmoothingEnabled = false; tvx.drawImage(NOISE[frameNo(t) % 4], 0, 0, W, H); tvx.imageSmoothingEnabled = true; tvOSD(t); return TVC; }
  street(tvx, t, .3);
  if (t < T_TV + 2.2) {                                                            // 转场：上一幕的铬金属「谎话」还在电视里往下沉
    tvx.save(); tvx.globalAlpha = clamp(1 - (t - T_TV) / 2.2);
    chrome(tvx, '谎话', W / 2 + 150, H * .52 + 160 + (t - T_TV) * 140, 105, {}); tvx.restore();
  }
  worldRain(tvx, t, { rain: .6, n: 360, len: 1.3, col: '200,215,255', lw: 1.6, rt: M.gap(t)?.[0] ?? t });
  tvOSD(t); return TVC;
}
function drawTV(c, t, s, o = {}) {
  const r = tvRect(s), jump = o.jump ?? 0;
  c.save(); c.translate(0, -jump * 24);
  const glow = c.createRadialGradient(W / 2, r.y + r.h / 2, r.w * .3, W / 2, r.y + r.h / 2, r.w * 1.3);
  glow.addColorStop(0, `rgba(90,120,255,${.12 + jump * .12})`); glow.addColorStop(1, 'rgba(0,0,0,0)'); c.fillStyle = glow; c.fillRect(0, 0, W, H);
  c.fillStyle = '#0d0e12'; c.beginPath(); c.roundRect(r.x - 40 * s, r.y - 40 * s, r.w + 80 * s, r.h + 80 * s, 40 * s); c.fill();
  c.save(); c.beginPath(); c.roundRect(r.x, r.y, r.w, r.h, 60 * s); c.clip();
  c.drawImage(tvContent(t, o.static), r.x, r.y, r.w, r.h);
  c.fillStyle = 'rgba(0,0,0,.35)'; for (let y = r.y; y < r.y + r.h; y += 4 * Math.max(s, .5)) c.fillRect(r.x, y, r.w, 1.2 * Math.max(s, .5));
  const v = c.createRadialGradient(W / 2, r.y + r.h / 2, r.h * .2, W / 2, r.y + r.h / 2, r.w * .62); v.addColorStop(0, `rgba(255,255,255,${jump * .12})`); v.addColorStop(1, 'rgba(0,0,0,.65)');
  c.fillStyle = v; c.fillRect(r.x, r.y, r.w, r.h);
  c.restore(); c.restore();
}
scene({ name: '余震', t0: T_TV, t1: T_BACK,
  bg: () => ({ mode: 'smoke', a: '#010205', b: '#060a18', amt: .5, speed: .3 }),
  draw(c, t, lt) {
    const s = lerp(1.05, .42, ease(lt / 1.8));
    dust(c, t, 60, 9, .18, '#9fb4ff');
    drawTV(c, t, s, { static: !!M.gap(t), jump: M.hit('kick', t, 9) });
  },
  fx: t => ({ grade: 'cold', gradeMix: .5, grain: .12, vig: 1, hud: .8, hudMode: '◀◀ REW', tc: T_TV - (t - T_TV) * 3, ca: 1 + M.hit('kick', t, 8) * 5 }),
});

// ================================================================ 10 · 回放：底鼓砸进电视，每个鼓点闪回一个镜头
// 第 3 格最长（看得见的约 1 秒）：「对质」的聊天记录倒放——四条消息从最下面一条开始，往上一条条缩回去消失
// （发出去的过程倒过来），最后一条跟着屏幕像 CRT 一样合上。用关键帧控制倒放的时间：每条消息消失时放慢，
// 中间快进；跳过原片的两段鼓停黑屏和"对方正在输入…"。
const REW_CHAT = [[0, 46.62], [.10, 46.31], [.22, 46.172], [.2201, 45.84], [.40, 44.87], [.52, 44.732], [.5201, 44.25],
  [.62, 43.60], [.74, 43.462], [.7401, 42.99], [.84, 42.10], [1.03, 41.885]];
function rewChat(k) {
  for (let i = 1; i < REW_CHAT.length; i++) {
    const [a, ma] = REW_CHAT[i - 1], [b, mb] = REW_CHAT[i];
    if (k < b) return lerp(ma, mb, (k - a) / (b - a));
  }
  return REW_CHAT[REW_CHAT.length - 1][1];
}
const MEM = [[3.0, 'invert'], [25.3, 'flip'], [46.62, 'none', rewChat], [36.0, 'sick'], [33.95, 'red'], [50.2, 'flip'], [57.0, 'none'], [62.9, 'invert'],
  [66.5, 'red'], [74.8, 'flip'], [12.3, 'sick'], [38.0, 'invert'], [30.9, 'flip'], [48.0, 'red'], [68.9, 'flip'], [5.5, 'sick']];
const T_LAST = D.kicks.filter(k => k < 96).pop(), T_CRT2 = T_LAST + 1.25;
const MEV = [...D.kicks, ...D.snares, ...D.notes808.map(n => n.t)].filter(x => x >= T_SLAM && x < T_LAST - .02).sort((a, b) => a - b)
  .filter((x, i, a) => i === 0 || x - a[i - 1] > .09);
// 闪回倒着放：每个镜头从它的时间点以 1.5 倍速往回走（雨往天上飞、字往回弹、碎片往回收）
function memAt(t) {
  const i = lastAt(MEV, t); if (i < 0) return null;
  const [m0, how, spd = 1.5] = MEM[i % MEM.length], s = sceneAt(m0);
  const mt = typeof spd === 'function' ? spd(t - MEV[i]) : m0 - (t - MEV[i]) * spd;   // spd 可以是倍速，也可以是自定义的倒放曲线
  return { mt: Math.max(s.t0 + .005, mt), s, how, i, since: t - MEV[i] };
}
// 录像带倒带的画面毛病：往上扫的跟踪噪声带、底部的磁头噪声、大大的 ◀◀
function rewindFX(c, t) {
  const f = frameNo(t);
  for (let k = 0; k < 3; k++) {
    const h = 22 + hash(k, 41) * 34, y = mod(H - (t * 1150 + k * 430), H + 140) - 70;
    c.fillStyle = 'rgba(255,255,255,.07)'; c.fillRect(0, y, W, h);
    const r = rand(f * 7 + k * 131);
    for (let j = 0; j < 46; j++) { c.fillStyle = `rgba(255,255,255,${.35 + r() * .6})`; c.fillRect(r() * W, y + r() * h, 20 + r() * 150, 1.5 + r() * 2); }
  }
  const r = rand(f * 13 + 7);                                                      // 底部磁头噪声：一条被横向撕扯的带子
  c.fillStyle = 'rgba(0,0,0,.55)'; c.fillRect(0, H - 54, W, 44);
  for (let j = 0; j < 9; j++) { c.fillStyle = `rgba(230,235,255,${.15 + r() * .35})`; c.fillRect(r() * 300 - 150, H - 52 + j * 5, W, 1.5); }
  if (f % 10 < 6) {                                                                 // ◀◀
    c.save(); c.translate(150, 170);
    for (const [col, dx] of [[C.red, 6], ['#fff', 0]]) { c.fillStyle = col;
      for (const ox of [0, 62]) { c.beginPath(); c.moveTo(ox + dx, 6); c.lineTo(ox + 58 + dx, -34 + 6); c.lineTo(ox + 58 + dx, 34 + 6); c.closePath(); c.fill(); } }
    c.restore();
  }
}
scene({ name: '回放', t0: T_BACK, t1: D.kicks.find(k => k > 95) ?? 95.8,
  bg(t) {
    if (t < T_SLAM || M.gap(t)) return { mode: M.gap(t) && t > T_SLAM ? 'static' : 'solid', a: '#000000', amt: 1 };
    if (t >= T_LAST) return { mode: 'tunnel', a: '#000000', b: '#8a96b0', c: '#1a1f2c', amt: .8, speed: 3, pulse: M.hit('note', t, 3) };
    const m = memAt(t), s = m.s; return s.bg.call(s, m.mt, m.mt - s.t0);
  },
  draw(c, t, lt) {
    if (t < T_SLAM) return;
    if (t < T_SLAM + .14) { drawTV(c, t, lerp(.42, 3.4, easeIn((t - T_SLAM) / .14)), { jump: 1 }); return; }
    if (M.gap(t)) return;
    if (t >= T_LAST) {                                                              // 最后一下底鼓：叠成铬金属「谎话」
      const k = t - T_LAST, s = 1 + .5 * Math.exp(-k * 14);
      c.save(); c.translate(W / 2, H / 2); c.scale(s, s); chrome(c, D.title, 0, 0, 560, { sweep: k * .8 }); c.restore();
      flare(c, W / 2 + 120, H / 2, Math.exp(-k * 1.2) * 1.1, 1400);
      return;
    }
    const m = memAt(t), s = m.s;
    const kb = Math.max(M.hit('kick', t, 9), M.hit('note', t, 9)), sc = 1 + .045 * kb;
    const roll = (hash(frameNo(t) >> 1, 61) - .5) * 18 - mod(t * 90, 24);           // 倒带时画面上下滚动、抖
    c.save(); c.translate(W / 2, H / 2 + kb * 14 + roll); c.scale(sc, sc); c.translate(-W / 2, -H / 2);   // 闪回的画面整体跟着鼓点弹
    if (m.how === 'flip') { c.translate(W, 0); c.scale(-1, 1); }
    if (s.weather) drawWorld(c, m.mt, s.weather.call(s, m.mt, m.mt - s.t0));
    s.draw.call(s, c, m.mt, m.mt - s.t0); c.restore();
    rewindFX(c, t);
    const lw = 8 + kb * 22; c.strokeStyle = C.red; c.lineWidth = lw; c.strokeRect(36 + lw / 2 - 4, 36 + lw / 2 - 4, W - 72 - lw + 8, H - 72 - lw + 8);   // 红框随鼓点脉动
  },
  fx(t) {
    const m = t >= T_SLAM && t < T_LAST ? memAt(t) : null, h = m ? Math.exp(-m.since * 14) : 0;
    return { ca: 5 + h * 9, glitch: m ? .25 + h * .5 : 0, gseed: m ? m.i : 0, flash: m ? (m.since < 2 / 30 ? .8 : 0) : (t >= T_LAST && t < T_LAST + .07 ? 1 : 0),
      invert: m && m.how === 'invert' ? 1 : 0, grade: m && (m.how === 'red' || m.how === 'sick') ? m.how : 'silver', gradeMix: m ? (m.how === 'red' || m.how === 'sick' ? 1 : 0) : .6,
      zoom: 1 + h * .1, barrel: .05 + h * .3, bloom: t >= T_LAST ? .9 : .5, hud: .8, hudMode: '◀◀ REW', tc: 88 - (t - 88) * 12,
      dark: t < T_SLAM ? 1 : 0, sq: crt((t - T_CRT2) / .3), bounce: 3, bounce808: true, tear: m ? (m.s.name === '对质' ? .35 : .9) : 0, wave: m ? .1 : 0 };
  },
});

// ================================================================ 11 · GAME OVER
// CONTINUE? 倒数：严格一秒一个数，9 → 0，数字和 CONTINUE? 同时出现；鼓停时倒数跟着暗一下。
// 数到 0 → 标题浮现 → 灯管闪几下熄灭 → 雪花 → 黑屏。
const T_END0 = SCENES[SCENES.length - 1].t1;
const T_CD0 = T_END0 + .1, T_ZERO = T_CD0 + 9, T_TITLE = T_ZERO + .4;
const T_OFF = D.duration - .8, T_STATIC = [T_OFF + .1, T_OFF + .45];             // 灯灭 → 一阵雪花 → 黑
const T_LASTDROP = T_STATIC[1] + .06;                                               // 黑屏里最后落下一滴雨
// 霓虹灯闪烁：大部分时间亮着，随机冒出一串很快的闪（每 1–2 帧切换，有时全灭、有时只暗一下）。
// CONTINUE? 和数字像两根不同的灯管，各闪各的；rate 越大闪得越频繁
function neonOn(t, seed, rate) {
  const slot = Math.floor(t * 6), f = frameNo(t);                                   // 每 1/6 秒决定一次这一小段要不要闪
  if (hash(slot, seed) > rate) return 1;
  const r = hash(f >> (hash(slot, seed + 2) < .5 ? 0 : 1), seed + 1);               // 有的段每帧切换，有的段每 2 帧切换
  return r < .4 ? 0 : r < .6 ? .3 : 1;
}
function lightOn(t) {
  if (t < T_TITLE || t > T_OFF) return 0;
  const left = T_OFF - t;                                                          // 熄灭前越闪越频繁
  if (left < .9 && hash(frameNo(t), 6) < lerp(.6, .1, left / .9)) return .12;
  return clamp((t - T_TITLE) / .35);
}
scene({ name: 'GAME OVER', t0: T_END0, t1: D.duration + 1,
  weather: (t, lt) => ({ rain: .2 * clamp(1 - lt / 8), n: 160 }),
  bg: t => t > T_STATIC[0] && t < T_STATIC[1] ? { mode: 'static', a: '#000000', amt: .8 } : ({ mode: 'smoke', a: '#010103', b: '#0a0f1e', amt: .5 * clamp(1 - (t - 100) / 6), speed: .4 }),
  draw(c, t, lt) {
    if (t < T_TITLE) {
      const n = clamp(9 - Math.floor(t - T_CD0), 0, 9), k = t < T_ZERO ? mod(t - T_CD0, 1) : t - T_ZERO;
      const out = t > T_ZERO + .35 ? (frameNo(t) % 2 ? 1 : 0) : 1;
      if (!out) return;
      const signA = neonOn(t, 81, .42), numA = neonOn(t, 83, .28);
      const typed = 'CONTINUE?'.slice(0, Math.floor(lt / .045));                    // 转场：从 CRT 缩成的点开始打字
      if (lt < .12) { c.fillStyle = '#fff'; c.beginPath(); c.arc(W / 2, H / 2, 5, 0, 7); c.fill(); }
      c.font = `700 96px "${PIX}", monospace`; const fw = c.measureText('CONTINUE?').width;
      c.save(); c.globalAlpha = signA; c.shadowColor = 'rgba(255,40,70,.9)'; c.shadowBlur = 26 * signA;   // 霓虹：一圈红色辉光
      pixel(c, typed, W / 2 - fw / 2, H * .3, 96, { shadow: C.red, align: 'left' }); c.restore();
      if (t >= T_CD0) { c.save(); c.globalAlpha = numA; c.translate(W / 2, H * .58);
        pixel(c, String(n), 0, 0, 330, { fill: n === 0 ? C.red : C.white, shadow: n === 0 ? C.black : C.red }); c.restore(); }
      c.globalAlpha = 1;
      return;
    }
    if (t >= T_LASTDROP) {                                                          // 最后一滴雨：从画面中间落下去（呼应开头）
      const k = (t - T_LASTDROP) / (D.duration - .03 - T_LASTDROP), y = lerp(H / 2, H + 140, easeIn(k));
      c.fillStyle = '#000'; c.fillRect(0, 0, W, H);
      c.strokeStyle = 'rgba(230,238,255,.9)'; c.lineWidth = 3; c.beginPath(); c.moveTo(W / 2, y - 120 * Math.min(1, k * 2)); c.lineTo(W / 2, y); c.stroke();
      c.fillStyle = '#fff'; c.beginPath(); c.arc(W / 2, y, 4, 0, 7); c.fill();
      return;
    }
    const a = lightOn(t);
    if (a <= 0) return;
    c.globalAlpha = a;
    chrome(c, D.title, W / 2, H / 2 - 70, 300, { sweep: (t - T_TITLE) * .6 });
    chrome(c, D.artist, W / 2, H / 2 + 150, 84, { fam: GOTH, w: 400 });
    text(c, '2026', W / 2, H / 2 + 240, 24, { w: 400, fam: MONO, fill: 'rgba(255,255,255,.6)' });
    c.globalAlpha = 1;
  },
  fx: t => ({ grain: .1, scan: .18, ca: .8, bloom: .6, flash: t >= T_TITLE && t < T_TITLE + .06 ? .4 : 0, dark: t > T_STATIC[1] && t < T_LASTDROP ? 1 : 0,
    glitch: t > T_STATIC[0] && t < T_STATIC[1] ? .6 : 0, gseed: frameNo(t) }),
});
