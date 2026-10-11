"""配乐和声音设计：D 小调，80 BPM，按 sea.html 的段落表编曲。

    node export.mjs --cues cues.json     （导出段落表、海浪强度和音效点）
    python music.py                      -> output/music.wav（导出用）、music.mp3（播放器用）

全部用 numpy / scipy 算出来：弦乐、合唱（共振峰滤波）、钢琴、钢片琴、定音鼓、太鼓、低音铜管 braam、主旋律；
海浪（按每段的 surf 强度起伏）、雨、雷、鲸歌、入水、浪花拍岸等音效。最后混响、限幅，ffmpeg 响度标准化到 -14 LUFS。
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
BEAT = 60 / CUES['bpm']
BAR = CUES['bar']
TOTAL = CUES['total']
N = int((TOTAL + 3) * SR)
NBARS = int(round(TOTAL / BAR))
F32 = np.float32
rng = np.random.default_rng(11)
BUS = {k: np.zeros((2, N), F32) for k in ('mus', 'drm', 'sfx', 'amb', 'verb')}


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


def put(sig, t0, bus, gain=1.0, pan=0.0, verb=0.0):
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


def env(n, a=.005, d=.2, s=0.0, r=.05, hold=None):
    t = np.arange(n) / SR
    hold = n / SR - r if hold is None else hold
    e = np.where(t < a, t / max(a, 1e-4), s + (1 - s) * np.exp(-(t - a) / max(d, 1e-4)))
    return (e * np.clip((hold + r - t) / max(r, 1e-4), 0, 1)).astype(F32)


def saw(f, t, det=0.0):
    return (2 * ((f * (1 + det) * t) % 1.0) - 1).astype(F32)


# ------------------------------------------------------------------------------------------- 乐器
def strings(t0, notes, dur, g=1.0, cut=2200, att=.8):
    t = tv(dur + 1.2)
    L = np.zeros(len(t), F32)
    R = np.zeros(len(t), F32)
    for m in notes:
        f = midi(m)
        vib = 1 + .003 * np.sin(2 * np.pi * 5.2 * t + m)
        for k, det in enumerate((-.006, -.002, .002, .006)):
            ph = np.cumsum(f * (1 + det) * vib) / SR
            s = (2 * (ph % 1) - 1).astype(F32)
            if k % 2:
                L += s
            else:
                R += s
    e = env(len(t), att, 2, .9, 1.2, hold=dur)
    put(np.stack([lp(L, cut), lp(R, cut)]) * e / (len(notes) * 2), t0, 'mus', g * .16, verb=.7)


VOWEL = [(730, 1090, 2440), (570, 840, 2410), (300, 870, 2240)]     # a / o / u


def choir(t0, notes, dur, g=1.0, vowel=0, att=1.2):
    t = tv(dur + 1.5)
    out = np.zeros((2, len(t)), F32)
    for j, m in enumerate(notes):
        f = midi(m)
        for c, det in enumerate((-.004, .004)):
            ph = np.cumsum(f * (1 + det) * (1 + .004 * np.sin(2 * np.pi * (4.6 + j * .3) * t))) / SR
            src = (2 * (ph % 1) - 1).astype(F32)
            v = sum(bp(src, fm * .9, fm * 1.1) * w for fm, w in zip(VOWEL[vowel], (1, .6, .25)))
            out[c] += v
    e = env(len(t), att, 2, .9, 1.5, hold=dur)
    put(out * e / len(notes), t0, 'mus', g * .5, verb=1.0)


def piano(t0, m, g=1.0, pan=0.0):
    t = tv(3.5)
    f = midi(m)
    s = sum(np.sin(2 * np.pi * f * k * (1 + .0004 * k * k) * t) * np.exp(-t * (1.1 + k * .7)) / k for k in range(1, 7))
    s = s * (1 - np.exp(-t / .002)) + hp(noise(len(t)), 2000) * np.exp(-t / .01) * .02
    put(s, t0, 'mus', g * .16, pan=pan, verb=.6)


def celesta(t0, m, g=1.0, pan=0.0):
    t = tv(2.2)
    f = midi(m)
    s = (np.sin(2 * np.pi * f * t) + .3 * np.sin(2 * np.pi * f * 4 * t) * np.exp(-t * 6)) * np.exp(-t * 1.6)
    put(s, t0, 'mus', g * .07, pan=pan, verb=.8)


def pluck(t0, m, g=1.0, pan=0.0):
    t = tv(.5)
    f = midi(m)
    s = np.sin(2 * np.pi * f * t + 1.2 * np.exp(-t * 20) * np.sin(2 * np.pi * f * 2 * t)) * np.exp(-t * 9)
    put(s, t0, 'mus', g * .08, pan=pan, verb=.35)


def lead(t0, m, dur, g=1.0):
    t = tv(dur + .4)
    f = midi(m) * (1 + .005 * np.sin(2 * np.pi * 5 * t) * np.clip((t - .2) / .3, 0, 1))
    ph = np.cumsum(f) / SR
    s = (2 * (ph % 1) - 1) * .6 + (2 * ((ph * 1.003) % 1) - 1) * .4
    put(lp(s.astype(F32), 1700, 2) * env(len(t), .12, .5, .8, .35, hold=dur), t0, 'mus', g * .1, verb=.7)


def spic(t0, m, g=1.0):
    t = tv(.22)
    s = lp(saw(midi(m), t) + saw(midi(m), t, .004), 1400) * env(len(t), .004, .07, 0, .03)
    put(s, t0, 'mus', g * .14, pan=-.2, verb=.2)


def timp(t0, m=38, g=1.0):
    t = tv(2.2)
    f = midi(m) * (1 + .15 * np.exp(-t / .05))
    s = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / .7) + lp(noise(len(t)), 600) * np.exp(-t / .05) * .5
    put(s, t0, 'drm', g * .5, verb=.5)


def taiko(t0, g=1.0, pan=0.0):
    t = tv(.8)
    f = 62 + 50 * np.exp(-t / .03)
    s = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / .22) + lp(noise(len(t)), 1200) * np.exp(-t / .03) * .6
    put(np.tanh(s * 1.5), t0, 'drm', g * .55, pan=pan, verb=.35)


def braam(t0, m, dur=2.6, g=1.0):
    t = tv(dur)
    s = sum(saw(midi(m + o), t, d) for o in (0, -12) for d in (-.008, -.003, .003, .008)) / 8
    k = np.clip(t / .25, 0, 1) * np.exp(-np.clip(t - .25, 0, None) / 1.2)
    seg = int(.05 * SR)
    out = np.zeros(len(t), F32)
    for i in range(0, len(t), seg):       # 截止频率随包络扫动：一块一块滤
        c = 180 + 1400 * k[min(i + seg // 2, len(t) - 1)]
        out[i:i + seg] = lp(s[max(0, i - 2048):i + seg], c, 2)[-len(s[i:i + seg]):]
    put(np.tanh(out * 2.2) * env(len(t), .02, 1.5, .7, .6), t0, 'mus', g * .28, verb=.6)


def sub(t0, m, dur, g=1.0):
    t = tv(dur)
    put(np.sin(2 * np.pi * midi(m) * t) * env(len(t), .4, 1, .9, .6), t0, 'mus', g * .22)


# ------------------------------------------------------------------------------------------- 音效
def sfx(c):
    t0, name = c['t'], c['name']
    if name == 'type':
        t = tv(.04)
        put(hp(noise(len(t)), 2500) * np.exp(-t / .007), t0, 'sfx', .18, pan=(c.get('n', 0) - 3) * .08)
    elif name == 'piano':
        piano(t0, c['m'], 1.3)
        piano(t0, c['m'] - 12, .6)
    elif name == 'bell':
        celesta(t0, c['m'], 2.2)
        celesta(t0 + .01, c['m'] + 12, .8)
    elif name in ('swell', 'riser'):
        d = c.get('dur', 2.0)
        t = tv(d)
        k = t / d
        x = noise(len(t))
        s = (lp(x, 1200) * (1 - k) + hp(x, 2500) * k) * k ** 2.2
        if name == 'riser':
            s += np.sin(2 * np.pi * np.cumsum(150 * (6 ** k)) / SR) * k ** 2 * .3
        put(s, t0, 'sfx', .35, verb=.6)
    elif name == 'hit':
        g = c.get('k', 1.0)
        timp(t0, 38, 1.4 * g)
        t = tv(3)
        s = lp(noise(len(t)), 6000) * np.exp(-t / .5) * .25 + np.sin(2 * np.pi * np.cumsum(30 + 30 * np.exp(-t / .1)) / SR) * np.exp(-t / .9)
        put(s, t0, 'sfx', .5 * g, verb=.8)
    elif name == 'whoosh':
        t = tv(1.0)
        k = t / 1.0
        x = noise(len(t))
        put((lp(x, 700) * (1 - k) + bp(x, 1500, 5000) * k) * np.sin(np.pi * k) ** 2, t0 - .5, 'sfx', .3, verb=.4)
    elif name == 'tick':
        t = tv(.3)
        put(np.sin(2 * np.pi * 1760 * t) * np.exp(-t * 18), t0, 'sfx', .12, verb=.5)
    elif name == 'choir':
        choir(t0, [62, 69, 74, 77], c.get('dur', 6), 1.0, 0, att=2)
    elif name == 'chime':
        for i, m in enumerate((86, 89, 93, 98)):
            celesta(t0 + i * .12, m, 1.8, pan=(i - 1.5) * .35)
    elif name == 'thunder':
        g = c.get('k', 1.0)
        t = tv(5.5)
        x = noise(len(t))
        roll = lp(np.abs(noise(len(t))), 4) * 6
        s = lp(x, 260, 3) * (np.exp(-t / 1.6) * (.5 + roll)) * 3 + hp(x, 1500) * np.exp(-t / .06) * 1.2 + lp(x, 900) * np.exp(-t / .25)
        put(np.tanh(s), t0, 'sfx', .6 * g, verb=.5)
    elif name == 'splash':
        t = tv(1.6)
        s = lp(noise(len(t)), 5000) * np.exp(-t / .25)
        for i in range(25):
            d = .05 + rng.random() * 1.2
            tb = tv(.06)
            f0 = 400 + rng.random() * 1200
            b = np.sin(2 * np.pi * np.cumsum(f0 * (1 + tb * 8)) / SR) * np.exp(-tb / .015)
            put(b, t0 + d, 'sfx', .1, pan=rng.random() - .5)
        put(s, t0, 'sfx', .55, verb=.4)
    elif name == 'whale':
        d = c.get('dur', 4)
        t = tv(d)
        k = t / d
        base = 110 if c.get('lo') else 260
        f = base * (1 + .9 * np.sin(np.pi * k) ** 2 - .3 * k) * (1 + .01 * np.sin(2 * np.pi * 6 * t))
        ph = np.cumsum(f) / SR
        s = (np.sin(2 * np.pi * ph) + .4 * np.sin(4 * np.pi * ph) + .2 * np.sin(6 * np.pi * ph)) * np.sin(np.pi * k) ** 1.5
        put(lp(s.astype(F32), 1500), t0, 'sfx', .22, pan=-.3, verb=1.4)
    elif name == 'wave':
        t = tv(4.5)
        x = noise(len(t))
        e = np.clip(t / .5, 0, 1) ** 2 * np.exp(-np.clip(t - .5, 0, None) / 1.1)
        s = (lp(x, 700) * .9 + bp(x, 1000, 6000) * np.exp(-np.clip(t - .4, 0, None) / .6) * .6) * e
        put(s, t0, 'amb', .5, pan=.35, verb=.3)


# ------------------------------------------------------------------------------------------- 编曲
PROG = [(38, (62, 65, 69)), (34, (62, 65, 70)), (41, (60, 65, 69)), (36, (60, 64, 67))]     # Dm  Bb  F  C
MEL = [[(0, 74, 2), (2, 72, 1), (3, 69, 1), (4, 70, 2), (6, 69, 1), (7, 65, 1)],
       [(0, 72, 1.5), (1.5, 74, .5), (2, 76, 2), (4, 77, 1.5), (5.5, 76, .5), (6, 74, 2)]]
SCN = CUES['scenes']


def scene_at(t):
    s = SCN[0]
    for x in SCN:
        if t >= x['t0'] - 1e-6:
            s = x
    return s


for b in range(NBARS):
    t0 = b * BAR
    sc = scene_at(t0 + .01)
    mode, lt = sc['mus'], t0 - sc['t0']
    root, ch = PROG[b % 4]
    ch = list(ch)
    up = [m + 12 for m in ch]
    if mode == 'cold':
        strings(t0, [root + 12, ch[0]], BAR, .35, 700, 1.5)
    elif mode == 'words':
        strings(t0, ch, BAR, .6, 1400, 1.2)
        for k in range(8):
            piano(t0 + k * BEAT / 2, [ch[0], ch[1], ch[2], ch[1] + 12][k % 4] + 12, .55 if k % 2 else .8, pan=(k % 4 - 1.5) * .2)
        sub(t0, root, BAR, .6)
    elif mode == 'math':
        strings(t0, ch, BAR, .5, 1200)
        for k in range(16):
            pluck(t0 + k * BEAT / 4, up[k % 3] + (12 if k % 8 >= 6 else 0), .7 + .3 * (k % 4 == 0), pan=(-.4 if k % 2 else .4))
        for k in range(4):
            taiko(t0 + k * BEAT, .25 + .1 * (k == 0))
    elif mode == 'dawn':
        strings(t0, ch + [ch[0] + 12], BAR, 1.0, 3000, .6)
        choir(t0, [ch[0], ch[2], ch[0] + 12], BAR, .7)
        sub(t0, root, BAR, 1)
        timp(t0, root + 12, .7)
        if lt >= BAR * 2:
            for beat, m, d in MEL[(b // 2) % 2]:
                if (b % 2) * 4 <= beat < (b % 2) * 4 + 4:
                    lead(t0 + (beat - (b % 2) * 4) * BEAT, m, d * BEAT, .9)
    elif mode == 'day':
        strings(t0, ch + [ch[2] + 12], BAR, .9, 3200, .4)
        choir(t0, [ch[0] + 12, ch[2] + 12], BAR, .45)
        for k in range(8):
            spic(t0 + k * BEAT / 2, root + 12 + (12 if k % 2 else 0), .9)
        for x, g in ((0, 1), (1.5, .6), (2, .8), (3, .7), (3.5, .5)):
            taiko(t0 + x * BEAT, g * .8, pan=(x - 2) * .15)
        for beat, m, d in MEL[(b // 2) % 2]:
            if (b % 2) * 4 <= beat < (b % 2) * 4 + 4:
                lead(t0 + (beat - (b % 2) * 4) * BEAT, m, d * BEAT, 1.0)
        sub(t0, root, BAR, .8)
    elif mode == 'storm':
        strings(t0, [ch[0] - 12, ch[0], ch[1]], BAR, .9, 1600, .2)
        choir(t0, [ch[0] - 12, ch[0], ch[2]], BAR, .7, vowel=1)
        braam(t0, root + 12, 2.6, 1.0)
        for k in range(16):
            spic(t0 + k * BEAT / 4, root + 12 + (7 if k % 8 == 6 else 0), .8 + .4 * (k % 4 == 0))
        for x, g in ((0, 1.2), (.5, .5), (1, .8), (1.5, .6), (2, 1.1), (2.75, .6), (3, .9), (3.5, .8)):
            taiko(t0 + x * BEAT, g, pan=(x - 2) * .2)
        sub(t0, root, BAR, 1)
        if lt >= sc['d'] - BAR:
            for k in range(16):
                timp(t0 + k * BEAT / 4, 38, .3 + .5 * k / 16)
    elif mode == 'night':
        strings(t0, ch, BAR, .55, 1100, 1.5)
        choir(t0, [ch[0] + 12, ch[2] + 12], BAR, .35, vowel=2, att=2)
        for k in range(8):
            celesta(t0 + k * BEAT / 2, up[[0, 1, 2, 1, 2, 0, 2, 1][k]] + 12, .9 if k % 2 == 0 else .6, pan=(k % 4 - 1.5) * .3)
        sub(t0, root, BAR, .5)
    elif mode == 'dive':
        under = lt + BAR > (sc.get('under') or 0)
        strings(t0, [root, root + 7, ch[0]], BAR, .7, 380 if under else 1600, 1.0)
        sub(t0, root - 12 + 12, BAR, 1.0)
        for k in (0, 2):                         # 低沉的心跳
            taiko(t0 + k * BEAT, .25)
            taiko(t0 + k * BEAT + .28, .15)
        if lt >= BAR * 4:
            for k in range(4):
                celesta(t0 + k * BEAT + .1, up[k % 3] + 12, .35, pan=(k - 1.5) * .4)
    elif mode == 'space':
        strings(t0, ch + [ch[0] + 12, ch[2] + 12], BAR, .9, 2600, 1.2)
        choir(t0, [ch[0], ch[2], ch[0] + 12], BAR, .9, att=1.5)
        sub(t0, root, BAR, .8)
        for k in range(4):
            celesta(t0 + k * BEAT, up[(k + b) % 3] + 24, .5, pan=(k - 1.5) * .4)
    elif mode == 'shore':
        last = b == NBARS - 1
        strings(t0, ch, BAR * (2 if last else 1), .6, 1500, 1.2)
        for k in range(8 if not last else 1):
            piano(t0 + k * BEAT / 2, [ch[0], ch[1], ch[2], ch[1] + 12][k % 4] + 12, .5 if k % 2 else .75, pan=(k % 4 - 1.5) * .2)
        if last:
            piano(t0, 62, 1)
            piano(t0, 57, .7)
            piano(t0, 50, .7)
        sub(t0, root, BAR, .5)

for c in CUES['sfx']:
    sfx(c)

# ------------------------------------------------------------------------------------------- 海浪 / 雨 的环境声
t = np.arange(N) / SR
surf = np.zeros(N, F32)
rain = np.zeros(N, F32)
underw = np.zeros(N, F32)
for s in SCN:
    i0, i1 = int(s['t0'] * SR), int((s['t0'] + s['d']) * SR)
    surf[i0:i1] = s['surf']
    rain[i0:i1] = s['rain']
    if s['under'] is not None:
        j = int((s['t0'] + s['under']) * SR)
        underw[j:i1] = 1
        surf[j:i1] = 0
k = int(1.2 * SR)
sm = lambda x: np.convolve(x, np.ones(k) / k, mode='same').astype(F32)
surf, rain, underw = sm(surf), sm(rain), sm(underw)
swell = .55 + .45 * np.clip(np.sin(2 * np.pi * t / 7.3) * .6 + np.sin(2 * np.pi * t / 11.1 + 1) * .4, -1, 1)
nz = np.stack([noise(N), noise(N)])
brown = np.stack([lp(nz[c], 500, 2) * 2.5 for c in range(2)])
hiss = np.stack([bp(nz[c], 900, 5000) for c in range(2)])
BUS['amb'] += (brown * (surf * swell) + hiss * (surf * swell ** 3) * .35) * .35
BUS['amb'] += np.stack([lp(hp(nz[1 - c], 1800), 7000) for c in range(2)]) * rain * .05
BUS['amb'] += np.stack([lp(nz[c], 220, 3) * 3 for c in range(2)]) * underw * .4

# ------------------------------------------------------------------------------------------- 混音
L_ = int(3.6 * SR)
irt = np.arange(L_) / SR
ir = np.stack([lp(noise(L_), 5000) * np.exp(-irt / .9), lp(noise(L_), 5000) * np.exp(-irt / .9)])
ir /= np.sqrt((ir ** 2).sum(axis=1, keepdims=True)) * 9
vb = np.stack([fftconvolve(BUS['verb'][c], ir[c])[:N] for c in range(2)]).astype(F32)
mix = BUS['mus'] * 1.0 + BUS['drm'] * .9 + BUS['sfx'] * .9 + BUS['amb'] * 1.0 + vb * .8
mix = np.stack([hp(mix[0], 28), hp(mix[1], 28)])
end = int(TOTAL * SR)
fade = int(2.5 * SR)
mix[:, end - fade:end] *= np.linspace(1, 0, fade) ** 1.5
mix = mix[:, :end]
mix = np.tanh(mix / np.abs(mix).max() * 1.5) * .9
os.makedirs(os.path.join(here, 'output'), exist_ok=True)
raw = os.path.join(here, 'output', 'music_raw.wav')
with wave.open(raw, 'wb') as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes((mix.T * 32767).astype(np.int16).tobytes())
wav = os.path.join(here, 'output', 'music.wav')
subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', raw, '-af', 'loudnorm=I=-15:TP=-1:LRA=14', '-ar', str(SR), wav], check=True)
subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', wav, '-c:a', 'libmp3lame', '-b:a', '192k', os.path.join(here, 'music.mp3')], check=True)
os.remove(raw)
print(f'写出 output/music.wav、music.mp3（{TOTAL:.1f}s，{len(CUES["sfx"])} 个音效）')
