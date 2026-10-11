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
  bg: t => t < 2 ? ({ mode: 'fog', a: '#050607', b: '#2a3038', c: '#9fb0c4', amt: .9 }) : t < 4 ? ({ mode: 'city', a: '#02040a', b: '#0b1c3c', c: '#7fd4ff', amt: 1 }) : ({ mode: 'solid', a: '#000000' }),
  draw(c, t) {
    if (t < 2) {
      const lx = W / 2, ly = 150; tubeLamp(c, lx, ly, 260, 1); lightCone(c, lx, ly + 8, 240, 1100, 800, 1);
      bigWord(c, 'MEMORY', t, 0, { size: 230, y: 520, a: .9 });
      c.save(); coneClip(c, lx, ly + 8, 240, 1100, 800); rain(c, t, { n: 220, spd: 1400, len: 60, wind: .05, alpha: .5 }); c.restore();
      fogLayer(c, t, .25);
    } else if (t < 4) {
      underpass(c, t); headlights(c, .55, 760, 1, '200,225,255'); rain(c, t, { n: 500, spd: 1900, len: 90, wind: .12, alpha: .45, col: 'rgb(150,200,255)' });
      vLine(c, LN(58.766), t, 1500, 200, 72, { all: true });
    } else {
      corridor(c, t, i => i > 6 ? 0 : 1, { z: t * .5, blue: 1 });
      vLine(c, LN(30.091), t, 1560, 220, 78, { all: true, fam: SERIF });
    }
  },
});
