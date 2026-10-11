'use strict';
// ============================================================================
// 人：骨架驱动的剪影。永远看不清脸，只有轮廓光。
//   关节坐标是人自己的坐标系：脚底中心为原点，往上是负 y，身高约 1000；z 是往人正前方。
//   yaw：0 = 面朝镜头，π = 背对镜头，±π/2 = 侧身。画的时候 X = x·cos(yaw) + z·sin(yaw)。
// ============================================================================

const J = ['head', 'neck', 'lSh', 'rSh', 'lEl', 'rEl', 'lHa', 'rHa', 'pel', 'lHip', 'rHip', 'lKn', 'rKn', 'lFt', 'rFt'];
const P3 = (x, y, z = 0) => [x, y, z];
// 姿势库（l = 人自己的左边，画面上面朝镜头时在右边）
const POSE = {
  stand: {
    head: P3(0, -890), neck: P3(0, -800), lSh: P3(150, -752), rSh: P3(-150, -752),
    lEl: P3(168, -562, -8), rEl: P3(-168, -562, -8), lHa: P3(158, -398, 12), rHa: P3(-158, -398, 12),
    pel: P3(0, -468), lHip: P3(62, -468), rHip: P3(-62, -468), lKn: P3(64, -255, 6), rKn: P3(-64, -255, 6), lFt: P3(68, -44), rFt: P3(-68, -44),
  },
  pockets: {                                                                     // 手插兜
    lEl: P3(185, -585, -30), rEl: P3(-185, -585, -30), lHa: P3(110, -480, 45), rHa: P3(-110, -480, 45),
  },
  rap: {                                                                         // 右手抬到胸前，掌心朝下
    rEl: P3(-190, -610, 60), rHa: P3(-85, -665, 150),
    lEl: P3(180, -585, -20), lHa: P3(115, -478, 40),
  },
  rap2: {                                                                        // 右手往外甩
    rEl: P3(-250, -660, 40), rHa: P3(-350, -715, 90),
    lEl: P3(180, -585, -20), lHa: P3(115, -478, 40),
  },
  point: {                                                                       // 右手直直指出去
    rEl: P3(-280, -740, 60), rHa: P3(-420, -752, 120),
  },
  headDown: { head: P3(0, -858, 46), neck: P3(0, -790, 22) },
  sit: {                                                                         // 坐在路边，手肘撑在膝盖上，头低着
    head: P3(0, -548, 80), neck: P3(0, -468, 46), lSh: P3(146, -430, 24), rSh: P3(-146, -430, 24),
    lEl: P3(130, -262, 150), rEl: P3(-130, -262, 150), lHa: P3(52, -236, 230), rHa: P3(-52, -236, 230),
    pel: P3(0, -150), lHip: P3(64, -150), rHip: P3(-64, -150), lKn: P3(92, -290, 230), rKn: P3(-92, -290, 230),
    lFt: P3(96, -44, 250), rFt: P3(-96, -44, 250),
  },
};
const POSE_ALL = {};
for (const k in POSE) { POSE_ALL[k] = {}; for (const j of J) POSE_ALL[k][j] = (POSE[k][j] ?? POSE.stand[j]).slice(); }
function blendPose(a, b, k) { const o = {}; for (const j of J) o[j] = a[j].map((v, i) => lerp(v, b[j][i], k)); return o; }
function addTo(p, j, dx, dy, dz = 0) { p[j] = [p[j][0] + dx, p[j][1] + dy, p[j][2] + dz]; }

// 走路：phase 0..1 一步周期（两步）
function walkPose(base, phase, amt = 1) {
  const p = blendPose(base, base, 0), s = Math.sin(phase * 6.283), c = Math.cos(phase * 6.283);
  addTo(p, 'lFt', 0, -Math.max(0, s) * 60 * amt, s * 150 * amt); addTo(p, 'rFt', 0, -Math.max(0, -s) * 60 * amt, -s * 150 * amt);
  addTo(p, 'lKn', 0, -Math.max(0, s) * 30 * amt, (s * 80 + 30) * amt); addTo(p, 'rKn', 0, -Math.max(0, -s) * 30 * amt, (-s * 80 + 30) * amt);
  addTo(p, 'lHa', 0, 0, -s * 90 * amt); addTo(p, 'rHa', 0, 0, s * 90 * amt); addTo(p, 'lEl', 0, 0, -s * 40 * amt); addTo(p, 'rEl', 0, 0, s * 40 * amt);
  const bob = Math.abs(c) * 14 * amt;
  for (const j of ['head', 'neck', 'lSh', 'rSh', 'lEl', 'rEl', 'lHa', 'rHa', 'pel', 'lHip', 'rHip']) addTo(p, j, 0, -bob + 10 * amt, 0);
  return p;
}
// 呼吸、跟拍子点头
function liven(p, t, o = {}) {
  const br = Math.sin(t * 1.6) * 4, nod = (o.nod ?? 0);
  for (const j of ['head', 'neck', 'lSh', 'rSh']) addTo(p, j, 0, br, 0);
  addTo(p, 'head', 0, nod * 26, nod * 30); addTo(p, 'neck', 0, nod * 10, nod * 8);
  addTo(p, 'lSh', 0, nod * 8); addTo(p, 'rSh', 0, nod * 8);
  return p;
}

// ---------------------------------------------------------------- 画剪影
const FIG = mk(), fgx = FIG.getContext('2d');
const RIM = mk(), rmx = RIM.getContext('2d');
const ERO = mk(), erx = ERO.getContext('2d');
const RAW = mk(), rwx = RAW.getContext('2d');

// 一段四肢：两头不一样粗的圆头梯形
function limb(c, ax, ay, bx, by, w0, w1) {
  const dx = bx - ax, dy = by - ay, L = Math.hypot(dx, dy) || 1, nx = -dy / L, ny = dx / L;
  c.beginPath(); c.arc(ax, ay, w0 / 2, 0, 6.283); c.fill();
  c.beginPath(); c.arc(bx, by, w1 / 2, 0, 6.283); c.fill();
  c.beginPath(); c.moveTo(ax + nx * w0 / 2, ay + ny * w0 / 2); c.lineTo(bx + nx * w1 / 2, by + ny * w1 / 2); c.lineTo(bx - nx * w1 / 2, by - ny * w1 / 2); c.lineTo(ax - nx * w0 / 2, ay - ny * w0 / 2); c.closePath(); c.fill();
}
// 把姿势画成黑色的形状（画在 c 上，已经平移缩放好）。比例按真人：约 7.5 个头身，斜肩，宽松卫衣，帽兜
function bodyShape(c, p, yaw, o = {}) {
  const cy = Math.cos(yaw), sy = Math.sin(yaw), prof = Math.abs(sy), front = Math.abs(cy);
  const X = j => p[j][0] * cy + p[j][2] * sy, Y = j => p[j][1];
  const hw = (xw, zd) => xw * front + zd * prof;                                // 某一层的半宽：正面看是左右宽，侧面看是前后厚
  const L = (a, b, w0, w1) => limb(c, X(a), Y(a), X(b), Y(b), w0, w1);
  const bulk = o.bulk ?? 1;
  // 腿（宽松的裤子）+ 鞋
  L('lHip', 'lKn', 112 * bulk, 90 * bulk); L('rHip', 'rKn', 112 * bulk, 90 * bulk); L('lKn', 'lFt', 90 * bulk, 76 * bulk); L('rKn', 'rFt', 90 * bulk, 76 * bulk);
  for (const f of ['lFt', 'rFt']) { c.beginPath(); c.ellipse(X(f) + sy * 34, Y(f) + 18, lerp(44, 118, prof), 30, 0, 0, 6.283); c.fill(); }
  // 身体：沿脊柱一层层取半宽
  const spX = (X('neck') + X('pel')) / 2, yS = (Y('lSh') + Y('rSh')) / 2, yHem = Y('pel') + 44;
  const shW = Math.max(Math.abs(X('lSh') - X('rSh')) / 2, 64 * prof) + 16 * bulk;
  const nX = X('neck'), pX = X('pel'), at = k => lerp(nX, pX, k);
  const lv = [[yS - 34, hw(62, 56), 0], [yS - 16, shW * .72, .03], [yS + 6, shW * .96, .07], [yS + 40, shW + 4, .14], [yS + 120, hw(150, 82) * bulk, .38], [Y('pel') - 40, hw(142, 78) * bulk, .85], [yHem, hw(146, 80) * bulk, 1]];
  c.beginPath();
  lv.forEach(([yy, w, k], i) => { const xx = at(k) + w; i ? c.lineTo(xx, yy) : c.moveTo(xx, yy); });
  for (let i = lv.length - 1; i >= 0; i--) { const [yy, w, k] = lv[i]; c.lineTo(at(k) - w, yy); }
  c.closePath(); c.fill(); c.lineJoin = 'round'; c.lineWidth = 26; c.strokeStyle = c.fillStyle; c.stroke();
  // 胳膊（卫衣袖子）+ 手
  L('lSh', 'lEl', 80 * bulk, 70 * bulk); L('rSh', 'rEl', 80 * bulk, 70 * bulk); L('lEl', 'lHa', 70 * bulk, 58 * bulk); L('rEl', 'rHa', 70 * bulk, 58 * bulk);
  for (const [h, e] of [['lHa', 'lEl'], ['rHa', 'rEl']]) {
    const a = Math.atan2(Y(h) - Y(e), X(h) - X(e));
    c.beginPath(); c.ellipse(X(h) + Math.cos(a) * 26, Y(h) + Math.sin(a) * 26, 34, 24, a, 0, 6.283); c.fill();
  }
  // 帽兜：比头大一圈，顶上略尖，往下顺到肩膀
  const hx = X('head'), hy = Y('head'), hwH = hw(64, 88);
  c.beginPath();
  c.moveTo(hx - hwH, hy + 20);
  c.bezierCurveTo(hx - hwH * 1.08, hy - 74, hx - hwH * .55 - sy * 20, hy - 104, hx - sy * 26, hy - 104);
  c.bezierCurveTo(hx + hwH * .55 - sy * 20, hy - 104, hx + hwH * 1.08, hy - 74, hx + hwH, hy + 20);
  c.quadraticCurveTo(hx + hwH * .9, hy + 64, at(0) + shW * .45, yS - 22);
  c.lineTo(at(0) - shW * .45, yS - 22);
  c.quadraticCurveTo(hx - hwH * .9, hy + 64, hx - hwH, hy + 20);
  c.closePath(); c.fill();
  if (prof > .35) {                                                              // 侧身：帽兜前沿罩住脸
    const d = Math.sign(sy) * prof;
    c.beginPath(); c.moveTo(hx + d * 50, hy - 100); c.quadraticCurveTo(hx + d * 118, hy - 60, hx + d * 104, hy + 34); c.lineTo(hx + d * 40, hy + 60); c.lineTo(hx, hy); c.closePath(); c.fill();
  }
}

// 画一个人：x, y = 脚底在画面上的位置；s = 缩放（1 ≈ 身高 1000px）
// o.rim 轮廓光颜色，o.light 光源位置 [x, y]（决定哪边亮），o.back 逆光（一圈都亮），o.col 身体颜色，o.blur 拖影/失焦
function figure(c, pose, x, y, s, o = {}) {
  const yaw = o.yaw ?? Math.PI;
  rwx.setTransform(1, 0, 0, 1, 0, 0); rwx.clearRect(0, 0, W, H);
  rwx.save(); rwx.translate(x, y); rwx.scale(s, s); rwx.fillStyle = '#000'; bodyShape(rwx, pose, yaw, o); rwx.restore();
  fgx.setTransform(1, 0, 0, 1, 0, 0); fgx.clearRect(0, 0, W, H);
  fgx.filter = s > 1.1 ? 'url(#goo14)' : s > .45 ? 'url(#goo8)' : 'url(#goo4)'; fgx.drawImage(RAW, 0, 0); fgx.filter = 'none';
  // 身体
  c.save(); c.globalAlpha *= o.a ?? 1;
  if (o.blur) c.filter = `blur(${o.blur}px)`;
  if (o.col && o.col !== '#000') {                                                // 换颜色：先把剪影染色
    rmx.setTransform(1, 0, 0, 1, 0, 0); rmx.globalCompositeOperation = 'source-over'; rmx.clearRect(0, 0, W, H); rmx.drawImage(FIG, 0, 0);
    rmx.globalCompositeOperation = 'source-in'; rmx.fillStyle = o.col; rmx.fillRect(0, 0, W, H); rmx.globalCompositeOperation = 'source-over';
    c.drawImage(RIM, 0, 0);
  } else c.drawImage(FIG, 0, 0);
  c.filter = 'none';
  // 轮廓光
  if (o.rim) {
    const rw = (o.rimW ?? 5) * Math.max(.5, s * 1.4);
    // 腐蚀：剪影和自己往四个方向挪一点的交集
    erx.setTransform(1, 0, 0, 1, 0, 0); erx.globalCompositeOperation = 'source-over'; erx.clearRect(0, 0, W, H); erx.drawImage(FIG, 0, 0);
    erx.globalCompositeOperation = 'destination-in';
    if (o.back) for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) erx.drawImage(FIG, dx * rw * .6, dy * rw * .6);
    let lx = 0, ly = -1;
    if (o.light) { lx = o.light[0] - x; ly = o.light[1] - (y - 500 * s); const L = Math.hypot(lx, ly) || 1; lx /= L; ly /= L; }
    erx.drawImage(FIG, -lx * rw * 1.6, -ly * rw * 1.6);                           // 朝光的那一边更亮
    erx.globalCompositeOperation = 'source-over';
    rmx.setTransform(1, 0, 0, 1, 0, 0); rmx.globalCompositeOperation = 'source-over'; rmx.clearRect(0, 0, W, H); rmx.drawImage(FIG, 0, 0);
    rmx.globalCompositeOperation = 'destination-out'; rmx.drawImage(ERO, 0, 0);
    rmx.globalCompositeOperation = 'source-in'; rmx.fillStyle = o.rim; rmx.fillRect(0, 0, W, H); rmx.globalCompositeOperation = 'source-over';
    c.globalCompositeOperation = 'lighter';
    if (o.sheen) {                                                                // 衣服上很淡的一层受光（有体积感）
      erx.globalCompositeOperation = 'source-over'; erx.clearRect(0, 0, W, H); erx.drawImage(FIG, 0, 0); erx.globalCompositeOperation = 'source-in';
      const L0 = o.light ?? [x, y - 1200 * s], g = erx.createRadialGradient(L0[0], L0[1], 0, L0[0], L0[1], 1100 * s);
      g.addColorStop(0, o.rim); g.addColorStop(1, 'rgba(0,0,0,0)'); erx.fillStyle = g; erx.fillRect(0, 0, W, H); erx.globalCompositeOperation = 'source-over';
      c.globalAlpha = (o.a ?? 1) * o.sheen; c.filter = 'blur(6px)'; c.drawImage(ERO, 0, 0);
    }
    c.globalAlpha = (o.a ?? 1) * (o.rimA ?? 1);
    c.filter = `blur(${1.2 + (o.blur ?? 0)}px)`; c.drawImage(RIM, 0, 0);
    c.globalAlpha = (o.a ?? 1) * (o.rimA ?? 1) * .55; c.filter = `blur(${10 + (o.blur ?? 0)}px)`; c.drawImage(RIM, 0, 0);
    c.filter = 'none';
  }
  c.restore();
}

// 地上的倒影（湿地面）：把剪影上下翻过来，压暗、拉糊
function reflection(c, a = .35, y0 = H, blur = 6, col) {
  c.save(); c.globalAlpha *= a; c.filter = `blur(${blur}px)`;
  c.translate(0, y0 * 2); c.scale(1, -1);
  c.beginPath(); c.rect(0, y0, W, H); c.clip();
  c.drawImage(col ? RIM : FIG, 0, 0);
  c.restore(); c.filter = 'none';
}
