"""配乐和音效：A 小调合成器浪潮（synthwave），120 BPM，按 clawd.html 的段落表编曲。

    node export.mjs --cues cues.json     （导出段落表和音效点）
    python music.py                      -> output/music.wav（导出用）、music.mp3（播放器用）

全部用 numpy / scipy 一个取样一个取样算出来：底鼓、军鼓、拍手、踩镲、锯齿波贝斯、Pad、琶音、主旋律、
FM 钟声，加上打字、提示音、冲击、上升音等音效；最后做侧链压缩、混响、延迟和限幅，ffmpeg 响度标准化到 -14 LUFS。
"""
import json
import os
import subprocess
import wave

import numpy as np
from scipy.signal import butter, sosfilt, fftconvolve

here = os.path.dirname(os.path.abspath(__file__))
CUES = json.load(open(os.path.join(here, 'cues.json'), encoding='utf-8'))
SR = 44100
BPM = CUES['bpm']
BEAT = 60 / BPM
BAR = CUES['bar']
TOTAL = CUES['total']
N = int((TOTAL + 2) * SR)
NBARS = int(round(TOTAL / BAR))
F32 = np.float32
rng = np.random.default_rng(7)
BUS = {k: np.zeros((2, N), F32) for k in ('drm', 'bass', 'pad', 'arp', 'lead', 'sfx', 'verb', 'dly')}
KICKS = []


def midi(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def tv(dur):
    return np.arange(max(1, int(dur * SR))) / SR


def noise(n):
    return rng.standard_normal(n).astype(F32)


def lp(x, f, o=2):
    return sosfilt(butter(o, min(f, SR * .45), 'low', fs=SR, output='sos'), x).astype(F32)


def hp(x, f, o=2):
    return sosfilt(butter(o, f, 'high', fs=SR, output='sos'), x).astype(F32)


def bp(x, lo, hi, o=2):
    return sosfilt(butter(o, [lo, min(hi, SR * .45)], 'band', fs=SR, output='sos'), x).astype(F32)


def put(sig, t0, bus, gain=1.0, pan=0.0, verb=0.0, dly=0.0):
    """把单声道（带声像）或立体声信号放进某条总线的 t0 秒处，可带混响 / 延迟发送。"""
    sig = np.asarray(sig, F32)
    if sig.ndim == 1:
        a = (np.clip(pan, -1, 1) + 1) * np.pi / 4
        sig = np.stack([sig * np.cos(a), sig * np.sin(a)]) * 1.4142
    i = int(round(t0 * SR))
    if i < 0:
        sig, i = sig[:, -i:], 0
    n = min(sig.shape[1], N - i)
    if n <= 0:
        return
    seg = sig[:, :n] * gain
    BUS[bus][:, i:i + n] += seg
    if verb:
        BUS['verb'][:, i:i + n] += seg * verb
    if dly:
        BUS['dly'][:, i:i + n] += seg * dly


def env(n, a=.005, d=.2, s=0.0, r=.05, hold=None):
    t = np.arange(n) / SR
    hold = n / SR - r if hold is None else hold
    e = np.where(t < a, t / max(a, 1e-4), s + (1 - s) * np.exp(-(t - a) / max(d, 1e-4)))
    return (e * np.clip((n / SR - t) / max(r, 1e-4), 0, 1)).astype(F32)


def saw(f, t, det=0.0):
    ph = (f * (1 + det) * t) % 1.0
    return (2 * ph - 1).astype(F32)


def sq(f, t, pw=.5):
    return np.where((f * t) % 1.0 < pw, 1.0, -1.0).astype(F32)


# ------------------------------------------------------------------------------------------- 乐器
def kick(t0, g=1.0):
    t = tv(.45)
    f = 45 + 110 * np.exp(-t / .03)
    ph = 2 * np.pi * np.cumsum(f) / SR
    s = np.sin(ph) * np.exp(-t / .28) + hp(noise(len(t)), 2000) * np.exp(-t / .004) * .25
    put(np.tanh(s * 1.6) * .9, t0, 'drm', g)
    KICKS.append(t0)


def snare(t0, g=1.0):
    t = tv(.3)
    s = bp(noise(len(t)), 1200, 7000) * np.exp(-t / .09) * .8 + np.sin(2 * np.pi * 185 * t) * np.exp(-t / .05) * .6
    put(s, t0, 'drm', g * .55, verb=.25)


def clap(t0, g=1.0):
    t = tv(.25)
    e = sum(np.exp(-np.clip(t - k * .012, 0, None) / .008) * (t >= k * .012) for k in range(3)) + np.exp(-t / .07) * .5
    put(bp(noise(len(t)), 900, 3200) * e * .6, t0, 'drm', g * .5, verb=.3)


def hat(t0, g=1.0, open_=False, pan=.25):
    t = tv(.28 if open_ else .05)
    put(hp(noise(len(t)), 7000) * np.exp(-t / (.09 if open_ else .014)), t0, 'drm', g * .22, pan=pan)


def bass(t0, m, dur, g=1.0, cut=900):
    t = tv(dur)
    f = midi(m)
    s = saw(f, t) + saw(f, t, .006) + np.sin(2 * np.pi * f / 2 * t) * .8
    put(lp(s, cut, 3) * env(len(t), .004, dur * .8, .5, .03), t0, 'bass', g * .32)


def pad(t0, notes, dur, g=1.0, cut=1600, verb=.6):
    t = tv(dur + .6)
    sL = np.zeros(len(t), F32)
    sR = np.zeros(len(t), F32)
    for m in notes:
        f = midi(m)
        sL += saw(f, t, -.004) + saw(f, t, .003)
        sR += saw(f, t, .004) + saw(f, t, -.002)
    e = env(len(t), .35, 1.0, .85, .6, hold=dur)
    st = np.stack([lp(sL, cut, 2), lp(sR, cut, 2)]) * e / len(notes)
    put(st, t0, 'pad', g * .24, verb=verb)


def pluck(t0, m, g=1.0, cut=2600, pan=0.0, dly=.35, kind='sq'):
    t = tv(.22)
    f = midi(m)
    s = (sq(f, t, .3) if kind == 'sq' else saw(f, t)) * .6 + saw(f, t, .005) * .4
    put(lp(s, cut, 2) * env(len(t), .002, .08, 0, .02), t0, 'arp', g * .16, pan=pan, dly=dly, verb=.15)


def lead(t0, m, dur, g=1.0):
    t = tv(dur + .15)
    f = midi(m) * (1 + .006 * np.sin(2 * np.pi * 5.5 * t) * np.clip((t - .15) / .2, 0, 1))
    ph = np.cumsum(f) / SR
    s = (2 * (ph % 1) - 1) + (2 * ((ph * 1.004) % 1) - 1) * .7 + np.sin(2 * np.pi * ph * .5) * .4
    put(lp(s.astype(F32), 3200, 2) * env(len(t), .02, .3, .7, .12, hold=dur), t0, 'lead', g * .13, verb=.45, dly=.2)


def bell(t0, m, g=1.0, dec=1.4, pan=0.0):
    t = tv(dec * 2.2)
    f = midi(m)
    s = np.sin(2 * np.pi * f * t + 1.8 * np.exp(-t / .4) * np.sin(2 * np.pi * f * 3.5 * t)) * np.exp(-t / dec)
    put(s, t0, 'lead', g * .14, pan=pan, verb=.7)


def sub(t0, m, dur, g=1.0):
    t = tv(dur)
    put(np.sin(2 * np.pi * midi(m) * t) * env(len(t), .3, 1, .9, .5), t0, 'bass', g * .25)


# ------------------------------------------------------------------------------------------- 音效
def sfx(c):
    t0, name = c['t'], c['name']
    if name == 'type':
        t = tv(.035)
        s = hp(noise(len(t)), 2500) * np.exp(-t / .006) + np.sin(2 * np.pi * (2600 + (c.get('n', 0) % 5) * 180) * t) * np.exp(-t / .01) * .3
        put(s, t0, 'sfx', .22, pan=((c.get('n', 0) % 7) - 3) * .08)
    elif name == 'enter':
        t = tv(.2)
        put(np.sin(2 * np.pi * 110 * t) * np.exp(-t / .05) + hp(noise(len(t)), 1500) * np.exp(-t / .008) * .6, t0, 'sfx', .5, verb=.3)
    elif name == 'whoosh':
        d = c.get('dur', .6); t = tv(d); x = noise(len(t)); k = t / d
        s = (lp(x, 900) * (1 - k) + hp(x, 2500) * k) * np.sin(np.pi * k) ** 1.5
        put(s, t0 - d * .4, 'sfx', .35, verb=.3)
    elif name == 'tick':
        t = tv(.05)
        f = 3200 if c.get('hi') else 2400
        put(np.sin(2 * np.pi * f * t) * np.exp(-t / .012), t0, 'sfx', .2 if c.get('hi') else .12, pan=.3 if c.get('hi') else -.2)
    elif name == 'bell':
        bell(t0, [81, 84, 86, 88, 91][c.get('n', 0) % 5], 1.6, 1.6, pan=(c.get('n', 0) - 2) * .25)
    elif name == 'riser':
        d = c.get('dur', 2.0); t = tv(d); k = t / d
        x = noise(len(t))
        s = (lp(x, 1500) * (1 - k) + hp(x, 3000) * k) * k ** 2 * .7
        f = 180 * (8 ** k)
        s += np.sin(2 * np.pi * np.cumsum(f) / SR) * k ** 2 * .25
        put(s, t0, 'sfx', .4, verb=.4)
    elif name == 'hit':
        g = c.get('k', 1.0); t = tv(1.4)
        f = 32 + 40 * np.exp(-t / .08)
        s = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / .5) + lp(noise(len(t)), 3500) * np.exp(-t / .12) * .7
        put(np.tanh(s * 1.4), t0, 'sfx', .55 * g, verb=.5)
    elif name == 'glitch':
        t = tv(.22); r = np.random.default_rng(int(t0 * 100))
        fs = np.repeat(r.uniform(200, 2400, 12), len(t) // 12 + 1)[:len(t)]
        s = sq(1, np.cumsum(fs) / SR) * (r.random(len(t)) > .3)
        put(np.round(s * 4) / 4 * np.exp(-t / .12), t0, 'sfx', .1, pan=.4)
    elif name == 'ping':
        t = tv(.3)
        put((np.sin(2 * np.pi * 1320 * t) + .5 * np.sin(2 * np.pi * 1980 * t)) * np.exp(-t / .07), t0, 'sfx', .2, pan=-.4, dly=.3)
    elif name == 'ret':
        for i, f in enumerate((990, 1480)):
            t = tv(.12)
            put(np.sin(2 * np.pi * f * t) * np.exp(-t / .04), t0 + i * .07, 'sfx', .17, pan=.4)
    elif name == 'shutter':
        for dt in (0, .045):
            t = tv(.03)
            put(bp(noise(len(t)), 2500, 7000) * np.exp(-t / .006), t0 + dt, 'sfx', .14, pan=.2)
    elif name == 'chime':
        for i, m in enumerate((81, 84, 88, 93)):
            bell(t0 + i * .09, m, 1.3, 1.8, pan=(i - 1.5) * .3)
    elif name == 'error':
        for i in range(2):
            t = tv(.16)
            put(lp(sq(220, t) + sq(207, t), 1800) * env(len(t), .003, .2, .8, .02), t0 + i * .2, 'sfx', .12)
    elif name == 'fix':
        for i, m in enumerate((84, 88, 91, 96)):
            bell(t0 + i * .07, m, 1.2, 1.2)
    elif name == 'send':
        sfx({'t': t0, 'name': 'whoosh', 'dur': .5})
        sfx({'t': t0 + .35, 'name': 'ping'})


# ------------------------------------------------------------------------------------------- 编曲
PROG = [(45, (57, 60, 64)), (41, (57, 60, 65)), (48, (55, 60, 64)), (43, (55, 59, 62))]     # Am  F  C  G
LEAD = [  # 每两小节一句主旋律：(拍, midi, 拍长)
    [(0, 76, 1.5), (1.5, 74, .5), (2, 72, 1), (3, 74, 1), (4, 76, 2), (6, 79, 1), (7, 76, 1)],
    [(0, 81, 1.5), (1.5, 79, .5), (2, 76, 1), (3, 74, 1), (4, 72, 1.5), (5.5, 74, .5), (6, 71, 2)],
]
MODES = {   # 每个段落有哪些声部
    'intro':     dict(pad=.6, cut=700, sub=True),
    'drop':      dict(kick=1, snare=1, hat='8o', bass=8, pad=1, arp=1, lead=1),
    'groove':    dict(kick=1, snare=1, hat='16', bass=8, pad=.8, arp=1),
    'breakdown': dict(pad=1, cut=1100, bells=1, sub=True),
    'drive':     dict(kick=1, snare=1, hat='16', bass=16, pad=.7, arp=1),
    'build':     dict(kick=1, hat='16', bass=8, pad=.7, arp=1, roll=2),
    'drop2':     dict(kick=1, snare=1, hat='8o', bass=16, pad=1, arp=1, lead=1.2, clap=1),
    'break2':    dict(pad=.9, cut=1300, bells=1, hat='8', sub=True),
    'build2':    dict(kick=1, snare=1, hat='16', bass=8, pad=.8, arp=1, roll=1),
    'outro':     dict(pad=1, cut=1200, bells=1, lead=.6, sub=True),
}


def mode_at(t):
    m = CUES['scenes'][0]
    for s in CUES['scenes']:
        if t >= s['t0'] - 1e-6:
            m = s
    return m


for b in range(NBARS):
    t0 = b * BAR
    sc = mode_at(t0 + .01)
    M = MODES[sc['mus']]
    root, chord = PROG[b % 4]
    bars_left = int(round((sc['t0'] + sc['d'] - t0) / BAR))
    last_bar_of_film = b == NBARS - 1
    if M.get('pad'):
        pad(t0, chord, BAR, M['pad'], M.get('cut', 1600))
    if M.get('sub'):
        sub(t0, root - 12 + 12, BAR, .8)
    if M.get('kick'):
        for k in range(4):
            kick(t0 + k * BEAT)
    if M.get('snare') and not (M.get('roll') and bars_left <= M['roll']):
        for k in (1, 3):
            snare(t0 + k * BEAT)
            if M.get('clap'):
                clap(t0 + k * BEAT)
    if M.get('roll') and bars_left <= M['roll']:                      # 军鼓滚奏：越来越密、越来越响
        span = M['roll'] * BAR
        start = sc['t0'] + sc['d'] - span
        x = start
        while x < sc['t0'] + sc['d'] - 1e-3:
            k = (x - start) / span
            if t0 <= x < t0 + BAR:
                snare(x, .35 + .75 * k)
            x += BEAT / (2 if k < .5 else 4 if k < .8 else 8)
    h = M.get('hat')
    if h:
        step = BEAT / (4 if h == '16' else 2)
        for k in range(int(BAR / step)):
            x = t0 + k * step
            off = h.endswith('o') and k % 2 == 1
            hat(x, (.8 if k % 2 else .55) * (1.2 if off else 1), open_=off, pan=.25 if k % 2 else -.15)
    if M.get('bass'):
        n = M['bass']
        for k in range(n):
            bass(t0 + k * BAR / n, root + (12 if k % 2 else 0), BAR / n * .9, 1.0, 700 if n == 16 else 900)
    if M.get('arp'):
        pat = [0, 1, 2, 1, 2, 0, 1, 2, 0, 2, 1, 2, 0, 1, 2, 1]
        for k in range(16):
            m = chord[pat[k]] + (12 if k % 8 >= 4 else 0) + 12
            pluck(t0 + k * BEAT / 4, m, M['arp'] * (1 if k % 4 == 0 else .7), pan=(-.35 if k % 2 else .35))
    if M.get('bells'):
        for k in range(4):
            bell(t0 + k * BEAT, chord[k % 3] + 24, .6, 1.6, pan=(k - 1.5) * .3)
    if M.get('lead'):
        phrase = LEAD[(b // 2) % 2]
        for beat, m, dur in phrase:
            if (b % 2) * 4 <= beat < (b % 2) * 4 + 4:
                lead(t0 + (beat - (b % 2) * 4) * BEAT, m, dur * BEAT, M['lead'])
# 最后一个长和弦
pad(TOTAL - .5, PROG[0][1], 2.0, .6, 1000)

for c in CUES['sfx']:
    sfx(c)

# ------------------------------------------------------------------------------------------- 混音
t = np.arange(N) / SR
duck = np.ones(N, F32)                     # 侧链：底鼓一响，Pad / 贝斯 / 琶音让开
for k in KICKS:
    i = int(k * SR)
    n = min(int(.35 * SR), N - i)
    duck[i:i + n] = np.minimum(duck[i:i + n], 1 - .7 * np.exp(-np.arange(n) / SR / .09))
for k in ('bass', 'pad', 'arp'):
    BUS[k] *= duck

# 延迟：附点八分音符，带反馈
d = int(BEAT * .75 * SR)
dl = np.zeros_like(BUS['dly'])
x = BUS['dly'].copy()
for i in range(1, 6):
    g = .42 ** i
    src = lp(x[(i + 1) % 2], 3000)            # 左右交替（乒乓）
    dl[i % 2, d * i:] += src[:N - d * i] * g
# 混响：指数衰减噪声当冲击响应
L = int(2.4 * SR)
ir_t = np.arange(L) / SR
ir = np.stack([lp(noise(L), 6000) * np.exp(-ir_t / .55), lp(noise(L), 6000) * np.exp(-ir_t / .55)])
ir /= np.abs(ir).sum(axis=1, keepdims=True) ** .5 * 40
vb = np.stack([fftconvolve(BUS['verb'][c] + dl[c] * .3, ir[c])[:N] for c in range(2)]).astype(F32)

mix = (BUS['drm'] * 1.0 + BUS['bass'] * .9 + BUS['pad'] * .9 + BUS['arp'] * .85 + BUS['lead'] * .9 + BUS['sfx'] * 1.0
       + dl * .5 + vb * .9)
mix = np.stack([hp(mix[0], 25), hp(mix[1], 25)])
end = int(TOTAL * SR)
fade = int(1.5 * SR)
mix[:, end - fade:end] *= np.linspace(1, 0, fade)
mix[:, end:] = 0
mix = mix[:, :end]
mix = np.tanh(mix / np.abs(mix).max() * 1.6) * .89                  # 软限幅
os.makedirs(os.path.join(here, 'output'), exist_ok=True)
raw = os.path.join(here, 'output', 'music_raw.wav')
with wave.open(raw, 'wb') as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes((mix.T * 32767).astype(np.int16).tobytes())
wav = os.path.join(here, 'output', 'music.wav')
subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', raw, '-af', 'loudnorm=I=-14:TP=-1:LRA=11', '-ar', str(SR), wav], check=True)
subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', wav, '-c:a', 'libmp3lame', '-b:a', '192k', os.path.join(here, 'music.mp3')], check=True)
os.remove(raw)
print(f'写出 output/music.wav、music.mp3（{TOTAL:.1f}s，{len(CUES["sfx"])} 个音效）')
