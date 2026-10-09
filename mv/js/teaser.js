'use strict';
// ============================================================================
// 《谎话》竖屏预告 · 1080×1920 · 15 秒 · 朦胧（分镜见 teaser/STORYBOARD.md）
// 隔着一扇起雾的玻璃回想那个下雨的夜晚：全程失焦，只有唱到「谎话」的那一下对焦清楚。
// 不用 MV 的画面，只沿用 MV 的世界（雨夜、冷蓝、一点红、TAPE 01、23:47、最后一滴雨）。
// 时间点和 teaser/audio.py 对齐：「我」1.74s，「谎」10.10s，磁带 11.0s 开始卡住、11.65s 停死。
// 约定同 MV：画面只依赖 t；随机数一律 rand(seed) / hash(i)。
// ============================================================================
const T_DUR = 15;
const T_WRITE = [1.55, 4.5];       // 手指在雾上写 23:47
const T_PHONE = [5.1, 7.2];        // 玻璃后面手机亮起
const T_CAR = [7.0, 9.7];          // 一辆车的车灯从玻璃后面横扫过去
const T_PUSH = 9.0;                // 镜头慢慢推近、雨变密
const T_HUANG = 10.10;             // 「谎」：对焦清楚
const T_BLUR = 10.95;              // 雾重新盖回来
const T_STOP = [11.0, 11.65];      // 磁带卡住：画面里的动作跟着慢下来、停住
const T_CARD = 11.75;              // 片尾
const T_DROP = 13.85;              // 最后一滴雨
const T_FADE = 14.55;

// 磁带卡住时画面跟着"走慢"：返回画面用的时间
function vt(t) {
  if (t < T_STOP[0]) return t;
  const L = T_STOP[1] - T_STOP[0], k = Math.min(t - T_STOP[0], L);
  return T_STOP[0] + L / 2.6 * (1 - Math.pow(1 - k / L, 2.6));   // ∫(1-k/L)^1.6，和音频的减速曲线一样
}

// ---------------------------------------------------------------- 画布
const BG = mk(), bgx = BG.getContext('2d');          // 背景：夜色 + 失焦的路灯光斑
const MASK = mk(), mkx = MASK.getContext('2d');      // 雾被擦掉的地方：手指写的字、水痕
const FOG = mk(), fgx = FOG.getContext('2d');
const SHARP = mk(), shx = SHARP.getContext('2d');    // 擦掉雾的地方：看得清楚一点的背景
const PH = mk(520, 860), phx = PH.getContext('2d');  // 玻璃后面的手机
const FOGTEX = (() => {                               // 雾不是均匀的：一层很淡的大块明暗
  const c = mk(), x = c.getContext('2d'), r = rand(77);
  for (let i = 0; i < 70; i++) {
    const px = r() * W, py = r() * H, R = 120 + r() * 420, g = x.createRadialGradient(px, py, 0, px, py, R);
    g.addColorStop(0, `rgba(255,255,255,${.05 + r() * .08})`); g.addColorStop(1, 'rgba(255,255,255,0)');
    x.fillStyle = g; x.fillRect(px - R, py - R, R * 2, R * 2);
  }
  return c;
})();

// ---------------------------------------------------------------- 背景：失焦的城市夜色
const BOKEH = (() => {
  const r = rand(5), cols = ['90,124,255', '90,124,255', '130,160,255', '200,215,255', '255,176,102', '255,176,102', '255,74,94'];
  return Array.from({ length: 26 }, () => ({ x: r(), y: .08 + r() * .9, R: 50 + r() * r() * 230, col: cols[Math.floor(r() * cols.length)], a: .18 + r() * .4, ph: r() * 6.28, sp: .2 + r() * .5 }));
})();
function drawBG(t, push, dim) {
  const c = bgx; c.setTransform(1, 0, 0, 1, 0, 0); c.globalAlpha = 1;
  const g = c.createLinearGradient(0, 0, 0, H); g.addColorStop(0, '#01030a'); g.addColorStop(.55, '#050c22'); g.addColorStop(1, '#0b1636');
  c.fillStyle = g; c.fillRect(0, 0, W, H);
  c.save(); c.translate(W / 2, H * .48); c.scale(push, push); c.translate(-W / 2, -H * .48);
  c.globalCompositeOperation = 'lighter';
  BOKEH.forEach(b => {
    const x = b.x * W + Math.sin(t * .11 * b.sp + b.ph) * 40, y = b.y * H + Math.cos(t * .07 * b.sp + b.ph) * 26;
    const a = b.a * dim * (.8 + .2 * Math.sin(t * 1.3 * b.sp + b.ph)), R = b.R;
    const rg = c.createRadialGradient(x, y, 0, x, y, R);
    rg.addColorStop(0, `rgba(${b.col},${a})`); rg.addColorStop(.62, `rgba(${b.col},${a * .7})`); rg.addColorStop(1, `rgba(${b.col},0)`);
    c.fillStyle = rg; c.beginPath(); c.arc(x, y, R, 0, 7); c.fill();
  });
  // 车灯：一对暖白的灯从右往左扫过去，前面一大片光晕
  const ck = (t - T_CAR[0]) / (T_CAR[1] - T_CAR[0]);
  if (ck > 0 && ck < 1) {
    const x = lerp(W * 1.35, -W * .35, ease(ck)), y = H * .63, a = Math.sin(Math.PI * ck);
    const wash = c.createRadialGradient(x, y, 0, x, y, 620); wash.addColorStop(0, `rgba(255,232,200,${.32 * a})`); wash.addColorStop(1, 'rgba(255,232,200,0)');
    c.fillStyle = wash; c.fillRect(x - 620, y - 620, 1240, 1240);
    [-115, 115].forEach(dx => {
      const g2 = c.createRadialGradient(x + dx, y, 0, x + dx, y, 150); g2.addColorStop(0, `rgba(255,250,240,${.95 * a})`); g2.addColorStop(.25, `rgba(255,236,205,${.6 * a})`); g2.addColorStop(1, 'rgba(255,220,180,0)');
      c.fillStyle = g2; c.beginPath(); c.arc(x + dx, y, 150, 0, 7); c.fill();
    });
  }
  c.restore(); c.globalCompositeOperation = 'source-over';
}
const carAt = t => { const ck = (t - T_CAR[0]) / (T_CAR[1] - T_CAR[0]); return ck > 0 && ck < 1 ? { x: lerp(W * 1.35, -W * .35, ease(ck)), y: H * .63, a: Math.sin(Math.PI * ck) } : null; };

// ---------------------------------------------------------------- 手指在雾上写「23:47」
// 字形是手写的几笔（单位格：宽 1，高 1.6），一个字一个字连着写，笔画之间稍微停一下
const GLYPHS = {
  2: [[[.08, .4], [.22, .13], [.5, .03], [.8, .13], [.88, .4], [.74, .72], [.4, 1.06], [.07, 1.5], [.96, 1.47]]],
  3: [[[.1, .16], [.46, .03], [.83, .16], [.85, .43], [.5, .7], [.88, .96], [.9, 1.26], [.55, 1.5], [.12, 1.4]]],
  ':': [[[.5, .52], [.5, .6]], [[.5, 1.15], [.5, 1.23]]],
  4: [[[.64, .03], [.07, 1.05], [.98, 1.04]], [[.73, .45], [.74, 1.56]]],
  7: [[[.05, .07], [.94, .05], [.42, 1.56]]],
};
const WRITE = (() => {                                 // 把所有笔画排成一条时间线：[{pts, len, t0, t1}]
  const str = '23:47', U = 140, gap = 34, widths = [...str].map(ch => ch === ':' ? .55 : 1);
  const total = widths.reduce((a, b) => a + b, 0) * U + gap * (str.length - 1);
  let x0 = (W - total) / 2; const y0 = H * .34, strokes = [];
  [...str].forEach((ch, i) => {
    const w = widths[i] * U, r = rand(i * 31 + 7);
    GLYPHS[ch].forEach(st => strokes.push(st.map(([u, v]) => [x0 + (ch === ':' ? u * w * 2 - w * .5 : u * w) + (r() - .5) * 8, y0 + v * U + (r() - .5) * 8])));
    x0 += w + gap;
  });
  const lens = strokes.map(p => p.slice(1).reduce((s, q, k) => s + Math.hypot(q[0] - p[k][0], q[1] - p[k][1]), 0));
  const pause = .09, sum = lens.reduce((a, b) => a + b, 0), span = T_WRITE[1] - T_WRITE[0] - pause * (strokes.length - 1);
  let tt = T_WRITE[0];
  return strokes.map((pts, k) => { const d = span * lens[k] / sum, o = { pts, len: lens[k], t0: tt, t1: tt + d }; tt += d + pause; return o; });
})();
// 写完以后，字的底部积水往下淌出几道水痕
// [第几笔, 第几个点, 淌多长]：2 的底、4 的竖、3 的尾、7 的底
const DRIPS = [[0, 8, 260], [5, 1, 380], [1, 8, 200], [6, 2, 330]].map(([s, p, L], i) => ({ s, p, L, t0: WRITE[s].t1 + .4 + i * .25 }));
function strokePartial(c, pts, len, k) {
  let left = len * k; c.beginPath(); c.moveTo(pts[0][0], pts[0][1]);
  for (let i = 1; i < pts.length && left > 0; i++) {
    const [ax, ay] = pts[i - 1], [bx, by] = pts[i], d = Math.hypot(bx - ax, by - ay), f = Math.min(1, left / d);
    c.lineTo(ax + (bx - ax) * f, ay + (by - ay) * f); left -= d;
  }
  c.stroke();
}
// 雾慢慢重新盖回来：写完 0.7 秒后开始，2.4 秒盖满
const refog = t => 1 - ease((t - T_WRITE[1] - .7) / 2.4);

// ---------------------------------------------------------------- 玻璃上的水
// 往下滑的大水滴（划过的地方把雾擦掉，留下一道水痕）；满屏的小水珠（最前景）
const SLIDERS = (() => {
  const r = rand(11);
  return Array.from({ length: 22 }, (_, i) => ({ x: r() * W, y0: r() * H * .7 - H * .1, t0: r() * 12, v: 60 + r() * 140, R: 7 + r() * 9, wob: r() * 6, dense: i >= 14 }));
})();
const BEADS = (() => { const r = rand(31); return Array.from({ length: 150 }, () => ({ x: r() * W, y: r() * H, R: 2 + r() * r() * 9, a: .4 + r() * .5 })); })();
function sliderY(s, v) {                              // 走走停停：水滴在玻璃上不是匀速滑的
  const k = Math.max(0, v - s.t0); return s.y0 + s.v * (k + .35 * Math.sin(k * 1.7 + s.wob) - .35 * Math.sin(s.wob));
}
function drawDrop(c, x, y, R, a) {
  c.save(); c.globalAlpha = a; c.translate(x, y); c.scale(1, 1.15);
  const g = c.createRadialGradient(0, R * .35, 0, 0, R * .1, R);
  g.addColorStop(0, 'rgba(215,228,255,.55)'); g.addColorStop(.6, 'rgba(120,150,230,.18)'); g.addColorStop(.93, 'rgba(8,14,40,.35)'); g.addColorStop(1, 'rgba(8,14,40,0)');
  c.fillStyle = g; c.beginPath(); c.arc(0, 0, R, 0, 7); c.fill();
  c.fillStyle = 'rgba(255,255,255,.8)'; c.beginPath(); c.arc(-R * .3, -R * .38, R * .14, 0, 7); c.fill();
  c.restore();
}

// ---------------------------------------------------------------- 很淡的歌词碎片（雾后面）
const FRAGS = [
  { s: '我的心像大雨落下', t0: 1.9, t1: 4.9, x: .5, y: .62, size: 62 },
  { s: '有真有假', t0: 6.0, t1: 8.6, x: .3, y: .24, size: 84 },
  { s: '别再想着', t0: 8.9, t1: 10.05, x: .64, y: .74, size: 72 },
];

// ---------------------------------------------------------------- 一帧
const LAY = mk(), lx = LAY.getContext('2d');
function drawLayer(t) {
  const c = lx; c.setTransform(1, 0, 0, 1, 0, 0); c.globalAlpha = 1; c.globalCompositeOperation = 'source-over'; c.filter = 'none';
  const v = vt(t), card = t >= T_CARD;
  const focus = t < T_HUANG - .02 ? 0 : t < T_BLUR ? clamp((t - T_HUANG + .02) / .06) : 1 - ease((t - T_BLUR) / .65);
  const push = 1 + .12 * ease((v - T_PUSH) / 2.6) + .03 * focus;
  const stopDim = t < T_STOP[0] ? 1 : 1 - ease((t - T_STOP[0]) / (T_STOP[1] - T_STOP[0]));

  // 背景（失焦）
  const bgBlur = card ? 16 : lerp(lerp(14, 22, ease((v - T_PUSH) / 2.6)), 3, focus);
  drawBG(card ? t * .4 : v, push, card ? .4 * clamp((t - T_CARD) / 1.2) : .75);
  c.fillStyle = '#000'; c.fillRect(0, 0, W, H);
  c.filter = `blur(${bgBlur.toFixed(1)}px)`; c.drawImage(BG, 0, 0); c.filter = 'none';

  if (!card) {
    // 玻璃后面的手机：亮起 → 一条消息 → 暗下（糊得看不清内容）
    const pk = t - T_PHONE[0];
    if (pk > 0 && t < T_PHONE[1] + .4) {
      const a = clamp(pk / .15) * (pk < .3 ? (frameNo(t) % 3 ? 1 : .55) : 1) * clamp((T_PHONE[1] + .35 - t) / .35);
      phx.setTransform(1, 0, 0, 1, 0, 0); phx.clearRect(0, 0, 520, 860);
      phx.fillStyle = '#0c0f1a'; phx.beginPath(); phx.roundRect(140, 90, 240, 500, 44); phx.fill();          // 机身
      const sg = phx.createLinearGradient(0, 104, 0, 576); sg.addColorStop(0, '#9fb3ff'); sg.addColorStop(.6, '#5a72d8'); sg.addColorStop(1, '#2b3a86');
      phx.fillStyle = sg; phx.beginPath(); phx.roundRect(152, 102, 216, 476, 34); phx.fill();                // 亮屏
      phx.fillStyle = '#0c0f1a'; phx.beginPath(); phx.roundRect(232, 112, 56, 14, 7); phx.fill();             // 刘海
      phx.fillStyle = 'rgba(240,244,255,.95)'; phx.fillRect(205, 180, 110, 34);                               // 锁屏时间
      const nk = easeOut((pk - .45) / .25);
      if (nk > 0) { phx.globalAlpha = nk; phx.fillStyle = '#f4f6ff'; phx.beginPath(); phx.roundRect(166, 300 - (1 - nk) * 24, 188, 64, 14); phx.fill(); phx.globalAlpha = 1; }
      c.save(); c.globalAlpha = a * .8; c.filter = 'blur(16px)'; c.drawImage(PH, W * .64 - 260, H * .6 - 430); c.restore();   // 手机本身（糊）
      c.save(); c.globalAlpha = a * .5; c.globalCompositeOperation = 'lighter'; c.filter = 'blur(60px)'; c.drawImage(PH, W * .64 - 260, H * .6 - 430); c.restore();   // 屏幕照出来的光
    }
    // 歌词碎片：很淡、发虚、慢慢往上飘
    FRAGS.forEach(f => {
      if (t < f.t0 || t > f.t1) return;
      const k = (t - f.t0) / (f.t1 - f.t0), a = Math.sin(Math.PI * k) * .2;
      c.save(); c.filter = 'blur(3px)'; c.globalAlpha = a;
      text(c, f.s, f.x * W, f.y * H - k * 40, f.size, { w: 400, fam: SERIF, fill: '#dfe6ff' }); c.restore();
    });
  }

  // 雾被擦掉的地方（MASK）：手指写的字 + 积水往下淌 + 水滴滑过的水痕
  const m = mkx; m.setTransform(1, 0, 0, 1, 0, 0); m.clearRect(0, 0, W, H); m.lineCap = 'round'; m.lineJoin = 'round'; m.strokeStyle = '#fff';
  if (!card && t > T_WRITE[0]) {
    m.globalAlpha = clamp(refog(t)) * .82; m.lineWidth = 42; m.filter = 'blur(7px)';                      // 手指擦过：边缘是软的，还留一点残雾
    WRITE.forEach(w => { if (t > w.t0) strokePartial(m, w.pts, w.len, clamp((t - w.t0) / (w.t1 - w.t0))); });
    m.lineWidth = 11;
    DRIPS.forEach(d => { const k = easeOut((t - d.t0) / 1.8); if (k <= 0) return; const [x, y] = WRITE[d.s].pts[d.p]; m.beginPath(); m.moveTo(x, y); m.lineTo(x + 3, y + d.L * k); m.stroke(); });
    m.filter = 'none';
  }
  const sv = card ? (t - T_CARD) * .7 + 4 : v;
  SLIDERS.forEach(s => {
    if (s.dense && !card && v < T_PUSH) return;
    const y = sliderY(s, sv); if (sv < s.t0 || y > H + 40) return;
    const top = Math.max(s.y0, y - s.v * 2.6), g = m.createLinearGradient(0, top, 0, y);
    g.addColorStop(0, 'rgba(255,255,255,0)'); g.addColorStop(1, 'rgba(255,255,255,.45)');
    m.globalAlpha = 1; m.strokeStyle = g; m.lineWidth = s.R * .6; m.filter = 'blur(2px)'; m.beginPath(); m.moveTo(s.x, top); m.lineTo(s.x + Math.sin(y * .01) * 3, y); m.stroke(); m.filter = 'none';
  });
  // 擦掉的地方看得清楚一点：没那么糊的背景，只留在 MASK 里
  shx.setTransform(1, 0, 0, 1, 0, 0); shx.globalCompositeOperation = 'source-over'; shx.clearRect(0, 0, W, H);
  shx.filter = 'blur(4px)'; shx.drawImage(BG, 0, 0); shx.filter = 'none';
  shx.globalCompositeOperation = 'destination-in'; shx.drawImage(MASK, 0, 0); shx.globalCompositeOperation = 'source-over';
  c.drawImage(SHARP, 0, 0);
  const f = fgx; f.setTransform(1, 0, 0, 1, 0, 0); f.globalCompositeOperation = 'source-over'; f.globalAlpha = 1; f.clearRect(0, 0, W, H);
  // 雾：整块盖上去，再用 MASK 擦掉
  const fogA = card ? .62 : lerp(lerp(.6, .72, ease((v - T_PUSH) / 2.6)), .05, focus);
  f.fillStyle = `rgba(34,44,70,${fogA})`; f.fillRect(0, 0, W, H);
  f.globalAlpha = fogA * .55; f.drawImage(FOGTEX, 0, 0); f.globalAlpha = 1;
  f.globalCompositeOperation = 'destination-out'; f.drawImage(MASK, 0, 0); f.globalCompositeOperation = 'source-over';
  c.drawImage(FOG, 0, 0);
  const car = !card && carAt(v);
  if (car) {                                                                      // 车灯照在起雾的玻璃上，雾被照亮一大片
    const g = c.createRadialGradient(car.x, car.y, 0, car.x, car.y, 700); g.addColorStop(0, `rgba(255,228,196,${.2 * car.a})`); g.addColorStop(1, 'rgba(255,228,196,0)');
    c.save(); c.globalCompositeOperation = 'lighter'; c.fillStyle = g; c.fillRect(0, 0, W, H); c.restore();
  }

  // 玻璃最前面的小水珠（推近时更密）
  const beadN = card ? 90 : Math.round(lerp(70, 150, ease((v - T_PUSH) / 2.6)));
  for (let i = 0; i < beadN; i++) { const b = BEADS[i]; drawDrop(c, b.x, b.y, b.R, b.a * (card ? .6 : 1) * clamp(t / .8)); }
  SLIDERS.forEach(s => {
    if (s.dense && !card && v < T_PUSH) return;
    const y = sliderY(s, sv); if (sv < s.t0 || y > H + 40) return;
    drawDrop(c, s.x + Math.sin(y * .01) * 3, y, s.R, .9);
  });

  if (!card) {
    // 测谎仪：一道很细的红线从底部划过，唱到「谎」时冲一下
    if (t > 9.15 && t < T_STOP[1]) {
      const a = clamp((t - 9.15) / .4) * stopDim * .55, base = H * .86, spike = Math.exp(-Math.abs(t - T_HUANG) * 9);
      c.save(); c.strokeStyle = `rgba(230,30,55,${a})`; c.lineWidth = 2.2; c.shadowColor = 'rgba(255,40,70,.9)'; c.shadowBlur = 14; c.filter = 'blur(.6px)';
      c.beginPath();
      for (let x = 0; x <= W; x += 12) {
        const u = x / W, n = Math.sin(u * 37 + v * 9) * 7 + Math.sin(u * 91 - v * 13) * 4 + (u > .55 && u < .75 ? -Math.sin((u - .55) / .2 * Math.PI) * 260 * spike : 0);
        x ? c.lineTo(x, base + n) : c.moveTo(x, base + n);
      }
      c.stroke(); c.restore();
    }
    // 「谎话」：对焦清楚的那一下，然后被雾吞回去
    if (t >= T_HUANG - .02) {
      const a = clamp((t - T_HUANG + .02) / .04) * (1 - .85 * ease((t - T_BLUR) / .65)), bl = 18 * ease((t - T_BLUR) / .65);
      c.save(); c.globalAlpha = a; if (bl > .3) c.filter = `blur(${bl.toFixed(1)}px)`;
      const s = 1 + .04 * Math.exp(-(t - T_HUANG) * 6);
      c.translate(W / 2, H * .47); c.scale(s, s); chrome(c, '谎话', 0, 0, 330, { sweep: (t - T_HUANG) * 1.4 }); c.restore();
    }
  } else {
    // 片尾：dangao_w / 10.17 从雾里浮出来；角落 TAPE 01；最后一滴雨
    const k = t - T_CARD, a = ease(k / .9), bl = lerp(10, 1.4, ease(k / 1.1));
    c.save(); c.globalAlpha = a; c.filter = `blur(${bl.toFixed(1)}px)`;
    chrome(c, 'dangao_w', W / 2, H * .45, 120, { fam: GOTH, w: 400 });
    text(c, '10.17', W / 2, H * .545, 210, { w: 400, fam: ANTON, fill: '#e9edf5' });
    text(c, 'MV · 2026', W / 2, H * .615, 30, { w: 400, fam: MONO, fill: 'rgba(233,237,245,.7)' });
    c.restore();
    if (k > .3) {
      c.save(); c.globalAlpha = .55 * clamp((k - .3) / .5);
      if (frameNo(t) % 30 < 18) { c.fillStyle = C.red; c.beginPath(); c.arc(78, 118, 9, 0, 7); c.fill(); }
      pixel(c, 'TAPE 01', 102, 120, 30, { align: 'left', fill: '#e9edf5' }); c.restore();
    }
    if (t >= T_DROP) {                                // 最后一滴雨从画面中间落下去（呼应 MV 的开头和结尾）
      const dk = clamp((t - T_DROP) / .6), y = lerp(H * .66, H + 140, easeIn(dk));
      c.strokeStyle = 'rgba(230,238,255,.9)'; c.lineWidth = 3; c.beginPath(); c.moveTo(W / 2, y - 130 * Math.min(1, dk * 2)); c.lineTo(W / 2, y); c.stroke();
      c.fillStyle = '#fff'; c.beginPath(); c.arc(W / 2, y, 4.5, 0, 7); c.fill();
    }
  }

  // 后期
  const fx = { zoom: 1 + .015 * focus * Math.exp(-(t - T_HUANG) * 5), rot: Math.sin(t * .13) * .003, barrel: .03, shift: [Math.sin(t * .21) * .002, Math.cos(t * .17) * .002], sq: [1, 1],
    ca: 1.2 + focus * 1.5, split: 0, glitch: 0, gseed: frameNo(t), wave: 0, tear: 0, invert: 0, xerox: 0,
    grade: 'cold', gradeMix: .45, bloom: .5 + focus * .3, bloomThr: .5, flash: t >= T_HUANG && t < T_HUANG + 2 / 30 ? .32 : 0, flashCol: '#ffffff',
    dark: t < .7 ? 1 - ease(t / .7) : t < T_CARD ? (t > T_STOP[0] ? .92 * ease((t - T_STOP[0]) / (T_STOP[1] - T_STOP[0])) : 0) : ease((t - T_FADE) / (T_DUR - .05 - T_FADE)),
    grain: .2, scan: .1, vig: 1.05, lb: 0 };
  // 磁带卡住时：一条跟踪噪声带从下往上扫过
  if (t > T_STOP[0] && t < T_STOP[1]) {
    const y = lerp(H, -80, (t - T_STOP[0]) / (T_STOP[1] - T_STOP[0])), r = rand(frameNo(t));
    c.fillStyle = 'rgba(255,255,255,.08)'; c.fillRect(0, y, W, 70);
    for (let j = 0; j < 30; j++) { c.fillStyle = `rgba(255,255,255,${.3 + r() * .5})`; c.fillRect(r() * W, y + r() * 70, 20 + r() * 160, 2); }
  }
  return fx;
}

// ---------------------------------------------------------------- 渲染 + 运动模糊
const OUT = document.getElementById('cv'); OUT.width = W; OUT.height = H; const ox = OUT.getContext('2d');
function render(t, samples = 1, shutter = .5) {
  for (let k = 0; k < samples; k++) {
    const ts = samples > 1 ? t + ((k + .5) / samples - .5) * shutter / 30 : t;
    const fx = drawLayer(ts);
    GLX.frame(LAY, ts, { mode: 'solid', a: '#000000' }, fx, 1 / samples, k === 0);
  }
  ox.setTransform(1, 0, 0, 1, 0, 0); ox.drawImage(GLX.present(), 0, 0);
}

// ---------------------------------------------------------------- 导出接口 + 播放器
const Q = new URLSearchParams(location.search), EXPORT = Q.has('export');
const FONTS = [`400 20px "${SANS}"`, `900 20px "${SANS}"`, `400 20px "${SERIF}"`, `20px "${GOTH}"`, `20px "${ANTON}"`, `20px "${MONO}"`, `700 20px "${PIX}"`];
const fontsReady = Promise.all(FONTS.map(f => document.fonts.load(f, '谎A'))).then(() => document.fonts.ready).then(() => true);
window.__ready = () => fontsReady;
window.__info = () => ({ duration: T_DUR, title: '谎话 · 预告' });
window.__frame = (t, samples = 3, png = false) => { render(t, samples); return png ? OUT.toDataURL('image/png') : OUT.toDataURL('image/jpeg', .92); };
if (EXPORT) document.body.classList.add('export');
else {
  const au = document.getElementById('au'), pp = document.getElementById('pp'), seek = document.getElementById('seek'), tm = document.getElementById('time');
  const loop = () => { const t = au.currentTime; render(t, 1); seek.value = t / T_DUR * 1000; tm.textContent = t.toFixed(2); if (!au.paused) requestAnimationFrame(loop); };
  const toggle = () => au.paused ? au.play() : au.pause();
  au.onplay = () => { pp.textContent = '❚❚'; requestAnimationFrame(loop); }; au.onpause = () => { pp.textContent = '▶'; };
  pp.onclick = toggle; OUT.onclick = toggle;
  seek.oninput = () => { au.currentTime = seek.value / 1000 * T_DUR; loop(); };
  addEventListener('keydown', e => {
    if (e.code === 'Space') { e.preventDefault(); toggle(); }
    else if (e.key === ',') { au.currentTime = Math.max(0, au.currentTime - 1 / 30); loop(); }
    else if (e.key === '.') { au.currentTime = Math.min(T_DUR, au.currentTime + 1 / 30); loop(); }
  });
  fontsReady.then(loop);
}
