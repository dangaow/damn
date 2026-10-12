'use strict';
// ============================================================================
// 意识流 · 证物照：每个底鼓 / 踩镲是一次相机闪光，拍下这段关系留下的一样东西。
// 东西都是实物（有材质、高光、投影），不是线稿；放在同一个雨夜里的几种表面上，旁边一块证物编号牌。
// 每个物件函数签名 (c, t, k, o)：以 (0,0) 为中心、高约 450px；k 是这张照片拍下后的秒数。
// ============================================================================

// ---------------------------------------------------------------- 表面
const SURF = (() => {                                                            // 预先算好的纹理，每帧只贴图
  const asphalt = mk(W, H), a = asphalt.getContext('2d'), r = rand(901);
  a.fillStyle = '#07090d'; a.fillRect(0, 0, W, H);
  for (let i = 0; i < 26000; i++) { const v = 18 + r() * 40 | 0; a.fillStyle = `rgba(${v},${v + 3},${v + 8},${.25 + r() * .5})`; a.fillRect(r() * W, r() * H, 1 + r() * 2.5, 1 + r() * 2.5); }
  for (let i = 0; i < 9; i++) { const x = r() * W, y = r() * H, R = 120 + r() * 380, g = a.createRadialGradient(x, y, 0, x, y, R);   // 积水的暗斑
    g.addColorStop(0, 'rgba(0,0,0,.55)'); g.addColorStop(1, 'rgba(0,0,0,0)'); a.fillStyle = g; a.fillRect(x - R, y - R, 2 * R, 2 * R); }
  const sheet = mk(W, H), s = sheet.getContext('2d'), r2 = rand(902);
  s.fillStyle = '#4c5159'; s.fillRect(0, 0, W, H);
  for (let i = 0; i < 14; i++) {                                                // 床单的褶皱：长长的明暗带
    const y = r2() * H * 1.4 - H * .2, ang = (r2() - .5) * .5, wdt = 40 + r2() * 160, light = r2() < .5;
    s.save(); s.translate(W / 2, y); s.rotate(ang); s.filter = `blur(${18 + r2() * 30 | 0}px)`;
    s.fillStyle = light ? 'rgba(230,234,240,.32)' : 'rgba(10,12,18,.38)'; s.fillRect(-W, -wdt / 2, W * 2, wdt); s.restore();
  }
  const paper = mk(W, H), p = paper.getContext('2d');
  p.fillStyle = '#8f8c86'; p.fillRect(0, 0, W, H);
  p.strokeStyle = 'rgba(40,40,50,.09)'; p.lineWidth = 1;
  for (let x = 0; x < W; x += 40) { p.beginPath(); p.moveTo(x, 0); p.lineTo(x, H); p.stroke(); }
  for (let y = 0; y < H; y += 40) { p.beginPath(); p.moveTo(0, y); p.lineTo(W, y); p.stroke(); }
  return { asphalt, sheet, paper };
})();
const SURFACES = ['asphalt', 'sheet', 'red', 'paper'];
// 闪光灯：画面中间偏上一团硬光，四周掉进黑里
function flashLight(c, cx, cy, a = .5, R = 900, col = '255,255,255') {
  const g = c.createRadialGradient(cx, cy, 0, cx, cy, R);
  g.addColorStop(0, `rgba(${col},${a})`); g.addColorStop(.45, `rgba(${col},${a * .35})`); g.addColorStop(1, `rgba(${col},0)`);
  c.fillStyle = g; c.fillRect(0, 0, W, H);
}
function drawSurface(c, kind, t, seed) {
  c.save();
  if (kind === 'asphalt') {
    c.drawImage(SURF.asphalt, 0, 0);
    flashLight(c, W * .5, H * .44, .26, 1000, '200,215,255');
    c.strokeStyle = 'rgba(210,222,255,.5)'; c.lineWidth = 1.6;                   // 同一场雨落在地上：一圈圈涟漪
    for (let i = 0; i < 22; i++) {
      const per = .7 + hash(i, seed) * .8, ph = hash(i, seed + 1) * per, age = mod(t + ph, per) / per, cyc = Math.floor((t + ph) / per);
      const x = hash(i * 31 + cyc, seed + 2) * W, y = hash(i * 17 + cyc, seed + 3) * H, rr = 4 + age * 34;
      c.globalAlpha = .5 * (1 - age); c.beginPath(); c.ellipse(x, y, rr, rr * .45, 0, 0, 7); c.stroke();
    }
    c.globalAlpha = 1;
  } else if (kind === 'sheet') {
    c.drawImage(SURF.sheet, 0, 0);
    flashLight(c, W * .48, H * .42, .22, 950);
  } else if (kind === 'red') {                                                   // 暗房：只有一盏红色安全灯
    c.fillStyle = '#120002'; c.fillRect(0, 0, W, H);
    flashLight(c, W * .55, H * .38, .9, 1100, '160,8,28');
    c.globalAlpha = .5; c.drawImage(SURF.asphalt, 0, 0); c.globalAlpha = 1;
    c.globalCompositeOperation = 'multiply'; c.fillStyle = '#ff2a3c'; c.fillRect(0, 0, W, H); c.globalCompositeOperation = 'source-over';
  } else {                                                                       // 证物纸：浅色方格纸，边上一把比例尺
    c.drawImage(SURF.paper, 0, 0);
    flashLight(c, W * .5, H * .45, .2, 900);
  }
  const v = c.createRadialGradient(W / 2, H / 2, H * .35, W / 2, H / 2, H * 1.05);  // 闪光够不到的四角
  v.addColorStop(0, 'rgba(0,0,0,0)'); v.addColorStop(1, `rgba(0,0,0,${kind === 'paper' ? .45 : .75})`); c.fillStyle = v; c.fillRect(0, 0, W, H);
  c.restore();
}
// 比例尺（证物照的那条黑白尺）
function evRuler(c, x, y, len, rot, light) {
  c.save(); c.translate(x, y); c.rotate(rot);
  c.fillStyle = light ? '#f4f3ef' : '#1a1b20'; c.fillRect(0, 0, len, 46);
  c.fillStyle = light ? '#111' : '#e9e9e6';
  for (let i = 0; i <= len / 20; i++) c.fillRect(i * 20, 0, 2, i % 5 ? 12 : i % 10 ? 18 : 26);
  for (let i = 0; i < len / 100; i++) if (i % 2) c.fillRect(i * 100, 38, 100, 8);
  text(c, 'CM', len - 30, 30, 15, { w: 400, fam: MONO, fill: light ? '#111' : '#e9e9e6' });
  c.restore();
}
// 证物编号牌：一块折起来的小三角牌
function evTag(c, x, y, n, rot = 0) {
  c.save(); c.translate(x, y); c.rotate(rot);
  c.fillStyle = 'rgba(0,0,0,.45)'; c.beginPath(); c.moveTo(-58, 46); c.lineTo(66, 46); c.lineTo(48, 70); c.lineTo(-40, 70); c.fill();   // 影子
  c.fillStyle = '#efeee9'; c.beginPath(); c.moveTo(-50, -44); c.lineTo(50, -44); c.lineTo(62, 46); c.lineTo(-62, 46); c.closePath(); c.fill();
  const g = c.createLinearGradient(0, -44, 0, 46); g.addColorStop(0, 'rgba(0,0,0,0)'); g.addColorStop(1, 'rgba(0,0,0,.18)'); c.fillStyle = g; c.fill();
  c.fillStyle = '#0d0e12'; c.fillRect(-50, -44, 100, 10);
  text(c, String(n).padStart(2, '0'), 0, 8, 58, { fam: ANTON, w: 400, fill: '#0d0e12' });
  c.restore();
}

// ---------------------------------------------------------------- 物件（实物）
const metal = (c, x0, y0, x1, y1, stops) => { const g = c.createLinearGradient(x0, y0, x1, y1); stops.forEach(([k, col]) => g.addColorStop(k, col)); return g; };
const SILVER = [[0, '#5a606b'], [.22, '#e9edf2'], [.4, '#9aa1ad'], [.62, '#f5f7fa'], [.8, '#6d7480'], [1, '#2c3038']];
function castShadow(c, a = .6, blur = 30, dx = 22, dy = 30) { c.shadowColor = `rgba(0,0,0,${a})`; c.shadowBlur = blur; c.shadowOffsetX = dx; c.shadowOffsetY = dy; }
const noShadow = c => { c.shadowColor = 'transparent'; c.shadowBlur = 0; c.shadowOffsetX = 0; c.shadowOffsetY = 0; };
const EVI = {
  // 手机：锁屏 23:47，一条看不清的新消息；刚拍下时在震
  phone(c, t, k) {
    const buzz = k < .5 ? Math.sin(k * 95) * 4 * (1 - k / .5) : 0; c.translate(buzz, 0);
    const lit = .85 + .15 * Math.exp(-k * 3);
    const gl = c.createRadialGradient(0, 40, 60, 0, 40, 420); gl.addColorStop(0, `rgba(110,140,255,${.22 * lit})`); gl.addColorStop(1, 'rgba(110,140,255,0)');
    c.fillStyle = gl; c.fillRect(-420, -380, 840, 840);                          // 屏幕照在桌面上
    c.save(); castShadow(c); c.fillStyle = '#0b0c10'; c.beginPath(); c.roundRect(-122, -238, 244, 476, 36); c.fill(); c.restore();
    c.strokeStyle = 'rgba(220,226,238,.45)'; c.lineWidth = 2.5; c.beginPath(); c.roundRect(-122, -238, 244, 476, 36); c.stroke();
    c.save(); c.beginPath(); c.roundRect(-109, -224, 218, 448, 26); c.clip();
    const sg = c.createLinearGradient(0, -224, 0, 224); sg.addColorStop(0, `rgba(36,52,112,${lit})`); sg.addColorStop(1, `rgba(8,12,32,${lit})`);
    c.fillStyle = sg; c.fillRect(-109, -224, 218, 448);
    text(c, '23:47', 0, -128, 66, { w: 400, fill: 'rgba(240,244,255,.95)' });
    text(c, '10月9日 星期四', 0, -78, 18, { w: 400, fill: 'rgba(220,228,255,.7)' });
    c.fillStyle = 'rgba(255,255,255,.16)'; c.beginPath(); c.roundRect(-94, -26, 188, 72, 16); c.fill();
    c.fillStyle = C.red; c.beginPath(); c.arc(-68, 10, 12, 0, 7); c.fill();
    const f = frameNo(t) >> 1;                                                  // 消息内容是一团看不清的马赛克
    for (let row = 0; row < 2; row++) for (let x = 0; x < (row ? 70 : 110); x += 7) { const v = hash(x * 13 + row * 7 + f * 3, 5); c.fillStyle = `rgba(230,236,255,${.25 + v * .45})`; c.fillRect(-46 + x, -8 + row * 18, 6, 9); }
    const rf = c.createLinearGradient(-110, -224, 110, 224); rf.addColorStop(0, 'rgba(255,255,255,0)'); rf.addColorStop(.42, 'rgba(255,255,255,.1)'); rf.addColorStop(.5, 'rgba(255,255,255,0)');
    c.fillStyle = rf; c.fillRect(-109, -224, 218, 448);
    c.restore();
  },
  // 手表：停在 23:47，红色秒针倒着走
  watch(c, t, k) {
    const strap = (y0, y1) => { const g = c.createLinearGradient(-58, 0, 58, 0); g.addColorStop(0, '#120d0c'); g.addColorStop(.5, '#2d2421'); g.addColorStop(1, '#100b0a');
      c.fillStyle = g; c.beginPath(); c.roundRect(-58, y0, 116, y1 - y0, 14); c.fill();
      c.strokeStyle = 'rgba(200,180,160,.25)'; c.setLineDash([6, 7]); c.lineWidth = 1.5; c.strokeRect(-48, Math.min(y0, y1) + 8, 96, Math.abs(y1 - y0) - 16); c.setLineDash([]); };
    c.save(); castShadow(c); strap(-250, -110); strap(110, 250);
    c.fillStyle = metal(c, -140, -140, 140, 140, SILVER); c.beginPath(); c.arc(0, 0, 140, 0, 7); c.fill(); c.restore();
    c.fillStyle = metal(c, 140, -10, 168, 10, SILVER); c.fillRect(138, -16, 26, 32);   // 表冠
    const dg = c.createRadialGradient(-30, -40, 10, 0, 0, 118); dg.addColorStop(0, '#f6f4ef'); dg.addColorStop(1, '#b9b6ae');
    c.fillStyle = dg; c.beginPath(); c.arc(0, 0, 116, 0, 7); c.fill();
    c.strokeStyle = '#1b1c20';
    for (let i = 0; i < 60; i++) { const a = i / 60 * 6.2832, r0 = i % 5 ? 104 : 94; c.lineWidth = i % 5 ? 1.2 : 3.4; c.beginPath(); c.moveTo(Math.cos(a) * r0, Math.sin(a) * r0); c.lineTo(Math.cos(a) * 110, Math.sin(a) * 110); c.stroke(); }
    const hand = (a, r, w, col) => { c.strokeStyle = col; c.lineWidth = w; c.lineCap = 'round'; c.beginPath(); c.moveTo(-Math.cos(a) * 14, -Math.sin(a) * 14); c.lineTo(Math.cos(a) * r, Math.sin(a) * r); c.stroke(); };
    hand((11 + 47 / 60) / 12 * 6.2832 - 1.5708, 62, 8, '#121317'); hand(47 / 60 * 6.2832 - 1.5708, 92, 5, '#121317');
    hand(-k * 6.2832 / 3 - 1.5708, 100, 2, C.red);
    c.fillStyle = '#121317'; c.beginPath(); c.arc(0, 0, 7, 0, 7); c.fill();
    c.fillStyle = 'rgba(255,255,255,.28)'; c.beginPath(); c.ellipse(-36, -50, 62, 26, -.6, 0, 7); c.fill();   // 表镜反光
  },
  // 镜子：一把手镜，照出一盏灯和一片黑；crack 0..1 时裂开，红色裂痕，左右两半错开
  mirror(c, t, k, o) {
    const crack = o.crack ?? 0;
    c.save(); castShadow(c);
    c.fillStyle = '#1a1b20'; c.beginPath(); c.roundRect(-26, 150, 52, 230, 16); c.fill();          // 手柄
    c.fillStyle = metal(c, -170, -170, 170, 170, SILVER); c.beginPath(); c.ellipse(0, 0, 165, 182, 0, 0, 7); c.fill(); c.restore();
    c.fillStyle = metal(c, -26, 0, 26, 0, SILVER); c.fillRect(-30, 168, 60, 18);
    const half = side => {
      c.save(); c.beginPath(); c.rect(side < 0 ? -200 : 0, -220, 200, 440); c.clip(); c.translate(side * crack * 10, crack * (side < 0 ? 8 : 22));
      c.beginPath(); c.ellipse(0, 0, 145, 162, 0, 0, 7); c.clip();
      const g = c.createLinearGradient(0, -162, 0, 162); g.addColorStop(0, '#05070c'); g.addColorStop(1, '#1d2433'); c.fillStyle = g; c.fillRect(-150, -170, 300, 340);
      const lx = 40, ly = -70, lg = c.createRadialGradient(lx, ly, 0, lx, ly, 130);                 // 镜子里：天花板上一盏灯
      lg.addColorStop(0, 'rgba(255,250,240,.95)'); lg.addColorStop(.15, 'rgba(255,230,200,.5)'); lg.addColorStop(1, 'rgba(255,230,200,0)');
      c.fillStyle = lg; c.fillRect(-150, -170, 300, 340);
      c.fillStyle = 'rgba(255,255,255,.12)'; c.beginPath(); c.moveTo(-150, -40); c.lineTo(-40, -170); c.lineTo(10, -170); c.lineTo(-150, 30); c.fill();   // 镜面反光
      c.restore();
    };
    half(-1); half(1);
    if (crack > 0) {
      const r = rand(77); c.strokeStyle = `rgba(230,17,43,${.9 * clamp(crack * 2)})`; c.lineWidth = 2.4;
      for (let i = 0; i < 9; i++) { const a = r() * 6.2832, len = 60 + r() * 100 * crack; c.beginPath(); c.moveTo(18, -30);
        let x = 18, y = -30; for (let s = 0; s < 4; s++) { x += Math.cos(a + (r() - .5) * .6) * len / 4; y += Math.sin(a + (r() - .5) * .6) * len / 4; c.lineTo(x, y); } c.stroke(); }
      c.strokeStyle = 'rgba(255,255,255,.7)'; c.lineWidth = 1.5; c.beginPath(); c.moveTo(0, -162); c.lineTo(4, 162); c.stroke();
    }
  },
  // 烟：抽了一半搁着，烟头还红着，冒一缕细烟
  cigarette(c, t, k) {
    c.save(); c.rotate(-.12);
    c.save(); castShadow(c, .55, 18, 14, 18);
    c.fillStyle = '#c69a62'; c.beginPath(); c.roundRect(-230, -19, 90, 38, 8); c.fill();
    c.fillStyle = '#ecebe6'; c.fillRect(-142, -19, 260, 38); c.restore();
    c.fillStyle = 'rgba(80,50,20,.35)'; for (let i = 0; i < 40; i++) c.fillRect(-226 + hash(i, 3) * 82, -15 + hash(i, 4) * 30, 2, 2);
    const tg = c.createLinearGradient(0, -19, 0, 19); tg.addColorStop(0, 'rgba(255,255,255,.5)'); tg.addColorStop(.5, 'rgba(255,255,255,0)'); tg.addColorStop(1, 'rgba(0,0,0,.35)');
    c.fillStyle = tg; c.fillRect(-230, -19, 348, 38);
    c.fillStyle = '#6b6a68'; c.beginPath(); c.roundRect(116, -18, 54, 36, 10); c.fill();
    c.fillStyle = 'rgba(40,40,40,.6)'; for (let i = 0; i < 26; i++) c.fillRect(118 + hash(i, 6) * 50, -16 + hash(i, 7) * 32, 3, 2);
    const glow = .75 + .25 * Math.sin(t * 7.3);
    const eg = c.createRadialGradient(172, 0, 0, 172, 0, 70); eg.addColorStop(0, `rgba(255,90,40,${.9 * glow})`); eg.addColorStop(.3, `rgba(224,17,43,${.45 * glow})`); eg.addColorStop(1, 'rgba(224,17,43,0)');
    c.fillStyle = eg; c.fillRect(102, -70, 140, 140);
    c.fillStyle = `rgba(255,${120 + 80 * glow | 0},70,1)`; c.fillRect(166, -14, 8, 28);
    c.restore();
    c.save(); c.lineCap = 'round';                                              // 烟：几缕细线往上卷
    for (let s = 0; s < 4; s++) {
      c.beginPath(); let px = 165, py = -36;
      for (let q = 1; q <= 30; q++) { const u = q / 30, x = 165 + Math.sin(u * 7 - t * 1.8 + s * 1.7) * 34 * u + s * 8 * u, y = -36 - u * 420; c.lineTo(x, y); px = x; py = y; }
      c.strokeStyle = `rgba(230,234,242,${.16 - s * .025})`; c.lineWidth = 4 + s * 3; c.stroke();
    }
    c.restore();
  },
  // 打火机：半透明的红色塑料，看得见里面的油
  lighter(c, t, k) {
    c.save(); castShadow(c); c.fillStyle = '#5d0410'; c.beginPath(); c.roundRect(-62, -150, 124, 300, 22); c.fill(); c.restore();
    const bg = c.createLinearGradient(-62, 0, 62, 0); bg.addColorStop(0, '#4a030c'); bg.addColorStop(.3, '#e0112b'); bg.addColorStop(.55, '#ff5a6c'); bg.addColorStop(1, '#3a0209');
    c.fillStyle = bg; c.beginPath(); c.roundRect(-62, -150, 124, 300, 22); c.fill();
    c.fillStyle = 'rgba(255,160,170,.25)'; c.fillRect(-54, -10 + Math.sin(t * 2) * 4, 108, 150);   // 油
    c.fillStyle = 'rgba(255,255,255,.35)'; c.fillRect(-42, -140, 12, 280);
    c.fillStyle = metal(c, -64, -230, 64, -150, SILVER); c.beginPath(); c.roundRect(-64, -232, 128, 86, 10); c.fill();
    c.fillStyle = '#1a1b20'; for (let i = 0; i < 3; i++) for (let j = 0; j < 2; j++) { c.beginPath(); c.arc(-26 + i * 26, -206 + j * 26, 6, 0, 7); c.fill(); }
    c.fillStyle = metal(c, 30, -260, 70, -230, SILVER); c.beginPath(); c.arc(46, -246, 22, 0, 7); c.fill();
  },
  // 戒指：一圈银，顶上一颗小钻，闪一下
  ring(c, t, k) {
    c.save(); castShadow(c, .6, 24, 18, 26); c.strokeStyle = metal(c, -130, -60, 130, 60, SILVER); c.lineWidth = 34; c.beginPath(); c.ellipse(0, 30, 128, 58, 0, 0, 7); c.stroke(); c.restore();
    c.strokeStyle = 'rgba(255,255,255,.55)'; c.lineWidth = 4; c.beginPath(); c.ellipse(0, 30, 128, 58, 0, 3.6, 5.9); c.stroke();
    c.strokeStyle = 'rgba(0,0,0,.45)'; c.lineWidth = 6; c.beginPath(); c.ellipse(0, 30, 112, 46, 0, .2, 2.9); c.stroke();
    c.fillStyle = metal(c, -40, -60, 40, -20, SILVER); c.beginPath(); c.moveTo(-34, -24); c.lineTo(34, -24); c.lineTo(22, -2); c.lineTo(-22, -2); c.fill();
    const dg = c.createLinearGradient(-40, -90, 40, -20); dg.addColorStop(0, '#ffffff'); dg.addColorStop(.5, '#b9c8ff'); dg.addColorStop(1, '#ffffff');
    c.fillStyle = dg; c.beginPath(); c.moveTo(0, -96); c.lineTo(40, -60); c.lineTo(0, -24); c.lineTo(-40, -60); c.closePath(); c.fill();
    c.strokeStyle = 'rgba(40,60,120,.5)'; c.lineWidth = 1.5; c.beginPath(); c.moveTo(-40, -60); c.lineTo(40, -60); c.moveTo(0, -96); c.lineTo(-14, -60); c.lineTo(0, -24); c.lineTo(14, -60); c.closePath(); c.stroke();
    const sp = Math.max(0, Math.sin(k * 3.1 + 1)) ** 8;                        // 钻石闪一下
    if (sp > .02) { c.save(); c.translate(-12, -72); c.globalAlpha *= sp; c.fillStyle = '#fff';
      for (const r of [0, 1.5708]) { c.save(); c.rotate(r + .3); c.beginPath(); c.ellipse(0, 0, 70, 3, 0, 0, 7); c.fill(); c.restore(); } c.restore(); }
  },
  // 钥匙
  key(c, t, k) {
    c.save(); c.rotate(.18); castShadow(c, .6, 22, 16, 22);
    c.fillStyle = metal(c, -240, -80, 240, 80, SILVER);
    c.beginPath(); c.arc(-160, 0, 82, 0, 7); c.arc(-160, 0, 30, 0, 7, true); c.fill('evenodd');
    c.beginPath(); c.moveTo(-90, -22); c.lineTo(240, -22); c.lineTo(240, 4); c.lineTo(214, 4); c.lineTo(206, 26); c.lineTo(186, 26); c.lineTo(178, 4); c.lineTo(150, 4); c.lineTo(142, 22); c.lineTo(122, 22); c.lineTo(114, 4); c.lineTo(-90, 22); c.closePath(); c.fill();
    noShadow(c); c.strokeStyle = 'rgba(255,255,255,.5)'; c.lineWidth = 2; c.beginPath(); c.moveTo(-80, -16); c.lineTo(236, -16); c.stroke();
    c.strokeStyle = 'rgba(200,205,215,.8)'; c.lineWidth = 6; c.beginPath(); c.arc(-160, 0, 112, 2.2, 6.9); c.stroke();   // 钥匙圈
    c.restore();
  },
  // 小丑牌
  joker(c, t, k) {
    c.save(); c.rotate(-.1); castShadow(c, .55, 26, 18, 26);
    c.fillStyle = '#f2f0eb'; c.beginPath(); c.roundRect(-140, -200, 280, 400, 18); c.fill(); noShadow(c);
    const cg = c.createLinearGradient(-140, 0, 140, 0); cg.addColorStop(0, 'rgba(0,0,0,.12)'); cg.addColorStop(.4, 'rgba(0,0,0,0)'); cg.addColorStop(1, 'rgba(0,0,0,.18)');
    c.fillStyle = cg; c.beginPath(); c.roundRect(-140, -200, 280, 400, 18); c.fill();
    c.strokeStyle = 'rgba(125,6,20,.6)'; c.lineWidth = 2; c.strokeRect(-118, -178, 236, 356);
    const dia = (x, y, s) => { c.fillStyle = C.red; c.beginPath(); c.moveTo(x, y - s); c.lineTo(x + s * .7, y); c.lineTo(x, y + s); c.lineTo(x - s * .7, y); c.closePath(); c.fill(); };
    dia(0, -10, 62); dia(-104, -130, 16); dia(104, 130, 16);
    text(c, 'J', -104, -164, 30, { fam: SERIF, fill: C.red }); text(c, 'JOKER', 0, 110, 34, { fam: SERIF, fill: '#1b1c20' });
  },
  // 面具：白瓷面具，左眼一道红色泪痕
  mask(c, t, k) {
    const face = () => { c.beginPath(); c.moveTo(0, -200); c.bezierCurveTo(150, -200, 175, -60, 150, 40); c.bezierCurveTo(130, 140, 60, 210, 0, 215); c.bezierCurveTo(-60, 210, -130, 140, -150, 40); c.bezierCurveTo(-175, -60, -150, -200, 0, -200); c.closePath(); };
    c.strokeStyle = '#0d0e12'; c.lineWidth = 3; c.lineCap = 'round';                               // 系带：从太阳穴垂下去，压在面具下面
    c.beginPath(); c.moveTo(-150, -10); c.bezierCurveTo(-200, 30, -190, 140, -230, 230); c.moveTo(150, -10); c.bezierCurveTo(210, 40, 230, 120, 260, 210); c.stroke();
    c.save(); castShadow(c); c.fillStyle = '#e4e2dc'; face(); c.fill(); c.restore();
    const fg = c.createRadialGradient(-40, -70, 20, 0, 0, 240); fg.addColorStop(0, 'rgba(255,255,255,.75)'); fg.addColorStop(.55, 'rgba(255,255,255,0)'); fg.addColorStop(1, 'rgba(50,50,60,.5)');
    c.fillStyle = fg; face(); c.fill();
    const eye = x => { c.beginPath(); c.moveTo(x - 50, -40); c.bezierCurveTo(x - 30, -72, x + 30, -72, x + 50, -40); c.bezierCurveTo(x + 30, -16, x - 30, -16, x - 50, -40); c.closePath(); };
    for (const x of [-62, 62]) {
      c.fillStyle = 'rgba(60,58,56,.35)'; c.save(); c.translate(0, 5); eye(x); c.restore(); c.fill();   // 眼洞的厚度
      c.fillStyle = '#06070a'; eye(x); c.fill();
    }
    const ng = c.createLinearGradient(-20, 0, 20, 0); ng.addColorStop(0, 'rgba(0,0,0,.18)'); ng.addColorStop(.5, 'rgba(255,255,255,.35)'); ng.addColorStop(1, 'rgba(0,0,0,.25)');
    c.fillStyle = ng; c.beginPath(); c.moveTo(-6, -40); c.quadraticCurveTo(-22, 50, -26, 86); c.quadraticCurveTo(0, 100, 26, 86); c.quadraticCurveTo(22, 50, 6, -40); c.closePath(); c.fill();
    c.strokeStyle = 'rgba(0,0,0,.28)'; c.lineWidth = 3; c.beginPath(); c.moveTo(-46, 140); c.quadraticCurveTo(0, 156, 46, 140); c.stroke();   // 嘴
    const tear = easeOut(k / 1.4);                                              // 泪痕慢慢往下淌
    c.strokeStyle = C.red; c.lineWidth = 7; c.beginPath(); c.moveTo(-74, -20); c.quadraticCurveTo(-80, 30, -72, -20 + 150 * tear); c.stroke();
    c.fillStyle = C.red; c.beginPath(); c.arc(-72, -16 + 150 * tear, 9, 0, 7); c.fill();
  },
  // 收起来的伞：躺在一小摊水里
  umbrella(c, t, k) {
    c.save(); c.rotate(-.5);
    const pg = c.createRadialGradient(0, 20, 20, 0, 20, 300); pg.addColorStop(0, 'rgba(150,170,220,.25)'); pg.addColorStop(1, 'rgba(150,170,220,0)');
    c.fillStyle = pg; c.beginPath(); c.ellipse(0, 20, 320, 90, 0, 0, 7); c.fill();
    c.save(); castShadow(c, .6, 20, 14, 20);
    c.fillStyle = '#121318'; c.beginPath(); c.moveTo(-230, 0); c.quadraticCurveTo(-40, -46, 170, -10); c.lineTo(170, 10); c.quadraticCurveTo(-40, 46, -230, 0); c.fill(); c.restore();
    c.strokeStyle = 'rgba(200,210,235,.35)'; c.lineWidth = 2;
    for (let i = 0; i < 4; i++) { c.beginPath(); c.moveTo(-200 + i * 20, 0); c.quadraticCurveTo(-20, -30 + i * 18, 160, -6 + i * 4); c.stroke(); }
    c.fillStyle = '#2a2c34'; c.fillRect(-20, -26, 22, 52);
    c.strokeStyle = metal(c, -300, 0, -230, 0, SILVER); c.lineWidth = 6; c.beginPath(); c.moveTo(-230, 0); c.lineTo(-290, 0); c.stroke();
    c.strokeStyle = '#1b1612'; c.lineWidth = 16; c.lineCap = 'round'; c.beginPath(); c.moveTo(170, 0); c.lineTo(230, 0); c.arc(230, 34, 34, -1.5708, 1.5708); c.stroke();
    c.fillStyle = 'rgba(225,235,255,.8)'; for (let i = 0; i < 12; i++) { c.beginPath(); c.arc(-180 + hash(i, 9) * 330, (hash(i, 10) - .5) * 30, 2 + hash(i, 11) * 3, 0, 7); c.fill(); }
    c.restore();
  },
};
const EVI_NAMES = Object.keys(EVI);
function drawEvi(c, name, t, k, o = {}) {
  c.save(); c.translate(o.x ?? 0, o.y ?? 0); if (o.r) c.rotate(o.r); c.scale(o.s ?? 1, o.s ?? 1); c.globalAlpha = o.a ?? 1;
  EVI[name](c, t, k, o); c.restore();
}
