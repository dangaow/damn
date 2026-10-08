'use strict';
// ============================================================================
// 基础：常量、工具函数、音乐时间轴查询。
// 约定：整支 MV 是一个函数 render(t)，只依赖 t；随机数一律用 rand(seed) / hash(i)，保证同一时刻每次画得一样。
// ============================================================================
const D = window.MV_DATA;
const W = 1920, H = 1080;
const STEP = D.step, BEAT = STEP * 4, BAR = STEP * 16;

// 配色：黑 / 银 / 血红，忧郁段落用冷蓝，桥段一处暖橙
const C = {
  black: '#040406', ink: '#0a0b10', navy: '#070d1f', white: '#f2f1ee', silver: '#c9ced6',
  red: '#e0112b', blood: '#7d0614', blue: '#7d93ff', ice: '#cfe0ff', amber: '#ff9a3c',
};
const SANS = 'Noto Sans SC', SERIF = 'Noto Serif SC', GOTH = 'UnifrakturMaguntia', ANTON = 'Anton', MONO = 'Space Mono', PIX = 'Silkscreen';
const fnt = (size, w = 900, fam = SANS) => `${w} ${size}px "${fam}", "${SANS}", "WenQuanYi Zen Hei", sans-serif`;

// ---------------------------------------------------------------- 工具
const clamp = (x, a = 0, b = 1) => Math.min(b, Math.max(a, x));
const lerp = (a, b, k) => a + (b - a) * k;
const ease = k => { k = clamp(k); return k * k * (3 - 2 * k); };
const easeOut = k => 1 - Math.pow(1 - clamp(k), 3);
const easeIn = k => Math.pow(clamp(k), 3);
const mod = (a, n) => ((a % n) + n) % n;
function rand(seed) { let a = seed | 0; return () => { a = a + 0x6D2B79F5 | 0; let t = Math.imul(a ^ a >>> 15, 1 | a); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }; }
const hash = (i, salt = 0) => rand((i | 0) * 9973 + salt * 7919 + 17)();
const frameNo = t => Math.floor(t * 30 + 1e-6);
function lastAt(arr, t, key = x => x) { let lo = 0, hi = arr.length - 1, r = -1; while (lo <= hi) { const m = (lo + hi) >> 1; if (key(arr[m]) <= t) { r = m; lo = m + 1; } else hi = m - 1; } return r; }
const mk = (w = W, h = H) => { const c = document.createElement('canvas'); c.width = w; c.height = h; return c; };
function sample(arr, t) { const x = t * D.fps, i = Math.floor(x); if (i < 0) return arr[0]; if (i >= arr.length - 1) return arr[arr.length - 1]; return lerp(arr[i], arr[i + 1], x - i); }

// ---------------------------------------------------------------- 音乐
// 事件列表：底鼓 / 军鼓 / 踩镲 / 808 音符；静音区间 gaps
const EV = { kick: D.kicks, snare: D.snares, hat: D.hats, note: D.notes808.map(n => n.t) };
const M = {
  energy: t => sample(D.energy, t),
  low: t => sample(D.bass, t),
  vocal: t => sample(D.vocal, t),
  // 最近一次事件：{ i, t, since }，没有则 since = Infinity
  last(kind, t) { const a = EV[kind], i = lastAt(a, t); return i < 0 ? { i, t: -1, since: Infinity } : { i, t: a[i], since: t - a[i] }; },
  // 冲击：事件发生瞬间 = 1，随后指数衰减
  hit(kind, t, decay = 10) { const s = M.last(kind, t).since; return isFinite(s) ? Math.exp(-s * decay) : 0; },
  count(kind, a, b) { const e = EV[kind]; return Math.max(0, lastAt(e, b - 1e-6) - lastAt(e, a - 1e-6)); },
  // 鼓突然全停：返回所在的静音区间 [a, b] 或 null
  gap(t) { const i = lastAt(D.gaps, t, g => g[0]); return i >= 0 && t < D.gaps[i][1] ? D.gaps[i] : null; },
  note(t) { const i = lastAt(D.notes808, t, n => n.t); return i < 0 ? null : { ...D.notes808[i], i, since: t - D.notes808[i].t }; },
  bar(t) { const i = Math.floor((t - D.bar0) / BAR); return { i, t0: D.bar0 + i * BAR, phase: mod((t - D.bar0) / BAR, 1), info: D.bars[i] || {} }; },
  lyric(t) {
    const i = lastAt(D.lyrics, t, l => l.t);
    const line = i >= 0 && t < D.lyrics[i].end ? D.lyrics[i] : null;
    return { i, line, k: line ? clamp((t - line.t) / (line.end - line.t)) : 0 };
  },
};
const lineAt = t0 => D.lyrics.find(l => Math.abs(l.t - t0) < .05);
const wordT = (line, ch) => (line.words.find(w => w.w === ch) || line.words[0]).t;

// jerk 镜头：底鼓按 3-3-2 轮流做"猛推 / 硬切 / 卡帧"
const JERK = ['punch', 'cut', 'freeze'];
function jerk(t) { const k = M.last('kick', t); return { ...k, act: k.i < 0 ? null : JERK[k.i % 3] }; }
// 到 t 为止发生过几次"硬切"（用来轮换版式）
function cuts(t, from = 0) { const a = lastAt(EV.kick, from - 1e-6) + 1, b = lastAt(EV.kick, t); let n = 0; for (let i = Math.max(a, 0); i <= b; i++) if (i % 3 === 1) n++; return n; }
// 卡帧：最近一次底鼓是 freeze 时，画面停在那一刻 3 帧
function frozen(t, frames = 3) { const j = jerk(t); return j.act === 'freeze' && j.since < frames / 30 ? j.t : t; }
// 静音时画面冻在静音开始的那一刻
function gapHold(t) { const g = M.gap(t); return g ? g[0] : t; }
