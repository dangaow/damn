# 《鸡蛋和可乐》demo：生成 FL Studio 能直接拖进去的多轨 MIDI，和一段能先听的预览（简单合成器，只是给听结构和旋律）。
# 用法：python gen.py OUT_DIR   → OUT_DIR/鸡蛋和可乐_demo.mid、OUT_DIR/鸡蛋和可乐_demo_preview.wav
import os, sys
import numpy as np

BPM, SR = 88, 44100
SPB = 60.0 / BPM                       # 每拍秒数
OUT = sys.argv[1] if len(sys.argv) > 1 else '.'
os.makedirs(OUT, exist_ok=True)

# —— 和弦（钢琴的排列，和低音根音）————————————————————————————————————————
CH = {
    'Abmaj7': ([56, 60, 63, 67], 44), 'Bb': ([58, 62, 65, 67], 46), 'Gm7': ([55, 58, 62, 65], 43), 'Cm7': ([55, 58, 60, 63], 36),
    'Fm7': ([56, 60, 63, 65], 41), 'Bbsus': ([58, 63, 65, 70], 46), 'Ebmaj7': ([55, 58, 62, 63], 39), 'Bb/D': ([53, 58, 62, 65], 38),
}
PROG = {'V': ['Abmaj7', 'Bb', 'Gm7', 'Cm7'], 'P': ['Fm7', 'Gm7', 'Abmaj7', 'Bbsus'], 'C': ['Ebmaj7', 'Bb/D', 'Cm7', 'Abmaj7'],
        'B': ['Cm7', 'Abmaj7', 'Ebmaj7', 'Bb'], 'O': ['Abmaj7', 'Bb', 'Ebmaj7', 'Ebmaj7']}
FORM = [('前奏', 4, 'V'), ('主歌一', 16, 'V'), ('導歌', 4, 'P'), ('副歌', 8, 'C'), ('主歌二', 8, 'V'), ('導歌', 4, 'P'),
        ('副歌', 8, 'C'), ('橋段', 8, 'B'), ('最後副歌', 8, 'C'), ('尾奏', 4, 'O')]

# 旋律（拍内偏移按顺序累加；None = 休止）。副歌 8 小节、导歌 4 小节
CHORUS = [[(70, .5), (70, .5), (72, .5), (70, .5), (67, 1.5), (None, .5)],
          [(67, .5), (65, .5), (67, .5), (68, .5), (67, .5), (65, .5), (63, 1)],
          [(63, .5), (65, .5), (67, .5), (70, .5), (72, 1), (70, .5), (67, .5)],
          [(68, .5), (67, .5), (65, 1), (None, 2)],
          [(70, .5), (70, .5), (72, .5), (74, .5), (75, 1.5), (None, .5)],
          [(74, .5), (72, .5), (70, .5), (72, .5), (70, .5), (67, .5), (65, 1)],
          [(67, .5), (68, .5), (70, .5), (72, .5), (70, .5), (68, .5), (67, 1)],
          [(65, .5), (67, .5), (63, 2), (None, 1)]]
PRE = [[(72, .5), (72, .5), (70, .5), (68, .5), (70, 1), (None, 1)],
       [(70, .5), (70, .5), (72, .5), (74, .5), (70, 2)],
       [(75, .5), (74, .5), (72, .5), (70, .5), (72, .5), (70, .5), (68, 1)],
       [(67, .5), (68, .5), (70, 3)]]
for b in CHORUS + PRE: assert abs(sum(d for _, d in b) - 4) < 1e-9

# —— 排出所有音符：(轨, 开始拍, 长度拍, 音高, 力度) ————————————————————————————
notes = []
def add(tr, t, d, p, v): notes.append((tr, t, d, p, v))
bar = 0; sections = []
for name, n, kind in FORM:
    sections.append((name, bar))
    for i in range(n):
        t0 = (bar + i) * 4.0
        cname = PROG[kind][i % 4]; voic, root = CH[cname]
        if cname == 'Bbsus': voic2 = [58, 62, 65, 70]
        last_out = kind == 'O' and i >= 2
        # 钢琴：主歌是切分的 lo-fi 拍，副歌 / 尾奏长音
        if kind in ('V', 'P') and name != '前奏' or (name == '前奏' and i > 0):
            for off, d in ((0, 1.5), (1.5, 1.0), (3.0, 1.0)):
                vv = voic if not (cname == 'Bbsus' and off >= 1.5) else voic2
                for p in vv: add('鋼琴', t0 + off, d, p, 62 if off else 74)
        else:
            vv = voic
            for p in vv: add('鋼琴', t0, 4 if not last_out else 8, p, 70)
        if name == '前奏' and i == 0:
            for p in voic: add('鋼琴', t0 + 1, 3, p, 60)
        # 拨弦琶音：副歌和桥段
        if kind in ('C', 'B'):
            arp = sorted(voic) + [voic[1] + 12]
            for k in range(8): add('撥弦', t0 + k * 0.5, 0.45, arp[[0, 2, 1, 3, 4, 2, 3, 1][k]] + 12, 58 if k % 2 else 70)
        # 低音
        if name != '前奏' and not last_out:
            if kind == 'B':
                add('貝斯', t0, 4, root, 90)
            else:
                for off, d in ((0, 1.25), (1.75, 0.5), (2.5, 1.0)): add('貝斯', t0 + off, d, root, 100 if off == 0 else 84)
        if last_out and i == 2: add('貝斯', t0, 8, root, 90)
        # 鼓（GM：36 底鼓 38 军鼓 39 拍手 42 闭镲 46 开镲 37 边击）
        if name == '前奏':
            if i == 3:
                for k in range(8): add('鼓', t0 + k * 0.5, 0.1, 42, 60)
        elif last_out:
            pass
        elif kind == 'B':
            add('鼓', t0, 0.25, 36, 100); add('鼓', t0 + 2, 0.25, 38, 90); add('鼓', t0 + 2, 0.25, 39, 70)
            for k in (1, 3): add('鼓', t0 + k, 0.1, 37, 55)
        else:
            for off in (0, 1.75, 2.5) + ((3.75,) if kind == 'C' else ()): add('鼓', t0 + off, 0.25, 36, 105)
            for off in (1, 3): add('鼓', t0 + off, 0.25, 38, 96); add('鼓', t0 + off, 0.25, 39, 80)
            for k in range(8):
                sw = 0.06 if k % 2 else 0.0                            # 一点摇摆
                if kind == 'C' and k == 7: add('鼓', t0 + 3.5 + sw, 0.4, 46, 70)
                else: add('鼓', t0 + k * 0.5 + sw, 0.1, 42, 72 if k % 2 == 0 else 52)
            if kind == 'P' and i == 3:                                  # 导歌最后一小节：军鼓滚奏进副歌
                for k in range(8): add('鼓', t0 + 2 + k * 0.25, 0.1, 38, 50 + k * 7)
        # 旋律（人声参考）
        mel = CHORUS[i % 8] if kind == 'C' else PRE[i % 4] if kind == 'P' else None
        if mel:
            t = t0
            for p, d in mel:
                if p is not None: add('旋律參考', t, d * 0.95, p, 92)
                t += d
    bar += n
TOTAL_BEATS = bar * 4 + 6

# —— MIDI ————————————————————————————————————————————————————————————————————
import mido
TPB = 480
# MIDI 里的文字只能是拉丁字符：轨名和段落标记用英文
ASCII = {'前奏': 'Intro', '主歌一': 'Verse 1', '導歌': 'Pre-chorus', '副歌': 'Chorus', '主歌二': 'Verse 2', '橋段': 'Bridge', '最後副歌': 'Final chorus', '尾奏': 'Outro',
         '鋼琴': 'Keys', '撥弦': 'Pluck', '貝斯': 'Bass', '旋律參考': 'Melody guide', '鼓': 'Drums'}
mid = mido.MidiFile(type=1, ticks_per_beat=TPB)
meta = mido.MidiTrack(); mid.tracks.append(meta)
meta.append(mido.MetaMessage('track_name', name='Jidan he Kele demo', time=0))
meta.append(mido.MetaMessage('set_tempo', tempo=mido.bpm2tempo(BPM), time=0))
meta.append(mido.MetaMessage('key_signature', key='Eb', time=0))
last = 0
for name, b in sections:
    meta.append(mido.MetaMessage('marker', text=ASCII[name], time=b * 4 * TPB - last)); last = b * 4 * TPB
CHAN = {'鋼琴': (0, 4), '撥弦': (1, 46), '貝斯': (2, 38), '旋律參考': (3, 73), '鼓': (9, 0)}   # (通道, GM 音色)
for tr, (ch, prog) in CHAN.items():
    track = mido.MidiTrack(); mid.tracks.append(track)
    track.append(mido.MetaMessage('track_name', name=ASCII[tr], time=0))
    if ch != 9: track.append(mido.Message('program_change', channel=ch, program=prog, time=0))
    ev = []
    for t_, s, d, p, v in notes:
        if t_ != tr: continue
        ev.append((int(round(s * TPB)), 1, p, v)); ev.append((int(round((s + d) * TPB)), 0, p, 0))
    ev.sort(key=lambda e: (e[0], e[1]))
    now = 0
    for tick, on, p, v in ev:
        track.append(mido.Message('note_on' if on else 'note_off', channel=ch, note=p, velocity=v, time=tick - now)); now = tick
mid_path = os.path.join(OUT, '雞蛋和可樂_demo.mid'); mid.save(mid_path)

# —— 预览合成 ————————————————————————————————————————————————————————————————
N = int(TOTAL_BEATS * SPB * SR); mix = {k: np.zeros(N) for k in CHAN}
rng = np.random.default_rng(7)
def hz(p): return 440.0 * 2 ** ((p - 69) / 12)
def env(n, a, r):
    e = np.ones(n); na = min(n, int(a * SR)); e[:na] = np.linspace(0, 1, na) if na else 1
    nr = min(n, int(r * SR)); e[n - nr:] *= np.linspace(1, 0, nr) if nr else 1; return e
def piano(f, n, v):                                         # 加法合成 + 指数衰减（像电钢琴）
    t = np.arange(n) / SR; y = sum((0.6 / k) * np.sin(2 * np.pi * f * k * t) * np.exp(-t * (1.2 + 1.5 * k)) for k in range(1, 6))
    return y * v * env(n, 0.004, 0.08)
def pluck(f, n, v):
    t = np.arange(n) / SR; return (np.sin(2 * np.pi * f * t) + 0.3 * np.sin(4 * np.pi * f * t)) * np.exp(-t * 9) * v
def bass(f, n, v):
    t = np.arange(n) / SR; y = np.tanh(1.6 * np.sin(2 * np.pi * f * t)); return y * v * env(n, 0.005, 0.05) * 0.8
def lead(f, n, v):                                          # 柔和三角波 + 颤音：「人声参考」
    t = np.arange(n) / SR; vib = 1 + 0.004 * np.sin(2 * np.pi * 5.2 * t) * np.clip(t * 3 - 0.6, 0, 1)
    ph = 2 * np.pi * f * np.cumsum(vib) / SR; y = 2 / np.pi * np.arcsin(np.sin(ph)); return y * v * env(n, 0.03, 0.08) * 0.55
def drum(p, n, v):
    n = int(0.45 * SR); t = np.arange(n) / SR
    if p == 36: f = 50 + 90 * np.exp(-t * 30); y = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 7); y = np.tanh(2 * y)
    elif p in (38, 39):
        nz = rng.standard_normal(n); y = (0.7 * nz * np.exp(-t * (22 if p == 38 else 30)) + (0.5 * np.sin(2 * np.pi * 190 * t) * np.exp(-t * 25) if p == 38 else 0))
    elif p in (42, 46): nz = np.diff(rng.standard_normal(n + 1)); y = 0.35 * nz * np.exp(-t * (60 if p == 42 else 9))
    else: y = 0.4 * np.sin(2 * np.pi * 900 * t) * np.exp(-t * 60)
    return y * v
VOICE = {'鋼琴': piano, '撥弦': pluck, '貝斯': bass, '旋律參考': lead}
GAIN = {'鋼琴': 0.18, '撥弦': 0.10, '貝斯': 0.32, '旋律參考': 0.22, '鼓': 0.42}
for tr, s, d, p, v in notes:
    i0 = int(s * SPB * SR); n = int((d * SPB + (0.25 if tr == '鋼琴' else 0.05)) * SR)
    y = drum(p, n, v / 127) if tr == '鼓' else VOICE[tr](hz(p), n, v / 127)
    i1 = min(N, i0 + len(y)); mix[tr][i0:i1] += GAIN[tr] * y[:i1 - i0]
# 前奏的音效：微波炉「叮」、易开罐「呲」；尾奏的「碰」
def place(y, sec):
    i0 = int(sec * SR); i1 = min(N, i0 + len(y)); mix['鼓'][i0:i1] += y[:i1 - i0]
t = np.arange(int(1.6 * SR)) / SR
place(0.25 * (np.sin(2 * np.pi * 2093 * t) + 0.4 * np.sin(2 * np.pi * 4186 * t)) * np.exp(-t * 3), 0.0)
t = np.arange(int(0.5 * SR)) / SR; nz = np.diff(rng.standard_normal(len(t) + 1))
place(0.3 * nz * np.exp(-t * 6) * np.clip(t * 40, 0, 1), 4 * SPB * 2.0)
place(0.4 * np.sin(2 * np.pi * 1300 * t) * np.exp(-t * 35), (bar * 4 - 6) * SPB)

out = sum(mix.values())
out = np.stack([out + 0.15 * mix['撥弦'] - 0.15 * mix['鋼琴'], out - 0.15 * mix['撥弦'] + 0.15 * mix['鋼琴']], 1)
out = np.tanh(out / (np.max(np.abs(out)) + 1e-9) * 1.4) * 0.85        # 轻微饱和 + 归一
import soundfile as sf
wav_path = os.path.join(OUT, '雞蛋和可樂_demo_preview.wav'); sf.write(wav_path, out.astype(np.float32), SR, subtype='FLOAT')
print(mid_path, wav_path, f'{bar} 小節, {len(out) / SR:.0f} 秒')
print('段落：', '  '.join(f'{n} {b * 4 * SPB:.0f}s' for n, b in sections))
