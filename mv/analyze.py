"""分析歌曲、分轨和歌词，生成画面要用的时间轴数据。

    python analyze.py        -> data.js（mv.html 直接加载）、data.json（方便查看）

输入：
  song.mp3                  完整歌曲（播放、导出用）
  stems/beat.mp3            伴奏：读底鼓（70–130Hz 的瞬态，和 808 长音区分开）
  stems/drums.mp3           鼓分轨：读军鼓、踩镲、鼓突然全停的"静音"
  stems/bass.mp3            贝斯分轨：读 808 每个音的起点、音高、滑音
  lyrics.lrc / lyrics_words.lrc   歌词（见下）
速度固定为 94 BPM（伴奏是在 DAW 里按网格排的，鼓点都量化到十六分音符网格上）。
人声响度 = 原曲减去对齐后的伴奏，给测谎仪用。

歌词来源：lyrics.lrc 决定每句的文字和开始时间；lyrics_words.lrc（逐字 LRC，<mm:ss.xx> 标记每个字）
提供每个字的时间。逐字时间超出这一句范围或顺序错乱的字，按前后可信的字插值；没有逐字数据的句子按字均分。
"""
import json
import os
import re

import librosa
import numpy as np

here = os.path.dirname(os.path.abspath(__file__))
SONG = os.path.join(here, 'song.mp3')
LRC = os.path.join(here, 'lyrics.lrc')
WORDS_LRC = os.path.join(here, 'lyrics_words.lrc')
FPS = 30                       # 能量曲线的采样率，和视频帧率一致
STEMS = os.path.join(here, 'stems')
BPM = 94.0
STEP = 60 / BPM / 4            # 十六分音符
STEMS = os.path.join(here, 'stems')
BPM = 94.0
STEP = 60 / BPM / 4            # 十六分音符
LAST_LINE = 4.0                # 最后一句歌词没有"下一句"作结束点，给它的显示时长
MAX_LINE = 6.0                 # 一句歌词最长显示多久（防止间奏时字幕一直挂着）


def tokenize(text):
    """英文单词算一个"字"，空白不算。"""
    return re.findall(r"[A-Za-z0-9']+|[^\sA-Za-z0-9']", text)


def stamp(mm, frac_s, cs):
    sec, _, frac = frac_s.partition('.')
    f = int(frac) / 100 if cs else float('0.' + frac) if frac else 0.0
    return int(mm) * 60 + int(sec) + f


def parse_lrc(path, duration):
    meta, lines = {}, []
    for raw in open(path, encoding='utf-8-sig'):
        raw = raw.strip()
        m = re.fullmatch(r'\[(ti|ar|al|by):(.*)\]', raw)
        if m:
            meta[m[1]] = m[2].strip()
            continue
        stamps = re.findall(r'\[(\d+):(\d+(?:\.\d+)?)\]', raw)
        text = re.sub(r'\[[^\]]*\]', '', raw).strip()
        for mm, ss in stamps:
            if text:
                lines.append({'t': stamp(mm, ss, False), 'text': text})
    lines.sort(key=lambda l: l['t'])
    for i, l in enumerate(lines):
        nxt = lines[i + 1]['t'] if i + 1 < len(lines) else min(duration, l['t'] + LAST_LINE)
        l['end'] = round(min(nxt, l['t'] + MAX_LINE), 3)
        l['t'] = round(l['t'], 3)
    return meta, lines


def parse_word_lrc(path):
    """逐字 LRC：每行 [行时间]<字时间>字 <字时间>字 …，返回每行的 [(字, 时间)]。
    有的生成工具把 1 秒写成 ".100"（百分秒进位溢出），所以小数部分统一按百分秒解析。"""
    if not os.path.exists(path):
        return []
    rows = []
    for raw in open(path, encoding='utf-8-sig'):
        pairs = re.findall(r'<(\d+):(\d+(?:\.\d+)?)>\s*([^<]*)', raw)
        if pairs:
            rows.append([(w.strip(), stamp(mm, ss, True)) for mm, ss, w in pairs if w.strip()])
    return rows


def word_times(line, row):
    """把逐字时间套到这一句上：不可信的字（超出本句范围或比前一个字早）按前后可信的字插值。"""
    tokens = tokenize(line['text'])
    t0, t1 = line['t'], line['t'] + (line['end'] - line['t']) * 0.9   # 句尾留一点空
    times = [None] * len(tokens)
    if row and len(row) == len(tokens):
        last = t0 - 0.3
        for k, (_, wt) in enumerate(row):
            if t0 - 0.3 <= wt <= line['end'] and wt >= last:
                times[k] = max(wt, t0)
                last = wt
    good = [k for k, v in enumerate(times) if v is not None]
    # 两端没有可信字时，句首用这一句的开始时间、句尾用 t1 作锚点
    anchors = [(-1, t0)] if not good or good[0] != 0 else []
    anchors += [(k, times[k]) for k in good]
    if not good or good[-1] != len(tokens) - 1:
        anchors.append((len(tokens), t1))
    for (ka, ta), (kb, tb) in zip(anchors, anchors[1:]):
        for k in range(max(ka + 1, 0), min(kb, len(tokens))):
            times[k] = ta + (tb - ta) * (k - ka) / (kb - ka) if ka >= 0 else t0 + (tb - t0) * k / kb
    return [{'w': w, 't': round(tt, 3)} for w, tt in zip(tokens, times)], len(good)


def norm(x):
    lo, hi = np.percentile(x, 2), np.percentile(x, 98)
    return np.clip((x - lo) / max(hi - lo, 1e-9), 0, 1)


def load(path, sr):
    return librosa.load(path, sr=sr, mono=True)[0]


def align(ref, y, sr, a=5.0, b=15.0, search=0.1):
    """y 相对 ref 的延迟（秒）：在 a..b 秒这段里逐样本找相关性最大的偏移。"""
    seg = ref[int(a * sr):int(b * sr)]
    best = (-1, 0)
    for lag in range(-int(search * sr), int(search * sr), 2):
        yy = y[int(a * sr) - lag:int(b * sr) - lag]
        c = float(np.dot(seg, yy) / (np.linalg.norm(seg) * np.linalg.norm(yy) + 1e-9))
        best = max(best, (c, lag))
    return best[1] / sr, best[0]


def band_db(S, f, lo, hi):
    return librosa.amplitude_to_db(S[(f >= lo) & (f < hi)].sum(0), ref=1)


def step_max(E, ft, a, w):
    m = (ft >= a - .01) & (ft < a + w - .01)
    return float(E[m].max()) if m.any() else -99.0


def grid_phase(times):
    """十六分音符网格的相位：让最多的鼓点落在网格上。"""
    best = (0, 0)
    for ph in np.linspace(0, STEP, 400, endpoint=False):
        r = np.abs(((times - ph + STEP / 2) % STEP) - STEP / 2)
        best = max(best, ((r < .02).mean(), ph))
    return best[1]


def drums_from_stems(duration):
    sr = 44100
    song = load(SONG, sr)
    beat = load(os.path.join(STEMS, 'beat.mp3'), sr)
    drums = load(os.path.join(STEMS, 'drums.mp3'), sr)
    off, corr = align(song, beat, sr)
    print(f'伴奏对齐：偏移 {off * 1000:.1f}ms，相关度 {corr:.3f}')

    hop = 128
    # ---- 底鼓：伴奏里 70–130Hz 的峰值（808 长音主要在 40–70Hz）
    S = np.abs(librosa.stft(beat, n_fft=4096, hop_length=hop)); f = librosa.fft_frequencies(sr=sr, n_fft=4096)
    ft = librosa.frames_to_time(np.arange(S.shape[1]), sr=sr, hop_length=hop) + off
    K, SUB = band_db(S, f, 70, 130), band_db(S, f, 40, 70)
    on = librosa.onset.onset_detect(onset_envelope=np.maximum(0, np.diff(K, prepend=K[0])), sr=sr, hop_length=hop, units='time') + off
    ph = grid_phase(on)
    bar0 = ph + round((1.34 - ph) / STEP) * STEP                    # 第一小节在 1.34s 附近
    nsteps = int((duration - bar0) / STEP) + 1
    steps = bar0 + np.arange(nsteps) * STEP
    kv = np.array([step_max(K, ft, a, STEP) for a in steps])
    subv = np.array([step_max(SUB, ft, a, STEP) for a in steps])
    kicks = [float(steps[i]) for i in range(nsteps) if kv[i] >= 58 and kv[i] - (kv[i - 1] if i else -99) >= 6]

    # ---- 军鼓、踩镲、静音：鼓分轨
    Sd = np.abs(librosa.stft(drums, n_fft=2048, hop_length=hop)); fd = librosa.fft_frequencies(sr=sr, n_fft=2048)
    ftd = librosa.frames_to_time(np.arange(Sd.shape[1]), sr=sr, hop_length=hop) + off
    MID, HI, ALL = band_db(Sd, fd, 1000, 4000), band_db(Sd, fd, 10000, 20000), band_db(Sd, fd, 200, 20000)
    snares = [float(a) for a in steps if max(step_max(MID, ftd, a, STEP / 2), step_max(MID, ftd, a + STEP / 2, STEP / 2)) >= 62]
    slots = bar0 + np.arange(nsteps * 2) * STEP / 2                  # 三十二分音符
    hv = np.array([step_max(HI, ftd, a, STEP / 2) for a in slots])
    av = np.array([step_max(ALL, ftd, a, STEP / 2) for a in slots])
    hats = [float(slots[i]) for i in range(len(slots)) if hv[i] >= 48 and hv[i] - (hv[i - 1] if i else 0) >= 8]
    gaps, i = [], 0
    while i < len(slots):                                            # 连续两个以上三十二分音符几乎无声
        if av[i] < 25:
            j = i
            while j + 1 < len(slots) and av[j + 1] < 25:
                j += 1
            if j > i and slots[j] + STEP / 2 < duration - 2:
                gaps.append([round(float(slots[i]), 3), round(float(slots[j] + STEP / 2), 3)])
            i = j + 1
        else:
            i += 1

    # ---- 808 音符：贝斯分轨
    srb = 22050
    bass = load(os.path.join(STEMS, 'bass.mp3'), srb)
    on = librosa.onset.onset_detect(y=bass, sr=srb, hop_length=hop, units='time', backtrack=True, delta=.15, wait=6) + off
    f0, _, _ = librosa.pyin(bass, fmin=30, fmax=200, sr=srb, hop_length=hop, frame_length=4096)
    tt = librosa.times_like(f0, sr=srb, hop_length=hop) + off
    rdb = librosa.amplitude_to_db(librosa.feature.rms(y=bass, hop_length=hop, frame_length=2048)[0], ref=np.max)
    notes = []
    for i, o in enumerate(on):
        nxt = on[i + 1] if i + 1 < len(on) else o + 1
        m = (tt >= o + .03) & (tt < min(nxt, o + .4)); fv = f0[m]; fv = fv[~np.isnan(fv)]
        lm = (tt >= o) & (tt < o + .1)
        if len(fv) < 4 or not lm.any() or rdb[lm].max() < -30:
            continue
        p0, p1 = float(np.median(fv[:5])), float(np.median(fv[-5:]))
        n = {'t': round(float(o), 3), 'midi': round(float(librosa.hz_to_midi(p0)), 1)}
        if abs(12 * np.log2(p1 / p0)) >= 2:
            n['glide'] = round(float(librosa.hz_to_midi(p1)), 1)
        notes.append(n)

    # ---- 每小节：808 有没有响；加花 = 808 连砸三下 D2（及以上，MIDI ≥ 37.5）
    bars = []
    for b in range(nsteps // 16 + 1):
        a = bar0 + b * 16 * STEP
        sub = float(subv[b * 16:(b + 1) * 16].mean()) if b * 16 < nsteps else -99
        high = sum(1 for n in notes if a - .05 <= n['t'] < a + 16 * STEP - .05 and n['midi'] >= 37.5)
        bars.append({'t': round(a, 3), 'b808': sub > 45, 'fill': high >= 3})

    # ---- 人声响度（测谎仪）：原曲减去对齐后的伴奏
    lag = int(round(off * sr))
    bb = np.zeros_like(song); n = min(len(song) - max(lag, 0), len(beat))
    bb[max(lag, 0):max(lag, 0) + n] = beat[:n]
    g = float(np.dot(song, bb) / (np.dot(bb, bb) + 1e-9))
    vocal = song - g * bb
    S = np.abs(librosa.stft(vocal, n_fft=2048, hop_length=512)); f = librosa.fft_frequencies(sr=sr, n_fft=2048)
    vt = librosa.frames_to_time(np.arange(S.shape[1]), sr=sr, hop_length=512)
    voc = band_db(S, f, 200, 4000)

    return {
        'bar0': round(bar0, 4), 'step': STEP,
        'kicks': [round(k, 3) for k in kicks], 'snares': [round(x, 3) for x in snares],
        'hats': [round(x, 3) for x in hats], 'gaps': gaps, 'bars': bars, 'notes808': notes,
    }, (vt, voc)


def main():
    y, sr = librosa.load(SONG, sr=22050, mono=True)
    duration = len(y) / sr
    hop = 512

    S = np.abs(librosa.stft(y, hop_length=hop))
    freqs = librosa.fft_frequencies(sr=sr)
    rms = librosa.feature.rms(S=S)[0]
    ft = librosa.frames_to_time(np.arange(len(rms)), sr=sr, hop_length=hop)
    grid = np.arange(0, duration, 1 / FPS)
    energy = norm(np.interp(grid, ft, librosa.amplitude_to_db(rms, ref=np.max)))
    low = norm(np.interp(grid, ft, S[freqs < 150].sum(axis=0)))

    dr, (vt, voc) = drums_from_stems(duration)
    vocal = norm(np.interp(grid, vt, voc))
    beats = [dr['bar0'] + k * 4 * STEP for k in range(int((duration - dr['bar0']) / (4 * STEP)) + 1)]

    meta, lyrics = parse_lrc(LRC, duration)
    rows = parse_word_lrc(WORDS_LRC)
    fixed = []
    for i, l in enumerate(lyrics):
        l['words'], ok = word_times(l, rows[i] if i < len(rows) else None)
        if ok < len(l['words']):
            fixed.append(f"{l['t']:.2f}s「{l['text']}」{len(l['words']) - ok} 个字")
    data = {
        'title': meta.get('ti') or 'DEMO',
        'artist': meta.get('ar', ''),
        'duration': round(duration, 3),
        'bpm': BPM,
        'fps': FPS,
        'beats': [round(b, 3) for b in beats],
        'downbeats': [round(b, 3) for b in beats[::4]],
        **dr,
        'energy': [round(float(v), 3) for v in energy],
        'bass': [round(float(v), 3) for v in low],
        'vocal': [round(float(v), 3) for v in vocal],
        'lyrics': lyrics,
    }
    js = json.dumps(data, ensure_ascii=False, separators=(',', ':'))
    open(os.path.join(here, 'data.js'), 'w', encoding='utf-8').write(f'window.MV_DATA = {js};\n')
    json.dump(data, open(os.path.join(here, 'data.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)

    print(f"时长 {duration:.2f}s  BPM {BPM}  第一小节 {dr['bar0']:.3f}s  底鼓 {len(dr['kicks'])}  军鼓 {len(dr['snares'])}  "
          f"踩镲 {len(dr['hats'])}  静音 {len(dr['gaps'])}  808 音符 {len(dr['notes808'])}  歌词 {len(lyrics)} 句")
    print('加花小节：' + ' '.join(str(i) for i, b in enumerate(dr['bars']) if b['fill']))
    print('808 停的小节：' + ' '.join(str(i) for i, b in enumerate(dr['bars']) if not b['b808']))
    if fixed:
        print('逐字时间不可信、已插值：' + '；'.join(fixed))


if __name__ == '__main__':
    main()
