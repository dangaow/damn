'use strict';
// ============================================================================
// 意识流里乱飞的物件：都是以 (0,0) 为中心、高约 450px 的线稿。
// 每个函数签名 (c, t, k)：t 是全局时间，k 是这个物件出现后的秒数（用来做物件自己的小动作）。
// 线条颜色 / 点缀色 / 辉光由 drawObj 的 o.ink / o.acc / o.glow 决定（亮底上画成黑线，暗底上画成发光的白线）。
// ============================================================================
let LINE = '#e9edf5', ACC = '#ff2a45', GLOW = 'rgba(200,215,255,.6)';
function lineStyle(c, w = 3.2) { c.strokeStyle = LINE; c.lineWidth = w; c.lineJoin = 'round'; c.lineCap = 'round'; c.shadowColor = GLOW ?? 'transparent'; c.shadowBlur = GLOW ? 14 : 0; }

const OBJ = {
  // 手机：锁屏 23:47，一条新消息
  phone(c, t, k) {
    c.translate(k < .35 ? Math.sin(k * 90) * 3 : 0, 0);                            // 刚出现时震一下
    lineStyle(c); c.beginPath(); c.roundRect(-115, -215, 230, 430, 30); c.stroke();
    c.fillStyle = 'rgba(120,150,255,.12)'; c.beginPath(); c.roundRect(-100, -195, 200, 390, 18); c.fill();
    c.shadowBlur = 0; text(c, '23:47', 0, -95, 56, { w: 400, fam: MONO, fill: LINE });
    lineStyle(c, 2); c.beginPath(); c.roundRect(-86, -12, 172, 50, 12); c.stroke();
    c.fillStyle = ACC; c.beginPath(); c.arc(-62, 13, 7, 0, 7); c.fill();
  },
  // 时钟：停在 23:47，红色秒针倒着走
  clock(c, t, k) {
    lineStyle(c); c.beginPath(); c.arc(0, 0, 190, 0, 7); c.stroke();
    for (let i = 0; i < 12; i++) { const a = i / 12 * 6.2832; c.lineWidth = i % 3 ? 2 : 4; c.beginPath(); c.moveTo(Math.cos(a) * 165, Math.sin(a) * 165); c.lineTo(Math.cos(a) * 185, Math.sin(a) * 185); c.stroke(); }
    const hand = (a, r, w) => { c.lineWidth = w; c.beginPath(); c.moveTo(0, 0); c.lineTo(Math.cos(a) * r, Math.sin(a) * r); c.stroke(); };
    hand((11 + 47 / 60) / 12 * 6.2832 - 1.5708, 100, 6); hand(47 / 60 * 6.2832 - 1.5708, 150, 4);
    c.strokeStyle = ACC; hand(-k * 6.2832 * .8 - 1.5708, 170, 2);
    c.fillStyle = LINE; c.beginPath(); c.arc(0, 0, 8, 0, 7); c.fill();
  },
  // 月亮：一弯月牙，暗面只剩一圈很淡的轮廓
  moon(c, t, k) {
    lineStyle(c, 1.5); c.globalAlpha *= .4; c.beginPath(); c.arc(0, 0, 175, 0, 7); c.stroke(); c.globalAlpha /= .4;
    c.save(); c.beginPath(); c.arc(0, 0, 175, 0, 7); c.clip();
    c.fillStyle = LINE; c.shadowColor = GLOW ?? 'transparent'; c.shadowBlur = GLOW ? 30 : 0;
    c.beginPath(); c.arc(0, 0, 175, 0, 7); c.arc(70 + k * 6, -30, 160, 0, 7, true); c.fill('evenodd'); c.restore();
  },
  // 眼睛：跟着军鼓眨一下，瞳孔慢慢移动
  eye(c, t, k) {
    const open = 1 - M.hit('snare', t, 9) * .9;
    lineStyle(c);
    const lid = () => { c.beginPath(); c.moveTo(-230, 0); c.quadraticCurveTo(0, -170 * open, 230, 0); c.quadraticCurveTo(0, 170 * open, -230, 0); c.closePath(); };
    lid(); c.stroke();
    c.save(); lid(); c.clip();
    const ix = Math.sin(k * 1.7) * 40, iy = Math.cos(k * 1.3) * 12;
    c.beginPath(); c.arc(ix, iy, 82, 0, 7); c.stroke();
    for (let i = 0; i < 18; i++) { const a = i / 18 * 6.2832; c.lineWidth = 1.2; c.beginPath(); c.moveTo(ix + Math.cos(a) * 36, iy + Math.sin(a) * 36); c.lineTo(ix + Math.cos(a) * 78, iy + Math.sin(a) * 78); c.stroke(); }
    c.fillStyle = '#05060a'; c.beginPath(); c.arc(ix, iy, 32, 0, 7); c.fill(); c.lineWidth = 2; c.stroke();
    c.fillStyle = LINE; c.beginPath(); c.arc(ix + 14, iy - 14, 7, 0, 7); c.fill();
    c.restore();
  },
  // 镜子：crack 0..1 时裂开（红色裂痕），左右两半错开、往下垮
  mirror(c, t, k, crack = 0) {
    const half = (side) => {
      c.save(); c.beginPath(); c.rect(side < 0 ? -300 : 0, -300, 300, 600); c.clip();
      c.translate(side * crack * 14, crack * (side < 0 ? 18 : 34));
      lineStyle(c); c.beginPath(); c.ellipse(0, 0, 160, 225, 0, 0, 7); c.stroke(); c.lineWidth = 1.6; c.beginPath(); c.ellipse(0, 0, 145, 210, 0, 0, 7); c.stroke();
      c.globalAlpha *= .35; c.lineWidth = 2; for (let i = 0; i < 3; i++) { c.beginPath(); c.moveTo(-90 + i * 34, -120 + i * 10); c.lineTo(-30 + i * 34, -170 + i * 10); c.stroke(); }
      c.restore();
    };
    half(-1); half(1);
    if (crack > 0) {                                                                // 从撞击点放射出去的裂痕
      const r = rand(23); c.save(); c.strokeStyle = ACC; c.lineWidth = 2.2; c.shadowColor = ACC; c.shadowBlur = 10;
      for (let i = 0; i < 9; i++) {
        const a = i / 9 * 6.2832 + r() * .4, L = (90 + r() * 140) * easeOut(crack);
        c.beginPath(); c.moveTo(0, -20); let x = 0, y = -20;
        for (let s = 1; s <= 4; s++) { x = Math.cos(a + (r() - .5) * .5) * L * s / 4; y = -20 + Math.sin(a + (r() - .5) * .5) * L * s / 4 * 1.3; c.lineTo(x, y); }
        c.stroke();
      }
      c.restore();
    }
  },
  // 戒指：在转（椭圆的扁平程度变化），顶上一颗钻
  ring(c, t, k) {
    const ry = 150 * Math.max(.18, Math.abs(Math.cos(k * 1.6))), a = 1;
    lineStyle(c, 9); c.beginPath(); c.ellipse(0, 30, 150, ry, 0, 0, 7); c.stroke();
    c.lineWidth = 1.5; c.beginPath(); c.ellipse(0, 30, 132, ry * .88, 0, 0, 7); c.stroke();
    const top = 30 - ry;
    lineStyle(c, 2.4); c.beginPath(); c.moveTo(-42, top - 6); c.lineTo(-24, top - 34); c.lineTo(24, top - 34); c.lineTo(42, top - 6); c.lineTo(0, top + 30 * a); c.closePath(); c.stroke();
    c.beginPath(); c.moveTo(-42, top - 6); c.lineTo(42, top - 6); c.moveTo(-24, top - 34); c.lineTo(0, top + 30); c.lineTo(24, top - 34); c.stroke();
  },
  // 钥匙：慢慢转
  key(c, t, k) {
    c.rotate(-.35 + k * .25);
    lineStyle(c); c.beginPath(); c.arc(-150, 0, 62, 0, 7); c.stroke(); c.beginPath(); c.arc(-150, 0, 22, 0, 7); c.stroke();
    c.beginPath(); c.moveTo(-88, -11); c.lineTo(165, -11); c.lineTo(165, 11); c.lineTo(150, 11); c.lineTo(150, 42); c.lineTo(128, 42); c.lineTo(128, 26); c.lineTo(108, 26); c.lineTo(108, 48); c.lineTo(86, 48); c.lineTo(86, 11); c.lineTo(-88, 11); c.stroke();
  },
  // 门：半开，门缝里漏出暖光，慢慢再开大一点
  door(c, t, k) {
    const gx = lerp(70, 30, ease(k / 1.2));
    const g = c.createLinearGradient(gx, 0, 360, 0); g.addColorStop(0, 'rgba(255,190,120,.45)'); g.addColorStop(1, 'rgba(255,190,120,0)');
    c.fillStyle = g; c.beginPath(); c.moveTo(gx, 235); c.lineTo(140, 260); c.lineTo(330, 430); c.lineTo(10, 430); c.closePath(); c.fill();
    c.fillStyle = 'rgba(255,200,140,.35)'; c.beginPath(); c.moveTo(gx, -235); c.lineTo(140, -260); c.lineTo(140, 260); c.lineTo(gx, 235); c.closePath(); c.fill();
    lineStyle(c); c.beginPath(); c.rect(-140, -260, 280, 520); c.stroke();
    c.beginPath(); c.moveTo(-140, -260); c.lineTo(gx, -235); c.lineTo(gx, 235); c.lineTo(-140, 260); c.stroke();
    c.beginPath(); c.arc(gx - 30, 10, 8, 0, 7); c.stroke();
  },
  // 打火机：火苗跟着踩镲抖
  lighter(c, t, k) {
    lineStyle(c); c.beginPath(); c.roundRect(-58, -40, 116, 230, 14); c.stroke(); c.beginPath(); c.rect(-58, -88, 116, 48); c.stroke();
    c.beginPath(); c.arc(32, -104, 18, 0, 7); c.stroke();
    const f = 1 + M.hit('hat', t, 14) * .35 + Math.sin(t * 31) * .05, sway = Math.sin(t * 7) * 8;
    const flame = (h, w, col, blur) => { c.strokeStyle = col; c.shadowColor = col; c.shadowBlur = blur; c.beginPath(); c.moveTo(-6, -104); c.bezierCurveTo(-w, -104 - h * .35, -w * .4 + sway, -104 - h * .8, sway, -104 - h); c.bezierCurveTo(w * .4 + sway, -104 - h * .8, w, -104 - h * .35, 6, -104); c.stroke(); };
    c.lineWidth = 3; flame(140 * f, 46, 'rgba(255,90,60,.95)', 24); c.lineWidth = 2; flame(80 * f, 22, 'rgba(255,230,190,.95)', 14);
  },
  // 烟：一支烟，烟头一明一暗，烟雾往上卷
  cigarette(c, t, k) {
    c.rotate(-.12);
    lineStyle(c); c.beginPath(); c.rect(-200, -14, 360, 28); c.stroke();
    c.fillStyle = 'rgba(255,170,90,.22)'; c.fillRect(100, -14, 60, 28);
    const ember = .6 + .4 * Math.sin(t * 5);
    c.fillStyle = `rgba(255,${60 + ember * 80 | 0},50,${ember})`; c.shadowColor = ACC; c.shadowBlur = 20 * ember; c.beginPath(); c.arc(-202, 0, 13, 0, 7); c.fill();
    c.shadowBlur = 8; c.strokeStyle = 'rgba(220,228,245,.5)'; c.lineWidth = 2;
    for (let i = 0; i < 3; i++) {
      c.beginPath();
      for (let y = 0; y < 340; y += 8) { const x = -205 + Math.sin(y * .022 + t * 1.8 + i * 2.1) * (8 + y * .12) + i * 10; y === 0 ? c.moveTo(x, -14 - y) : c.lineTo(x, -14 - y); }
      c.stroke();
    }
  },
  // 面具：右眼流下一道红色泪痕
  mask(c, t, k) {
    lineStyle(c);
    c.beginPath(); c.moveTo(0, -230); c.bezierCurveTo(150, -230, 175, -90, 160, 10); c.bezierCurveTo(145, 130, 70, 220, 0, 235);
    c.bezierCurveTo(-70, 220, -145, 130, -160, 10); c.bezierCurveTo(-175, -90, -150, -230, 0, -230); c.stroke();
    const eyeH = (x) => { c.beginPath(); c.moveTo(x - 50, -40); c.quadraticCurveTo(x, -78, x + 50, -40); c.quadraticCurveTo(x, -18, x - 50, -40); c.stroke(); };
    eyeH(-68); eyeH(68);
    c.lineWidth = 2; c.beginPath(); c.moveTo(-40, 120); c.quadraticCurveTo(0, 105, 40, 120); c.stroke();
    const L = easeOut(k / 1.4) * 170;                                               // 泪痕往下流
    if (L > 2) { c.strokeStyle = ACC; c.shadowColor = ACC; c.lineWidth = 3; c.beginPath(); c.moveTo(70, -28); c.quadraticCurveTo(78, -28 + L * .5, 66, -28 + L); c.stroke();
      c.fillStyle = ACC; c.beginPath(); c.arc(66, -28 + L, 5, 0, 7); c.fill(); }
  },
  // 小丑牌：在翻转，背面是格纹
  joker(c, t, k) {
    const sx = Math.cos(k * 2.4); c.scale(Math.max(.04, Math.abs(sx)), 1);
    lineStyle(c); c.beginPath(); c.roundRect(-145, -205, 290, 410, 18); c.stroke();
    c.lineWidth = 1.5; c.beginPath(); c.roundRect(-128, -188, 256, 376, 12); c.stroke();
    if (sx < 0) { c.globalAlpha *= .5; for (let i = -6; i <= 6; i++) { c.beginPath(); c.moveTo(i * 22 - 100, -188); c.lineTo(i * 22 + 100, 188); c.moveTo(i * 22 + 100, -188); c.lineTo(i * 22 - 100, 188); c.stroke(); } return; }
    c.shadowBlur = 0; text(c, 'J', -100, -150, 52, { w: 400, fam: ANTON, fill: LINE }); c.save(); c.rotate(Math.PI); text(c, 'J', -100, -150, 52, { w: 400, fam: ANTON, fill: LINE }); c.restore();
    c.fillStyle = ACC; c.shadowColor = ACC; c.shadowBlur = 18; c.beginPath(); c.moveTo(0, -80); c.lineTo(58, 0); c.lineTo(0, 80); c.lineTo(-58, 0); c.closePath(); c.fill();
    c.shadowBlur = 0; text(c, 'JOKER', 0, 140, 24, { w: 400, fam: MONO, fill: LINE });
  },
  // 雨伞：周围开始下雨
  umbrella(c, t, k) {
    lineStyle(c);
    c.beginPath(); c.moveTo(-250, 0); c.bezierCurveTo(-240, -190, 240, -190, 250, 0); c.stroke();
    for (let i = 0; i < 4; i++) { const x0 = -250 + i * 125; c.beginPath(); c.moveTo(x0, 0); c.quadraticCurveTo(x0 + 62, -30, x0 + 125, 0); c.stroke(); }
    c.lineWidth = 1.8; for (let i = 0; i <= 4; i++) { c.beginPath(); c.moveTo(0, -150); c.quadraticCurveTo(-250 + i * 125, -110, -250 + i * 125, 0); c.stroke(); }
    c.lineWidth = 3.2; c.beginPath(); c.moveTo(0, -150); c.lineTo(0, 215); c.arc(-28, 215, 28, 0, Math.PI); c.stroke();
    c.beginPath(); c.moveTo(0, -150); c.lineTo(0, -178); c.stroke();
    const r = rand(9); c.strokeStyle = 'rgba(200,215,255,.6)'; c.lineWidth = 1.6; c.shadowBlur = 0;   // 伞外的雨
    for (let i = 0; i < 40; i++) { const x = (r() - .5) * 900, y = mod(r() * 900 + k * 900, 900) - 450; if (Math.abs(x) < 260 && y < 20) continue; c.beginPath(); c.moveTo(x, y); c.lineTo(x - 6, y + 30); c.stroke(); }
  },
};
const OBJ_NAMES = Object.keys(OBJ);
function drawObj(c, name, t, k, o = {}) {
  const keep = [LINE, ACC, GLOW];
  LINE = o.ink ?? '#e9edf5'; ACC = o.acc ?? '#ff2a45'; GLOW = o.glow === undefined ? 'rgba(200,215,255,.6)' : o.glow;
  c.save(); c.translate(o.x ?? 0, o.y ?? 0); if (o.r) c.rotate(o.r); c.scale((o.s ?? 1) * (o.sx ?? 1), (o.s ?? 1) * (o.sy ?? 1)); c.globalAlpha = o.a ?? 1;
  OBJ[name](c, t, k, o.crack); c.restore();
  [LINE, ACC, GLOW] = keep;
}
