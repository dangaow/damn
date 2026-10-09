"""片尾音轨：原曲 + 片尾（伴奏桥段降 2 个半音，像录像带转慢：变调也变速），输出完整音轨。

    python3 credits_audio.py      → output/谎话_完整音轨.wav（44.1k 立体声）

片尾用伴奏 stems/beat.mp3 的第 24–33 小节（1:02.61–1:25.59，没有底鼓、只有军鼓）。
- 降调：整体放慢到 2^(-2/12) ≈ 0.891 倍，一小节从 2.553 秒变成 2.866 秒，9 小节约 25.8 秒
- 开头 0.4 秒像磁带刚转起来：速度从 0.3 倍滑到正常（之后的小节线不受影响，和画面对得上）
- 高频收一点，像隔着玻璃听（音量本身就比片尾倒数那段小 2–3 dB）
- 第 31 小节（原曲点题后鼓回来那一下）开始渐出，到片尾结束时无声
画面那边的时间（js/scenes.js 的 CRED_*）和这里用同一套算法，改这里的参数要同步改那边。
"""
import json, os, subprocess
import numpy as np
from scipy.signal import resample_poly

HERE = os.path.dirname(os.path.abspath(__file__))
SR = 44100
D = json.load(open(os.path.join(HERE, 'data.json')))
BAR = D['step'] * 16
bar = lambda i: D['bar0'] + i * BAR
S0, S1 = bar(24), bar(33)                  # 伴奏里取的这一段
UP, DOWN = 110, 98                         # 110/98 ≈ 2^(2/12)：每个样本拉长 → 播放时慢、低 2 个半音
SPIN = .4                                  # 磁带转起来用的秒数
FADE_BAR = 7                               # 从第几小节（片尾里数）开始渐出
GAIN_DB = 0


def load(path):
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-f', 'f32le', '-ac', '2', '-ar', str(SR), '-'], capture_output=True, check=True).stdout
    return np.frombuffer(raw, np.float32).reshape(-1, 2).astype(np.float64)


song = load(os.path.join(HERE, 'song.mp3'))
beat = load(os.path.join(HERE, 'stems', 'beat.mp3'))
seg = beat[int(round(S0 * SR)):int(round(S1 * SR))]
slow = resample_poly(seg, UP, DOWN, axis=0)          # 放慢后的片尾（时长 × 110/98）

# 磁带转起来：前 SPIN 秒按变速读 slow，速度 0.3 → 1；SPIN 秒之后和匀速读完全对齐
n_spin = int(SPIN * SR)
u = np.arange(n_spin) / n_spin
rate = .3 + .7 * (1 - (1 - u) ** 2)
pos = n_spin - np.cumsum(rate[::-1])[::-1]           # 读到第 n_spin 个样本时正好追上匀速的位置
pos = np.clip(pos, 0, None)
head = np.stack([np.interp(pos, np.arange(len(slow)), slow[:, ch]) for ch in range(2)], axis=1)
cred = np.concatenate([head, slow[n_spin:]])
cred[:256] *= np.linspace(0, 1, 256)[:, None]        # 起头去爆音

# 渐出：第 FADE_BAR 小节起到结尾
L = len(cred) / SR
fade0 = FADE_BAR * BAR * UP / DOWN
t = np.arange(len(cred)) / SR
g = np.clip(1 - (t - fade0) / (L - fade0), 0, 1)
cred *= ((g * g * (3 - 2 * g)) * 10 ** (GAIN_DB / 20))[:, None]

tmp = os.path.join(HERE, 'output', '_credits_raw.wav'); os.makedirs(os.path.dirname(tmp), exist_ok=True)
out = os.path.join(HERE, 'output', '谎话_完整音轨.wav')
full = np.concatenate([song, cred, np.zeros((int(.6 * SR), 2))])   # 片尾后黑屏留 0.6 秒
pcm = (np.clip(full, -1, 1) * 32767).astype('<i2')
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 's16le', '-ar', str(SR), '-ac', '2', '-i', '-', tmp], input=pcm.tobytes(), check=True)
# 只给片尾那段收高频（原曲部分原样）
c0 = len(song) / SR
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', tmp, '-af', f"highshelf=f=4500:g=-5:enable='gte(t,{c0:.4f})',lowpass=f=11000:enable='gte(t,{c0:.4f})'",
                '-c:a', 'pcm_s16le', out], check=True)
os.remove(tmp)
print(f'原曲 {c0:.3f}s + 片尾 {L:.3f}s + 黑 0.6s = {len(full) / SR:.3f}s → {out}')
print(f'片尾小节长 {BAR * UP / DOWN:.4f}s，渐出起点 {fade0:.3f}s（片尾内）')
