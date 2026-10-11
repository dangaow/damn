'use strict';
// ============================================================================
// 基础：常量、工具函数、音乐时间轴查询。
// 约定：整支 MV 是一个函数 render(t)，只依赖 t；随机数一律用 rand(seed) / hash(i)，保证同一时刻每次画得一样。
// ============================================================================
const D = window.MV_DATA;
const W = 1920, H = 1080;
const STEP = D.step, BEAT = STEP * 4, BAR = STEP * 16;
const bar = k => D.bar0 + k * BAR;                                                  // 第 k 小节开始的时间

// 配色：黑 / 钢灰 / 冰白，唯一的颜色是冰蓝
const C = {
  black: '#030405', ink: '#08090b', steel: '#5d6772', fog: '#b9c2cb', white: '#eef3f7',
  ice: '#7fd4ff', iceDeep: '#2a78c8', navy: '#050b18', night: '#0a1630',
};
const SANS = 'Noto Sans SC', SERIF = 'Noto Serif SC', ANTON = 'Anton', GROT = 'Space Grotesk', MONO = 'Space Mono';
const fnt = (size, w = 700, fam = SANS) => `${w} ${size}px "${fam}", "${SANS}", "WenQuanYi Zen Hei", sans-serif`;

// ---------------------------------------------------------------- 工具
const clamp = (x, a = 0, b = 1) => Math.min(b, Math.max(a, x));
const lerp = (a, b, k) => a + (b - a) * k;
const ease = k => { k = clamp(k); return k * k * (3 - 2 * k); };
const easeOut = k => 1 - Math.pow(1 - clamp(k), 3);
const easeIn = k => Math.pow(clamp(k), 3);
const easeInOut = k => { k = clamp(k); return k < .5 ? 4 * k * k * k : 1 - Math.pow(-2 * k + 2, 3) / 2; };
const back = k => { k = clamp(k); const s = 1.9; return 1 + (s + 1) * Math.pow(k - 1, 3) + s * Math.pow(k - 1, 2); };   // 过冲再回来
const span = (t, a, b) => clamp((t - a) / (b - a));
const mod = (a, n) => ((a % n) + n) % n;
function rand(seed) { let a = seed | 0; return () => { a = a + 0x6D2B79F5 | 0; let t = Math.imul(a ^ a >>> 15, 1 | a); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }; }
const hash = (i, salt = 0) => rand((i | 0) * 9973 + salt * 7919 + 17)();
const frameNo = t => Math.floor(t * 30 + 1e-6);
function lastAt(arr, t, key = x => x) { let lo = 0, hi = arr.length - 1, r = -1; while (lo <= hi) { const m = (lo + hi) >> 1; if (key(arr[m]) <= t) { r = m; lo = m + 1; } else hi = m - 1; } return r; }
const mk = (w = W, h = H) => { const c = document.createElement('canvas'); c.width = w; c.height = h; return c; };
function sample(arr, t) { const x = t * D.fps, i = Math.floor(x); if (i < 0) return arr[0]; if (i >= arr.length - 1) return arr[arr.length - 1]; return lerp(arr[i], arr[i + 1], x - i); }

// ---------------------------------------------------------------- 音乐
const EV = { kick: D.kicks, snare: D.snares, hat: D.hats, note: D.notes808.map(n => n.t) };
const M = {
  energy: t => sample(D.energy, t),
  low: t => sample(D.bass, t),
  vocal: t => sample(D.vocal, t),
  // 最近一次事件：{ i, t, since }，没有则 since = Infinity
  last(kind, t) { const a = EV[kind], i = lastAt(a, t); return i < 0 ? { i, t: -1, since: Infinity } : { i, t: a[i], since: t - a[i] }; },
  // 冲击：事件发生瞬间 = 1，随后指数衰减
  hit(kind, t, decay = 10) { const s = M.last(kind, t).since; return isFinite(s) ? Math.exp(-s * decay) : 0; },
  // [a, b) 之间的事件
  between(kind, a, b) { const e = EV[kind], out = []; for (let i = Math.max(0, lastAt(e, a - 1e-6) + 1); i < e.length && e[i] < b; i++) out.push({ i, t: e[i] }); return out; },
  gap(t) { const i = lastAt(D.gaps, t, g => g[0]); return i >= 0 && t < D.gaps[i][1] ? D.gaps[i] : null; },
  bar(t) { const i = Math.floor((t - D.bar0) / BAR); return { i, t0: bar(i), phase: mod((t - D.bar0) / BAR, 1), info: D.bars[i] || {} }; },
  lyric(t) {
    const i = lastAt(D.lyrics, t, l => l.t);
    const line = i >= 0 && t < D.lyrics[i].end ? D.lyrics[i] : null;
    return { i, line, k: line ? clamp((t - line.t) / (line.end - line.t)) : 0 };
  },
};
// 按开始时间找一句歌词（分镜里写的时间）
const LN = t0 => { const l = D.lyrics.find(l => Math.abs(l.t - t0) < .05); if (!l) throw new Error('没有这一句歌词：' + t0); return l; };
// 唱到第几个字（含正在唱的）；英文单词算一个
const wordsSung = (line, t) => line.words.filter(w => w.t <= t + .02).length;
