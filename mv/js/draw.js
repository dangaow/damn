'use strict';
// ============================================================================
// 画面元素（都画在 2D 层上，GL 背景在它下面）
// ============================================================================

// ---------------------------------------------------------------- 文字
function fit(c, str, maxW, maxS, w = 900, fam = SANS) { c.font = fnt(100, w, fam); return Math.min(maxS, 100 * maxW / Math.max(1, c.measureText(str).width)); }
function text(c, str, x, y, size, o = {}) {
  c.font = fnt(size, o.w ?? 900, o.fam ?? SANS); c.textAlign = o.align ?? 'center'; c.textBaseline = o.base ?? 'middle';
  if (o.stroke) { c.lineJoin = 'round'; c.lineWidth = o.lw ?? size * .04; c.strokeStyle = o.stroke; c.strokeText(str, x, y); }
  if (o.fill !== null) { c.fillStyle = o.fill ?? C.white; c.fillText(str, x, y); }
}
// 竖排
function vtext(c, str, x, y, size, o = {}) { [...str].forEach((ch, k) => text(c, ch, x, y + k * size * (o.lead ?? 1.02), size, { ...o, base: 'top' })); }

// 砸字：t0 时刻从放大状态砸下来，带衰减的抖动
function slam(c, t, t0, seed, fn, o = {}) {
  const k = Math.max(0, t - t0), s = 1 + (o.amt ?? .45) * Math.exp(-k * 22), sh = (o.shake ?? 40) * Math.exp(-k * 12);
  c.save(); c.translate(o.x ?? W / 2, o.y ?? H / 2); c.translate((hash(seed, 1) - .5) * sh, (hash(seed, 2) - .5) * sh); c.scale(s, s); fn(); c.restore();
}

// ---------------------------------------------------------------- 银色铬金属字
const CHROME = new Map();
function chromeBase(str, size, fam, w) {
  const key = `${str}|${size | 0}|${fam}|${w}`;
  if (CHROME.has(key)) return CHROME.get(key);
  const m = mk(10, 10).getContext('2d'); m.font = fnt(size, w, fam);
  const tw = m.measureText(str).width, pad = size * .25, cw = Math.ceil(tw + pad * 2), ch = Math.ceil(size * 1.7);
  const cv2 = mk(cw, ch), x = cv2.getContext('2d'), cy = ch / 2;
  x.font = fnt(size, w, fam); x.textAlign = 'center'; x.textBaseline = 'middle'; x.lineJoin = 'round';
  x.lineWidth = size * .07; x.strokeStyle = '#08090c'; x.strokeText(str, cw / 2, cy);
  // 高光边 / 暗边：用错位的实心字叠出来（描边会把汉字内部重叠的笔画轮廓也描出来）
  const d = Math.max(1.5, size * .012);
  x.fillStyle = '#2a2d33'; x.fillText(str, cw / 2, cy + d);
  x.fillStyle = 'rgba(255,255,255,.9)'; x.fillText(str, cw / 2, cy - d);
  const g = x.createLinearGradient(0, cy - size * .55, 0, cy + size * .55);
  [[0, '#ffffff'], [.22, '#dfe3ea'], [.44, '#6e747e'], [.5, '#1b1e23'], [.54, '#c3c9d3'], [.74, '#f5f7fa'], [1, '#858b95']].forEach(([s, col]) => g.addColorStop(s, col));
  x.fillStyle = g; x.fillText(str, cw / 2, cy);
  const r = { c: cv2, w: cw, h: ch };
  CHROME.set(key, r); return r;
}
const SWEEP = mk(W, 800), swx = SWEEP.getContext('2d');
// sweep：0..1 一道高光从左扫到右；不传则不扫
function chrome(c, str, x, y, size, o = {}) {
  const b = chromeBase(str, Math.round(size), o.fam ?? SANS, o.w ?? 900);
  let src = b.c;
  if (o.sweep != null && o.sweep > -.2 && o.sweep < 1.2 && b.w <= W && b.h <= 800) {
    swx.setTransform(1, 0, 0, 1, 0, 0); swx.globalCompositeOperation = 'source-over'; swx.clearRect(0, 0, b.w, b.h); swx.drawImage(b.c, 0, 0);
    swx.globalCompositeOperation = 'source-atop';
    const sx = lerp(-b.w * .3, b.w * 1.3, o.sweep), g = swx.createLinearGradient(sx - b.h * .5, 0, sx + b.h * .5, b.h);
    g.addColorStop(0, 'rgba(255,255,255,0)'); g.addColorStop(.5, 'rgba(255,255,255,.95)'); g.addColorStop(1, 'rgba(255,255,255,0)');
    swx.fillStyle = g; swx.fillRect(0, 0, b.w, b.h);
    swx.globalCompositeOperation = 'source-over';
    src = SWEEP;
  }
  const ax = o.align === 'left' ? 0 : o.align === 'right' ? 1 : .5;
  c.drawImage(src, 0, 0, b.w, b.h, x - b.w * ax, y - b.h / 2, b.w, b.h);
  return b.w;
}

// ---------------------------------------------------------------- 像素字（K.O. / CONTINUE?）
function pixel(c, str, x, y, size, o = {}) {
  c.font = `700 ${size}px "${PIX}", monospace`; c.textAlign = o.align ?? 'center'; c.textBaseline = 'middle';
  if (o.shadow) { c.fillStyle = o.shadow; c.fillText(str, x + size * .08, y + size * .08); }
  if (o.stroke) { c.lineWidth = size * .08; c.strokeStyle = o.stroke; c.lineJoin = 'miter'; c.strokeText(str, x, y); }
  c.fillStyle = o.fill ?? C.white; c.fillText(str, x, y);
}

// ---------------------------------------------------------------- 雨（seed 不同 = 不同的一层；tf 是雨的时间，冻住时传固定值）
function rain(c, tf, o) {
  const r = rand(o.seed ?? 1), span = H + 400;
  c.save(); c.strokeStyle = o.col ?? 'rgb(170,190,255)'; c.lineCap = 'round';
  for (let i = 0; i < o.n; i++) {
    const x0 = r() * (W + 800) - 400, sp = o.spd * (.6 + r() * .7), len = o.len * (.5 + r()), ph = r(), a = o.alpha * (.3 + r() * .7), lw = (o.lw ?? 1) * (.8 + r() * 1.8);
    const y = mod(ph * span + tf * sp, span) - 200, x = x0 - y * o.wind;
    c.globalAlpha = a; c.lineWidth = lw; c.beginPath(); c.moveTo(x, y); c.lineTo(x - len * o.wind, y + len); c.stroke();
  }
  c.restore();
}
function bokeh(c, t, o) {
  const r = rand(o.seed ?? 3);
  for (let i = 0; i < o.n; i++) {
    const x = r() * W + Math.sin(t * .15 + r() * 6) * 60, y = (o.y0 ?? 0) + r() * H * (o.span ?? .85), R = (o.r ?? 1) * (30 + r() * 110), col = o.cols[r() * o.cols.length | 0], a = o.alpha * (.4 + r() * .6) * (1 + (o.pulse ?? 0));
    const g = c.createRadialGradient(x, y, 0, x, y, R);
    g.addColorStop(0, `rgba(${col},${a})`); g.addColorStop(.7, `rgba(${col},${a * .45})`); g.addColorStop(1, `rgba(${col},0)`);
    c.fillStyle = g; c.fillRect(x - R, y - R, 2 * R, 2 * R);
  }
}
function dust(c, t, n, seed, a = .3, col = '#fff') {
  const r = rand(seed); c.save(); c.fillStyle = col;
  for (let i = 0; i < n; i++) {
    const x = mod(r() * W + t * (r() - .5) * 30, W), y = mod(r() * H - t * (8 + r() * 14), H), s = .6 + r() * 2.4;
    c.globalAlpha = a * (.3 + .7 * (.5 + .5 * Math.sin(t * (1 + r() * 2) + r() * 6))); c.fillRect(x, y, s, s);
  }
  c.restore();
}
// 玻璃上的水珠（画在最前面）
function droplets(c, t, n, seed, a = 1) {
  const r = rand(seed);
  for (let i = 0; i < n; i++) {
    const x = r() * W, y0 = r() * H, slide = r() < .3 ? mod(t * (25 + r() * 70), H) : 0, rr = 3 + r() * 10, y = mod(y0 + slide, H);
    const g = c.createRadialGradient(x - rr * .35, y - rr * .35, 0, x, y, rr);
    g.addColorStop(0, `rgba(235,242,255,${.55 * a})`); g.addColorStop(.6, `rgba(120,150,230,${.12 * a})`); g.addColorStop(1, `rgba(20,30,60,${.25 * a})`);
    c.fillStyle = g; c.beginPath(); c.arc(x, y, rr, 0, 7); c.fill();
  }
}

// ---------------------------------------------------------------- 粒子：文字采样成点 → 炸开 / 重组
const PTS = new Map();
function textPoints(str, size, o = {}) {
  const key = `${str}|${size}|${o.fam}|${o.step}`;
  if (PTS.has(key)) return PTS.get(key);
  const cv2 = mk(W, Math.ceil(size * 1.6)), x = cv2.getContext('2d');
  x.font = fnt(size, o.w ?? 900, o.fam ?? SANS); x.textAlign = 'center'; x.textBaseline = 'middle'; x.fillStyle = '#fff'; x.fillText(str, W / 2, cv2.height / 2);
  const d = x.getImageData(0, 0, W, cv2.height).data, step = o.step ?? 7, pts = [];
  for (let y = 0; y < cv2.height; y += step) for (let xx = 0; xx < W; xx += step) if (d[(y * W + xx) * 4 + 3] > 128) pts.push([xx - W / 2, y - cv2.height / 2]);
  PTS.set(key, pts); return pts;
}
// 炸开：k = 炸开后的秒数（<0 时完整），返回每个点的当前位置
function burst(pts, k, seed, o = {}) {
  const pw = o.power ?? 900, gv = o.gravity ?? 900;
  return pts.map((p, i) => {
    if (k <= 0) return p;
    const a = hash(i, seed) * 6.283, v = pw * (.25 + hash(i, seed + 1)), lift = (hash(i, seed + 2) - .7) * pw * .5;
    return [p[0] + Math.cos(a) * v * k + p[0] * k * 1.5, p[1] + (Math.sin(a) * v + lift) * k + gv * k * k];
  });
}
function drawPts(c, pts, cx, cy, size, col, a = 1) {
  c.save(); c.fillStyle = col; c.globalAlpha = a;
  for (const p of pts) c.fillRect(cx + p[0] - size / 2, cy + p[1] - size / 2, size, size);
  c.restore();
}
// 从 A 的点云过渡到 B 的点云（k 0..1），点数不同时按比例对应
function morphPts(A, B, k) {
  const n = Math.max(A.length, B.length), out = [];
  for (let i = 0; i < n; i++) { const a = A[Math.floor(i * A.length / n)], b = B[Math.floor(i * B.length / n)], kk = ease((k - hash(i, 77) * .3) / .7); out.push([lerp(a[0], b[0], kk), lerp(a[1], b[1], kk)]); }
  return out;
}

// ---------------------------------------------------------------- 碎玻璃：把一张快照切成三角形碎片落下
const SNAP = mk(), snx = SNAP.getContext('2d');
function shards(seed, cols = 9, rows = 6) {
  const r = rand(seed), P = [];
  for (let j = 0; j <= rows; j++) for (let i = 0; i <= cols; i++) {
    const edge = i === 0 || j === 0 || i === cols || j === rows;
    P.push([i * W / cols + (edge ? 0 : (r() - .5) * W / cols * .8), j * H / rows + (edge ? 0 : (r() - .5) * H / rows * .8)]);
  }
  const T = [];
  for (let j = 0; j < rows; j++) for (let i = 0; i < cols; i++) {
    const a = P[j * (cols + 1) + i], b = P[j * (cols + 1) + i + 1], c2 = P[(j + 1) * (cols + 1) + i], d = P[(j + 1) * (cols + 1) + i + 1];
    if (r() < .5) T.push([a, b, d], [a, d, c2]); else T.push([a, b, c2], [b, d, c2]);
  }
  return T;
}
const SHARDS = shards(5);
function shatter(c, src, k, seed) {
  SHARDS.forEach((tri, i) => {
    const cx = (tri[0][0] + tri[1][0] + tri[2][0]) / 3, cy = (tri[0][1] + tri[1][1] + tri[2][1]) / 3;
    const delay = hash(i, seed) * .25 + Math.abs(cx - W / 2) / W * .2, kk = Math.max(0, k - delay);
    const dx = (cx - W / 2) * .4 * kk + (hash(i, seed + 1) - .5) * 300 * kk, dy = 1400 * kk * kk + (hash(i, seed + 2) - .6) * 200 * kk, rr = (hash(i, seed + 3) - .5) * 5 * kk;
    if (cy + dy > H + 300) return;
    c.save(); c.translate(cx + dx, cy + dy); c.rotate(rr); c.translate(-cx, -cy);
    c.beginPath(); c.moveTo(...tri[0]); c.lineTo(...tri[1]); c.lineTo(...tri[2]); c.closePath(); c.clip();
    c.globalAlpha = 1 - clamp(kk * .7); c.drawImage(src, 0, 0);
    c.strokeStyle = 'rgba(220,235,255,.7)'; c.lineWidth = 2; c.stroke();
    c.restore();
  });
}

// ---------------------------------------------------------------- 像素马赛克
function mosaic(c, x, y, w, h, t, o = {}) {
  const s = o.cell ?? 14, f = frameNo(t) >> 1;
  for (let yy = 0; yy < h; yy += s) for (let xx = 0; xx < w; xx += s) {
    const v = hash(xx * 31 + yy * 7 + f * 131, 5);
    c.fillStyle = v < .4 ? (o.a ?? '#120206') : v < .75 ? (o.b ?? '#7d0614') : (o.c ?? '#e0112b');
    c.fillRect(x + xx, y + yy, Math.min(s, w - xx), Math.min(s, h - yy));
  }
}

// ---------------------------------------------------------------- 融化：把一张图按列往下拖
function melt(c, src, sx, sy, sw, sh, k, seed) {
  const col = 6;
  for (let x = 0; x < sw; x += col) {
    const d = k * (120 + hash(x, seed) * 520) * (.4 + .6 * Math.abs(Math.sin(x * .013 + seed)));
    c.drawImage(src, sx + x, sy, col, sh, sx + x, sy + d, col, sh);
    if (d > 4) c.drawImage(src, sx + x, sy + sh * .5, col, 2, sx + x, sy + sh * .5, col, d + sh * .2);   // 拉丝
  }
}

// ---------------------------------------------------------------- 测谎仪：底部一条随人声跳的针线
// POLY(t) 返回该时刻的状态 { a: 显示透明度, amp: 振幅倍数, lie: 0..1 乱跳程度, spike: 0..1 冲顶 }
function polygraph(c, t, POLY) {
  const st = POLY(t);
  if (st.a <= .01) return;
  const y0 = H - 92, x1 = W - 170, span = 3.2, n = 260;
  c.save(); c.globalAlpha = st.a;
  c.strokeStyle = 'rgba(255,255,255,.12)'; c.lineWidth = 1; c.setLineDash([2, 6]);
  c.beginPath(); c.moveTo(60, y0); c.lineTo(x1, y0); c.moveTo(60, y0 - 46); c.lineTo(x1, y0 - 46); c.moveTo(60, y0 + 46); c.lineTo(x1, y0 + 46); c.stroke();
  c.setLineDash([]);
  let head = 0;
  c.beginPath();
  for (let i = 0; i <= n; i++) {
    const tt = t - span * (1 - i / n), x = lerp(60, x1, i / n), s = POLY(tt);
    const v = M.vocal(tt) * s.amp, f = Math.floor(tt * 60);
    const jit = (hash(f, 3) - .5) * (.15 + s.lie * 1.6) * (v + s.lie * .5);
    let yy = -(v * .55 + jit) * 46 - s.spike * 300 * Math.exp(-Math.pow((i - n) / 18, 2));
    yy = Math.max(-200, Math.min(46, yy));
    if (i === 0) c.moveTo(x, y0 + yy); else c.lineTo(x, y0 + yy);
    if (i === n) head = yy;
  }
  c.strokeStyle = st.lie > .3 ? 'rgba(255,90,100,.95)' : 'rgba(240,240,240,.85)'; c.lineWidth = 2; c.stroke();
  c.fillStyle = C.red; c.beginPath(); c.arc(x1, y0 + head, 5, 0, 7); c.fill();
  c.font = fnt(18, 400, MONO); c.fillStyle = 'rgba(255,255,255,.6)'; c.textAlign = 'left'; c.textBaseline = 'middle';
  c.fillText(st.lie > .3 ? 'POLYGRAPH  ·  DECEPTION' : 'POLYGRAPH  ·  CH-1', x1 + 18, y0 - 30);
  c.fillText(`${(M.vocal(t) * st.amp * 100 | 0).toString().padStart(3, '0')}`, x1 + 18, y0);
  c.restore();
}

// ---------------------------------------------------------------- 录像带 HUD
function hud(c, t, o = {}) {
  c.save(); c.font = fnt(26, 400, MONO); c.textBaseline = 'middle'; c.globalAlpha = o.a ?? .8;
  const m = 70, top = o.lb ? 70 : 64, bot = o.lb ? H - 70 : H - 64;
  c.textAlign = 'left';
  if (mod(t, 1) < .55) { c.fillStyle = C.red; c.beginPath(); c.arc(m + 12, top, 11, 0, 7); c.fill(); }
  c.fillStyle = C.white; c.fillText(o.label ?? 'REC', m + 36, top);
  c.fillText(o.tape ?? 'TAPE 01', m + 150, top);
  c.fillText(o.mode ?? '▶ PLAY', m, bot);
  c.textAlign = 'right';
  c.fillText(`${D.title} / ${D.artist}`, W - m, top);
  const tt = o.tc ?? t, f = Math.max(0, Math.floor(tt * 30)), tc = [f / 108000 | 0, (f / 1800 | 0) % 60, (f / 30 | 0) % 60, f % 30].map(v => String(v).padStart(2, '0')).join(':');
  c.fillText('TC ' + tc, W - m, bot);
  c.restore();
}

// ---------------------------------------------------------------- 杂项
function bars(c, n, seed, col, a = .5) { const r = rand(seed); c.save(); c.fillStyle = col; c.globalAlpha = a; for (let k = 0; k < n; k++) c.fillRect(0, r() * H, W, 1 + r() * 5); c.restore(); }
// 黑色涂抹条（机密文件）
function redact(c, x, y, w, h, k, o = {}) {
  if (k <= 0) return;
  c.save(); c.fillStyle = o.col ?? '#000'; c.fillRect(x, y, w * easeOut(k), h);
  if (o.label && k >= 1) { c.font = fnt(16, 400, MONO); c.fillStyle = 'rgba(255,255,255,.55)'; c.textAlign = 'left'; c.textBaseline = 'top'; c.fillText(o.label, x + 8, y + h + 6); }
  c.restore();
}
// 背景里滚动的空心字
function scrollRows(c, t, str, o) {
  c.save(); c.font = fnt(o.size, o.w ?? 900, o.fam ?? SANS); c.strokeStyle = o.col; c.lineWidth = o.lw ?? 2; c.textBaseline = 'middle'; c.textAlign = 'left';
  const unit = c.measureText(str + (o.sep ?? '  ')).width;
  for (let row = 0; row < o.rows; row++) {
    const y = (row + .5) * H / o.rows;
    for (let x = -mod(t * o.spd * (row % 2 ? 1 : -1) + row * 137, unit); x < W; x += unit) c.strokeText(str, x, y);
  }
  c.restore();
}
// 横向变形镜头光
function flare(c, x, y, a, len = 1100, col = '150,190,255') {
  if (a <= .01) return;
  c.save(); c.globalCompositeOperation = 'lighter';
  const g = c.createLinearGradient(x - len, 0, x + len, 0);
  g.addColorStop(0, `rgba(${col},0)`); g.addColorStop(.5, `rgba(${col},${.8 * a})`); g.addColorStop(1, `rgba(${col},0)`);
  c.fillStyle = g; c.fillRect(x - len, y - 3, len * 2, 6); c.globalAlpha = .4; c.fillRect(x - len, y - 14, len * 2, 28);
  const rg = c.createRadialGradient(x, y, 0, x, y, 120); rg.addColorStop(0, `rgba(255,255,255,${a})`); rg.addColorStop(1, 'rgba(255,255,255,0)');
  c.globalAlpha = 1; c.fillStyle = rg; c.fillRect(x - 120, y - 120, 240, 240);
  c.restore();
}
