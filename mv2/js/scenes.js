'use strict';
// ============================================================================
// 分镜 v3（见 STORYBOARD.md）：人不在，只拍人刚离开的地方；字是主角。每个场景：
//   t0..t1  起止秒数
//   bg(t)   GL 背景 { mode, a, b, c, amt, speed, pulse }
//   draw(c, t, lt)  2D 层
//   fx(t, lt)       后期参数（覆盖 FX0）
// 字幕、片尾字幕在 overlay() 里，画在后期之后。名字只出现在场景里的金属铭牌上（stepPlaque / pillarPlaque）。
// ============================================================================
const FX0 = {
  zoom: 1, rot: 0, barrel: .02, shift: [0, 0], sq: [1, 1], ca: .6, split: 0, glitch: 0, gseed: 0, wave: 0, tear: 0,
  invert: 0, xerox: 0, grade: 'mono', gradeMix: 1, bloom: .55, bloomThr: .55, flash: 0, flashCol: '#ffffff', dark: 0,
  grain: .07, scan: 0, vig: .8, lb: 1,
};
const SCENES = [];
const scene = o => (SCENES.push(o), o);
function sceneAt(t) { for (let i = SCENES.length - 1; i >= 0; i--) if (t >= SCENES[i].t0) return SCENES[i]; return SCENES[0]; }
const MV_END = D.duration;

// ---------------------------------------------------------------- 公用
const FOGBG = (amt = .9, lamp = 1) => ({ mode: 'fog', a: '#040506', b: '#262c33', c: '#a8b8cc', amt: amt * lamp });
const CITY = (amt = 1) => ({ mode: 'city', a: '#02040a', b: '#0b1c3c', c: '#7fd4ff', amt });
const BLACK = { mode: 'solid', a: '#000000' };
const kickFx = (t, a = .3, d = 14) => M.hit('kick', t, d) * a;                       // 底鼓：闪一下
const punch = (t, a = .035) => 1 + M.hit('kick', t, 11) * a;                         // 底鼓：推一下
const RAIN_SEED = 7;                                                                 // 同一场雨
// 雨的时间：鼓停时冻住
const rainT = t => { const g = M.gap(t); return g && g[0] > 50 && g[0] < 60 ? g[0] : t; };

// 一盏灯下面一块空地：x 灯的位置，on 亮度；o.rain / o.snow 天气；o.pile 地上积雪
function lampSpace(c, t, o = {}) {
  const x = o.x ?? W / 2, y = o.y ?? 150, on = o.on ?? 1, h = o.h ?? 720, s = o.s ?? 1;
  tubeLamp(c, x, y, 260 * s, on);
  lightCone(c, x, y + 8, 240 * s, 1100 * s, h, on);
  if (o.pile) { c.save(); c.globalAlpha = o.pile * .55 * (.25 + .75 * on); c.filter = 'blur(8px)'; c.fillStyle = '#aab4be'; c.beginPath(); c.ellipse(x, y + h + 10, 380 * s, 46 * s, 0, 0, 6.283); c.fill(); c.restore(); c.filter = 'none'; }
  if (o.rain && on > .05) { c.save(); coneClip(c, x, y + 8, 240 * s, 1100 * s, h); rain(c, rainT(t), { seed: RAIN_SEED, n: o.rain, spd: 1500, len: 60, wind: .04, alpha: .55 * on }); c.restore(); }
}
// 湿地 / 雪地上的一串脚印，往远处延伸。按地面透视投影：近大远小、越远越扁。
// o.drift：每米往旁边偏多少（两串脚印往两边走）；o.snow：雪地上的凹痕，否则是湿地上的水印
const FLOOR = { yh: 560, f: 1100, hc: .9 };                                            // 地平线、焦距、镜头高度
const floorPt = (cx, X, Z) => [cx + FLOOR.f * X / Z, FLOOR.yh + FLOOR.f * FLOOR.hc / Z];
// 鞋底轮廓（单位：米）：u 从脚跟 -0.5 到脚尖 0.5，返回半宽；arch 那一侧更窄
function soleOutline(side) {
  const L = .28, ell = (u, c0, r) => Math.sqrt(Math.max(0, 1 - Math.pow((u - c0) / r, 2)));
  const hw = u => u > .2 ? .052 * ell(u, .2, .3) : u > -.1 ? lerp(.037, .052, (u + .1) / .3) : u > -.3 ? .037 : .042 * ell(u, -.3, .2);
  const pts = [];
  for (let i = 0; i <= 24; i++) { const u = -.5 + i / 24; pts.push([hw(u), u * L]); }
  for (let i = 24; i >= 0; i--) { const u = -.5 + i / 24; pts.push([-hw(u) * (u > -.3 && u < .1 ? .8 : 1), u * L]); }    // 脚弓那一侧略窄
  return pts.map(([v, l]) => [v * side, l]);
}
const SOLE = { l: soleOutline(-1), r: soleOutline(1) };
function footprints(c, t, t0, o = {}) {
  const n = o.n ?? 18, every = o.every ?? .22, cx = o.x ?? W / 2, drift = o.drift ?? 0, z0 = 2.3, stride = .75;
  c.save();
  for (let i = 0; i < n; i++) {
    const appear = t0 + i * every; if (t < appear) break;
    const right = i % 2, Z = z0 + i * stride * .5, X = (right ? .11 : -.11) + drift * (Z - z0), yaw = Math.atan(drift);
    const fade = clamp((t - appear) / .2) * clamp(1.15 - (Z - z0) / 6) * (o.a ?? 1);
    if (fade <= 0) continue;
    const poly = SOLE[right ? 'r' : 'l'].map(([v, l]) => { const vv = v * Math.cos(yaw) + l * Math.sin(yaw), ll = l * Math.cos(yaw) - v * Math.sin(yaw); return floorPt(cx, X + vv, Z + ll); });
    c.beginPath(); poly.forEach((p, k) => k ? c.lineTo(...p) : c.moveTo(...p)); c.closePath();
    if (o.snow) {                                                                  // 雪地：浅灰的凹痕，靠灯的一边有一点阴影
      c.globalAlpha = fade * .55; c.fillStyle = '#5d6872'; c.fill();
      c.globalAlpha = fade * .35; c.lineWidth = 2; c.strokeStyle = '#1d232a'; c.stroke();
    } else {                                                                       // 湿地：水印 + 边缘一点灯光反光
      c.globalAlpha = fade * .62; c.fillStyle = '#14181d'; c.fill();
      c.globalAlpha = fade * .45; c.lineWidth = 1.2; c.strokeStyle = '#c9d6e4'; c.stroke();
    }
  }
  c.restore();
}
// 一排灯往雾里一盏盏亮过去（"跟着你走"）：按透视往远处排；o.drift 每米往旁边偏多少
function lampPath(c, t, t0, o = {}) {
  const n = o.n ?? 9, every = o.every ?? .2, cx = o.x ?? W / 2, drift = o.drift ?? 0, x0 = o.x0 ?? 0, z0 = 2.6, gap = 1.6;
  for (let i = n - 1; i >= 0; i--) {                                              // 从远往近画
    const on = lampFlicker(t, t0 + i * every); if (on <= 0) continue;
    const Z = z0 + i * gap, X = x0 + drift * (Z - z0), sc = 2.6 / Z, fade = clamp(1.2 - i / n);
    const [x, yF] = floorPt(cx, X, Z), yC = FLOOR.yh - FLOOR.f * .9 / Z;
    c.save(); c.globalAlpha = fade;
    tubeLamp(c, x, yC, 220 * sc, on * Math.min(1, sc * 1.2));
    lightCone(c, x, yC + 6 * sc, 200 * sc, 760 * sc, yF - yC, on * .9 * Math.min(1, sc * sc * 1.5));
    c.restore();
  }
}
// YuKIluv7 的铭牌：灯下房间左下角一级水泥台阶，正面钉着金属牌，斜着拍。
// 透视：台阶正面的上下边都收向地平线上同一个消失点 VP（画面右外），牌子跟着同一个透视；台阶往左伸出画面外。
// on 灯的亮度，spec 反光扫过的位置
function stepPlaque(c, t, o = {}) {
  const on = o.on ?? 1, VP = [2700, FLOOR.yh], x0 = -60, x1 = 820, T0 = 690, B0 = 880;
  const edge = (y0, x) => lerp(y0, VP[1], (x - x0) / (VP[0] - x0));                // 从左边 (x0, y0) 收向 VP 的直线
  const fT = x => edge(T0, x), fB = x => edge(B0, x);
  const depth = .1, back = (x, y) => [lerp(x, -1400, depth), lerp(y, FLOOR.yh, depth)];   // 台面往里退（第二个消失点在左边很远）
  concrete(c, [back(x0, fT(x0)), back(x1, fT(x1)), [x1, fT(x1)], [x0, fT(x0)]], '#5a616a', '#41474e');                 // 台面（被灯照到）
  concrete(c, [[x0, fT(x0)], [x1, fT(x1)], [x1, fB(x1)], [x0, fB(x0)]], '#272c32', '#121518');                         // 正面
  c.fillStyle = 'rgba(0,0,0,.6)'; c.beginPath(); c.moveTo(x1, fT(x1)); c.lineTo(...back(x1, fT(x1))); c.lineTo(x1 + 18, fT(x1) + 4); c.lineTo(x1 + 4, fB(x1)); c.lineTo(x1, fB(x1)); c.closePath(); c.fill();   // 右端一点侧面
  const xL = 170, xR = 640, pl = (x, k) => lerp(fT(x), fB(x), k);
  plane(c, plaqueTex('YuKIluv7', 'VOCAL / LYRICS / COMPOSITION / ARRANGEMENT', false), xL, pl(xL, .22), pl(xL, .72), xR, pl(xR, .22), pl(xR, .72), { spec: o.spec, dark: (1 - on) * .85 + .12 });
  if (o.snow) { c.save(); c.globalAlpha = o.snow; c.filter = 'blur(2px)'; c.fillStyle = '#dfe6ec'; c.beginPath(); [back(x0, fT(x0)), back(x1, fT(x1)), [x1, fT(x1)], [x0, fT(x0)]].forEach((q, i) => i ? c.lineTo(...q) : c.moveTo(...q)); c.closePath(); c.fill(); c.restore(); c.filter = 'none'; }
}
// dangao_w 的铭牌：天桥底下前景一根水泥柱子，柱子侧面钉着金属牌，被雨淋湿
function pillarPlaque(c, t, o = {}) {
  const VP = [1500, 540], e = (y0, x) => lerp(y0, VP[1], (x + 20) / (VP[0] + 20));                        // 柱子侧面的上下边收向同一个消失点
  const yT = x => e(-160, x), yB = x => e(H + 160, x);
  concrete(c, [[-20, yT(-20)], [470, yT(470)], [470, yB(470)], [-20, yB(-20)]], '#0c1424', '#04070d');
  c.save(); c.beginPath(); c.moveTo(-20, yT(-20)); c.lineTo(470, yT(470)); c.lineTo(470, yB(470)); c.lineTo(-20, yB(-20)); c.closePath(); c.clip();
  rain(c, t * .25, { seed: 31, n: 70, spd: 300, len: 160, wind: 0, alpha: .18, col: 'rgb(160,200,240)' });          // 柱子上往下流的水
  c.restore();
  const xL = 80, xR = 400, mid = x => lerp(yT(x), yB(x), .44), hh = x => (yB(x) - yT(x)) * .085;
  plane(c, plaqueTex('dangao_w', 'VOCAL / LYRICS / COMPOSITION', true), xL, mid(xL) - hh(xL), mid(xL) + hh(xL), xR, mid(xR) - hh(xR), mid(xR) + hh(xR), { spec: o.spec, specA: .7, dark: .38 });
  c.fillStyle = 'rgba(160,210,255,.25)'; c.fillRect(466, yT(470), 4, yB(470) - yT(470));                               // 柱子边缘的一道蓝光
}
// 落下来的拍立得：从 t0 开始，每隔 gap 落一张，落到地上停住
function polaroidFall(c, t, t0, o = {}) {
  const n = o.n ?? 9;
  for (let i = 0; i < n; i++) {
    const t1 = t0 + i * (o.gap ?? .22); if (t < t1) break;
    const k = t - t1, land = .9, x = 260 + hash(i, 41) * (W - 520), yEnd = 560 + hash(i, 42) * 330;
    const y = k < land ? lerp(-260, yEnd, easeOut(k / land)) : yEnd, rot = (hash(i, 43) - .5) * 1.2 + (k < land ? Math.sin(k * 7 + i) * .3 * (1 - k / land) : 0);
    polaroid(c, x + (k < land ? Math.sin(k * 5 + i) * 40 : 0), y, 230 + hash(i, 44) * 70, rot, i * 13 + 1, { a: o.a ?? 1 });
    if (o.snowCover) { c.save(); c.globalAlpha = clamp((t - t1 - 1) / 3) * o.snowCover; c.translate(x, y); c.rotate(rot); c.fillStyle = '#e8eef4'; c.filter = 'blur(3px)'; c.fillRect(-150, -170, 300, 340); c.restore(); c.filter = 'none'; }
  }
}
// 甩镜头：k 0..1，画面整体横着甩走一屏，中间糊
function whip(c, k, draw) {
  const e = easeInOut(k), off = -e * W, blur = Math.sin(Math.PI * clamp(k)) * 60;
  c.save(); if (blur > 1) c.filter = `blur(${blur}px)`;
  c.translate(off, 0); draw(0); c.translate(W, 0); draw(1);
  c.restore(); c.filter = 'none';
}
// 当前这一句的大字：在这一句里显示，下一句开始时消失
function lineWord(c, t, t0, t1, str, o = {}) { if (t >= t0 && t < t1 + .3) bigWord(c, str, t, t0, { out: t1 - .05, ...o }); }

// ============================================================================
// 0 · 开灯
// ============================================================================
const lampFlicker = (t, t0) => { const k = t - t0; return k < 0 ? 0 : k < .07 ? 1 : k < .17 ? 0 : k < .22 ? .8 : k < .34 ? .1 : 1; };
scene({ name: '开灯', t0: 0, t1: .966,
  bg(t) { return FOGBG(.9, lampFlicker(t, .25)); },
  draw(c, t) { lampSpace(c, t, { on: lampFlicker(t, .25) }); stepPlaque(c, t, { on: lampFlicker(t, .25) * .9 }); fogLayer(c, t, .2 * lampFlicker(t, .25)); },
  fx(t) { return { dark: 1 - clamp(t / .25) * 0 }; },
});

// ============================================================================
// 1 · 前奏 · YuKIluv7：灯下空地，字在雾里
// ============================================================================
scene({ name: '前奏', t0: .966, t1: 7.046,
  bg(t) { return { ...FOGBG(), pulse: kickFx(t, .5) }; },
  draw(c, t) {
    const rainN = t < 5.57 ? 0 : lerp(60, 260, span(t, 5.57, 6.8));
    const strobe = t > 4.1 ? 1 - kickFx(t, .55, 18) : 1;
    lampSpace(c, t, { on: strobe, rain: rainN });
    stepPlaque(c, t, { on: .9 * strobe, spec: lerp(-.3, 1.3, span(t, 1.3, 2.6)) });
    // MEMORY：越来越淡、越来越糊
    if (t < 2.85) bigWord(c, 'MEMORY', t, .966, { size: 220, y: 470, a: 1 - span(t, 1.9, 2.8) * .9, blur: span(t, 1.6, 2.8) * 14, dur: 1.4, slam: false });
    // HISTORY：砸出来，一道线划掉
    if (t >= 2.803 && t < 4.3) {
      bigWord(c, 'HISTORY', t, 2.803, { style: 'heavy', size: 250, y: 470, out: 4.15 });
      const k = span(t, 3.74, 3.92); if (k > 0 && t < 4.15) { c.fillStyle = C.white; c.fillRect(W / 2 - 520, 476, 1040 * easeOut(k), 16); }
    }
    // REPLACE：被雪花噪点盖住、替换
    if (t >= 4.22 && t < 5.65) {
      bigWord(c, 'REPLACE', t, 4.22, { size: 200, y: 470, out: 5.5 });
      const k = span(t, 5.03, 5.1); if (k > 0 && t < 5.5) noiseBlock(c, W / 2 - 600, 380, 1200 * k, 180, t, .9);
    }
    // 我心里下起 rain
    if (t >= 5.57) vLine(c, LN(5.57), t, 1540, 230, 74);
    fogLayer(c, t, .22);
  },
  fx(t) { return { zoom: 1 + span(t, .966, 7) * .07, flash: t > 4.1 ? kickFx(t, .12) : 0 }; },
});

// ============================================================================
// 2 · ISOLATED
// ============================================================================
scene({ name: 'ISOLATED', t0: 7.046, t1: 8.185,
  bg: () => BLACK,
  draw(c, t, lt) {
    lampSpace(c, t, { y: 430, s: .18, h: 150 });
    const letters = [...'ISOLATED'], size = 190, sp = 70;
    c.font = fnt(size, 300, GROT); c.letterSpacing = `${sp}px`; const full = c.measureText('ISOLATED').width; c.letterSpacing = '0px';
    let x = W / 2 - full / 2 + sp / 2;
    letters.forEach((ch, i) => {
      const t0 = 7.046 + i * .055; const w = tw(c, ch, size, 300, GROT) + sp;
      if (t >= t0) { const k = t - t0; c.save(); c.globalAlpha = clamp(k / .03); const s = 1 + .25 * Math.exp(-k * 25); c.translate(x + w / 2 - sp / 2, 720); c.scale(s, s); text(c, ch, 0, 0, size, { fam: GROT, w: 300 }); c.restore(); }
      x += w;
    });
  },
  fx(t, lt) { return { flash: Math.exp(-lt * 9) * .8, glitch: t > 8.0 ? .6 : 0, gseed: frameNo(t), dark: span(t, 8.1, 8.185) }; },
});

// ============================================================================
// 3 · 主歌一 A：字在砸，背景在灯下 / 灯墙 / 雨之间硬切
// ============================================================================
const V1 = [8.185, 9.805, 10.899, 12.181, 13.472, 15.173, 16.401, 18.103, 19.191, 20.675, 21.875, 23.0];
const v1i = t => Math.max(0, lastAt(V1, t));
const wallKick = (t, i) => { const k = M.last('kick', t); return k.i < 0 ? .1 : (hash(i * 7 + k.i, 9) < .45 ? Math.exp(-k.since * 5) : .08); };
scene({ name: '主歌一A', t0: 8.185, t1: 23.0,
  bg(t) {
    const i = v1i(t);
    if (i === 1 || i === 6) return BLACK;
    if (i === 2) return { ...CITY(.7), b: '#101418' };
    if (i === 10) return BLACK;
    return { ...FOGBG(), pulse: kickFx(t, .5) };
  },
  draw(c, t) {
    const i = v1i(t), t0 = V1[i], t1 = V1[i + 1];
    if (i === 0) { lampSpace(c, t, { on: 1 - kickFx(t, .5, 18) }); lineWord(c, t, t0, t1, 'PAY', { style: 'heavy', size: 420, y: 500 }); }
    if (i === 1) {                                                                // 灯墙 + 左右拆开的宽字
      lightWall(c, 14, j => wallKick(t, j));
      lineWord(c, t, t0, t1, 'DEMAND', { size: 190, y: 540, track0: 400, track: 150, dur: .9 });
    }
    if (i === 2) {                                                                // 雨 + 竖着叠起来的字
      rain(c, t, { seed: RAIN_SEED, n: 420, spd: 1700, len: 90, wind: .1, alpha: .4, col: 'rgb(200,215,235)' });
      [...'KNOW'].forEach((ch, k) => { if (t >= t0 + k * .09) bigWord(c, ch, t, t0 + k * .09, { style: 'heavy', size: 170, x: 420, y: 270 + k * 165, out: t1 - .05 }); });
    }
    if (i === 3) {
      lampSpace(c, t, { on: 1 - kickFx(t, .5, 18) });
      lineWord(c, t, t0, t1, "DON'T", { style: 'heavy', size: 320, y: 430 });
      if (t > 12.6) lineWord(c, t, 12.6, t1, 'LIKE THIS', { size: 80, y: 650, track: 40 });
    }
    if (i === 4) {                                                                // 准星找目标，锁在空地上
      lampSpace(c, t, {});
      lineWord(c, t, t0, t1, 'TARGET', { size: 260, y: 360, a: .25, slam: false });
      const k = span(t, t0, 14.55), lock = t > 14.6 ? 1 : 0;
      const px = lerp(lerp(420, 1500, ease(span(k, 0, .4))), W / 2, ease(span(k, .4, 1))), py = lerp(lerp(380, 520, ease(span(k, 0, .4))), 880, ease(span(k, .4, 1)));
      crosshair(c, px + Math.sin(t * 9) * 6 * (1 - lock), py + Math.cos(t * 7) * 6 * (1 - lock), 70 + (1 - lock) * 20, 1, lock);
    }
    if (i === 5) {                                                                // 旁边又亮起一盏新灯，又灭
      lampSpace(c, t, { x: 700 });
      const on2 = lampFlicker(t, t0 + .1) * (1 - span(t, 16.1, 16.25));
      lampSpace(c, t, { x: 1400, on: on2 });
      lineWord(c, t, t0, t1, 'FRESH', { size: 170, y: 540, a: .9 });
    }
    if (i === 6) {                                                                // 负片
      lampSpace(c, t, { on: 1 - kickFx(t, .5, 18) });
      lineWord(c, t, t0, t1, 'NIGHTMARE', { style: 'heavy', size: 300, y: 520 });
    }
    if (i === 7) {                                                                // 玻璃裂开
      lampSpace(c, t, {});
      lineWord(c, t, t0, t1, 'WRONG', { style: 'heavy', size: 340, y: 520 });
      crack(c, 1180, 470, 900, span(t, 18.55, 18.75), 3);
    }
    if (i === 8) {                                                                // COMPLAIN 被黑条盖住
      lampSpace(c, t, {});
      crack(c, 1180, 470, 900, 1, 3);
      const cw = LN(19.191).words.find(w => w.w === 'complain').t;
      lineWord(c, t, cw - .25, t1, 'COMPLAIN', { size: 170, y: 540, track: 30, slam: false });
      if (t > cw) { c.save(); c.translate(W / 2, 540); c.rotate(-.02); redact(c, -760, -70, 1520, 140, span(t, cw + .15, cw + .35)); c.restore(); }
    }
    if (i === 9) {                                                                // 灯移走，地上一道长影子，前面没有人
      const lx = lerp(W / 2, 1450, ease(span(t, t0, t0 + 1)));
      lampSpace(c, t, { x: lx });
      c.save(); c.filter = 'blur(10px)'; c.fillStyle = 'rgba(0,0,0,.85)';
      const sx = W / 2 - 80, k = ease(span(t, t0, t0 + 1.1));
      c.beginPath(); c.moveTo(sx - 50, 872); c.lineTo(sx + 50, 872); c.lineTo(sx - 260 - 700 * k, 935); c.lineTo(sx - 420 - 760 * k, 920); c.closePath(); c.fill();
      c.restore(); c.filter = 'none';
      lineWord(c, t, t0, t1, 'SHADOW', { size: 150, y: 420, a: .8 });
    }
    if (i === 10) {                                                               // 门缝里的光，慢慢合上
      const k = ease(span(t, t0 + .3, 22.95)), wgap = lerp(70, 0, k);
      if (wgap > .5) {
        c.save(); c.fillStyle = '#e8f0f8'; c.fillRect(W / 2 - wgap / 2, 260, wgap, 620);
        c.globalCompositeOperation = 'lighter'; const g = c.createLinearGradient(W / 2 - 600, 0, W / 2 + 600, 0); g.addColorStop(0, 'rgba(200,220,245,0)'); g.addColorStop(.5, `rgba(200,220,245,${.3 * wgap / 70})`); g.addColorStop(1, 'rgba(200,220,245,0)');
        c.fillStyle = g; c.fillRect(W / 2 - 600, 260, 1200, 620);
        c.beginPath(); c.moveTo(W / 2 - wgap / 2, 880); c.lineTo(W / 2 + wgap / 2, 880); c.lineTo(W / 2 + wgap * 6, 1080); c.lineTo(W / 2 - wgap * 6, 1080); c.closePath(); c.fillStyle = `rgba(200,220,245,${.25 * wgap / 70})`; c.fill();
        c.restore();
      }
      vLine(c, { ...LN(21.875), words: LN(21.875).words.filter(w => !/^[a-z']+$/i.test(w.w)) }, t, 1450, 300, 80);
    }
    if (i !== 1 && i !== 10) fogLayer(c, t, .2);
  },
  fx(t) {
    const i = v1i(t);
    return { zoom: punch(t), flash: kickFx(t, .14), invert: i === 6 ? 1 : 0, wave: i === 6 ? .15 : 0,
      glitch: (i === 6 && M.hit('kick', t, 20) > .4) ? .5 : 0, gseed: frameNo(t), split: i === 7 ? span(t, 18.55, 18.6) * 3 * (1 - span(t, 18.7, 19)) : 0 };
  },
});

// ============================================================================
// 4 · 主歌一 B：灯管走廊，身后一盏盏灭；「黑白」时冰蓝熄灭
// ============================================================================
const V1B = [23.359, 24.723, 26.111, 27.346, 28.803, 30.091, 31.554, 32.770];
scene({ name: '走廊', t0: 23.0, t1: 34.17,
  bg: () => BLACK,
  draw(c, t) {
    const passed = Math.max(0, lastAt(V1B, t) + 1), bw = LN(30.091).words, tb = bw.find(w => w.w === '黑').t, tw2 = bw.find(w => w.w === '白').t;
    let blue = 1;
    if (t > tb) blue = (t < tb + .08 || (t > tw2 && t < tw2 + .08)) ? 1 : t < tw2 + .25 ? .15 : 0;
    const allOff = t > 31.554 ? 1 : 0;
    corridor(c, t, i => allOff ? 0 : (i > 13 - passed * 1.7 ? 0 : 1) * (1 - kickFx(t, .35, 16)), { z: (t - 23) * .55, blue });
    // 当前这一句竖排，前一句淡出
    const li = lastAt(V1B, t);
    for (const j of [li - 1, li]) {
      if (j < 0) continue;
      const ln = LN(V1B[j]), a = j === li ? 1 : 1 - span(t, V1B[li], V1B[li] + .35);
      if (a <= 0) continue;
      vLine(c, ln, t, j % 2 ? 360 : 1560, 210, 84, { fam: SERIF, a });
    }
  },
  fx(t) { return { zoom: 1 + (t - 23) * .006 + M.hit('kick', t, 11) * .02, flash: kickFx(t, .08) }; },
});

// ============================================================================
// 5 / 9 · 换气：全黑打字
// ============================================================================
function typeScene(name, t0, t1, line, snowy) {
  scene({ name, t0, t1, bg: () => BLACK,
    draw(c, t) {
      if (snowy) snow(c, t, { n: lerp(30, 160, span(t, line, t1)), a: .8 });
      typewriter(c, '可是 i want u know that', t, line, { size: 52, y: H / 2, cps: 11 });
    },
    fx: () => ({ bloom: .35 }) });
}
typeScene('换气一', 34.17, 36.96, 34.671, false);

// ============================================================================
// 6 / 10 · 副歌
// ============================================================================
function hook(c, t, L, second) {
  const [lTurn, lFollow, lForget, lLove, lMore, end] = L;
  const lamps = second ? [W * .26, W * .74] : [W / 2];
  const space = (dx, o = {}) => { c.save(); c.translate(dx, 0); for (const x of lamps) lampSpace(c, t, { x, s: second ? .75 : 1, on: 1 - kickFx(t, .4, 18), pile: second ? .35 : 0, ...o }); c.restore(); };
  if (t < lTurn) { space(0); }
  else if (t < lFollow) {                                                         // 甩镜头，那边也是空的
    whip(c, span(t, lTurn + .05, lTurn + .45), () => space(0));
    lineWord(c, t, lTurn + .45, lFollow, 'TURN AROUND', { size: 120, y: 420, track: 50 });
  } else if (t < lForget) {                                                       // 一排灯往雾里一盏盏亮过去
    if (second) { lampPath(c, t, lFollow, { x0: -.7, drift: -.45, n: 6 }); lampPath(c, t, lFollow + .1, { x0: .7, drift: .45, n: 6 }); }
    else lampPath(c, t, lFollow, { x0: -1.1, drift: .3 });
    lineWord(c, t, lFollow, lForget, 'FOLLOWING', { size: 110, y: 800, track: 60, a: .9 });
  } else if (t < lLove) {                                                          // 拍立得落下
    space(0);
    polaroidFall(c, t, lForget, { snowCover: second ? 1 : 0 });
    lineWord(c, t, lForget + .1, lLove, "DON'T LET ME FORGET", { size: 74, y: 300, track: 26 });
  } else if (t < lMore) {
    if (!second) lightWall(c, 14, () => .5 + .5 * M.hit('kick', t, 6));
    else space(0);
    lineWord(c, t, lLove, lMore, 'U LOVE', { style: 'heavy', size: 420, y: 530 });
  } else {                                                                         // ONEMORE 碎成雾；第二次两盏灯往两边退远
    const k = ease(span(t, lMore, end));
    if (second) { c.save(); for (const [x, d] of [[W * .26, -1], [W * .74, 1]]) lampSpace(c, t, { x: x + d * k * 420, s: .75 * (1 - k * .5), h: 720 - k * 200, pile: .35 }); c.restore(); }
    else space(0);
    bigWord(c, 'ONEMORE', t, lMore, { size: 200, y: 500, blur: span(t, lMore + .9, end) * 18, a: 1 - span(t, lMore + 1.2, end) * .85, track: 40 + span(t, lMore, end) * 120, dur: 1.2 });
  }
  if (second) snow(c, t, { n: 220 });
  fogLayer(c, t, .2);
}
const H1 = [37.415, 38.464, 40.300, 42.902, 44.378, 45.33];
scene({ name: '副歌一', t0: 36.96, t1: 45.33,
  bg(t) { return t >= 42.902 && t < 44.378 ? BLACK : { ...FOGBG(), pulse: kickFx(t, .5) }; },
  draw(c, t) { hook(c, t, H1, false); },
  fx(t) { return { zoom: punch(t), flash: kickFx(t, .15), invert: t >= 42.902 && t < 44.378 && M.hit('kick', t, 30) > .5 ? 1 : 0 }; },
});

// ============================================================================
// 7 · 间奏一：灯墙跟着底鼓亮，ISOLATED 的碎片，越剪越快，最后全灭
// ============================================================================
scene({ name: '间奏一', t0: 45.33, t1: 56.49,
  bg: () => BLACK,
  draw(c, t) {
    if (t > 55.97 + .25) return;                                                   // 最后一拍：灯全灭
    const fast = t > 53.7;
    const lv = j => {
      const k = M.last('kick', t), s = M.last('snare', t);
      if (fast) return Math.max(Math.exp(-k.since * 8), Math.exp(-s.since * 10) * .8);
      const pat = hash(k.i * 5 + j, 3) < .5 ? 1 : .1, wave = .5 + .5 * Math.sin(j * .9 - t * 6);
      return Math.max(pat * Math.exp(-k.since * 3), wave * .25, Math.exp(-s.since * 9) * (j % 3 === 0 ? .9 : 0));
    };
    lightWall(c, 16, lv);
    // ISOLATED 的字母碎片
    const sn = M.last('snare', t);
    if (sn.since < .18) {
      const ch = 'ISOLATED'[sn.i % 8];
      bigWord(c, ch, t, sn.t, { style: 'heavy', size: 520, x: 300 + hash(sn.i, 5) * (W - 600), y: 540, a: 1 - sn.since / .18 });
    }
    if (t > 50.9 && t < 53.7) { const k = M.last('kick', t); if (k.since < .3) bigWord(c, 'ISOLATED', t, k.t, { size: 120, y: 540, track: 80, a: 1 - k.since / .3, slam: false }); }
  },
  fx(t) {
    const k = M.last('kick', t);
    return { zoom: punch(t, .05), flash: kickFx(t, .2), invert: k.i % 4 === 3 && k.since < .2 ? 1 : 0, glitch: t > 54.9 && t < 56.2 ? .5 : 0, gseed: frameNo(t),
      split: M.hit('snare', t, 18) * 2, rot: k.i % 2 ? .01 : -.01 };
  },
});

// ============================================================================
// 8 · dangao_w：深蓝雨夜
// ============================================================================
const DG = [56.68, 58.766, 61.907, 63.459, 64.621, 65.573, 67.365, 69.277, 71.549, 74.314, 77.110, 79.636, 80.844, 82.276, 83.701, 84.901, 86.294, 87.922, 89.294, 89.98];
const dgRain = (c, t, o = {}) => rain(c, rainT(t), { seed: RAIN_SEED, n: o.n ?? 520, spd: 1900, len: 90, wind: .12, alpha: o.a ?? .45, col: 'rgb(150,200,255)' });
// 竖排的一句，放在右边或左边
const dgLine = (c, t, t0, x = 1560, o = {}) => vLine(c, LN(t0), t, x, 210, o.size ?? 76, { fam: o.fam ?? SANS, fill: o.fill, a: o.a });
// 手机界面
function chatScreen(c, w, h, t) {
  c.fillStyle = '#0d1015'; c.fillRect(0, 0, w, h);
  c.fillStyle = '#151a21'; c.fillRect(0, 0, w, 92);
  text(c, '唯一', w / 2, 62, 22, { w: 900, fill: '#e8eef4' });
  const msgs = [[79.0, '在吗'], [79.9, '我们能不能再聊聊'], [81.1, '?']], seenAt = [79.6, 80.5, 81.7];
  let y = 150;
  msgs.forEach(([mt, s], i) => {
    if (t < mt) return;
    c.font = fnt(20, 300, SANS); const bw = c.measureText(s).width + 36;
    c.fillStyle = '#7fd4ff'; c.beginPath(); c.roundRect(w - 20 - bw, y, bw, 46, 14); c.fill();
    text(c, s, w - 20 - bw / 2, y + 24, 20, { w: 300, fill: '#06121c' });
    if (t > seenAt[i]) text(c, '已读', w - 24, y + 62, 13, { w: 300, align: 'right', fill: 'rgba(232,238,244,.55)' });
    y += 86;
  });
}
function contactScreen(c, w, h, t) {
  c.fillStyle = '#0d1015'; c.fillRect(0, 0, w, h);
  text(c, '联系人', 26, 70, 26, { w: 900, align: 'left', fill: '#e8eef4' });
  const names = ['A', '阿', '唯一', '…'];
  const press = span(t, 86.3, 86.6), deleted = t > 87.55;
  names.forEach((n, i) => {
    const y = 130 + i * 70;
    if (n === '唯一') {
      if (deleted) { c.globalAlpha = 1 - span(t, 87.55, 87.8); }
      c.fillStyle = `rgba(127,212,255,${.18 * press})`; c.fillRect(0, y - 30, w, 64);
    }
    text(c, n, 30, y, 22, { w: 300, align: 'left', fill: '#e8eef4' }); c.globalAlpha = 1;
    c.fillStyle = 'rgba(232,238,244,.08)'; c.fillRect(30, y + 34, w - 30, 1);
  });
  if (t > 86.75 && t < 87.55) {                                                    // 弹窗：删除？
    c.fillStyle = 'rgba(0,0,0,.6)'; c.fillRect(0, 0, w, h);
    c.fillStyle = '#1b2129'; c.beginPath(); c.roundRect(30, 250, w - 60, 170, 18); c.fill();
    text(c, '删除联系人「唯一」？', w / 2, 300, 18, { w: 300, fill: '#e8eef4' });
    const hit = t > 87.2;
    c.fillStyle = hit ? '#7fd4ff' : 'rgba(127,212,255,.15)'; c.beginPath(); c.roundRect(56, 345, w - 112, 48, 12); c.fill();
    text(c, '删除', w / 2, 370, 18, { w: 900, fill: hit ? '#06121c' : '#7fd4ff' });
  }
}
scene({ name: 'dangao_w', t0: 56.49, t1: 89.98,
  bg(t) {
    const i = lastAt(DG, t);
    if (t > 89.46) return { ...FOGBG(), pulse: 0 };
    if (i >= 13 && i <= 14) return { mode: 'solid', a: '#03060c' };
    if ((i >= 4 && i <= 5) || i === 11 || i === 12 || i >= 15) return BLACK;
    return CITY(1);
  },
  draw(c, t) {
    const i = Math.max(0, lastAt(DG, t)), T = DG[i];
    // a 忘记你一次：打出来，再删掉；全静时雨冻住
    if (t < 58.766) {
      underpass(c, t); dgRain(c, t);
      const hk = span(rainT(t), 56.75, 57.75); headlights(c, hk, 640, .55);                 // 车灯扫过，铭牌闪一下
      pillarPlaque(c, t, { spec: lerp(1.4, -.4, hk) });
      const s = '我试着再忘记你一次', typed = wordsSung(LN(56.68), t), del = Math.floor(span(t, 57.55, 57.88) * s.length);
      const str = [...s].slice(0, Math.max(0, typed - del)).join('');
      c.save(); c.font = fnt(70, 300, SANS); c.letterSpacing = '14px'; c.textAlign = 'left'; c.textBaseline = 'middle'; c.fillStyle = C.white;
      const full = c.measureText(s).width, cx = W / 2 + 200; c.fillText(str, cx - full / 2, 520); const pw = c.measureText(str).width;
      if (mod(t, .8) < .45) c.fillRect(cx - full / 2 + pw + 8, 485, 4, 70); c.restore();
      return;
    }
    // b/c 牢笼：百叶窗的影子一根根收紧，字被关在后面、被挤扁
    if (t < 64.621) {
      underpass(c, t); dgRain(c, t, { n: 300 });
      blinds(c, -.08, 120, .45, .18);
      const squeeze = t > 61.907 ? lerp(1, .55, ease(span(t, 61.907, 64.5))) : 1;
      c.save(); c.translate(W / 2, 0); c.scale(squeeze, 1); c.translate(-W / 2, 0);
      if (t < 61.907) dgLine(c, t, 58.766, W / 2 + 40, { size: 80 });
      else { dgLine(c, t, 61.907, W / 2 + 120, { size: 80 }); if (t > 63.459) dgLine(c, t, 63.459, W / 2 - 120, { size: 80 }); }
      c.restore();
      const close = ease(span(t, 58.766, 64.5)), n = 9, gap = W / n;
      const shown = Math.min(n, M.between('snare', 58.7, t + 1e-3).length + M.between('kick', 58.7, t + 1e-3).length + 3);
      c.fillStyle = '#010204';
      for (let j = 0; j < n; j++) {
        if (j >= shown) continue;
        const bw = lerp(18, gap * .62, close), x = (j + .5) * gap;
        const drop = clamp((t - 58.766 - j * .03) / .2);
        c.fillRect(x - bw / 2, lerp(-H, 0, easeOut(drop)), bw, H);
      }
      return;
    }
    // d 苦涩的像 mojo：一杯冰，一滴落下溅开（慢动作）
    if (t < 67.365) {
      glow(c, W / 2, 520, 700, .12, '127,212,255');
      glassCup(c, W / 2, 860, 1.25, t, span(t, 65.0, 66.9));
      dgLine(c, t, 64.621, 1560, { size: 70 }); if (t > 65.573) dgLine(c, t, 65.573, 1430, { size: 70 });
      if (t > 66.1) bigWord(c, 'MOJO', t, 66.1, { size: 120, x: 480, y: 540, a: .85, track: 50 });
      return;
    }
    // e 不想去 / 没有你的未来：车灯迎面开来把画面烧白，然后空路上尾灯远去
    if (t < 71.549) {
      underpass(c, t); dgRain(c, t);
      if (t < 69.277) { const k = span(t, 67.365, 69.277), s = .1 + Math.pow(k, 3) * 2.5; for (const dx of [-1, 1]) glow(c, W / 2 + dx * 140 * s, 700, 380 * s, .4 + k * .6); flare(c, W / 2, 700, k, 900 + 900 * k); dgLine(c, t, 67.365, 1560); }
      else { const k = span(t, 69.277, 71.5), s = lerp(1, .05, easeOut(k)); for (const dx of [-1, 1]) glow(c, W / 2 + dx * 160 * s, lerp(760, 640, k), 140 * s + 20, .7, '200,225,255'); dgLine(c, t, 69.277, 1560); }
      return;
    }
    // f 风铃：「叮」，一道白光把画面斜着劈开，两半错开
    if (t < 74.314) {
      const cutT = LN(71.549).words.find(w => w.w === '切').t, cut = span(t, cutT, cutT + .12), sep = easeOut(span(t, cutT + .05, cutT + .5));
      const drawAll = () => { underpass(c, t); dgRain(c, t, { n: 380 }); chime(c, W / 2, 330, 1.3, Math.sin(t * 2.4) * .12 * (1 + 2 * Math.exp(-(t - 71.6) * 2))); dgLine(c, t, 71.549, 1560); };
      if (cut <= 0) { drawAll(); glow(c, W / 2, 420, 400, Math.exp(-Math.max(0, t - 71.6) * 3) * .5 * (t > 71.6 ? 1 : 0)); return; }
      const ang = -.42, nx = Math.sin(ang), ny = -Math.cos(ang);
      for (const side of [-1, 1]) {
        c.save(); c.beginPath(); c.translate(W / 2, H / 2); c.rotate(ang); c.rect(-W * 2, side < 0 ? -H * 2 : 0, W * 4, H * 2); c.rotate(-ang); c.translate(-W / 2, -H / 2); c.clip();
        c.translate(side * nx * 40 * sep + side * 30 * sep, side * ny * 40 * sep); drawAll(); c.restore();
      }
      c.save(); c.translate(W / 2, H / 2); c.rotate(ang); c.fillStyle = '#fff'; c.globalAlpha = 1 - span(t, cutT + .1, cutT + .6) * .7; c.fillRect(-W * 1.5 * cut, -3, W * 3 * cut, 6); c.restore();
      return;
    }
    // g 再来遍你还是会离开：尾灯开走，倒带，再开走 ×3
    if (t < 77.110) {
      underpass(c, t); dgRain(c, t);
      const cyc = (t - 74.314) / .93, ph = mod(cyc, 1), k = ph < .75 ? ph / .75 : 1 - (ph - .75) / .25;
      const s = lerp(1, .06, easeOut(k));
      for (const dx of [-1, 1]) glow(c, W / 2 + dx * 170 * s, lerp(800, 640, k), 150 * s + 18, .8, '200,225,255');
      dgLine(c, t, 74.314, 1560);
      if (ph > .75) bigWord(c, '<< REWIND', t, 74.314 + Math.floor(cyc) * .93 + .7, { size: 40, x: 260, y: 230, slam: false, style: 'bold', track: 8, a: .9 });
      return;
    }
    // h 不懂：「不懂」在底鼓上叠满画面
    if (t < 79.636) {
      underpass(c, t); dgRain(c, t, { n: 300 });
      for (const e of M.between('kick', 77.11, t + 1e-3)) {
        const k = t - e.t; c.save(); c.globalAlpha = Math.max(.15, Math.exp(-k * 2));
        text(c, '不懂', 200 + hash(e.i, 2) * (W - 400), 250 + hash(e.i, 3) * 580, 120 + hash(e.i, 4) * 160, { w: 900, fill: C.white }); c.restore();
      }
      dgLine(c, t, 77.110, 1600, { size: 70 });
      return;
    }
    // i 不在乎 / 不理睬：手机，已读，没有回复
    if (t < 82.276) {
      phone(c, W / 2, 560, 1.15, -.04, (cc, w, h) => chatScreen(cc, w, h, t));
      dgLine(c, t, 79.636, 1560, { size: 70 }); if (t > 80.844) dgLine(c, t, 80.844, 1430, { size: 70 });
      dgRain(c, t, { n: 160, a: .25 });
      return;
    }
    // j 花束从桥上掉进黑色的海
    if (t < 84.901) {
      sea(c, t, 600);
      const hitT = 83.75;
      if (t < hitT) { const k = span(t, 82.3, hitT); bouquet(c, W / 2 + Math.sin(k * 3) * 40, lerp(-60, 610, k * k), .9, k * 3.2, {}); }
      else {
        const k = t - hitT; const r = rand(4);
        c.save(); c.fillStyle = '#d8e4f0';
        for (let j = 0; j < 40; j++) { const a = -Math.PI / 2 + (r() - .5) * 1.8, v = 300 + r() * 700, px = W / 2 + Math.cos(a) * v * k, py = 610 + Math.sin(a) * v * k + 1400 * k * k; if (py < 640) { c.globalAlpha = 1 - clamp(k); c.beginPath(); c.arc(px, py, 3 + r() * 6, 0, 6.283); c.fill(); } }
        c.restore();
        glow(c, W / 2, 680 + k * 60, 260, .5 * Math.exp(-k * .9), '127,212,255');
        for (let rr = 0; rr < 3; rr++) { const kk = k - rr * .35; if (kk <= 0) continue; c.save(); c.strokeStyle = `rgba(200,225,250,${.5 * Math.exp(-kk)})`; c.lineWidth = 2; c.beginPath(); c.ellipse(W / 2, 612, 80 + kk * 400, 8 + kk * 30, 0, 0, 6.283); c.stroke(); c.restore(); }
        if (k < .6) bigWord(c, '扑通', t, hitT, { style: 'zh', size: 120, x: 1350, y: 380, a: 1 - k / .6 });
      }
      dgLine(c, t, t < 83.701 ? 82.276 : 83.701, 1560, { size: 70 });
      return;
    }
    // k 别再傻了 / 删掉曾经的唯一 / 就只能这样了
    if (t < 89.294) {
      dgRain(c, t, { n: 160, a: .2 });
      const off = span(t, 87.95, 88.15);
      if (off < 1) phone(c, W / 2, 560, 1.15, .03, (cc, w, h) => { contactScreen(cc, w, h, t); if (off > 0) { cc.fillStyle = `rgba(0,0,0,${off})`; cc.fillRect(0, 0, w, h); } }, { light: 1 - off });
      else phone(c, W / 2, 560, 1.15, .03, (cc, w, h) => { cc.fillStyle = '#000'; cc.fillRect(0, 0, w, h); rain(cc, t, { seed: 3, n: 40, spd: 500, len: 30, wind: .05, alpha: .25 }); }, { light: 0, bg: '#000' });
      dgLine(c, t, t < 86.294 ? 84.901 : t < 87.922 ? 86.294 : 87.922, 1560, { size: 70 });
      return;
    }
    // l back to you, yuki：雨里的蓝光一下切回冷白
    if (t < 89.46) { underpass(c, t); dgRain(c, t); bigWord(c, 'back to you, yuki', t, 89.294, { size: 70, y: 540, track: 16, slam: false }); }
    else { lampSpace(c, t, { rain: 120 }); stepPlaque(c, t, { spec: lerp(-.3, 1.3, span(t, 89.5, 89.95)) }); bigWord(c, 'back to you, yuki', t, 89.294, { size: 70, y: 540, track: 16, slam: false, a: 1 - span(t, 89.7, 89.98) }); }
  },
  fx(t) {
    const i = lastAt(DG, t);
    const fx = { grade: 'none', gradeMix: 0, zoom: punch(t, .025), flash: kickFx(t, .08), flashCol: '#cfe6ff' };
    if (t >= 69.1 && t < 69.5) fx.flash = Math.max(fx.flash, 1 - span(t, 69.25, 69.5));                 // 车灯烧白
    if (i === 9 && mod((t - 74.314) / .93, 1) > .75) { fx.tear = .6; fx.glitch = .3; fx.gseed = frameNo(t); }   // 倒带
    if (i === 10) fx.flash = kickFx(t, .35);
    if (t > 89.46) { fx.grade = 'mono'; fx.gradeMix = 1; fx.flash = Math.max(fx.flash, 1 - span(t, 89.46, 89.7)); fx.flashCol = '#ffffff'; }
    if (t >= 57.889 && t < 58.761) fx.zoom = 1.03;                                                         // 全静：画面冻住
    return fx;
  },
});

typeScene('换气二', 89.98, 92.77, 90.547, true);

// ============================================================================
// 10 · 副歌二：雪里两盏灯
// ============================================================================
const H2 = [93.237, 94.324, 96.063, 98.800, 100.082, 103.94];
scene({ name: '副歌二', t0: 92.77, t1: 103.94,
  bg(t) { return { ...FOGBG(), pulse: kickFx(t, .5) }; },
  draw(c, t) { hook(c, t, H2, true); },
  fx(t) { return { zoom: punch(t), flash: kickFx(t, .15) }; },
});

// ============================================================================
// 11 · 间奏二：两个世界之间硬切，越来越快
// ============================================================================
scene({ name: '间奏二', t0: 103.94, t1: 112.31,
  bg(t) { const n = M.between('kick', 103.9, t + 1e-3).length + (t > 109.5 ? M.between('snare', 109.5, t + 1e-3).length : 0); return n % 2 ? CITY(1) : { ...FOGBG(), pulse: kickFx(t, .5) }; },
  draw(c, t) {
    const n = M.between('kick', 103.9, t + 1e-3).length + (t > 109.5 ? M.between('snare', 109.5, t + 1e-3).length : 0);
    const k = M.last('kick', t), sp = lerp(-.3, 1.3, span(k.since, 0, .4));
    if (n % 2) { underpass(c, t); dgRain(c, t); pillarPlaque(c, t, { spec: sp }); if (n % 4 === 1) crack(c, 900 + hash(n, 1) * 500, 400 + hash(n, 2) * 200, 900, 1, n); }
    else { lampSpace(c, t, { pile: .5 }); stepPlaque(c, t, { spec: sp, snow: .55 }); snow(c, t, { n: 200 }); }
    fogLayer(c, t, .15);
  },
  fx(t) {
    const n = M.between('kick', 103.9, t + 1e-3).length, k = M.last('kick', t);
    return { grade: n % 2 ? 'none' : 'mono', gradeMix: n % 2 ? 0 : 1, zoom: punch(t, .05), flash: kickFx(t, .25), invert: n % 3 === 2 && k.since < .15 ? 1 : 0,
      glitch: t > 110.9 ? .4 : 0, gseed: frameNo(t), split: M.hit('snare', t, 16) * 2.5 };
  },
});

// ============================================================================
// 12 · 尾奏：灯下的空地积了雪，灯闪两下灭掉；片尾字幕在 overlay 里
// ============================================================================
const OFF = 118.2;
scene({ name: '尾奏', t0: 112.31, t1: MV_END,
  bg(t) { return t < OFF + .4 ? FOGBG(.9, lampFlicker2(t)) : BLACK; },
  draw(c, t) {
    if (t > OFF + .4) return;
    const on = lampFlicker2(t);
    lampSpace(c, t, { on, pile: lerp(.4, .9, span(t, 112.3, 117)) });
    snow(c, t, { n: 200, a: .4 + .6 * on });
    fogLayer(c, t, .2 * on);
  },
  fx(t) { return { zoom: 1 + span(t, 112.3, 118) * .06 }; },
});
function lampFlicker2(t) { const k = t - OFF; return k < 0 ? 1 : k < .08 ? .1 : k < .16 ? 1 : k < .22 ? 0 : k < .28 ? .7 : k < .4 ? .2 : 0; }

// ============================================================================
// 后期之后：字幕、片尾字幕
// ============================================================================
const NOSUB = [[34.17, 36.96], [89.98, 92.77]];                                      // 打字机那两段不用字幕
function overlay(c, t) {
  c.save();
  const { line } = M.lyric(t);
  if (line && !NOSUB.some(([a, b]) => t >= a && t < b)) subtitle(c, line, t, { y: H - 70 });
  // 片尾
  if (t > OFF + .5) {
    const rows = [[OFF + .6, 'ISOLATED', 90, GROT, 300, 40, 380], [OFF + 1.4, 'YuKIluv7 / dangao_w', 34, GROT, 700, 10, 480],
      [OFF + 2.4, '作词  YuKIluv7 / dangao_w', 24, MONO, 400, 4, 570], [OFF + 2.7, '作曲  YuKIluv7 / dangao_w', 24, MONO, 400, 4, 612], [OFF + 3.0, '编曲  YuKIluv7', 24, MONO, 400, 4, 654],
      [OFF + 3.8, 'De4thpain · 2026', 20, MONO, 400, 6, 740]];
    const fade = 1 - span(t, MV_END - .9, MV_END - .1);
    for (const [t0, s, size, fam, w, sp, y] of rows) {
      if (t < t0) continue;
      c.globalAlpha = clamp((t - t0) / .6) * fade;
      text(c, s, W / 2 + sp / 2, y, size, { fam, w, spacing: sp, fill: C.white });
    }
  }
  c.restore();
}
