'use strict';
// ============================================================================
// 《谎话》封面（正方形）。
//   第一轮：a 雾上的字（不要）/ b 铬金属 / c 对质
//   第二轮（直接用 MV 里的版式，平面、硬边，不加发光）：
//   d 片名卡：MV 片头的片名卡 + 录像带角标     e 红白横条：副歌二的红白两条大字
//   f 三栏：副歌一的三栏竖排                   g 我：「都是我」那段，红网点 + 测谎仪格子上的大「我」
// 尺寸以 1500 为基准（S = 边长 / 1500），正式版 ?s=3000。
// ============================================================================
const Q = new URLSearchParams(location.search), V = Q.get('v') || 'a';
const S = W / 1500;
const LAY = mk(), lx = LAY.getContext('2d');

// ---------------------------------------------------------------- 夜色 + 失焦的光斑（玻璃后面）
function night(c, o = {}) {
  const g = c.createLinearGradient(0, 0, 0, H);
  g.addColorStop(0, '#01030a'); g.addColorStop(.55, '#050d26'); g.addColorStop(1, '#0c1738');
  c.fillStyle = g; c.fillRect(0, 0, W, H);
  const r = rand(o.seed ?? 5), k = o.sharp ? .42 : 1, gain = o.sharp ? 1.7 : 1;
  c.save(); c.globalCompositeOperation = 'lighter';
  const COLS = [['125,147,255', 34], ['207,224,255', 22], ['255,170,95', 6], ['255,214,160', 5], ['224,17,43', 3]];
  const pick = () => { let x = r() * 70; for (const [col, w] of COLS) { if ((x -= w) < 0) return col; } return COLS[0][0]; };
  for (let i = 0; i < 70; i++) {
    const x = r() * W, y = (.08 + r() * .86) * H, R = (40 + r() * r() * 190) * S * k, col = pick(), a = (.08 + r() * .3) * gain;
    const rg = c.createRadialGradient(x, y, 0, x, y, R);
    rg.addColorStop(0, `rgba(${col},${a})`); rg.addColorStop(o.sharp ? .82 : .6, `rgba(${col},${a * .7})`); rg.addColorStop(1, `rgba(${col},0)`);
    c.fillStyle = rg; c.beginPath(); c.arc(x, y, R, 0, 7); c.fill();
  }
  // 两盏暖色路灯（和 MV 回忆段里那两盏一样）
  [[.27, .3], [.74, .27]].forEach(([px, py]) => {
    const x = px * W, y = py * H, R = (o.sharp ? 120 : 210) * S;
    const rg = c.createRadialGradient(x, y, 0, x, y, R);
    rg.addColorStop(0, `rgba(255,236,205,${o.sharp ? .95 : .55})`); rg.addColorStop(.25, `rgba(255,170,90,${o.sharp ? .5 : .32})`); rg.addColorStop(1, 'rgba(255,140,60,0)');
    c.fillStyle = rg; c.beginPath(); c.arc(x, y, R, 0, 7); c.fill();
  });
  if (o.sharp) {                                         // 擦开的地方看得清：远处很多小灯点
    const p = rand(77);
    for (let i = 0; i < 260; i++) { const x = p() * W, y = (.18 + p() * .7) * H, R = (1.5 + p() * 3.5) * S, col = p() < .7 ? '207,224,255' : p() < .5 ? '255,200,140' : '255,90,90';
      const rg = c.createRadialGradient(x, y, 0, x, y, R * 3); rg.addColorStop(0, `rgba(${col},.9)`); rg.addColorStop(1, `rgba(${col},0)`); c.fillStyle = rg; c.fillRect(x - R * 3, y - R * 3, R * 6, R * 6); }
  }
  c.restore();
}

// 玻璃上的一颗水珠（下半部透光，左上一点高光）
function drop(c, x, y, R, a = 1) {
  c.save(); c.globalAlpha = a; c.translate(x, y); c.scale(1, 1.12);
  const g = c.createRadialGradient(0, R * .35, 0, 0, R * .1, R);
  g.addColorStop(0, 'rgba(215,228,255,.6)'); g.addColorStop(.6, 'rgba(120,150,230,.2)'); g.addColorStop(.93, 'rgba(8,14,40,.4)'); g.addColorStop(1, 'rgba(8,14,40,0)');
  c.fillStyle = g; c.beginPath(); c.arc(0, 0, R, 0, 7); c.fill();
  c.fillStyle = 'rgba(255,255,255,.85)'; c.beginPath(); c.arc(-R * .3, -R * .38, R * .15, 0, 7); c.fill();
  c.restore();
}

// ---------------------------------------------------------------- a · 雾上的字
function coverA(c) {
  const BGc = mk(), bg = BGc.getContext('2d'); night(bg, { seed: 5 });
  const SHc = mk(), sh = SHc.getContext('2d'); night(sh, { seed: 5, sharp: true });

  // 雾：一层蓝灰，被后面的光照亮（把背景糊开叠上去），厚薄不匀
  const FOG = mk(), f = FOG.getContext('2d');
  f.fillStyle = 'rgba(92,106,140,.93)'; f.fillRect(0, 0, W, H);
  f.save(); f.globalAlpha = .85; f.filter = `blur(${70 * S}px)`; f.drawImage(BGc, 0, 0); f.restore();
  const r = rand(11);
  for (let i = 0; i < 26; i++) {
    const x = r() * W, y = r() * H, R = (160 + r() * 380) * S, light = r() < .5;
    const g = f.createRadialGradient(x, y, 0, x, y, R);
    g.addColorStop(0, light ? 'rgba(170,190,230,.07)' : 'rgba(6,10,22,.12)'); g.addColorStop(1, 'rgba(0,0,0,0)');
    f.fillStyle = g; f.fillRect(x - R, y - R, 2 * R, 2 * R);
  }

  // 手指擦掉的地方：「谎话」两个字 + 往下淌的水
  const MASK = mk(), m = MASK.getContext('2d');
  const size = 560 * S;
  m.save(); m.translate(W * .5, H * .47); m.rotate(-.06);
  m.font = `${size}px "Long Cang"`; m.textAlign = 'center'; m.textBaseline = 'middle';
  m.lineJoin = 'round'; m.lineCap = 'round'; m.lineWidth = size * .018; m.strokeStyle = '#fff'; m.fillStyle = '#fff';
  m.strokeText('谎话', 0, 0); m.fillText('谎话', 0, 0);
  m.restore();
  // 水往下淌：从字的下沿挂下来几道，末端一颗水珠
  const DRIPS = [[.33, .6, 300], [.41, .62, 170], [.55, .6, 380], [.66, .61, 230], [.7, .58, 120]];
  m.fillStyle = '#fff'; m.strokeStyle = '#fff'; m.lineCap = 'round';
  DRIPS.forEach(([px, py, L]) => {
    const x = px * W, y = py * H, len = L * S;
    m.lineWidth = 6 * S; m.beginPath(); m.moveTo(x, y); m.bezierCurveTo(x + 4 * S, y + len * .4, x - 3 * S, y + len * .7, x + 2 * S, y + len); m.stroke();
    m.beginPath(); m.ellipse(x + 2 * S, y + len + 6 * S, 9 * S, 12 * S, 0, 0, 7); m.fill();
  });
  // 玻璃上自己滑下来的几颗水珠擦出来的痕
  const tr = rand(23);
  for (let i = 0; i < 4; i++) {
    const x = tr() * W, y0 = tr() * H * .7, len = (120 + tr() * 420) * S, w = (5 + tr() * 7) * S;
    m.lineWidth = w; m.globalAlpha = .75; m.beginPath(); m.moveTo(x, y0); m.lineTo(x + (tr() - .5) * 20 * S, y0 + len); m.stroke();
  }
  m.globalAlpha = 1;
  const SOFT = mk(), so = SOFT.getContext('2d'); so.filter = `blur(${4 * S}px)`; so.drawImage(MASK, 0, 0);

  // 合成：糊的背景 → 擦开处换成清楚的背景 → 盖上被擦掉一块的雾
  c.drawImage(BGc, 0, 0);
  const T1 = mk(), t1 = T1.getContext('2d'); t1.drawImage(SHc, 0, 0); t1.globalCompositeOperation = 'destination-in'; t1.drawImage(SOFT, 0, 0);
  c.drawImage(T1, 0, 0);
  const T2 = mk(), t2 = T2.getContext('2d'); t2.drawImage(FOG, 0, 0); t2.globalCompositeOperation = 'destination-out'; t2.drawImage(SOFT, 0, 0);
  c.drawImage(T2, 0, 0);
  // 擦开的边上积了一圈水：亮一点的边
  const EDGE = mk(), e = EDGE.getContext('2d'); e.filter = `blur(${10 * S}px)`; e.drawImage(MASK, 0, 0); e.filter = 'none';
  e.globalCompositeOperation = 'destination-out'; e.drawImage(MASK, 0, 0);
  c.save(); c.globalAlpha = .28; c.globalCompositeOperation = 'lighter'; c.drawImage(EDGE, 0, 0); c.restore();

  // 雾上的小水珠（字里面没有，被擦掉了）
  const BEADS = mk(), bd = BEADS.getContext('2d'), br = rand(31);
  for (let i = 0; i < 520; i++) { const R = (2 + br() * br() * 9) * S; drop(bd, br() * W, br() * H, R, .55 + br() * .45); }
  for (let i = 0; i < 26; i++) { const R = (12 + br() * 16) * S; drop(bd, br() * W, br() * H, R, .9); }
  DRIPS.forEach(([px, py, L]) => drop(bd, px * W + 2 * S, py * H + L * S + 6 * S, 13 * S, 1));
  bd.globalCompositeOperation = 'destination-out'; bd.drawImage(SOFT, 0, 0);
  DRIPS.forEach(([px, py, L]) => { bd.globalCompositeOperation = 'source-over'; drop(bd, px * W + 2 * S, py * H + L * S + 6 * S, 13 * S, 1); });
  c.drawImage(BEADS, 0, 0);

  // 署名
  c.save(); c.font = `${74 * S}px "${GOTH}"`; c.textAlign = 'right'; c.textBaseline = 'alphabetic'; c.fillStyle = 'rgba(236,240,250,.88)';
  c.fillText('dangao_w', W - 90 * S, H - 96 * S); c.restore();

  return { grade: 'cold', gradeMix: .35, bloom: .55, bloomThr: .45, ca: 1.1, grain: .22, vig: 1.05 };
}

// ---------------------------------------------------------------- b · 铬金属
function coverB(c) {
  c.fillStyle = '#040406'; c.fillRect(0, 0, W, H);
  const haze = c.createRadialGradient(W * .5, H * 1.05, 0, W * .5, H * 1.05, H * .8);
  haze.addColorStop(0, 'rgba(40,60,140,.45)'); haze.addColorStop(1, 'rgba(40,60,140,0)');
  c.fillStyle = haze; c.fillRect(0, 0, W, H);
  rain(c, 3.7, { n: 160, spd: 1, len: 120 * S, alpha: .1, wind: .14, seed: 7, lw: 1.2 * S, col: 'rgb(170,190,255)' });

  const size = Math.round(470 * S), cy = H * .42;
  const b = chromeBase('谎话', size, SANS, 900);
  c.drawImage(b.c, W / 2 - b.w / 2, cy - b.h / 2);
  // 倒影：地面是湿的
  const RF = mk(b.w, b.h), rf = RF.getContext('2d');
  rf.translate(0, b.h); rf.scale(1, -1); rf.drawImage(b.c, 0, 0); rf.setTransform(1, 0, 0, 1, 0, 0);
  rf.globalCompositeOperation = 'destination-in';
  const fade = rf.createLinearGradient(0, 0, 0, b.h); fade.addColorStop(0, 'rgba(0,0,0,0)'); fade.addColorStop(.72, 'rgba(0,0,0,0)'); fade.addColorStop(.88, 'rgba(0,0,0,.5)'); fade.addColorStop(1, 'rgba(0,0,0,.8)');
  rf.fillStyle = fade; rf.fillRect(0, 0, b.w, b.h);
  c.save(); c.globalAlpha = .22; c.filter = `blur(${6 * S}px)`; c.drawImage(RF, W / 2 - b.w / 2, cy + b.h * .5 - b.h * .62); c.restore();

  // 测谎仪：平稳的线在「谎」字下面猛跳一下
  const y0 = H * .75, xs = W * .08, xe = W * .92, spike = W * .37, r = rand(9);
  c.save();
  c.strokeStyle = 'rgba(224,17,43,.18)'; c.lineWidth = 1 * S;
  for (let gx = xs; gx <= xe + 1; gx += 50 * S) { c.beginPath(); c.moveTo(gx, y0 - 70 * S); c.lineTo(gx, y0 + 70 * S); c.stroke(); }
  c.beginPath(); c.moveTo(xs, y0); c.lineTo(xe, y0); c.stroke();
  c.strokeStyle = '#ff2a3c'; c.lineWidth = 4 * S; c.lineJoin = 'round'; c.shadowColor = 'rgba(255,40,60,.9)'; c.shadowBlur = 24 * S;
  c.beginPath();
  for (let x = xs; x <= xe; x += 4 * S) {
    const d = (x - spike) / (W * .012), env = Math.exp(-d * d / 6);
    const y = y0 + Math.sin(x * .02 / S) * 3 * S + (r() - .5) * 4 * S - env * Math.sin(d * 1.9) * 210 * S;
    x === xs ? c.moveTo(x, y) : c.lineTo(x, y);
  }
  c.stroke(); c.restore();

  c.save(); c.textAlign = 'center'; c.textBaseline = 'middle';
  c.font = `${92 * S}px "${GOTH}"`; c.fillStyle = '#c9ced6'; c.fillText('dangao_w', W / 2, H * .86);
  c.font = `${22 * S}px "${MONO}"`; c.fillStyle = 'rgba(201,206,214,.55)'; c.fillText('2026', W / 2, H * .91);
  c.restore();
  return { grade: 'cold', gradeMix: .2, bloom: .75, bloomThr: .55, ca: 1.3, grain: .2, vig: 1.15 };
}

// ---------------------------------------------------------------- c · 对质
function coverC(c) {
  const BGc = mk(), bg = BGc.getContext('2d'); night(bg, { seed: 17 });
  rain(bg, 2.3, { n: 220, spd: 1, len: 140 * S, alpha: .22, wind: .16, seed: 3, lw: 1.6 * S });
  c.save(); c.filter = `blur(${10 * S}px)`; c.drawImage(BGc, 0, 0); c.restore();
  c.fillStyle = 'rgba(2,4,12,.42)'; c.fillRect(0, 0, W, H);

  const fs = 64 * S, pad = 34 * S, rad = 40 * S, x0 = 150 * S, xr = W - 150 * S;
  c.save(); c.textBaseline = 'middle'; c.font = fnt(fs, 900);
  c.textAlign = 'center'; c.fillStyle = 'rgba(236,240,250,.55)'; c.font = `${26 * S}px "${MONO}"`; c.fillText('23:47', W / 2, 250 * S);
  c.font = fnt(30 * S, 400); c.fillText('你', W / 2, 205 * S);
  const MSG = [['你总说你没有做', 'L', 360], ['你总说你拯救我', 'L', 520], ['全都是bullshxt', 'R', 700], ['你从来没救赎过', 'R', 860]];
  MSG.forEach(([txt, side, y], i) => {
    c.font = fnt(fs, 900); const tw = c.measureText(txt).width, bw = tw + pad * 2, bh = fs + pad * 1.6, bx = side === 'L' ? x0 : xr - bw, by = y * S - bh / 2;
    c.save(); c.globalAlpha = i === 3 ? .45 : 1;
    c.fillStyle = side === 'L' ? 'rgba(232,236,245,.92)' : '#e0112b';
    c.beginPath(); c.roundRect(bx, by, bw, bh, rad); c.fill();
    c.fillStyle = side === 'L' ? '#0a0b10' : '#fff'; c.textAlign = 'left'; c.fillText(txt, bx + pad, y * S);
    if (txt.includes('shxt')) {                            // 脏字被涂掉
      const cut = c.measureText(txt.replace('shxt', '')).width, cw = c.measureText('shxt').width;
      c.fillStyle = '#0a0b10'; c.fillRect(bx + pad + cut - 4 * S, y * S - fs * .48, cw + 8 * S, fs * .96);
    }
    c.restore();
  });
  c.restore();

  c.save(); c.textAlign = 'left'; c.textBaseline = 'alphabetic';
  c.font = fnt(300 * S, 900); c.fillStyle = '#f2f1ee'; c.fillText('谎话', 140 * S, H - 170 * S);
  c.font = `${70 * S}px "${GOTH}"`; c.fillStyle = 'rgba(236,240,250,.85)'; c.fillText('dangao_w', 150 * S, H - 90 * S);
  c.restore();
  return { grade: 'cold', gradeMix: .3, bloom: .45, bloomThr: .6, ca: 1, grain: .2, vig: 1.1 };
}

// ================================================================ 第二轮：MV 里的版式
// 录像带角标（MV 的 HUD）：左上 ● REC TAPE 01，右上 谎话 / dangao_w，左下 ▶ PLAY，右下时间码
function tapeHud(c, o = {}) {
  const fs = o.fs ?? 22 * S, m = 64 * S, top = o.top ?? 62 * S, bot = o.bot ?? H - 62 * S, col = o.col ?? C.white, colTop = o.colTop ?? col, colBot = o.colBot ?? col;
  c.save(); c.font = fnt(fs, 400, MONO); c.textBaseline = 'middle'; c.globalAlpha = o.a ?? .85; c.fillStyle = colTop;
  c.textAlign = 'left';
  c.save(); c.fillStyle = o.dot ?? C.red; c.globalAlpha = 1; c.beginPath(); c.arc(m + fs * .42, top, fs * .4, 0, 7); c.fill(); c.restore();
  const recX = m + fs * 1.35; c.fillText('REC', recX, top); c.fillText('TAPE 01', recX + c.measureText('REC').width + fs * 2.4, top);
  c.textAlign = 'right'; c.fillText('谎话 / dangao_w', W - m, top);
  c.fillStyle = colBot; c.textAlign = 'left'; c.fillText('▶ PLAY', m, bot);
  c.textAlign = 'right'; c.fillText(o.tc ?? 'TC 00:00:06:00', W - m, bot);
  if (o.url) { c.textAlign = 'center'; c.font = fnt(fs * 1.1, 400, MONO); c.globalAlpha = Math.min(1, (o.a ?? .85) + .07); c.fillText(o.url, W / 2, bot); }
  c.restore();
}
// 套色没对准：先画两层错开的彩色，再画正色（MV 里的歌词就是这样）
function misText(c, str, x, y, size, o = {}) {
  c.save(); c.font = fnt(size, o.w ?? 900, o.fam ?? SANS); c.textAlign = o.align ?? 'center'; c.textBaseline = o.base ?? 'middle';
  const d = (o.d ?? 6) * S;
  if (o.ghost !== false) {
    c.globalAlpha = o.ga ?? .75; c.fillStyle = o.g1 ?? 'rgba(255,40,70,1)'; c.fillText(str, x - d, y + d * .3);
    c.fillStyle = o.g2 ?? 'rgba(60,170,255,1)'; c.fillText(str, x + d, y - d * .3); c.globalAlpha = 1;
  }
  c.fillStyle = o.fill ?? C.white; c.fillText(str, x, y);
  c.restore();
}
function vMis(c, str, x, y, size, o = {}) { [...str].forEach((ch, k) => misText(c, ch, x, y + k * size * (o.lead ?? 1.02), size, { ...o, base: 'top' })); }
// 网点：一格一个圆点，半径由 f(x, y) 决定（0..1）
function halftone(c, cell, col, f) {
  c.save(); c.fillStyle = col;
  for (let y = cell / 2; y < H; y += cell) for (let x = cell / 2; x < W; x += cell) { const r = f(x / W, y / H) * cell * .5; if (r > .3) { c.beginPath(); c.arc(x, y, r, 0, 7); c.fill(); } }
  c.restore();
}
const FLAT = { grade: 'none', gradeMix: 0, bloom: .12, bloomThr: .8, ca: 1.1, grain: .26, vig: .9, scan: .07 };

// d · 片名卡：黑底，满屏空心大字「谎话」被画面切掉，中间细框里是正片名，录像带角标在上下黑边里
function coverD(c) {
  c.fillStyle = '#040406'; c.fillRect(0, 0, W, H);
  const lb = 150 * S;
  c.save(); c.font = fnt(1060 * S, 900); c.textAlign = 'center'; c.textBaseline = 'middle';
  c.lineWidth = 3 * S; c.strokeStyle = 'rgba(201,206,214,.16)'; c.strokeText('谎话', W * .47, H * .5); c.restore();
  const fw = 820 * S, fh = 420 * S, fx = (W - fw) / 2, fy = H * .5 - fh / 2;
  c.save(); c.fillStyle = '#040406'; c.fillRect(fx, fy, fw, fh); c.strokeStyle = 'rgba(242,241,238,.9)'; c.lineWidth = 3 * S; c.strokeRect(fx, fy, fw, fh); c.restore();
  misText(c, '谎话', W / 2, H * .5 - 10 * S, 250 * S, { d: 5 });
  c.save(); c.font = `${40 * S}px "${GOTH}"`; c.textAlign = 'right'; c.fillStyle = 'rgba(242,241,238,.85)'; c.fillText('dangao_w', fx + fw - 26 * S, fy + fh - 30 * S); c.restore();
  c.fillStyle = '#000'; c.fillRect(0, 0, W, lb); c.fillRect(0, H - lb, W, lb);
  tapeHud(c, { top: lb / 2, bot: H - lb / 2 });
  return { ...FLAT };
}

// e / h / i · 横条：上面一条大字「谎话」，下面一条歌词（副歌二的版式）。配色：e 红白（太血腥，不用）/ h 冷蓝 / i 黑白银
const BANDS = {
  e: { top: C.red, bot: C.white, title: '#0a0b10', tg: ['rgba(255,214,0,1)', 'rgba(255,255,255,1)', .35], lyric: C.red, lg: ['rgba(255,214,0,1)', 'rgba(255,120,150,1)', .45], hudTop: '#0a0b10', hudBot: '#0a0b10', dot: '#0a0b10', line: 'rgba(0,0,0,.85)', sign: '#0a0b10' },
  h: { top: '#0c1838', bot: '#dfe3ea', title: '#e9edf6', tg: ['rgba(90,124,255,1)', 'rgba(200,220,255,1)', .5], lyric: '#0c1838', lg: ['rgba(90,124,255,1)', 'rgba(160,190,255,1)', .4], hudTop: '#dfe3ea', hudBot: '#0c1838', dot: C.red, line: 'rgba(4,8,20,.9)', sign: '#0c1838' },
  i: { top: '#0a0b10', bot: '#c9ced6', title: '#e4e7ec', tg: ['rgba(255,255,255,1)', 'rgba(110,118,134,1)', .35], lyric: '#0a0b10', lg: ['rgba(255,255,255,1)', 'rgba(110,118,134,1)', .45], hudTop: '#c9ced6', hudBot: '#0a0b10', dot: C.red, line: 'rgba(0,0,0,.9)', sign: '#0a0b10' },
};
function bands(c, P) {
  const mid = H * .54;
  c.fillStyle = P.top; c.fillRect(0, 0, W, mid);
  c.fillStyle = P.bot; c.fillRect(0, mid, W, H - mid);
  c.save(); c.beginPath(); c.rect(0, 0, W, mid); c.clip();
  misText(c, '谎话', W * .5, mid - 300 * S, 700 * S, { fill: P.title, g1: P.tg[0], g2: P.tg[1], ga: P.tg[2], d: 9 });
  c.restore();
  c.save(); c.beginPath(); c.rect(0, mid, W, H - mid); c.clip();
  misText(c, '就别再想着', 90 * S, mid + 150 * S, 150 * S, { align: 'left', fill: P.lyric, g1: P.lg[0], g2: P.lg[1], ga: P.lg[2], d: 5 });
  misText(c, '再跟我说谎话', 90 * S, mid + 320 * S, 150 * S, { align: 'left', fill: P.lyric, g1: P.lg[0], g2: P.lg[1], ga: P.lg[2], d: 5 });
  c.font = `${82 * S}px "${GOTH}"`; c.textAlign = 'right'; c.textBaseline = 'alphabetic'; c.fillStyle = P.sign; c.fillText('dangao_w', W - 76 * S, H - 160 * S);
  c.restore();
  c.fillStyle = P.line; c.fillRect(0, mid - 3 * S, W, 6 * S);
  tapeHud(c, { colTop: P.hudTop, colBot: P.hudBot, dot: P.dot, a: .85, fs: 31 * S, top: 72 * S, bot: H - 72 * S, tc: 'TC 00:01:00:00', url: 'makestudio.cn' });
  return { ...FLAT, ca: 0, vig: .7 };   // 整张不做色差（边缘会出彩边），错位只留在字上
}
const coverE = c => bands(c, BANDS.e), coverH = c => bands(c, BANDS.h), coverI = c => bands(c, BANDS.i);

// j · 雨夜字幕：上下黑边，中间是 MV 里那一帧雨夜（原尺寸重画，不放大截图），字幕「就别再想着再跟我说谎话」
function coverJ(c) {
  const bar = 290 * S, y0 = bar, y1 = H - bar;
  c.fillStyle = '#000'; c.fillRect(0, 0, W, H);
  const IMG = mk(), g = IMG.getContext('2d');
  const grd = g.createLinearGradient(0, y0, 0, y1); grd.addColorStop(0, '#040a1e'); grd.addColorStop(.6, '#0a1638'); grd.addColorStop(1, '#122150');
  g.fillStyle = grd; g.fillRect(0, 0, W, H);
  g.globalCompositeOperation = 'lighter';
  const r = rand(14);
  for (let i = 0; i < 46; i++) {
    const x = r() * W, y = y0 + r() * (y1 - y0), R = (40 + r() * r() * 170) * S, col = r() < .8 ? '90,124,255' : '200,220,255', a = .05 + r() * .18;
    const rg = g.createRadialGradient(x, y, 0, x, y, R); rg.addColorStop(0, `rgba(${col},${a})`); rg.addColorStop(.65, `rgba(${col},${a * .6})`); rg.addColorStop(1, `rgba(${col},0)`);
    g.fillStyle = rg; g.beginPath(); g.arc(x, y, R, 0, 7); g.fill();
  }
  g.globalCompositeOperation = 'source-over';
  rain(g, 4.2, { n: 340, spd: 1, len: 150 * S, alpha: .32, wind: .2, seed: 9, lw: 1.8 * S, col: 'rgb(170,190,255)' });
  rain(g, 1.3, { n: 90, spd: 1, len: 260 * S, alpha: .2, wind: .2, seed: 21, lw: 3.2 * S, col: 'rgb(200,214,255)' });
  dust(g, 3, 260, 5, .35, '#cfe0ff');
  c.save(); c.beginPath(); c.rect(0, y0, W, y1 - y0); c.clip(); c.drawImage(IMG, 0, 0);
  // 字幕：前半句宋体发蓝光，「谎话」两个字白色粗体
  const sz = 74 * S, ym = (y0 + y1) / 2 + 10 * S, a1 = '就别再想着再跟我说', a2 = '谎话';
  c.font = fnt(sz, 400, SERIF); const w1 = c.measureText(a1).width; c.font = fnt(sz * 1.12, 900, SANS); const w2 = c.measureText(a2).width;
  const x0 = W / 2 - (w1 + w2 + 8 * S) / 2;
  c.shadowColor = 'rgba(125,147,255,.9)'; c.shadowBlur = 22 * S;
  misText(c, a1, x0, ym, sz, { align: 'left', w: 400, fam: SERIF, fill: '#cfe0ff', d: 2, ga: .5, g1: 'rgba(255,60,90,1)', g2: 'rgba(80,200,255,1)' });
  c.shadowColor = 'rgba(220,230,255,.9)'; c.shadowBlur = 28 * S;
  misText(c, a2, x0 + w1 + 8 * S, ym, sz * 1.12, { align: 'left', fill: '#ffffff', d: 3, ga: .55 });
  c.restore();
  // 下黑边：歌名 + 署名；上黑边：录像带角标
  c.save(); c.textBaseline = 'alphabetic';
  misText(c, '谎话', 76 * S, y1 + 150 * S, 150 * S, { align: 'left', base: 'alphabetic', fill: '#f2f1ee', d: 4, ga: .6 });
  c.font = `${80 * S}px "${GOTH}"`; c.textAlign = 'right'; c.fillStyle = 'rgba(242,241,238,.9)'; c.fillText('dangao_w', W - 76 * S, y1 + 140 * S);
  c.restore();
  tapeHud(c, { col: '#e4e7ec', a: .8, fs: 31 * S, top: bar / 2, bot: H - 72 * S, tc: 'TC 00:01:18:42', url: 'makestudio.cn' });
  return { grade: 'cold', gradeMix: .25, bloom: .35, bloomThr: .6, ca: 0, grain: .24, vig: .8, scan: .05 };
}

// f · 三栏：黑 / 银 / 暗红三栏，竖排三句（副歌一）
function coverF(c) {
  const cw = W / 3, tones = [[C.black, C.white], [C.silver, C.black], ['#9a0718', C.black]], lines = ['不要这样就', '不要躲着我', '就别抱紧我'];
  const size = 200 * S, top = [190, 300, 245];
  lines.forEach((l, i) => {
    c.fillStyle = tones[i][0]; c.fillRect(i * cw, 0, cw + 1, H);
    vMis(c, l, i * cw + cw / 2, top[i] * S, size, { fill: tones[i][1], d: 4, ga: i === 0 ? .7 : .3 });
  });
  c.fillStyle = 'rgba(0,0,0,.9)'; c.fillRect(cw - 3 * S, 0, 6 * S, H); c.fillRect(2 * cw - 3 * S, 0, 6 * S, H);
  c.save(); c.font = fnt(46 * S, 900); c.textAlign = 'left'; c.textBaseline = 'alphabetic'; c.fillStyle = C.white; c.fillText('谎话', 64 * S, H - 190 * S);
  c.font = `${34 * S}px "${GOTH}"`; c.fillStyle = 'rgba(242,241,238,.8)'; c.fillText('dangao_w', 66 * S, H - 144 * S); c.restore();
  tapeHud(c, { a: .75, tc: 'TC 00:00:38:19' });
  return { ...FLAT };
}

// g · 我：暗红底、红网点、测谎仪的格子和曲线，一个巨大的「我」，左上三句歌词
function coverG(c) {
  c.fillStyle = '#12020a'; c.fillRect(0, 0, W, H);
  halftone(c, 16 * S, 'rgba(224,17,43,.55)', (x, y) => clamp(.15 + .75 * Math.exp(-((x - .6) ** 2 + (y - .62) ** 2) * 3.2)));
  c.save(); c.strokeStyle = 'rgba(224,17,43,.35)'; c.lineWidth = 1.5 * S;
  for (let x = 0; x <= W; x += 75 * S) { c.beginPath(); c.moveTo(x, 0); c.lineTo(x, H); c.stroke(); }
  for (let y = 0; y <= H; y += 75 * S) { c.beginPath(); c.moveTo(0, y); c.lineTo(W, y); c.stroke(); }
  const r = rand(4); c.strokeStyle = 'rgba(255,60,80,.8)'; c.lineWidth = 3 * S; c.beginPath();
  for (let x = 0, y = H * .74; x <= W; x += 10 * S) { y += (r() - .5) * 26 * S + (H * .74 - y) * .08; x ? c.lineTo(x, y) : c.moveTo(x, y); }
  c.stroke(); c.restore();
  misText(c, '我', W * .6, H * .58, 1100 * S, { fill: C.red, g1: 'rgba(0,0,0,1)', g2: 'rgba(255,120,140,1)', ga: .5, d: 10 });
  ['逃避的人是我', '不说话的也是我', '好像什么都是我'].forEach((l, i) => {
    misText(c, l, 90 * S, 230 * S + i * 74 * S, 52 * S, { align: 'left', fill: i === 2 ? C.white : 'rgba(242,241,238,.55)', d: 3, ga: .5 });
  });
  c.save(); c.font = fnt(46 * S, 900); c.textAlign = 'left'; c.textBaseline = 'alphabetic'; c.fillStyle = C.white; c.fillText('谎话', 90 * S, H - 190 * S);
  c.font = `${34 * S}px "${GOTH}"`; c.fillStyle = 'rgba(242,241,238,.8)'; c.fillText('dangao_w', 92 * S, H - 144 * S); c.restore();
  tapeHud(c, { a: .75, tc: 'TC 00:00:52:00' });
  return { ...FLAT, ca: 1.3 };
}

// ---------------------------------------------------------------- 渲染 + 导出
const OUT = document.getElementById('cv'); OUT.width = W; OUT.height = H; const ox = OUT.getContext('2d');
function render() {
  lx.setTransform(1, 0, 0, 1, 0, 0); lx.globalAlpha = 1; lx.globalCompositeOperation = 'source-over'; lx.filter = 'none';
  lx.clearRect(0, 0, W, H);
  const base = ({ a: coverA, b: coverB, c: coverC, d: coverD, e: coverE, f: coverF, g: coverG, h: coverH, i: coverI, j: coverJ }[V] || coverH)(lx);
  const fx = { zoom: 1, rot: 0, barrel: 0, shift: [0, 0], sq: [1, 1], split: 0, glitch: 0, gseed: 1, wave: 0, tear: 0, invert: 0, xerox: 0,
    flash: 0, flashCol: '#ffffff', dark: 0, scan: 0, lb: 0, ...base };
  GLX.frame(LAY, 0, { mode: 'solid', a: '#000000' }, fx, 1, true);
  ox.setTransform(1, 0, 0, 1, 0, 0); ox.drawImage(GLX.present(), 0, 0);
}
const FONTS = [`900 20px "${SANS}"`, `400 20px "${SANS}"`, `400 20px "${SERIF}"`, `20px "${GOTH}"`, `400 20px "${MONO}"`, `20px "Long Cang"`];
const ready = Promise.all(FONTS.map(f => document.fonts.load(f, '谎话A'))).then(() => document.fonts.ready).then(() => { render(); return true; });
window.__ready = () => ready;
window.__png = () => OUT.toDataURL('image/png');
