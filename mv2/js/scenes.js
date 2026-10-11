'use strict';
const FX0 = {
  zoom: 1, rot: 0, barrel: .02, shift: [0, 0], sq: [1, 1], ca: .6, split: 0, glitch: 0, gseed: 0, wave: 0, tear: 0,
  invert: 0, xerox: 0, grade: 'mono', gradeMix: 1, bloom: .55, bloomThr: .55, flash: 0, flashCol: '#ffffff', dark: 0,
  grain: .07, scan: 0, vig: .8, lb: 1,
};
const SCENES = [];
const scene = o => (SCENES.push(o), o);
function sceneAt(t) { for (let i = SCENES.length - 1; i >= 0; i--) if (t >= SCENES[i].t0) return SCENES[i]; return SCENES[0]; }
const MV_END = D.duration;
scene({ name: 'test', t0: 0, t1: 999,
  bg: () => ({ mode: 'fog', a: '#050607', b: '#2a3038', c: '#9fb0c4', amt: .9, speed: 1 }),
  draw(c, t) {
    const lx = W / 2, ly = 150;
    tubeLamp(c, lx, ly, 260, 1);
    lightCone(c, lx, ly + 8, 240, 1100, 800, 1);
    let p = POSE_ALL.stand; const k = Math.floor(t) % 6;
    if (k === 1) p = POSE_ALL.rap; if (k === 2) p = POSE_ALL.rap2; if (k === 3) p = walkPose(POSE_ALL.stand, t, 1); if (k === 4) p = POSE_ALL.sit; if (k === 5) p = POSE_ALL.pockets;
    p = liven(blendPose(p, p, 0), t, { nod: M.hit('kick', t, 6) });
    const yaw = k === 3 ? Math.PI / 2 : (k === 0 ? Math.PI * .5 + t % 1 * Math.PI : Math.PI);
    figure(c, p, lx, 940, .62, { yaw: k === 4 ? Math.PI / 2 : yaw, rim: '#e8f2ff', light: [lx, ly], back: true, sheen: .15 }); fogLayer(c, t, .25);
    c.save(); coneClip(c, lx, ly + 8, 240, 1100, 800); rain(c, t, { n: 200, spd: 1400, len: 60, wind: .05, alpha: .5 }); c.restore();
  },
});
