'use strict';
// ============================================================================
// 画面元素：字、光、雨、雪、道具（拍立得、手机、杯子、花束、风铃）、场景（灯管走廊、灯墙、百叶窗、天桥、海）
// 都画在 2D 层上；GL 背景在它下面，辉光 / 颗粒 / 黑边在它上面。
// ============================================================================

// ---------------------------------------------------------------- 字
function text(c, str, x, y, size, o = {}) {
  c.font = fnt(size, o.w ?? 700, o.fam ?? SANS); c.textAlign = o.align ?? 'center'; c.textBaseline = o.base ?? 'middle';
  if (o.spacing != null) c.letterSpacing = `${o.spacing}px`;
  if (o.stroke) { c.lineJoin = 'round'; c.lineWidth = o.lw ?? size * .04; c.strokeStyle = o.stroke; c.strokeText(str, x, y); }
  if (o.fill !== null) { c.fillStyle = o.fill ?? C.white; c.fillText(str, x, y); }
  if (o.spacing != null) c.letterSpacing = '0px';
}
const tw = (c, str, size, w = 700, fam = SANS, sp = 0) => { c.font = fnt(size, w, fam); c.letterSpacing = `${sp}px`; const v = c.measureText(str).width; c.letterSpacing = '0px'; return v; };

// 大字：t0 砸进来。style: 'thin'（细、宽字距，冷）/ 'heavy'（Anton，砸）/ 'zh'（粗黑体）/ 'serif'（粗宋体）
// o.k 外部控制的进度；o.out 消失时刻；o.fill 颜色；o.track 字距收拢
function bigWord(c, str, t, t0, o = {}) {
  const k = t - t0; if (k < 0) return;
  const out = o.out != null ? clamp((t - o.out) / (o.fade ?? .25)) : 0; if (out >= 1) return;
  const style = o.style ?? 'thin', size = o.size ?? 300;
  const fam = style === 'heavy' ? ANTON : style === 'thin' ? GROT : style === 'serif' ? SERIF : style === 'bold' ? GROT : SANS;
  const w = style === 'thin' ? 300 : style === 'heavy' ? 400 : style === 'bold' ? 700 : 900;
  const sp0 = o.track0 ?? (style === 'thin' ? size * .6 : size * .25), sp1 = o.track ?? (style === 'thin' ? size * .22 : size * .02);
  const sp = lerp(sp0, sp1, easeOut(k / (o.dur ?? .5)));
  const sc = (o.slam ?? true) ? 1 + .18 * Math.exp(-k * 18) : 1;
  c.save(); c.translate(o.x ?? W / 2, o.y ?? H / 2); c.scale(sc, sc); if (o.rot) c.rotate(o.rot);
  c.globalAlpha *= clamp(k / .05) * (1 - out) * (o.a ?? 1);
  if (o.blur) c.filter = `blur(${o.blur}px)`;
  text(c, str, sp / 2, 0, size, { fam, w, spacing: sp, fill: o.fill ?? C.white, stroke: o.stroke, lw: o.lw });
  c.filter = 'none'; c.restore();
}
// 竖排（中文）：tokens 一个一个往下，英文单词转 90°
function vtext(c, tokens, x, y, size, o = {}) {
  let yy = y; const lead = o.lead ?? 1.12;
  tokens.forEach((tk, i) => {
    const a = o.alphaOf ? o.alphaOf(i) : 1; if (a <= 0) { yy += /^[A-Za-z0-9']+$/.test(tk) ? tw(c, tk, size * .8, 400, MONO) + size * .3 : size * lead; return; }
    c.save(); c.globalAlpha *= a;
    if (/^[A-Za-z0-9']+$/.test(tk)) {
      const len = tw(c, tk, size * .8, 400, MONO);
      c.translate(x, yy + size * .1); c.rotate(Math.PI / 2); text(c, tk, 0, 0, size * .8, { ...o, fam: MONO, w: 400, align: 'left' }); c.restore();
      yy += len + size * .3; return;
    }
    text(c, tk, x, yy + size / 2, size, o); c.restore(); yy += size * lead;
  });
  return yy - y;
}
// 一句竖排歌词：唱到的字才出现（带一点从模糊到清楚）
function vLine(c, line, t, x, y, size, o = {}) {
  const tokens = line.words.map(w => w.w);
  vtext(c, tokens, x, y, size, { w: o.w ?? 900, fam: o.fam ?? SANS, fill: o.fill ?? C.white, lead: o.lead,
    alphaOf: i => (o.all ? 1 : clamp((t - line.words[i].t + .04) / .12)) * (o.a ?? 1) });
}
// 字幕：画面下方（黑边里）一行小等宽字，唱到的词才出现
function subtitle(c, line, t, o = {}) {
  if (!line || t < line.t - .05) return;
  const end = o.end ?? line.end; if (t > end + .2) return;
  const a = clamp((t - line.t + .05) / .1) * (1 - clamp((t - end) / .2)) * (o.a ?? 1);
  const words = line.words, size = o.size ?? 30, sp = 2;
  let s = ''; words.forEach((w, i) => { if (t >= w.t - .02) s += (i && /^[A-Za-z0-9']/.test(w.w) && /[A-Za-z0-9']$/.test(words[i - 1].w) ? ' ' : (i && (/^[A-Za-z0-9']/.test(w.w) || /[A-Za-z0-9']$/.test(words[i - 1].w)) ? ' ' : '')) + w.w; });
  c.save(); c.globalAlpha *= a;
  text(c, s, o.x ?? W / 2, o.y ?? H - 66, size, { fam: MONO, w: 400, fill: o.fill ?? 'rgba(238,243,247,.92)', spacing: sp });
  c.restore();
}
// 打字机：一行等宽字按时间打出来，光标闪
function typewriter(c, str, t, t0, o = {}) {
  const cps = o.cps ?? 14, chars = [...str], n = clamp(Math.floor((t - t0) * cps), 0, chars.length), s = chars.slice(0, n).join('');
  const size = o.size ?? 48, x = o.x ?? W / 2, y = o.y ?? H / 2;
  c.save(); c.font = fnt(size, 400, MONO); c.letterSpacing = '4px';
  const full = c.measureText(str).width, part = c.measureText(s).width;
  c.textAlign = 'left'; c.textBaseline = 'middle'; c.fillStyle = o.fill ?? C.white; c.fillText(s, x - full / 2, y);
  if (mod(t, .9) < .5 || n < chars.length) c.fillRect(x - full / 2 + part + 6, y - size * .5, size * .55, size);
  c.letterSpacing = '0px'; c.restore();
}
// 名牌：左下角，一条细线 + 名字（等宽、宽字距）+ 小字
function nameplate(c, name, sub, t, t0, t1) {
  const k = t - t0; if (k < 0 || t > t1 + .4) return;
  const a = clamp(k / .25) * (1 - clamp((t - t1) / .4));
  c.save(); c.globalAlpha *= a;
  const x = 120, y = H - 250, lw = 340 * easeOut(k / .5);
  c.fillStyle = C.ice; c.fillRect(x, y, lw, 2);
  const n = Math.floor(clamp((k - .15) * 24, 0, name.length));
  text(c, name.slice(0, n), x, y - 38, 46, { fam: GROT, w: 700, align: 'left', spacing: 10, fill: C.white });
  text(c, sub, x, y + 30, 20, { fam: MONO, w: 400, align: 'left', spacing: 6, fill: 'rgba(238,243,247,.65)' });
  c.restore();
}

// ---------------------------------------------------------------- 光
// 头顶的灯管：x, y 灯的位置，on 亮度 0..1
function tubeLamp(c, x, y, w, on, col = '#eef6ff') {
  c.save();
  c.fillStyle = '#15181c'; c.fillRect(x - w / 2 - 10, y - 14, w + 20, 14);
  c.fillStyle = on > .05 ? col : '#2a2e33'; c.globalAlpha = .3 + .7 * on; c.fillRect(x - w / 2, y, w, 9);
  if (on > .05) {
    c.globalCompositeOperation = 'lighter'; const g = c.createRadialGradient(x, y + 4, 0, x, y + 4, w * .9);
    g.addColorStop(0, `rgba(230,240,255,${.55 * on})`); g.addColorStop(1, 'rgba(230,240,255,0)'); c.fillStyle = g; c.fillRect(x - w, y - w, w * 2, w * 2);
  }
  c.restore();
}
// 光锥：从 (x, y) 往下张开，到地面 y + h，上宽 w0 下宽 w1
function lightCone(c, x, y, w0, w1, h, a, col = '220,232,245') {
  if (a <= 0) return;
  c.save(); c.globalCompositeOperation = 'lighter';
  const g = c.createLinearGradient(0, y, 0, y + h);
  g.addColorStop(0, `rgba(${col},${.5 * a})`); g.addColorStop(.6, `rgba(${col},${.16 * a})`); g.addColorStop(1, `rgba(${col},${.05 * a})`);
  c.fillStyle = g; c.beginPath(); c.moveTo(x - w0 / 2, y); c.lineTo(x + w0 / 2, y); c.lineTo(x + w1 / 2, y + h); c.lineTo(x - w1 / 2, y + h); c.closePath();
  c.filter = 'blur(18px)'; c.fill(); c.filter = 'none';
  // 地上的光圈
  const rg = c.createRadialGradient(x, y + h, 0, x, y + h, w1 * .6);
  rg.addColorStop(0, `rgba(${col},${.35 * a})`); rg.addColorStop(1, `rgba(${col},0)`);
  c.fillStyle = rg; c.save(); c.translate(x, y + h); c.scale(1, .18); c.translate(-x, -y - h); c.fillRect(x - w1, y + h - w1, w1 * 2, w1 * 2); c.restore();
  c.restore();
}
function coneClip(c, x, y, w0, w1, h) { c.beginPath(); c.moveTo(x - w0 / 2, y); c.lineTo(x + w0 / 2, y); c.lineTo(x + w1 / 2, y + h); c.lineTo(x - w1 / 2, y + h); c.closePath(); c.clip(); }
// 横向镜头光
function flare(c, x, y, a, len = 1100, col = '170,215,255') {
  if (a <= .01) return;
  c.save(); c.globalCompositeOperation = 'lighter';
  const g = c.createLinearGradient(x - len, 0, x + len, 0);
  g.addColorStop(0, `rgba(${col},0)`); g.addColorStop(.5, `rgba(${col},${.8 * a})`); g.addColorStop(1, `rgba(${col},0)`);
  c.fillStyle = g; c.fillRect(x - len, y - 3, len * 2, 6); c.globalAlpha = .35; c.fillRect(x - len, y - 16, len * 2, 32);
  const rg = c.createRadialGradient(x, y, 0, x, y, 260); rg.addColorStop(0, `rgba(255,255,255,${a})`); rg.addColorStop(.25, `rgba(230,240,255,${a * .6})`); rg.addColorStop(1, 'rgba(255,255,255,0)');
  c.globalAlpha = 1; c.fillStyle = rg; c.fillRect(x - 260, y - 260, 520, 520);
  c.restore();
}
function glow(c, x, y, r, a, col = '230,240,255') {
  if (a <= 0) return;
  c.save(); c.globalCompositeOperation = 'lighter'; const g = c.createRadialGradient(x, y, 0, x, y, r);
  g.addColorStop(0, `rgba(${col},${a})`); g.addColorStop(1, `rgba(${col},0)`); c.fillStyle = g; c.fillRect(x - r, y - r, r * 2, r * 2); c.restore();
}
// 失焦光斑
function bokeh(c, t, o) {
  const r = rand(o.seed ?? 3);
  c.save(); c.globalCompositeOperation = 'lighter';
  for (let i = 0; i < o.n; i++) {
    const x = r() * W + Math.sin(t * .15 + r() * 6) * 40 + (o.dx ?? 0), y = (o.y0 ?? 0) + r() * H * (o.span ?? .7), R = (o.r ?? 1) * (20 + r() * 80), col = o.cols[r() * o.cols.length | 0], a = o.alpha * (.3 + r() * .7);
    const g = c.createRadialGradient(x, y, 0, x, y, R);
    g.addColorStop(0, `rgba(${col},${a})`); g.addColorStop(.75, `rgba(${col},${a * .5})`); g.addColorStop(1, `rgba(${col},0)`);
    c.fillStyle = g; c.fillRect(x - R, y - R, 2 * R, 2 * R);
  }
  c.restore();
}

// ---------------------------------------------------------------- 天气
// 雨：tf 是雨的时间（冻住时传固定值）；同一个 seed = 同一场雨
function rain(c, tf, o) {
  const r = rand(o.seed ?? 1), span = H + 400;
  c.save(); c.strokeStyle = o.col ?? 'rgb(190,210,235)'; c.lineCap = 'round';
  if (o.lighter) c.globalCompositeOperation = 'lighter';
  for (let i = 0; i < o.n; i++) {
    const x0 = r() * (W + 800) - 400, sp = o.spd * (.6 + r() * .7), len = o.len * (.5 + r()), ph = r(), a = o.alpha * (.3 + r() * .7), lw = (o.lw ?? 1) * (.8 + r() * 1.6);
    const y = mod(ph * span + tf * sp, span) - 200, x = x0 - y * o.wind;
    c.globalAlpha = a; c.lineWidth = lw; c.beginPath(); c.moveTo(x, y); c.lineTo(x - len * o.wind, y + len); c.stroke();
  }
  c.restore();
}
// 雪：三层（远小慢、近大快且失焦）
function snow(c, t, o = {}) {
  const n = o.n ?? 160, a = o.a ?? 1;
  c.save(); c.fillStyle = '#fff';
  for (let layer = 0; layer < 3; layer++) {
    const r = rand((o.seed ?? 7) + layer * 31), m = Math.floor(n * [.55, .3, .15][layer]), sz = [2.2, 4.5, 11][layer], sp = [55, 95, 170][layer];
    if (layer === 2) c.filter = 'blur(4px)';
    for (let i = 0; i < m; i++) {
      const s = sz * (.6 + r() * .8), y = mod(r() * (H + 60) + t * sp * (.7 + r() * .6), H + 60) - 30;
      const x = mod(r() * W + Math.sin(t * (.5 + r()) + r() * 6) * 40 + t * (o.wind ?? 15), W);
      c.globalAlpha = a * (.35 + r() * .55) * [.7, .85, .6][layer];
      c.beginPath(); c.arc(x, y, s, 0, 6.283); c.fill();
    }
    c.filter = 'none';
  }
  c.restore();
}
// 呼出的白气
function breath(c, x, y, t, t0, a = 1) {
  const k = t - t0; if (k < 0 || k > 2) return;
  c.save(); c.globalCompositeOperation = 'lighter'; c.filter = 'blur(14px)';
  for (let i = 0; i < 4; i++) { const kk = k - i * .08; if (kk < 0) continue; const R = 20 + kk * 70, px = x + kk * 60 + i * 8, py = y - kk * 40; c.globalAlpha = a * .25 * (1 - kk / 2); c.fillStyle = '#e8eef5'; c.beginPath(); c.arc(px, py, R, 0, 6.283); c.fill(); }
  c.filter = 'none'; c.restore();
}

// ---------------------------------------------------------------- 道具
// 拍立得：s = 宽度；人像只是一个模糊的剪影，脸的地方被划掉
function polaroid(c, x, y, s, rot, seed, o = {}) {
  c.save(); c.translate(x, y); c.rotate(rot); c.globalAlpha *= o.a ?? 1;
  const h = s * 1.2, pw = s * .86, ph = pw;
  c.fillStyle = 'rgba(0,0,0,.45)'; c.fillRect(-s / 2 + 10, -h / 2 + 14, s, h);
  c.fillStyle = '#e9ecee'; c.fillRect(-s / 2, -h / 2, s, h);
  const px = -pw / 2, py = -h / 2 + s * .07;
  const g = c.createLinearGradient(0, py, 0, py + ph); g.addColorStop(0, '#9aa5b0'); g.addColorStop(1, '#2b3138');
  c.fillStyle = g; c.fillRect(px, py, pw, ph);
  c.save(); c.beginPath(); c.rect(px, py, pw, ph); c.clip(); c.filter = 'blur(3px)'; c.fillStyle = '#0d0f12';
  const hx = px + pw * (.35 + hash(seed, 1) * .3), hy = py + ph * .42;
  c.beginPath(); c.ellipse(hx, hy, pw * .12, pw * .15, 0, 0, 6.283); c.fill();
  c.beginPath(); c.moveTo(hx - pw * .32, py + ph); c.quadraticCurveTo(hx - pw * .3, hy + pw * .2, hx, hy + pw * .17); c.quadraticCurveTo(hx + pw * .3, hy + pw * .2, hx + pw * .32, py + ph); c.fill();
  c.filter = 'none';
  // 划掉的脸
  const r = rand(seed); c.strokeStyle = 'rgba(240,244,247,.95)'; c.lineWidth = s * .012; c.lineCap = 'round';
  c.beginPath(); c.moveTo(hx - pw * .16, hy - pw * .14);
  for (let i = 0; i < 16; i++) c.lineTo(hx + (i % 2 ? 1 : -1) * pw * (.12 + r() * .06), hy - pw * .14 + i * pw * .019 + r() * 4);
  c.stroke();
  c.restore(); c.restore();
}
// 手机：竖着，屏幕亮；screen(c, w, h) 在屏幕坐标里画内容
function phone(c, x, y, s, rot, screen, o = {}) {
  const w = 300 * s, h = 620 * s, r = 42 * s;
  c.save(); c.translate(x, y); c.rotate(rot);
  const rr = (x0, y0, ww, hh, rad) => { c.beginPath(); c.moveTo(x0 + rad, y0); c.arcTo(x0 + ww, y0, x0 + ww, y0 + hh, rad); c.arcTo(x0 + ww, y0 + hh, x0, y0 + hh, rad); c.arcTo(x0, y0 + hh, x0, y0, rad); c.arcTo(x0, y0, x0 + ww, y0, rad); c.closePath(); };
  glow(c, 0, 0, h * .8, .25 * (o.light ?? 1), '150,190,235');
  rr(-w / 2 - 8 * s, -h / 2 - 8 * s, w + 16 * s, h + 16 * s, r + 6 * s); c.fillStyle = '#0b0c0e'; c.fill(); c.strokeStyle = '#3a3f46'; c.lineWidth = 2; c.stroke();
  c.save(); rr(-w / 2, -h / 2, w, h, r); c.clip(); c.translate(-w / 2, -h / 2); c.scale(s, s);
  c.fillStyle = o.bg ?? '#0e1116'; c.fillRect(0, 0, 300, 620); screen(c, 300, 620); c.restore();
  c.fillStyle = '#000'; rr(-40 * s, -h / 2 + 12 * s, 80 * s, 22 * s, 11 * s); c.fill();
  c.restore();
}
// 杯子：玻璃杯 + 冰 + 深色液体；drop 0..1 一滴落下来溅开
function glassCup(c, x, y, s, t, drop) {
  c.save(); c.translate(x, y); c.scale(s, s);
  const w0 = 150, w1 = 120, h = 300;
  const path = () => { c.beginPath(); c.moveTo(-w0, -h); c.lineTo(-w1, 0); c.quadraticCurveTo(0, 18, w1, 0); c.lineTo(w0, -h); };
  // 液体
  c.save(); path(); c.closePath(); c.clip();
  const ly = -h * .62 + Math.sin(t * 2) * 2 - (drop > .3 ? Math.sin((drop - .3) * 20) * 6 * (1 - drop) : 0);
  const g = c.createLinearGradient(0, ly, 0, 0); g.addColorStop(0, '#1a2430'); g.addColorStop(1, '#040506');
  c.fillStyle = g; c.fillRect(-w0, ly, w0 * 2, h);
  // 冰块
  [[-50, -140, .2], [40, -165, -.3], [5, -95, .5]].forEach(([ix, iy, ir]) => {
    c.save(); c.translate(ix, iy + Math.sin(t * 1.3 + ix) * 3); c.rotate(ir); c.fillStyle = 'rgba(200,225,245,.18)'; c.strokeStyle = 'rgba(220,235,250,.6)'; c.lineWidth = 2.5;
    c.fillRect(-45, -45, 90, 90); c.strokeRect(-45, -45, 90, 90); c.restore();
  });
  c.fillStyle = 'rgba(210,230,250,.55)'; c.fillRect(-w0, ly - 2, w0 * 2, 3);
  c.restore();
  // 杯壁
  path(); c.strokeStyle = 'rgba(220,235,250,.75)'; c.lineWidth = 4; c.stroke();
  c.globalCompositeOperation = 'lighter'; c.fillStyle = 'rgba(220,235,250,.18)'; c.fillRect(-w0 + 22, -h + 10, 12, h - 30); c.fillRect(w0 - 50, -h + 30, 6, h - 80);
  c.globalCompositeOperation = 'source-over';
  // 一滴：落下 → 溅起
  if (drop > 0) {
    if (drop < .3) { const dy = lerp(-h - 300, ly, easeIn(drop / .3)); c.fillStyle = '#0a0d12'; c.strokeStyle = 'rgba(220,235,250,.8)'; c.lineWidth = 2; c.beginPath(); c.ellipse(0, dy, 9, 14, 0, 0, 6.283); c.fill(); c.stroke(); }
    else {
      const k = (drop - .3) / .7, r = rand(9);
      c.strokeStyle = 'rgba(220,235,250,.7)'; c.lineWidth = 2; c.beginPath(); c.ellipse(0, ly, 40 + k * 110, 6 + k * 10, 0, 0, 6.283); c.globalAlpha = 1 - k; c.stroke();
      c.fillStyle = 'rgba(15,20,28,1)'; c.strokeStyle = 'rgba(220,235,250,.8)';
      for (let i = 0; i < 14; i++) { const a = -Math.PI / 2 + (r() - .5) * 1.6, v = 150 + r() * 260, px = Math.cos(a) * v * k, py = ly + Math.sin(a) * v * k + 600 * k * k; c.beginPath(); c.arc(px, py, 3 + r() * 5, 0, 6.283); c.fill(); c.stroke(); }
      c.globalAlpha = 1;
    }
  }
  c.restore();
}
// 花束：冰蓝的勿忘我，纸包着
function bouquet(c, x, y, s, rot, o = {}) {
  c.save(); c.translate(x, y); c.rotate(rot); c.scale(s, s); c.globalAlpha *= o.a ?? 1;
  const blue = o.col ?? C.ice;
  c.strokeStyle = '#1b2228'; c.lineWidth = 5;
  for (let i = 0; i < 9; i++) { const a = -1.1 + i / 8 * 2.2; c.beginPath(); c.moveTo(0, 120); c.quadraticCurveTo(Math.sin(a) * 40, 0, Math.sin(a) * 110, -90 - Math.cos(a) * 60); c.stroke(); }
  const r = rand(4);
  for (let i = 0; i < 22; i++) {
    const a = -1.2 + r() * 2.4, d = 70 + r() * 90, fx = Math.sin(a) * d, fy = -50 - Math.cos(a) * d * .9, R = 14 + r() * 10;
    for (let k = 0; k < 5; k++) { const pa = k / 5 * 6.283 + i; c.beginPath(); c.ellipse(fx + Math.cos(pa) * R * .5, fy + Math.sin(pa) * R * .5, R * .42, R * .32, pa, 0, 6.283); c.fillStyle = blue; c.fill(); }
    c.beginPath(); c.arc(fx, fy, R * .18, 0, 6.283); c.fillStyle = '#f2f6fa'; c.fill();
  }
  // 包装纸
  c.beginPath(); c.moveTo(-120, -40); c.lineTo(120, -40); c.lineTo(28, 200); c.lineTo(-28, 200); c.closePath();
  const g = c.createLinearGradient(-120, 0, 120, 0); g.addColorStop(0, '#1d2329'); g.addColorStop(.45, '#58626c'); g.addColorStop(1, '#14181c');
  c.fillStyle = g; c.fill();
  c.fillStyle = '#c9d2da'; c.fillRect(-30, 120, 60, 14);
  c.restore();
}
// 风铃：玻璃铃 + 舌 + 纸条，swing 摆动角度
function chime(c, x, y, s, swing, o = {}) {
  c.save(); c.translate(x, y);
  c.strokeStyle = 'rgba(220,232,245,.7)'; c.lineWidth = 2; c.beginPath(); c.moveTo(0, -800); c.lineTo(0, 0); c.stroke();
  c.rotate(swing); c.scale(s, s);
  c.beginPath(); c.moveTo(-70, 110); c.quadraticCurveTo(-78, 0, 0, -6); c.quadraticCurveTo(78, 0, 70, 110); c.closePath();
  c.fillStyle = 'rgba(160,200,235,.18)'; c.fill(); c.strokeStyle = 'rgba(230,240,252,.9)'; c.lineWidth = 3; c.stroke();
  c.globalCompositeOperation = 'lighter'; c.fillStyle = 'rgba(230,240,252,.5)'; c.fillRect(-48, 20, 8, 70); c.globalCompositeOperation = 'source-over';
  c.save(); c.rotate(-swing * 1.8);
  c.strokeStyle = 'rgba(220,232,245,.8)'; c.beginPath(); c.moveTo(0, 0); c.lineTo(0, 210); c.stroke();
  c.fillStyle = '#d8e0e8'; c.fillRect(-16, 90, 32, 8);
  c.fillStyle = '#e9eef3'; c.fillRect(-24, 210, 48, 150); c.fillStyle = '#7d8790'; c.fillRect(-14, 230, 28, 3);
  c.restore();
  c.restore();
}
// 准星
function crosshair(c, x, y, r, a = 1, lock = 0) {
  c.save(); c.globalAlpha *= a; c.strokeStyle = lock > .5 ? C.ice : C.white; c.lineWidth = 2;
  c.beginPath(); c.arc(x, y, r, 0, 6.283); c.stroke();
  c.beginPath(); c.arc(x, y, r * .35, 0, 6.283); c.stroke();
  for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) { c.beginPath(); c.moveTo(x + dx * r * .6, y + dy * r * .6); c.lineTo(x + dx * r * 1.4, y + dy * r * 1.4); c.stroke(); }
  text(c, lock > .5 ? 'TARGET LOCKED' : 'SEARCHING', x + r * 1.5, y - r * 1.1, 18, { fam: MONO, w: 400, align: 'left', spacing: 4, fill: lock > .5 ? C.ice : C.white });
  c.restore();
}
// 玻璃裂纹：从 (x, y) 往外放射
function crack(c, x, y, R, k, seed) {
  if (k <= 0) return;
  const r = rand(seed); c.save(); c.strokeStyle = 'rgba(235,242,250,.9)'; c.lineCap = 'round';
  for (let i = 0; i < 11; i++) {
    let a = r() * 6.283, px = x, py = y; const L = R * (.4 + r() * .8) * easeOut(k), seg = 8;
    c.lineWidth = 1 + r() * 2.5; c.beginPath(); c.moveTo(px, py);
    for (let s = 0; s < seg; s++) { a += (r() - .5) * .5; px += Math.cos(a) * L / seg; py += Math.sin(a) * L / seg; c.lineTo(px, py); }
    c.stroke();
  }
  c.beginPath(); for (let ring = 1; ring <= 2; ring++) { const rr = R * .12 * ring * easeOut(k); for (let i = 0; i <= 9; i++) { const a = i / 9 * 6.283, q = rr * (.8 + r() * .4); i ? c.lineTo(x + Math.cos(a) * q, y + Math.sin(a) * q) : c.moveTo(x + Math.cos(a) * q, y + Math.sin(a) * q); } }
  c.lineWidth = 1.5; c.stroke(); c.restore();
}
// 黑条盖住（被消音）
function redact(c, x, y, w, h, k) { if (k <= 0) return; c.save(); c.fillStyle = '#000'; c.fillRect(x, y, w * easeOut(k), h); c.restore(); }

// ---------------------------------------------------------------- 场景
// 灯管走廊：一点透视，顶上一排灯管，off(i) 第 i 盏（0 = 最近）的亮度；z 往前走的距离
function corridor(c, t, lampOn, o = {}) {
  const vx = W / 2, vy = H * .46, z = o.z ?? 0, n = 14, blue = o.blue ?? 0;
  // 墙和地
  c.save();
  const g = c.createLinearGradient(0, vy, 0, H); g.addColorStop(0, '#0b0d10'); g.addColorStop(1, '#050607'); c.fillStyle = g; c.fillRect(0, vy, W, H - vy);
  c.fillStyle = '#060708'; c.fillRect(0, 0, W, vy);
  for (let i = n - 1; i >= 0; i--) {
    const d = (i + 1 - mod(z, 1)) * 1.0, sc = 1 / (d * .55 + .2), on = lampOn(i + Math.floor(z));
    const lw = 360 * sc, ly = vy - 520 * sc, lx = vx;
    if (sc < .05) continue;
    // 灯管
    c.globalAlpha = 1; c.fillStyle = on > .05 ? `rgba(235,244,255,${.4 + .6 * on})` : '#1a1d21'; c.fillRect(lx - lw / 2, ly, lw, Math.max(1.5, 12 * sc));
    if (on > .05) {
      c.globalCompositeOperation = 'lighter';
      const rg = c.createRadialGradient(lx, ly, 0, lx, ly, lw * 1.3); rg.addColorStop(0, `rgba(220,235,255,${.28 * on})`); rg.addColorStop(1, 'rgba(220,235,255,0)');
      c.fillStyle = rg; c.fillRect(lx - lw * 1.3, ly - lw * 1.3, lw * 2.6, lw * 2.6);
      // 地上的反光
      const fy = vy + 520 * sc * .85, fg = c.createRadialGradient(lx, fy, 0, lx, fy, lw * .8); fg.addColorStop(0, `rgba(200,220,245,${.16 * on})`); fg.addColorStop(1, 'rgba(200,220,245,0)');
      c.save(); c.translate(lx, fy); c.scale(1, .25); c.translate(-lx, -fy); c.fillStyle = fg; c.fillRect(lx - lw, fy - lw, lw * 2, lw * 2); c.restore();
      c.globalCompositeOperation = 'source-over';
    }
    // 墙上的竖线（门框）
    c.strokeStyle = 'rgba(120,135,150,.18)'; c.lineWidth = Math.max(1, 3 * sc);
    for (const sx of [-1, 1]) { const wx = vx + sx * 900 * sc; c.beginPath(); c.moveTo(wx, vy - 560 * sc); c.lineTo(wx, vy + 560 * sc); c.stroke(); }
  }
  // 墙上那块冰蓝的霓虹
  if (blue > 0) {
    const sc = 1 / (3.3 * .55 + .2), bx = vx - 900 * sc + 40 * sc, by = vy - 120 * sc;
    c.globalCompositeOperation = 'lighter'; c.fillStyle = `rgba(127,212,255,${blue})`; c.fillRect(bx, by, 14 * sc, 280 * sc);
    glow(c, bx, by + 140 * sc, 260 * sc, .5 * blue, '127,212,255');
    c.globalCompositeOperation = 'source-over';
  }
  c.restore();
}
// 灯墙：一排竖的灯条，levels(i) 亮度
function lightWall(c, n, levels, o = {}) {
  const x0 = o.x ?? 120, x1 = o.x1 ?? W - 120, y0 = o.y0 ?? 120, y1 = o.y1 ?? H - 260, gap = (x1 - x0) / n, bw = gap * (o.bw ?? .38);
  c.save();
  for (let i = 0; i < n; i++) {
    const v = clamp(levels(i)), x = x0 + gap * (i + .5);
    c.fillStyle = `rgba(235,244,255,${.06 + .94 * v})`; c.fillRect(x - bw / 2, y0, bw, y1 - y0);
    if (v > .05) { c.globalCompositeOperation = 'lighter'; const g = c.createLinearGradient(x - gap, 0, x + gap, 0); g.addColorStop(0, 'rgba(210,228,250,0)'); g.addColorStop(.5, `rgba(210,228,250,${.35 * v})`); g.addColorStop(1, 'rgba(210,228,250,0)'); c.fillStyle = g; c.fillRect(x - gap, y0 - 40, gap * 2, y1 - y0 + 80); c.globalCompositeOperation = 'source-over'; }
  }
  // 地面反光
  c.globalAlpha = .25; c.save(); c.translate(0, y1 * 2 + 20); c.scale(1, -1); c.filter = 'blur(8px)';
  for (let i = 0; i < n; i++) { const v = clamp(levels(i)), x = x0 + gap * (i + .5); c.fillStyle = `rgba(235,244,255,${.06 + .94 * v})`; c.fillRect(x - bw / 2, y1 - 300, bw, 300); }
  c.restore(); c.restore(); c.filter = 'none';
}
// 百叶窗投下的光：斜条纹，open 0..1 光条的宽度比例
function blinds(c, ang, period, open, a, col = '150,195,240') {
  c.save(); c.globalCompositeOperation = 'lighter'; c.translate(W / 2, H / 2); c.rotate(ang);
  c.filter = 'blur(3px)';
  for (let y = -H * 1.2; y < H * 1.2; y += period) { c.fillStyle = `rgba(${col},${a})`; c.fillRect(-W, y, W * 2, period * open); }
  c.restore(); c.filter = 'none';
}
// 天桥下：上方黑色的桥面和柱子，地面湿
function underpass(c, t, o = {}) {
  c.save();
  c.fillStyle = '#020306'; c.fillRect(0, 0, W, 170);
  c.fillStyle = '#05070b'; c.beginPath(); c.moveTo(0, 170); c.lineTo(W, 170); c.lineTo(W, 210); c.lineTo(0, 240); c.closePath(); c.fill();
  for (const px of [180, 1640]) { c.fillStyle = '#04060a'; c.fillRect(px, 170, 120, H - 170); c.fillStyle = 'rgba(120,160,210,.08)'; c.fillRect(px + 110, 170, 10, H - 170); }
  // 湿地面
  const g = c.createLinearGradient(0, 780, 0, H); g.addColorStop(0, 'rgba(10,22,44,.0)'); g.addColorStop(1, 'rgba(8,16,32,.85)'); c.fillStyle = g; c.fillRect(0, 780, W, H - 780);
  c.restore();
}
// 车灯扫过：k 0..1 从左到右
function headlights(c, k, y, a = 1, col = '230,240,255') {
  if (k <= 0 || k >= 1) return;
  const x = lerp(-600, W + 600, k);
  c.save(); c.globalCompositeOperation = 'lighter';
  for (const dx of [-90, 90]) {
    const g = c.createRadialGradient(x + dx, y, 0, x + dx, y, 700); g.addColorStop(0, `rgba(${col},${.55 * a})`); g.addColorStop(.2, `rgba(${col},${.2 * a})`); g.addColorStop(1, `rgba(${col},0)`);
    c.fillStyle = g; c.fillRect(x + dx - 700, y - 700, 1400, 1400);
  }
  c.restore();
  flare(c, x, y, a * .8, 1400);
}
// 黑色的海：水平线、细碎的反光
function sea(c, t, y0, o = {}) {
  c.save();
  const g = c.createLinearGradient(0, y0, 0, H); g.addColorStop(0, '#0a1424'); g.addColorStop(1, '#010204'); c.fillStyle = g; c.fillRect(0, y0, W, H - y0);
  const r = rand(5); c.globalCompositeOperation = 'lighter';
  for (let i = 0; i < 260; i++) {
    const d = Math.pow(r(), 1.8), y = y0 + d * (H - y0), x = r() * W, len = (10 + r() * 60) * (.3 + d * 1.6), ph = r() * 6.283;
    const a = (.15 + .5 * Math.max(0, Math.sin(t * (1 + r() * 2) + ph))) * (o.a ?? 1) * (1 - d * .5);
    c.fillStyle = `rgba(170,200,235,${a * .5})`; c.fillRect(x + Math.sin(t * .8 + ph) * 10, y, len, 1 + d * 2);
  }
  c.restore();
}
// 竖条：横着扫过画面的雪花噪点（替换 / 故障）
function noiseBlock(c, x, y, w, h, t, a = 1) {
  const f = frameNo(t), s = 6;
  c.save(); c.globalAlpha *= a;
  for (let yy = 0; yy < h; yy += s) for (let xx = 0; xx < w; xx += s) { const v = hash(xx * 13 + yy * 7 + f * 101, 3) * 255 | 0; c.fillStyle = `rgb(${v},${v},${v})`; c.fillRect(x + xx, y + yy, s, s); }
  c.restore();
}

// ---------------------------------------------------------------- 雾（画在人前面，有景深）
const FOG = (() => {
  const cv = mk(1024, 512), x = cv.getContext('2d'), r = rand(77);
  for (let i = 0; i < 90; i++) {
    const px = r() * 1024, py = r() * 512, R = 60 + r() * 160, g = x.createRadialGradient(px, py, 0, px, py, R);
    g.addColorStop(0, `rgba(255,255,255,${.05 + r() * .08})`); g.addColorStop(1, 'rgba(255,255,255,0)'); x.fillStyle = g;
    for (const dx of [-1024, 0, 1024]) x.fillRect(px - R + dx, py - R, R * 2, R * 2);
  }
  // 上下边缘淡掉，不会有接缝
  x.globalCompositeOperation = 'destination-in'; const fade = x.createLinearGradient(0, 0, 0, 512);
  fade.addColorStop(0, 'rgba(0,0,0,0)'); fade.addColorStop(.3, 'rgba(0,0,0,1)'); fade.addColorStop(.7, 'rgba(0,0,0,1)'); fade.addColorStop(1, 'rgba(0,0,0,0)');
  x.fillStyle = fade; x.fillRect(0, 0, 1024, 512);
  return cv;
})();
function fogLayer(c, t, a, o = {}) {
  if (a <= 0) return;
  c.save(); c.globalCompositeOperation = o.mode ?? 'screen'; c.globalAlpha = a;
  const sp = o.spd ?? 30, y = o.y ?? H * .35, h = o.h ?? H * .8, w = h * 2;
  for (let k = 0; k < 2; k++) {
    const off = mod(t * sp * (k ? 1.6 : 1) + k * 400, w);
    for (let x = -off; x < W; x += w) c.drawImage(FOG, x, y - (k ? 60 : 0), w, h);
  }
  c.restore();
}
