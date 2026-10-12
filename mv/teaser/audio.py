"""《谎话》竖屏预告的音频：15 秒，朦胧。

    python3 teaser/audio.py          → output/teaser_audio.wav（48k 立体声）

取原歌「大雨」那段（1:13.2 起）：放慢 8%（像磁带走慢了，音高也低一点）、磁带抖动、
一直闷着（低通，像隔着墙和雨在听）、长混响；底下一层合成的雨声、磁带底噪和噼啪声。
唱到「谎」那一刻滤波一下子全开（全片唯一清楚的声音），「话」拖着的时候磁带卡住停下，只剩雨声。
"""
import os, subprocess
import numpy as np
import scipy.signal as ss

HERE = os.path.dirname(os.path.abspath(__file__)); MV = os.path.dirname(HERE)
SR = 48000
DUR = 15.0
S0 = 73.2                 # 原歌从这里开始取
M0 = 1.2                  # 音乐在预告第几秒进来（前面只有雨）
SPEED = 0.92              # 放慢 8%
T_HUANG = 81.39           # 原歌「谎」
T_STOP = 11.0             # 预告第几秒磁带开始卡住
STOP_LEN = .65
rng = np.random.default_rng(17)

def out_t(src): return M0 + (src - S0) / SPEED          # 原歌时间 → 预告时间

def load(start, dur):
    raw = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', os.path.join(MV, 'song.mp3'), '-ss', str(start), '-t', str(dur),
                                   '-f', 'f32le', '-ac', '2', '-ar', str(SR), '-'])
    return np.frombuffer(raw, np.float32).reshape(-1, 2).astype(np.float64)

def lp(x, f, order=4): return ss.sosfilt(ss.butter(order, f, 'low', fs=SR, output='sos'), x, axis=0)
def hp(x, f, order=2): return ss.sosfilt(ss.butter(order, f, 'high', fs=SR, output='sos'), x, axis=0)
def bp(x, a, b, order=2): return ss.sosfilt(ss.butter(order, [a, b], 'band', fs=SR, output='sos'), x, axis=0)
def env(t, pts):                                          # 分段线性包络 [(时间, 值), ...]
    ts, vs = zip(*pts); return np.interp(t, ts, vs)

N = int(DUR * SR); t = np.arange(N) / SR

# ---------------------------------------------------------------- 音乐：取段、放慢、磁带抖动
src_len = (T_STOP + STOP_LEN + .5 - M0) * SPEED
x = load(S0, src_len)
x = ss.resample_poly(x, 25, 23, axis=0)                   # 25/23 ≈ 1/0.92：变长、变低
tm = np.arange(len(x)) / SR
wow = .0024 * np.sin(2 * np.pi * .55 * tm) + .0004 * np.sin(2 * np.pi * 6.3 * tm + 1.1) + .0002 * np.sin(2 * np.pi * 11.7 * tm)
pos = np.clip(tm - wow - .003, 0, tm[-1])
x = np.stack([np.interp(pos, tm, x[:, c]) for c in range(2)], 1)
music = np.zeros((N, 2)); i0 = int(M0 * SR); n = min(len(x), N - i0); music[i0:i0 + n] = x[:n]
music = hp(music, 140)                                     # 手机外放：低频收一点，不发闷

# ---------------------------------------------------------------- 闷：两档低通之间慢慢变化，「谎」那一刻全开
t_open = out_t(T_HUANG) - .03
lo, mid = lp(music, 620), lp(music, 1500)
w_mid = np.clip(env(t, [(0, .1), (6, .2), (t_open - .2, .6), (t_open, .6)]) + .08 * np.sin(2 * np.pi * .23 * t), 0, 1)[:, None]
w_dry = env(t, [(0, 0), (t_open, 0), (t_open + .09, 1), (DUR, 1)])
music = (lo * (1 - w_mid) + mid * w_mid) * (1 - w_dry[:, None]) + music * w_dry[:, None]
music *= env(t, [(0, 0), (M0, 0), (M0 + .9, 1), (DUR, 1)])[:, None]

# ---------------------------------------------------------------- 远：长混响（合成的暗色冲激响应），「谎话」那一刻干声多一点
ir_n = int(3.6 * SR); ti = np.arange(ir_n) / SR
ir = rng.standard_normal((ir_n, 2)) * np.exp(-ti * 6.9 / 3.2)[:, None]
ir = lp(ir, 2600); ir[:int(.035 * SR)] = 0; ir /= np.sqrt((ir ** 2).sum(0))
wet = np.stack([ss.fftconvolve(music[:, c], ir[:, c])[:N] for c in range(2)], 1)
wet_g = env(t, [(0, .9), (t_open, .9), (t_open + .1, .45), (DUR, .45)])
dry_g = env(t, [(0, .55), (t_open, .55), (t_open + .1, 1.0), (DUR, 1.0)])
bus = music * dry_g[:, None] + wet * wet_g[:, None]

# ---------------------------------------------------------------- 磁带卡住：播放速度从 1 降到 0（音高一路往下掉），然后没了
i_s = int(T_STOP * SR); k = np.arange(N - i_s) / SR
rate = np.clip(1 - k / STOP_LEN, 0, 1) ** 1.6
p = T_STOP + np.cumsum(rate) / SR
y_stop = np.stack([np.interp(p, t, bus[:, c]) for c in range(2)], 1) * np.clip(1 - (k - STOP_LEN + .12) / .12, 0, 1)[:, None]
bus[i_s:] = y_stop

# ---------------------------------------------------------------- 雨（隔着玻璃）：沙沙底噪 + 细碎的雨点 + 偶尔一滴大的
hiss = bp(rng.standard_normal((N, 2)), 500, 6500) * (.85 + .15 * np.sin(2 * np.pi * .17 * t[:, None] + np.array([0, 1.3])))
drops = np.zeros((N, 2))
for _ in range(int(DUR * 140)):
    i = rng.integers(0, N - 400); a = rng.uniform(.2, 1) ** 2; pan = rng.uniform(.2, .8)
    drops[i] += a * np.array([1 - pan, pan])
drops = bp(drops, 1200, 7000) * 6
big = np.zeros((N, 2))
for _ in range(int(DUR * 2.5)):
    i = rng.integers(0, N - 4000); pan = rng.uniform(.25, .75); big[i] += rng.uniform(.5, 1) * np.array([1 - pan, pan])
big = ss.lfilter([1], [1, -.995], bp(big, 300, 1400), axis=0) * .08
rumble = lp(np.cumsum(rng.standard_normal((N, 2)), 0) * .002, 180)
rumble -= lp(rumble, 20)
rain = lp(hiss * .05 + drops * .02 + big + rumble * .6, 5200)
rain *= env(t, [(0, 0), (.7, 1), (M0, 1), (M0 + 1.2, .55), (T_STOP, .55), (T_STOP + .4, 1), (DUR - .9, 1), (DUR, 0)])[:, None]

# ---------------------------------------------------------------- 磁带底噪 + 噼啪
tape = hp(rng.standard_normal((N, 2)), 3500) * .004
crk = np.zeros((N, 2))
for _ in range(int(DUR * 3)):
    i = rng.integers(0, N - 10); crk[i:i + 3] += rng.uniform(.1, .5) * rng.choice([-1, 1])
crk = lp(crk, 4500) * .5
clunk = np.zeros((N, 2)); ic = int((T_STOP + STOP_LEN) * SR); clunk[ic:ic + 1] = .9
clunk = lp(ss.lfilter([1], [1, -.97], clunk, axis=0), 400) * .12                    # 磁带停住时机器"咔"的一下
noise = (tape + crk) * env(t, [(0, 0), (.4, 1), (DUR - .6, 1), (DUR, 0)])[:, None]

mix = bus * .9 + rain + noise + clunk
mix /= np.max(np.abs(mix)) / .89
out = os.path.join(MV, 'output', 'teaser_audio_raw.wav'); os.makedirs(os.path.dirname(out), exist_ok=True)
import wave
with wave.open(out, 'wb') as f:
    f.setnchannels(2); f.setsampwidth(2); f.setframerate(SR)
    f.writeframes((np.clip(mix, -1, 1) * 32767).astype('<i2').tobytes())
subprocess.check_call(['ffmpeg', '-v', 'error', '-y', '-i', out, '-af', 'loudnorm=I=-15:TP=-1.2:LRA=11', '-ar', str(SR), os.path.join(MV, 'output', 'teaser_audio.wav')])
os.remove(out)
print(f'「我」{out_t(73.7):.2f}s  「谎」{out_t(T_HUANG):.2f}s  「话」{out_t(81.71):.2f}s  磁带停 {T_STOP}–{T_STOP + STOP_LEN:.2f}s')
