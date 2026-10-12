'use strict';
// ============================================================================
// 雨夜的那条街（主歌一）：镜头沿着同一条街慢慢推过去，每句停在一个地方。
// 世界坐标：x 从 0 往右，地面在 GY；三层视差（远楼 .35、楼 .7、街面 1、前景 1.7）。
// 画在一张离屏画布上，整体失焦（焦点在玻璃上的雨滴和字上），再贴到画面里。
// ============================================================================
const STR = mk(), stx = STR.getContext('2d');
const GY = H * .73;
// 每句停在哪里（街面那一层的世界 x）
const LANDMARK = { stop: 700, bench: 1150, vend: 1850, blank: 2600, store: 3450, signal: 4250, flat: 5050, arcade: 5900 };
function glowAt(c, x, y, R, col, a) {
  if (a <= .002) return;
  const g = c.createRadialGradient(x, y, 0, x, y, R); g.addColorStop(0, `rgba(${col},${a})`); g.addColorStop(.35, `rgba(${col},${a * .35})`); g.addColorStop(1, `rgba(${col},0)`);
  c.fillStyle = g; c.fillRect(x - R, y - R, 2 * R, 2 * R);
}
// 湿地面上的倒影：光源正下方一道竖着拉长的光
function reflect(c, x, w, len, col, a) {
  if (a <= .002) return;
  const g = c.createLinearGradient(0, GY, 0, GY + len); g.addColorStop(0, `rgba(${col},${a})`); g.addColorStop(1, `rgba(${col},0)`);
  c.fillStyle = g; c.fillRect(x - w / 2, GY, w, len);
}
// 一排楼：宽窄高矮随机，窗户零星亮着（冷蓝 / 暖橙）
function buildings(c, cam, par, seed, y0, hMin, hMax, dark, winA) {
  const r = rand(seed); let x = -400;
  const off = cam * par;
  while (x < 7600) {
    const w = 260 + r() * 420, h = hMin + r() * (hMax - hMin), sx = x - off;
    if (sx < W + 50 && sx + w > -50) {
      c.fillStyle = dark; c.fillRect(sx, y0 - h, w + 2, h);
      const cols = Math.floor((w - 40) / 46), rows = Math.floor((h - 60) / 58);
      for (let i = 0; i < cols; i++) for (let j = 0; j < rows; j++) {
        const v = hash(seed * 977 + Math.floor(x) * 3 + i * 31 + j * 7, 3); if (v > .1) continue;
        c.fillStyle = v < .035 ? `rgba(255,176,96,${winA})` : `rgba(150,175,255,${winA * .7})`; c.fillRect(sx + 28 + i * 46, y0 - h + 38 + j * 58, 14, 20);
      }
    }
    x += w + 10 + r() * 40;
  }
}
// ---------------------------------------------------------------- 街上的东西（世界 x，屏幕 x = wx - cam）
function busStop(c, x, t) {
  c.fillStyle = '#0d1220'; c.fillRect(x - 230, GY - 330, 460, 22);                       // 顶棚
  c.fillStyle = '#1b2338'; c.fillRect(x - 226, GY - 312, 8, 312); c.fillRect(x + 218, GY - 312, 8, 312);
  c.fillStyle = 'rgba(160,185,255,.07)'; c.fillRect(x - 218, GY - 308, 436, 300);         // 玻璃
  c.strokeStyle = 'rgba(190,210,255,.25)'; c.lineWidth = 2; c.strokeRect(x - 218, GY - 308, 436, 300);
  c.fillStyle = 'rgba(225,235,255,.85)'; c.fillRect(x + 90, GY - 300, 120, 220);         // 灯箱广告
  glowAt(c, x + 150, GY - 190, 260, '200,215,255', .35);
  c.fillStyle = 'rgba(40,60,110,.35)'; c.fillRect(x + 104, GY - 280, 92, 120);
  c.fillStyle = '#111622'; c.fillRect(x - 170, GY - 96, 240, 16); c.fillRect(x - 160, GY - 80, 8, 80); c.fillRect(x + 52, GY - 80, 8, 80);   // 长椅
  c.fillStyle = '#e8edf8'; c.fillRect(x - 214, GY - 380, 70, 40); text(c, '23', x - 179, GY - 360, 26, { fam: ANTON, w: 400, fill: '#0d1220' });   // 站牌
  reflect(c, x + 150, 140, 260, '200,215,255', .22);
}
function lampPost(c, x, t, col = '255,176,96') {
  c.fillStyle = '#0a0d16'; c.fillRect(x - 5, GY - 520, 10, 520); c.fillRect(x - 5, GY - 520, 90, 9);
  c.fillStyle = `rgba(${col},.95)`; c.beginPath(); c.ellipse(x + 80, GY - 506, 22, 8, 0, 0, 7); c.fill();
  glowAt(c, x + 80, GY - 500, 240, col, .55);
  const g = c.createLinearGradient(0, GY - 500, 0, GY); g.addColorStop(0, `rgba(${col},.12)`); g.addColorStop(1, `rgba(${col},.03)`);
  c.fillStyle = g; c.beginPath(); c.moveTo(x + 64, GY - 500); c.lineTo(x + 96, GY - 500); c.lineTo(x + 240, GY); c.lineTo(x - 80, GY); c.closePath(); c.fill();   // 光锥里的雨
  reflect(c, x + 80, 70, 300, col, .3);
}
function vending(c, x, t) {
  c.fillStyle = '#d6dbe6'; c.fillRect(x - 100, GY - 360, 200, 360);
  c.fillStyle = '#eef3ff'; c.fillRect(x - 86, GY - 344, 172, 200);
  const r = rand(17), cols = ['#e0112b', '#2a4bd6', '#f2f1ee', '#7d93ff', '#c9ced6'];
  for (let j = 0; j < 4; j++) for (let i = 0; i < 6; i++) { c.fillStyle = cols[Math.floor(r() * cols.length)]; c.fillRect(x - 78 + i * 27, GY - 334 + j * 48, 18, 34); }
  c.fillStyle = '#1a1f2c'; c.fillRect(x - 86, GY - 120, 172, 40); c.fillStyle = '#7d93ff'; c.fillRect(x + 50, GY - 132, 26, 8);
  glowAt(c, x, GY - 220, 360, '220,232,255', .4);
  reflect(c, x, 220, 280, '220,232,255', .28);
}
function blankBoard(c, x, t) {                                                           // 一块空白的灯箱：什么都没写
  c.fillStyle = '#0c0f18'; c.fillRect(x - 330, GY - 610, 660, 380);
  const flick = .9 + .1 * Math.sin(t * 31) * (hash(frameNo(t) >> 2, 9) < .2 ? 1 : 0);
  c.fillStyle = `rgba(240,244,255,${.95 * flick})`; c.fillRect(x - 306, GY - 586, 612, 332);
  glowAt(c, x, GY - 420, 560, '230,238,255', .3 * flick);
  c.fillStyle = '#0c0f18'; c.fillRect(x - 6, GY - 230, 12, 230);
  reflect(c, x, 520, 320, '230,238,255', .2);
}
function store(c, x, t) {
  c.fillStyle = '#0b0e17'; c.fillRect(x - 420, GY - 360, 840, 360);
  c.fillStyle = 'rgba(230,240,255,.9)'; c.fillRect(x - 400, GY - 340, 800, 50);           // 招牌
  c.fillStyle = '#2a4bd6'; c.fillRect(x - 400, GY - 300, 800, 10);
  c.fillStyle = 'rgba(205,220,255,.55)'; c.fillRect(x - 390, GY - 280, 780, 260);         // 窗
  c.fillStyle = 'rgba(30,40,70,.6)'; for (let i = 0; i < 4; i++) c.fillRect(x - 360 + i * 200, GY - 250, 150, 200);   // 货架
  c.fillStyle = 'rgba(240,245,255,.5)'; for (let i = 0; i < 4; i++) for (let j = 0; j < 4; j++) c.fillRect(x - 350 + i * 200, GY - 240 + j * 48, 130, 6);
  glowAt(c, x, GY - 180, 700, '210,225,255', .32);
  reflect(c, x, 760, 340, '210,225,255', .24);
}
function signal(c, x, t) {
  c.fillStyle = '#0a0d16'; c.fillRect(x - 6, GY - 470, 12, 470);
  c.fillStyle = '#121620'; c.fillRect(x - 40, GY - 470, 80, 150);
  c.fillStyle = '#ff1a3a'; c.beginPath(); c.arc(x, GY - 430, 22, 0, 7); c.fill();
  c.fillStyle = '#25070c'; c.beginPath(); c.arc(x, GY - 362, 22, 0, 7); c.fill();
  glowAt(c, x, GY - 430, 320, '255,26,58', .55);
  reflect(c, x, 90, 330, '255,26,58', .35);
}
function flat(c, x, t) {                                                                 // 公寓：楼上一扇亮着的窗
  c.fillStyle = '#0a0d17'; c.fillRect(x - 260, GY - 900, 520, 900);
  for (let i = 0; i < 4; i++) for (let j = 0; j < 7; j++) { c.fillStyle = 'rgba(30,38,60,.8)'; c.fillRect(x - 220 + i * 120, GY - 860 + j * 110, 70, 76); }
  c.fillStyle = 'rgba(255,190,120,.95)'; c.fillRect(x - 100, GY - 640, 70, 76);
  glowAt(c, x - 65, GY - 600, 240, '255,176,96', .45);
  c.fillStyle = 'rgba(255,200,140,.8)'; c.fillRect(x - 50, GY - 170, 100, 170);           // 楼门里的灯
  glowAt(c, x, GY - 90, 220, '255,190,130', .3);
  reflect(c, x, 120, 240, '255,190,130', .24);
}
function arcade(c, x, t) {                                                               // 街尾的电玩店：红色霓虹招牌，一闪一闪
  c.fillStyle = '#0a0b12'; c.fillRect(x - 420, GY - 520, 840, 520);
  const on = hash(frameNo(t) >> 1, 31) < .85 ? 1 : .25;
  c.save(); c.shadowColor = `rgba(255,26,58,${on})`; c.shadowBlur = 30;
  pixel(c, 'GAME', x, GY - 430, 120, { fill: `rgba(255,${60 + 40 * on | 0},${80 + 30 * on | 0},${on})` });
  c.restore();
  glowAt(c, x, GY - 430, 520, '255,26,58', .38 * on);
  c.fillStyle = 'rgba(120,140,255,.35)'; c.fillRect(x - 360, GY - 320, 720, 300);         // 店里屏幕的光
  for (let i = 0; i < 5; i++) { c.fillStyle = `rgba(${hash(i, 5) < .5 ? '255,60,90' : '120,160,255'},.5)`; c.fillRect(x - 330 + i * 140, GY - 280, 100, 80); }
  if (mod(t, .8) < .45) pixel(c, 'INSERT COIN', x, GY - 150, 40, { fill: '#ffffff' });
  reflect(c, x, 600, 340, '255,26,58', .26 * on);
}
// 整条街：返回这一刻的长椅在屏幕上的位置（手机放在上面）
function rainStreet(c, t, cam) {
  const s = stx; s.setTransform(1, 0, 0, 1, 0, 0); s.globalAlpha = 1; s.filter = 'none';
  const sky = s.createLinearGradient(0, 0, 0, GY); sky.addColorStop(0, '#010309'); sky.addColorStop(1, '#0b1630'); s.fillStyle = sky; s.fillRect(0, 0, W, GY);
  buildings(s, cam, .35, 11, GY - 120, 300, 640, '#050812', .35);
  buildings(s, cam, .7, 12, GY, 420, 820, '#070a14', .6);
  const ground = s.createLinearGradient(0, GY, 0, H); ground.addColorStop(0, '#0c1224'); ground.addColorStop(1, '#03050b'); s.fillStyle = ground; s.fillRect(0, GY, W, H - GY);
  s.fillStyle = 'rgba(150,175,255,.08)'; s.fillRect(0, GY, W, 3);                         // 路沿
  const at = wx => wx - cam;
  for (let lx = 300; lx < 7000; lx += 820) { const x = at(lx); if (x > -400 && x < W + 400) lampPost(s, x, t, (lx / 820 | 0) % 3 === 1 ? '150,175,255' : '255,176,96'); }
  const items = [[LANDMARK.stop, busStop], [LANDMARK.vend, vending], [LANDMARK.blank, blankBoard], [LANDMARK.store, store], [LANDMARK.signal, signal], [LANDMARK.flat, flat], [LANDMARK.arcade, arcade]];
  for (const [wx, fn] of items) { const x = at(wx); if (x > -900 && x < W + 900) fn(s, x, t); }
  s.strokeStyle = 'rgba(200,215,255,.35)'; s.lineWidth = 1.5;                             // 地上的涟漪
  for (let i = 0; i < 26; i++) {
    const per = .6 + hash(i, 81) * .7, ph = hash(i, 82) * per, age = mod(t + ph, per) / per, cyc = Math.floor((t + ph) / per);
    const x = hash(i * 13 + cyc, 83) * W, y = GY + 20 + hash(i * 7 + cyc, 84) * (H - GY - 20), rr = 6 + age * 40;
    s.globalAlpha = .6 * (1 - age); s.beginPath(); s.ellipse(x, y, rr, rr * .3, 0, 0, 7); s.stroke();
  }
  s.globalAlpha = 1;
  for (let fx = 900; fx < 9000; fx += 1500) {                                              // 前景：离镜头很近的柱子，走得比街快
    const x = fx - cam * 1.7; if (x < -200 || x > W + 200) continue;
    s.fillStyle = '#020306'; s.fillRect(x - 40, 0, 80, H);
  }
  c.save(); c.filter = 'blur(4px)'; c.drawImage(STR, 0, 0); c.restore();
  return { x: at(LANDMARK.stop) - 70, y: GY - 112 };
}
// 镜头：每句开始时推到这句的地方（停在画面偏右，左下角留给证词，右上角留给竖排字）
const STREET_STOPS = ['stop', 'bench', 'vend', 'blank', 'store', 'signal', 'flat', 'arcade'];
function streetCam(t) {
  const L0 = D.lyrics[0].t, xs = STREET_STOPS.map(k => LANDMARK[k] - W * .56);
  let i = -1; for (let j = 0; j < STREET_STOPS.length; j++) if (t >= D.lyrics[j].t - .2) i = j;
  if (i < 0) return xs[0] - 260 + (t - L0) * 30;
  const from = i ? xs[i - 1] + (D.lyrics[i].t - D.lyrics[i - 1].t) * 22 : xs[0] - 260, k = ease((t - D.lyrics[i].t + .2) / .8);
  return lerp(from, xs[i], k) + Math.max(0, t - D.lyrics[i].t - .6) * 22;            // 推到之后还在很慢地往前走
}

// ============================================================================
// 大雨：隔着一扇窗看出去。远处的楼只在闪电时被照亮，街上的灯和车灯都失焦成光斑，
// 窗玻璃上一道道雨水往下流（玻璃是清楚的，外面是虚的）。
// ============================================================================
const STORM_LAMPS = Array.from({ length: 14 }, (_, i) => ({ x: hash(i, 51) * W, y: H * (.42 + hash(i, 52) * .3), R: 50 + hash(i, 53) * 110,
  col: ['255,176,96', '150,175,255', '255,176,96', '200,215,255'][i % 4], a: .35 + hash(i, 54) * .35 }));
function stormView(c, t, L) {
  const s = stx; s.setTransform(1, 0, 0, 1, 0, 0); s.globalAlpha = 1;
  const sky = s.createLinearGradient(0, 0, 0, H * .7);
  sky.addColorStop(0, `rgb(${lerp(2, 120, L) | 0},${lerp(4, 140, L) | 0},${lerp(12, 200, L) | 0})`); sky.addColorStop(1, `rgb(${lerp(10, 60, L) | 0},${lerp(18, 80, L) | 0},${lerp(42, 140, L) | 0})`);
  s.fillStyle = sky; s.fillRect(0, 0, W, H);
  buildings(s, 2400 + t * 6, .35, 21, H * .62, 260, 620, `rgb(${lerp(3, 14, L) | 0},${lerp(5, 18, L) | 0},${lerp(12, 36, L) | 0})`, .3 * (1 - L));   // 闪电时楼变成剪影
  buildings(s, 2400 + t * 6, .7, 22, H * .7, 160, 380, '#04060c', .4 * (1 - L * .6));
  const g = s.createLinearGradient(0, H * .7, 0, H); g.addColorStop(0, '#0a1124'); g.addColorStop(1, '#020309'); s.fillStyle = g; s.fillRect(0, H * .7, W, H * .3);
  for (const l of STORM_LAMPS) { glowAt(s, l.x, l.y, l.R, l.col, l.a); const rg = s.createLinearGradient(0, H * .7, 0, H); rg.addColorStop(0, `rgba(${l.col},${l.a * .35})`); rg.addColorStop(1, `rgba(${l.col},0)`); s.fillStyle = rg; s.fillRect(l.x - l.R * .3, H * .7, l.R * .6, H * .3); }
  const sig = mod(t, 6) < 3 ? '255,26,58' : '80,255,160';                              // 红绿灯
  glowAt(s, W * .22, H * .5, 90, sig, .7); glowAt(s, W * .22, H * .5, 260, sig, .18);
  for (let i = 0; i < 3; i++) {                                                         // 开过去的车：一对白色车头灯，另一个方向一对红色尾灯
    const per = 2.6 + i * .9, k = mod(t + i * 1.3, per) / per, dir = i % 2 ? -1 : 1, x = dir > 0 ? lerp(-300, W + 300, k) : lerp(W + 300, -300, k), y = H * (.66 + i * .04);
    const col = dir > 0 ? '235,240,255' : '255,30,50';
    glowAt(s, x, y, 70, col, .8); glowAt(s, x + 90, y, 70, col, .8); glowAt(s, x + 45, y, 300, col, .14);
    const rg = s.createLinearGradient(0, y, 0, H); rg.addColorStop(0, `rgba(${col},.25)`); rg.addColorStop(1, `rgba(${col},0)`); s.fillStyle = rg; s.fillRect(x - 10, y, 110, H - y);
  }
  c.save(); c.filter = 'blur(9px)'; c.drawImage(STR, 0, 0); c.restore();
}
// 窗玻璃上往下流的雨水：每道有一个水头往下走，后面拖着一条湿的痕迹
function rivulets(c, t, n = 26, a = 1) {
  c.save(); c.lineCap = 'round';
  for (let i = 0; i < n; i++) {
    const x0 = hash(i, 61) * W, sp = 120 + hash(i, 62) * 260, per = (H + 300) / sp, age = mod(t + hash(i, 63) * per, per), head = age * sp - 150;
    const path = y => x0 + Math.sin(y * .012 + i) * 14 + Math.sin(y * .041 + i * 3) * 5;
    const top = Math.max(-20, head - 520);
    const g = c.createLinearGradient(0, top, 0, head); g.addColorStop(0, 'rgba(200,215,255,0)'); g.addColorStop(1, `rgba(200,215,255,${.28 * a})`);
    c.strokeStyle = g; c.lineWidth = 3 + hash(i, 64) * 3; c.beginPath();
    for (let y = top; y <= head; y += 12) { const x = path(y); y === top ? c.moveTo(x, y) : c.lineTo(x, y); } c.stroke();
    const hx = path(head), hr = 5 + hash(i, 65) * 5, hg = c.createRadialGradient(hx - hr * .3, head - hr * .3, 0, hx, head, hr * 1.4);
    hg.addColorStop(0, `rgba(240,246,255,${.8 * a})`); hg.addColorStop(.6, `rgba(140,165,235,${.25 * a})`); hg.addColorStop(1, 'rgba(20,30,60,0)');
    c.fillStyle = hg; c.beginPath(); c.arc(hx, head, hr * 1.4, 0, 7); c.fill();
  }
  c.restore();
}
