'use strict';
// ============================================================================
// 漫画的画法：墨线、分格、对白气泡、集中线、墨滴、网点、拟声字、勿忘我、雨、雪、封面插画的局部。
// 灰色直接画灰色就好：后期（gl.js）会把大片的灰变成网点。
// ============================================================================

// ---------------------------------------------------------------- 灰度（GL 会把它打成网点）
const gray = v => { const n = Math.round(lerp(0xf4, 0x11, clamp(v))); return `rgb(${n},${n - 2 < 0 ? 0 : n - 2},${n - 6 < 0 ? 0 : n - 6})`; };   // v: 0 = 纸, 1 = 墨
function toneRect(c, x, y, w, h, v) { c.fillStyle = gray(v); c.fillRect(x, y, w, h); }
function toneGrad(c, x, y, w, h, v0, v1, vertical = true) {
  const g = vertical ? c.createLinearGradient(0, y, 0, y + h) : c.createLinearGradient(x, 0, x + w, 0);
  g.addColorStop(0, gray(v0)); g.addColorStop(1, gray(v1)); c.fillStyle = g; c.fillRect(x, y, w, h);
}

// ---------------------------------------------------------------- 笔触：两头尖、中间粗的墨线
// pts：折线；w：最粗处的宽度；k：画到哪（0..1，用来做"正在画"的动画）
function stroke(c, pts, w, o = {}) {
  const k = o.k ?? 1; if (k <= 0 || pts.length < 2) return;
  const L = [0]; for (let i = 1; i < pts.length; i++) L.push(L[i - 1] + Math.hypot(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]));
  const tot = L[L.length - 1] || 1, end = tot * k, left = [], right = [];
  const taper = o.taper ?? .35;
  for (let i = 0; i < pts.length; i++) {
    let s = L[i], p = pts[i];
    if (s > end) { const a = (end - L[i - 1]) / Math.max(1e-6, L[i] - L[i - 1]); p = [lerp(pts[i - 1][0], p[0], a), lerp(pts[i - 1][1], p[1], a)]; s = end; }
    const a = pts[Math.max(0, i - 1)], b = pts[Math.min(pts.length - 1, i + 1)];
    let nx = -(b[1] - a[1]), ny = b[0] - a[0]; const nl = Math.hypot(nx, ny) || 1; nx /= nl; ny /= nl;
    const u = s / tot, ww = w * .5 * Math.pow(Math.max(0, Math.sin(Math.PI * clamp(u * (o.full ? 1 : 1)))) , taper) * (o.even ? 1 : 1);
    const width = o.even ? w * .5 : Math.max(w * .06, ww);
    left.push([p[0] + nx * width, p[1] + ny * width]); right.push([p[0] - nx * width, p[1] - ny * width]);
    if (L[i] >= end) break;
  }
  c.beginPath(); c.moveTo(...left[0]); for (const p of left) c.lineTo(...p); for (let i = right.length - 1; i >= 0; i--) c.lineTo(...right[i]);
  c.closePath(); c.fillStyle = o.col ?? C.ink; c.fill();
}
// 一笔弯弯的笔触：从 (x0,y0) 到 (x1,y1)，中间往旁边鼓 bend
function curve(x0, y0, x1, y1, bend = 0, n = 16) {
  const mx = (x0 + x1) / 2, my = (y0 + y1) / 2, dx = x1 - x0, dy = y1 - y0, L = Math.hypot(dx, dy) || 1;
  const cx = mx - dy / L * bend, cy = my + dx / L * bend, out = [];
  for (let i = 0; i <= n; i++) { const u = i / n; out.push([(1 - u) * (1 - u) * x0 + 2 * u * (1 - u) * cx + u * u * x1, (1 - u) * (1 - u) * y0 + 2 * u * (1 - u) * cy + u * u * y1]); }
  return out;
}
// 铅笔：几根很细、略错开的灰线
function pencil(c, pts, seed, o = {}) {
  const r = rand(seed), k = o.k ?? 1;
  c.save(); c.strokeStyle = o.col ?? C.pencil; c.lineCap = 'round'; c.globalAlpha = o.a ?? .85;
  for (let j = 0; j < (o.n ?? 3); j++) {
    c.lineWidth = (o.lw ?? 1.6) * (.6 + r() * .8); c.beginPath();
    const ox = (r() - .5) * (o.jit ?? 5), oy = (r() - .5) * (o.jit ?? 5), m = Math.max(2, Math.ceil(pts.length * k));
    for (let i = 0; i < m && i < pts.length; i++) { const p = pts[i]; i ? c.lineTo(p[0] + ox, p[1] + oy) : c.moveTo(p[0] + ox, p[1] + oy); }
    c.stroke();
  }
  c.restore();
}

// ---------------------------------------------------------------- 分格
// 格子框：k = 0..1 沿着边画出来（从左上角顺时针）；lw 粗细
function frameRect(c, x, y, w, h, o = {}) {
  const k = o.k ?? 1, lw = o.lw ?? 7; if (k <= 0) return;
  const per = 2 * (w + h), d = per * easeOut(k), P = [[x, y], [x + w, y], [x + w, y + h], [x, y + h], [x, y]];
  c.save(); c.strokeStyle = o.col ?? C.ink; c.lineWidth = lw; c.lineJoin = 'miter'; c.lineCap = 'square';
  c.beginPath(); c.moveTo(x, y); let acc = 0;
  for (let i = 1; i < P.length; i++) {
    const seg = Math.hypot(P[i][0] - P[i - 1][0], P[i][1] - P[i - 1][1]);
    if (acc + seg >= d) { const a = (d - acc) / seg; c.lineTo(lerp(P[i - 1][0], P[i][0], a), lerp(P[i - 1][1], P[i][1], a)); break; }
    c.lineTo(...P[i]); acc += seg;
  }
  c.stroke(); c.restore();
}
// 一个格子：先铺纸、再画内容（裁在格子里）、最后画框
// p = {x, y, w, h}；o.k 框画到哪；o.fill 纸色（null = 透明）；content(c, p) 画内容
function panel(c, p, content, o = {}) {
  const pop = o.pop ?? 1;                                                         // 弹出：0..1，从中心放大
  c.save();
  if (pop < 1) { const s = lerp(.86, 1, back(pop)); c.translate(p.x + p.w / 2, p.y + p.h / 2); c.scale(s, s); c.translate(-p.x - p.w / 2, -p.y - p.h / 2); c.globalAlpha *= clamp(pop * 3); }
  c.save(); c.beginPath(); c.rect(p.x, p.y, p.w, p.h); c.clip();
  if (o.fill !== null) { c.fillStyle = o.fill ?? C.paper; c.fillRect(p.x, p.y, p.w, p.h); }
  if (content) content(c, p);
  c.restore();
  frameRect(c, p.x, p.y, p.w, p.h, { k: o.k ?? 1, lw: o.lw, col: o.col });
  c.restore();
}
// 版式：把一个区域切成格子。rows = [[高度比, [各格宽度比]], ...]
function layout(x, y, w, h, rows, gut = 26) {
  const out = [], hs = rows.reduce((s, r) => s + r[0], 0); let yy = y;
  for (const [rh, cols] of rows) {
    const ph = (h - gut * (rows.length - 1)) * rh / hs, ws = cols.reduce((s, v) => s + v, 0); let xx = x;
    for (const cw of cols) { const pw = (w - gut * (cols.length - 1)) * cw / ws; out.push({ x: xx, y: yy, w: pw, h: ph }); xx += pw + gut; }
    yy += ph + gut;
  }
  return out;
}
const inset = (p, d) => ({ x: p.x + d, y: p.y + d, w: p.w - 2 * d, h: p.h - 2 * d });
const center = p => [p.x + p.w / 2, p.y + p.h / 2];

// ---------------------------------------------------------------- 字
function text(c, str, x, y, size, o = {}) {
  c.font = fnt(size, o.w ?? 700, o.fam ?? SANS); c.textAlign = o.align ?? 'center'; c.textBaseline = o.base ?? 'middle';
  if (o.stroke) { c.lineJoin = 'round'; c.lineWidth = o.lw ?? size * .12; c.strokeStyle = o.stroke; c.strokeText(str, x, y); }
  if (o.fill !== null) { c.fillStyle = o.fill ?? C.ink; c.fillText(str, x, y); }
}
const tw = (c, str, size, w = 700, fam = SANS) => { c.font = fnt(size, w, fam); return c.measureText(str).width; };
// 英文对白：按宽度换行，返回行数组
function wrap(c, words, size, maxW, fam = COMIC, w = 700) {
  const lines = []; let cur = '';
  for (const wd of words) { const s = cur ? cur + ' ' + wd : wd; if (cur && tw(c, s, size, w, fam) > maxW) { lines.push(cur); cur = wd; } else cur = s; }
  if (cur) lines.push(cur); return lines;
}
// 竖排：汉字一个一个往下排，英文单词转 90° 躺着排
function vtext(c, tokens, x, y, size, o = {}) {
  let yy = y; const lead = o.lead ?? 1.08;
  for (const tk of tokens) {
    if (/^[A-Za-z0-9']+$/.test(tk)) {
      const len = tw(c, tk, size * .9, o.w ?? 500, o.lat ?? COMIC);
      c.save(); c.translate(x, yy + size * .1); c.rotate(Math.PI / 2); text(c, tk, 0, 0, size * .9, { ...o, fam: o.lat ?? COMIC, align: 'left' }); c.restore();
      yy += len + size * .35;
    } else { text(c, tk, x, yy + size / 2, size, { ...o, base: 'middle' }); yy += size * lead; }
  }
  return yy - y;
}
const vlen = (c, tokens, size, o = {}) => tokens.reduce((s, tk) => s + (/^[A-Za-z0-9']+$/.test(tk) ? tw(c, tk, size * .9, o.w ?? 500, o.lat ?? COMIC) + size * .35 : size * (o.lead ?? 1.08)), 0);

// ---------------------------------------------------------------- 对白气泡
// 椭圆 / 云 / 爆炸 / 方框。tail = [x, y] 尾巴尖指向哪；k = 弹出进度
function bubblePath(c, x, y, w, h, type, seed) {
  c.beginPath();
  if (type === 'box') { c.rect(x - w / 2, y - h / 2, w, h); return; }
  if (type === 'cloud') {
    const n = 11, r = rand(seed);
    for (let i = 0; i <= n; i++) {
      const a0 = i / n * 6.283, a1 = (i + 1) / n * 6.283, am = (a0 + a1) / 2, bump = 1.16 + r() * .08;
      const p0 = [x + Math.cos(a0) * w / 2, y + Math.sin(a0) * h / 2], p1 = [x + Math.cos(a1) * w / 2, y + Math.sin(a1) * h / 2];
      if (i === 0) c.moveTo(...p0);
      c.quadraticCurveTo(x + Math.cos(am) * w / 2 * bump, y + Math.sin(am) * h / 2 * bump, ...p1);
    }
    c.closePath(); return;
  }
  if (type === 'shout') {
    const n = 22, r = rand(seed);
    for (let i = 0; i <= n; i++) { const a = i / n * 6.283, k = i % 2 ? 1.22 + r() * .12 : .92; const px = x + Math.cos(a) * w / 2 * k, py = y + Math.sin(a) * h / 2 * k; i ? c.lineTo(px, py) : c.moveTo(px, py); }
    c.closePath(); return;
  }
  c.ellipse(x, y, w / 2, h / 2, 0, 0, 6.283);
}
function bubble(c, x, y, w, h, o = {}) {
  const k = o.k ?? 1; if (k <= 0) return;
  const s = lerp(.5, 1, back(k)), type = o.type ?? 'round';
  c.save(); c.translate(x, y); c.scale(s, s); c.translate(-x, -y); c.globalAlpha *= clamp(k * 4);
  c.lineJoin = 'round';
  // 尾巴
  if (o.tail && type !== 'cloud') {
    const [tx, ty] = o.tail, a = Math.atan2(ty - y, tx - x), bw = Math.min(w, h) * .16;
    const bx = x + Math.cos(a) * w * .36, by = y + Math.sin(a) * h * .36;
    c.beginPath(); c.moveTo(bx - Math.sin(a) * bw, by + Math.cos(a) * bw); c.quadraticCurveTo(lerp(bx, tx, .6) + Math.sin(a) * bw * .3, lerp(by, ty, .6), tx, ty);
    c.lineTo(bx + Math.sin(a) * bw, by - Math.cos(a) * bw); c.closePath();
    c.fillStyle = o.fill ?? '#fff'; c.strokeStyle = C.ink; c.lineWidth = o.lw ?? 4.5; c.fill(); c.stroke();
  }
  bubblePath(c, x, y, w, h, type, o.seed ?? 1);
  c.fillStyle = o.fill ?? '#fff'; c.fill(); c.strokeStyle = o.col ?? C.ink; c.lineWidth = o.lw ?? 4.5; c.stroke();
  if (o.tail && type !== 'cloud') {                                               // 盖住尾巴和气泡相接处的线
    const [tx, ty] = o.tail, a = Math.atan2(ty - y, tx - x), bw = Math.min(w, h) * .16 - 3;
    const bx = x + Math.cos(a) * w * .36, by = y + Math.sin(a) * h * .36;
    c.beginPath(); c.moveTo(bx - Math.sin(a) * bw, by + Math.cos(a) * bw); c.lineTo(bx + Math.sin(a) * bw, by - Math.cos(a) * bw); c.lineTo(lerp(bx, x, .3), lerp(by, y, .3)); c.closePath();
    c.fillStyle = o.fill ?? '#fff'; c.fill();
  }
  if (o.tail && type === 'cloud') {                                               // 心声气泡：几个小圆点指过去
    const [tx, ty] = o.tail;
    for (let i = 1; i <= 3; i++) { const u = .5 + i * .16, rr = (4 - i) * 6; c.beginPath(); c.arc(lerp(x, tx, u), lerp(y, ty, u), rr, 0, 7); c.fillStyle = '#fff'; c.fill(); c.lineWidth = 3.5; c.stroke(); }
  }
  c.restore();
}
// 一句英文对白：气泡 + 逐词写出来。返回气泡尺寸
function sayLine(c, line, t, x, y, o = {}) {
  const words = line.words.map(w => w.w), n = wordsSung(line, t), size = o.size ?? 44, maxW = o.maxW ?? 520;
  const lines = wrap(c, words, size, maxW), bw = Math.max(...lines.map(s => tw(c, s, size, 700, COMIC))) + size * 1.6, bh = lines.length * size * 1.15 + size * 1.3;
  const k = o.k ?? span(t, line.t - .06, line.t + .16);
  bubble(c, x, y, o.w ?? bw * 1.08, o.h ?? bh, { k, tail: o.tail, type: o.type, seed: o.seed, fill: o.fill });
  if (k <= 0) return { w: bw, h: bh };
  const s = lerp(.5, 1, back(k));
  c.save(); c.translate(x, y); c.scale(s, s); c.translate(-x, -y);
  let idx = 0;
  lines.forEach((ln, li) => {
    const ws = ln.split(' '), full = tw(c, ln, size, 700, COMIC); let xx = x - full / 2;
    const yy = y + (li - (lines.length - 1) / 2) * size * 1.15;
    for (const wd of ws) {
      const on = idx < n || o.all; idx++;
      const ww = tw(c, wd + ' ', size, 700, COMIC);
      if (on) text(c, wd, xx, yy, size * (o.caps ? 1 : 1), { fam: COMIC, align: 'left', fill: o.ink ?? C.ink });
      xx += ww;
    }
  });
  c.restore();
  return { w: bw, h: bh };
}
// 一句中文：竖排进方框（旁白）或云朵（心声）
function thinkLine(c, line, t, x, y, o = {}) {
  const tokens = line.words.map(w => w.w), n = o.all ? tokens.length : wordsSung(line, t), size = o.size ?? 52;
  const len = vlen(c, tokens, size), bw = size * 1.9, bh = len + size * 1.2;
  const k = o.k ?? span(t, line.t - .06, line.t + .18);
  if (o.box !== false) bubble(c, x, y + bh / 2, o.type === 'cloud' ? bw * 1.7 : bw, o.type === 'cloud' ? bh * 1.15 : bh, { k, type: o.type ?? 'box', tail: o.tail, seed: o.seed, fill: o.fill, lw: o.lw });
  if (k <= 0) return { w: bw, h: bh };
  vtext(c, tokens.slice(0, n), x, y + size * .6, size, { w: o.w ?? 500, fill: o.ink ?? C.ink, lat: COMIC });
  return { w: bw, h: bh };
}

// ---------------------------------------------------------------- 效果线
// 集中线：从画面外往 (cx,cy) 收的细三角，r0 = 中间空出的半径
function focusLines(c, cx, cy, r0, n, seed, o = {}) {
  const r = rand(seed), R = o.R ?? 2400, a0 = o.t ?? 0;
  c.save(); c.fillStyle = o.col ?? C.ink; c.globalAlpha *= o.a ?? 1;
  for (let i = 0; i < n; i++) {
    const a = r() * 6.283 + a0 * (r() - .5) * .2, w = (o.w ?? .012) * (.3 + r()), rin = r0 * (1 + r() * .45);
    c.beginPath(); c.moveTo(cx + Math.cos(a - w) * R, cy + Math.sin(a - w) * R); c.lineTo(cx + Math.cos(a) * rin, cy + Math.sin(a) * rin); c.lineTo(cx + Math.cos(a + w) * R, cy + Math.sin(a + w) * R); c.fill();
  }
  c.restore();
}
// 速度线：一个方向的平行细线
function speedLines(c, x, y, w, h, ang, n, seed, o = {}) {
  const r = rand(seed);
  c.save(); c.beginPath(); c.rect(x, y, w, h); c.clip(); c.translate(x + w / 2, y + h / 2); c.rotate(ang);
  c.fillStyle = o.col ?? C.ink; c.globalAlpha *= o.a ?? 1; const L = Math.hypot(w, h);
  for (let i = 0; i < n; i++) { const yy = (r() - .5) * L, len = L * (.3 + r() * .7), xx = (r() - .5) * L * .6 + (o.shift ?? 0) * L, th = (o.lw ?? 3) * (.3 + r()); c.fillRect(xx - len / 2, yy, len, th); }
  c.restore();
}

// ---------------------------------------------------------------- 墨
// 墨滴砸在纸上：中间一团 + 往外溅的点和拉长的飞沫。k = 0..1 溅开的过程
function splat(c, x, y, R, seed, k = 1, col = C.ink) {
  if (k <= 0) return;
  const r = rand(seed), e = easeOut(k);
  c.save(); c.fillStyle = col;
  c.beginPath(); const n = 18;
  for (let i = 0; i <= n; i++) { const a = i / n * 6.283, rr = R * (.55 + (hash(i, seed) * .35)) * lerp(.4, 1, e); const px = x + Math.cos(a) * rr, py = y + Math.sin(a) * rr; i ? c.lineTo(px, py) : c.moveTo(px, py); }
  c.closePath(); c.fill();
  for (let i = 0; i < 26; i++) {
    const a = r() * 6.283, d = R * (.8 + r() * 2.4) * e, s = R * (.04 + r() * .13) * (1 - .3 * e);
    const px = x + Math.cos(a) * d, py = y + Math.sin(a) * d;
    c.beginPath(); c.ellipse(px, py, s * (1 + r() * 2), s, a, 0, 6.283); c.fill();
    if (r() < .35) { c.lineWidth = s * .7; c.strokeStyle = col; c.beginPath(); c.moveTo(x + Math.cos(a) * R * .5, y + Math.sin(a) * R * .5); c.lineTo(px, py); c.stroke(); }
  }
  c.restore();
}
// 墨在水里散开：一圈圈软的灰
function inkCloud(c, x, y, R, k, seed, col) {
  const r = rand(seed);
  c.save();
  for (let i = 0; i < 26; i++) {
    const a = r() * 6.283, d = R * r() * easeOut(k), rr = R * (.2 + r() * .5) * (.3 + k), px = x + Math.cos(a) * d, py = y + Math.sin(a) * d * .8 + R * .3 * k * r();
    const g = c.createRadialGradient(px, py, 0, px, py, rr);
    const cc = col ?? '17,16,20';
    g.addColorStop(0, `rgba(${cc},${.22 * (1 - k * .5)})`); g.addColorStop(1, `rgba(${cc},0)`);
    c.fillStyle = g; c.fillRect(px - rr, py - rr, rr * 2, rr * 2);
  }
  c.restore();
}

// ---------------------------------------------------------------- 拟声字：白边 + 黑字，斜着
function sfx(c, str, x, y, size, o = {}) {
  c.save(); c.translate(x, y); c.rotate(o.rot ?? -.12); const s = o.k == null ? 1 : lerp(1.6, 1, easeOut(o.k)); c.scale(s, s);
  if (o.k != null) c.globalAlpha *= clamp(o.k * 5);
  const fam = o.fam ?? BRUSH, w = o.w ?? 400;
  c.font = fnt(size, w, fam); c.textAlign = 'center'; c.textBaseline = 'middle'; c.lineJoin = 'round';
  c.lineWidth = size * .2; c.strokeStyle = o.outline ?? '#fff'; c.strokeText(str, 0, 0);
  if (o.inkStroke) { c.lineWidth = size * .06; c.strokeStyle = C.ink; c.strokeText(str, 0, 0); }
  c.fillStyle = o.fill ?? C.ink; c.fillText(str, 0, 0);
  c.restore();
}

// ---------------------------------------------------------------- 勿忘我
// 五片圆花瓣，蓝色，中间白圈；k 开花进度（0 = 花苞）；col 可以换成灰（被抽干）
function flower(c, x, y, R, o = {}) {
  const k = o.k ?? 1; if (k <= 0) return;
  const rot = o.rot ?? 0, col = o.col ?? C.blue, e = back(k);
  c.save(); c.translate(x, y); c.rotate(rot + (1 - e) * 1.2); c.scale(e, e);
  for (let i = 0; i < 5; i++) {
    const a = i / 5 * 6.283; c.save(); c.rotate(a);
    c.beginPath(); c.ellipse(0, -R * .52, R * .42, R * .5, 0, 0, 6.283); c.fillStyle = col; c.fill();
    c.lineWidth = Math.max(1.5, R * .05); c.strokeStyle = o.line ?? C.ink; c.stroke();
    c.beginPath(); c.moveTo(0, -R * .18); c.lineTo(0, -R * .6); c.lineWidth = Math.max(1, R * .025); c.strokeStyle = o.vein ?? 'rgba(20,40,90,.45)'; c.stroke();
    c.restore();
  }
  c.beginPath(); c.arc(0, 0, R * .2, 0, 6.283); c.fillStyle = '#fff'; c.fill(); c.lineWidth = Math.max(1.2, R * .035); c.strokeStyle = C.ink; c.stroke();
  c.beginPath(); c.arc(0, 0, R * .07, 0, 6.283); c.fillStyle = C.ink; c.fill();
  c.restore();
}
// 一片花瓣（飘落用）
function petal(c, x, y, R, rot, col = C.blue) {
  c.save(); c.translate(x, y); c.rotate(rot);
  c.beginPath(); c.ellipse(0, 0, R * .42, R * .52, 0, 0, 6.283); c.fillStyle = col; c.fill(); c.lineWidth = Math.max(1.2, R * .05); c.strokeStyle = C.ink; c.stroke();
  c.restore();
}
// 花茎和叶子
function stem(c, x0, y0, x1, y1, seed, o = {}) {
  stroke(c, curve(x0, y0, x1, y1, (hash(seed, 3) - .5) * 80), o.w ?? 6, { k: o.k ?? 1, taper: .2 });
  if ((o.k ?? 1) > .5) {
    const mx = lerp(x0, x1, .55), my = lerp(y0, y1, .55), s = hash(seed, 4) < .5 ? 1 : -1, L = o.leaf ?? 50;
    c.save(); c.translate(mx, my); c.rotate(s * .9 - Math.PI / 2 + Math.atan2(y1 - y0, x1 - x0) + Math.PI / 2);
    c.beginPath(); c.moveTo(0, 0); c.quadraticCurveTo(L * .5, -L * .35, L, 0); c.quadraticCurveTo(L * .5, L * .35, 0, 0);
    c.fillStyle = o.leafCol ?? gray(.25); c.fill(); c.lineWidth = 3; c.strokeStyle = C.ink; c.stroke(); c.restore();
  }
}

// ---------------------------------------------------------------- 天气
// 漫画的雨：斜的细墨线一排排落下
function mangaRain(c, t, o = {}) {
  const r = rand(o.seed ?? 11), n = o.n ?? 160, ang = o.ang ?? .32, spd = o.spd ?? 2400, x0 = o.x ?? 0, y0 = o.y ?? 0, w = o.w ?? W, h = o.h ?? H;
  c.save(); c.beginPath(); c.rect(x0, y0, w, h); c.clip(); c.strokeStyle = o.col ?? C.ink; c.lineCap = 'round';
  for (let i = 0; i < n; i++) {
    const L = (o.len ?? 110) * (.5 + r()), x = r() * (w + h * ang) + x0, ph = r(), lw = (o.lw ?? 2) * (.5 + r());
    const y = y0 + mod(ph * (h + L) + t * spd * (.8 + r() * .4), h + L) - L;
    c.globalAlpha = (o.a ?? 1) * (.4 + r() * .6); c.lineWidth = lw;
    c.beginPath(); c.moveTo(x - (y - y0) * ang, y); c.lineTo(x - (y - y0 + L) * ang, y + L); c.stroke();
  }
  c.restore();
}
// 雪：大小不一的白点，带黑边的大片（漫画里的雪）
function snow(c, t, o = {}) {
  const r = rand(o.seed ?? 21), n = o.n ?? 120, x0 = o.x ?? 0, y0 = o.y ?? 0, w = o.w ?? W, h = o.h ?? H;
  c.save();
  for (let i = 0; i < n; i++) {
    const sz = (o.size ?? 1) * (3 + Math.pow(r(), 3) * 16), sp = (o.spd ?? 70) * (.5 + r() * .8) * (.6 + sz / 20), ph = r(), sw = r() * 6.283;
    const y = y0 + mod(ph * (h + 60) + t * sp, h + 60) - 30, x = x0 + mod(r() * w + Math.sin(t * .8 + sw) * 30 + t * (o.wind ?? 12), w);
    c.globalAlpha = o.a ?? 1;
    c.beginPath(); c.arc(x, y, sz, 0, 6.283); c.fillStyle = '#fff'; c.fill();
    if (sz > 7 && !o.noEdge) { c.lineWidth = 1.6; c.strokeStyle = C.ink; c.stroke(); }
  }
  c.restore();
}

// ---------------------------------------------------------------- 封面插画
const COVER = new Image(); COVER.src = window.COVER_SRC;
const coverReady = new Promise(res => { if (COVER.complete && COVER.naturalWidth) res(); else COVER.onload = () => res(); });
// 封面上的区域（原图 1000×1000 的坐标）
const CROP = {
  full: [0, 0, 1000, 1000], face: [540, 560, 300, 330], eye: [688, 612, 130, 88], mouth: [598, 742, 144, 96],
  collar: [560, 826, 280, 126], buckle: [625, 860, 110, 80], hair: [470, 300, 480, 330], wing: [0, 0, 520, 620], slash: [480, 0, 520, 520],
  slash2: [80, 700, 480, 300], head: [470, 300, 470, 560],
};
// 把一块区域铺满格子（cover 模式），z = 额外放大，(px, py) = 焦点偏移（-1..1）
function coverCrop(c, name, p, o = {}) {
  if (!COVER.naturalWidth) return;
  const [sx, sy, sw, sh] = CROP[name], z = o.z ?? 1;
  const s = Math.max(p.w / sw, p.h / sh) * z, dw = sw * s, dh = sh * s;
  const dx = p.x + (p.w - dw) / 2 + (o.px ?? 0) * (dw - p.w) / 2, dy = p.y + (p.h - dh) / 2 + (o.py ?? 0) * (dh - p.h) / 2;
  c.save(); c.beginPath(); c.rect(p.x, p.y, p.w, p.h); c.clip();
  if (o.flip) { c.translate(p.x * 2 + p.w, 0); c.scale(-1, 1); }
  c.globalAlpha *= o.a ?? 1;
  c.drawImage(COVER, sx, sy, sw, sh, dx, dy, dw, dh);
  c.restore();
}

// ---------------------------------------------------------------- "你"：背影剪影（只有头发和肩膀）
// turn：0 = 背对，1 = 转过来一半（只看到侧脸的轮廓）；hairSwing 头发甩开
function backFigure(c, x, y, s, o = {}) {
  const turn = o.turn ?? 0, sw = o.swing ?? 0, col = o.col ?? C.ink;
  c.save(); c.translate(x, y); c.scale(s, s);
  c.fillStyle = col;
  // 肩膀
  c.beginPath(); c.moveTo(-170, 260); c.quadraticCurveTo(-150, 120, -50, 95); c.lineTo(50, 95); c.quadraticCurveTo(150, 120, 170, 260); c.closePath(); c.fill();
  // 脖子 + 头
  c.beginPath(); c.ellipse(turn * 12, -10, 78, 92, 0, 0, 6.283); c.fill(); c.fillRect(-28, 40, 56, 70);
  if (turn > 0) {                                                                // 侧脸轮廓：右边露出一点鼻尖和下巴
    c.beginPath(); c.moveTo(70 + turn * 10, -30); c.lineTo(86 + turn * 18, 0); c.lineTo(72 + turn * 10, 12); c.lineTo(76 + turn * 8, 40); c.lineTo(40, 70); c.closePath(); c.fill();
  }
  // 头发：一缕一缕甩开
  for (let i = 0; i < 26; i++) {
    const a = -2.6 + i / 25 * 2.2 + Math.PI / 2 - .4, L = 150 + hash(i, 5) * 70, bx = Math.cos(a) * 70, by = Math.sin(a) * 80 - 10;
    const fling = sw * (i / 25 - .5) * 120;
    stroke(c, curve(bx, by - 30, bx * 1.1 + fling, by + L * .9 + Math.abs(fling) * .2, 18 * (hash(i, 6) - .5) + sw * 40), 10 + hash(i, 7) * 10, { col, taper: .5 });
  }
  c.restore();
}

// ---------------------------------------------------------------- 杂项
function cross(c, x, y, s, o = {}) { stroke(c, [[x - s, y - s], [x + s, y + s]], o.w ?? s * .22, { k: o.k1 ?? 1 }); stroke(c, [[x + s, y - s], [x - s, y + s]], o.w ?? s * .22, { k: o.k2 ?? 1 }); }
// 乱涂：铅笔团
function scribble(c, x, y, R, k, seed, o = {}) {
  const r = rand(seed), pts = [], n = Math.floor(120 * k);
  let a = 0; for (let i = 0; i < n; i++) { a += 1.3 + r() * 1.5; const rr = R * (.2 + r() * .8); pts.push([x + Math.cos(a) * rr, y + Math.sin(a) * rr * .8]); }
  if (pts.length > 2) { c.save(); c.strokeStyle = o.col ?? C.ink; c.lineWidth = o.lw ?? 3; c.lineJoin = 'round'; c.globalAlpha *= o.a ?? .9; c.beginPath(); c.moveTo(...pts[0]); for (let i = 1; i < pts.length; i++) { const p = pts[i - 1], q = pts[i]; c.quadraticCurveTo(p[0], p[1], (p[0] + q[0]) / 2, (p[1] + q[1]) / 2); } c.stroke(); c.restore(); }
}
// 脚印（鞋底）
function footprint(c, x, y, s, rot, a = 1) {
  c.save(); c.translate(x, y); c.rotate(rot); c.globalAlpha *= a; c.fillStyle = C.ink;
  c.beginPath(); c.ellipse(0, -s * .35, s * .36, s * .55, 0, 0, 6.283); c.fill();
  c.beginPath(); c.ellipse(0, s * .55, s * .28, s * .3, 0, 0, 6.283); c.fill();
  c.restore();
}
// 纸页（带一点阴影），用于翻页 / 滑进来的白纸
function sheet(c, x, y, w, h, o = {}) {
  c.save();
  if (o.shadow !== false) { c.fillStyle = 'rgba(17,16,20,.28)'; c.fillRect(x + 14, y + 14, w, h); }
  c.fillStyle = o.fill ?? C.paper; c.fillRect(x, y, w, h);
  if (o.edge !== false) { c.strokeStyle = C.ink; c.lineWidth = 2; c.strokeRect(x, y, w, h); }
  c.restore();
}
// 页码 / 小字
function pageNo(c, n, a = 1) { c.save(); c.globalAlpha *= a; text(c, `— ${n} —`, W / 2, H - 30, 20, { fam: SERIF, w: 300, fill: C.pencil }); c.restore(); }
