"""分析《Isolated》的歌曲、分轨和歌词，生成画面要用的时间轴数据。

    python analyze.py        -> data.js（mv.html 直接加载）、data.json（方便查看）

输入：
  song.mp3                  完整歌曲（播放、导出用）
  stems/drums.mp3           鼓：读底鼓（30–110Hz）、军鼓 / 拍手（2–6kHz）、踩镲（9–16kHz）、鼓突然全停的"静音"
  stems/bass.mp3            贝斯 / 808：每个音的起点、音高、滑音
  stems/music.mp3           伴奏（其余乐器）
  lyrics.lrc                歌词：每句的文字和开始时间（没有逐字时间，每个字的时间按人声响度在这一句里分配）
速度固定为 86 BPM（鼓量化在十六分音符网格上）。第一小节从 0.67s 开始（和弦在这里换，鼓也在小节线上停 / 回来）。
人声响度 = 原曲减去三条分轨（分轨和原曲逐样本对齐，相关度 0.98）。
"""
import json
import os
import re

import librosa
import numpy as np

here = os.path.dirname(os.path.abspath(__file__))
SONG = os.path.join(here, 'song.mp3')
LRC = os.path.join(here, 'lyrics.lrc')
STEMS = os.path.join(here, 'stems')
FPS = 30                       # 曲线的采样率，和视频帧率一致
BPM = 86.0
STEP = 60 / BPM / 4            # 十六分音符
BAR0_NEAR = 0.67               # 第一小节在这附近
LAST_LINE = 4.0                # 最后一句歌词没有"下一句"作结束点，给它的显示时长
MAX_LINE = 6.0                 # 一句歌词最长显示多久（间奏时字幕不会一直挂着）


def tokenize(text):
    """英文单词算一个"字"，空白不算。"""
    return re.findall(r"[A-Za-z0-9']+|[^\sA-Za-z0-9']", text)


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
                lines.append({'t': int(mm) * 60 + float(ss), 'text': text})
    lines.sort(key=lambda l: l['t'])
    for i, l in enumerate(lines):
        nxt = lines[i + 1]['t'] if i + 1 < len(lines) else min(duration, l['t'] + LAST_LINE)
        l['end'] = round(min(nxt, l['t'] + MAX_LINE), 3)
        l['t'] = round(l['t'], 3)
    return meta, lines


def word_times(line, vt, voc):
    """没有逐字时间：在这一句里找人声响着的那段，按人声响度的累积把每个字均匀摆进去。"""
    tokens = tokenize(line['text'])
    m = (vt >= line['t']) & (vt < line['end'])
    t0, t1 = line['t'], line['t'] + (line['end'] - line['t']) * .85
    if m.sum() > 4:
        v = voc[m]
        on = np.where(v > max(.25, np.percentile(v, 30)))[0]
        if len(on):
            t1 = min(t1, vt[m][on[-1]] + .05)
        w = np.cumsum(np.clip(v, .05, None)); w /= w[-1]
        tt = vt[m]
        times = [float(np.interp(k / len(tokens), w, tt)) for k in range(len(tokens))]
        times = [max(t0, min(x, t1)) for x in times]
    else:
        times = [t0 + (t1 - t0) * k / len(tokens) for k in range(len(tokens))]
    times[0] = t0
    return [{'w': w_, 't': round(x, 3)} for w_, x in zip(tokens, times)]


def norm(x):
    lo, hi = np.percentile(x, 2), np.percentile(x, 98)
    return np.clip((x - lo) / max(hi - lo, 1e-9), 0, 1)


def load(path, sr):
    return librosa.load(path, sr=sr, mono=True)[0]


def band_db(S, f, lo, hi):
    return librosa.amplitude_to_db(S[(f >= lo) & (f < hi)].sum(0) + 1e-6, ref=1)


def step_max(E, ft, a, w):
    m = (ft >= a - .012) & (ft < a + w - .012)
    return float(E[m].max()) if m.any() else -99.0


def grid_phase(times):
    """十六分音符网格的相位：让最多的鼓点落在网格上。"""
    best = (0, 0)
    for ph in np.linspace(0, STEP, 400, endpoint=False):
        r = np.abs(((times - ph + STEP / 2) % STEP) - STEP / 2)
        best = max(best, ((r < .015).mean(), ph))
    return best[1]


def analyze_stems(duration):
    sr, hop = 44100, 128
    song = load(SONG, sr)
    drums, bass, music = (load(os.path.join(STEMS, f'{k}.mp3'), sr) for k in ('drums', 'bass', 'music'))
    n = min(map(len, (song, drums, bass, music)))
    song, drums, bass, music = (x[:n] for x in (song, drums, bass, music))

    # ---- 鼓：底鼓 / 军鼓 / 踩镲
    S = np.abs(librosa.stft(drums, n_fft=2048, hop_length=hop)); f = librosa.fft_frequencies(sr=sr, n_fft=2048)
    ft = librosa.frames_to_time(np.arange(S.shape[1]), sr=sr, hop_length=hop)
    K, SN, HI, ALL = band_db(S, f, 30, 110), band_db(S, f, 2000, 6000), band_db(S, f, 9000, 16000), band_db(S, f, 30, 16000)
    env = np.maximum(0, np.diff(K, prepend=K[0])) + np.maximum(0, np.diff(SN, prepend=SN[0]))
    on = librosa.onset.onset_detect(onset_envelope=env, sr=sr, hop_length=hop, units='time', delta=.12, wait=8)
    ph = grid_phase(on)
    bar0 = ph + round((BAR0_NEAR - ph) / STEP) * STEP
    nsteps = int((duration - bar0) / STEP) + 1
    steps = bar0 + np.arange(nsteps) * STEP
    kv = np.array([step_max(K, ft, a, STEP) for a in steps])
    sv = np.array([step_max(SN, ft, a, STEP) for a in steps])
    hv = np.array([step_max(HI, ft, a, STEP) for a in steps])
    rise = lambda v, i: v[i] - (v[i - 1] if i else -99)
    kicks = [float(steps[i]) for i in range(nsteps) if kv[i] >= 47 and rise(kv, i) >= 5]
    snares = [float(steps[i]) for i in range(nsteps) if sv[i] >= 51 and rise(sv, i) >= 6]
    hats = [float(steps[i]) for i in range(nsteps) if hv[i] >= 44 and rise(hv, i) >= 6]
    # 静音：整整一拍以上鼓几乎没声音
    av = np.array([step_max(ALL, ft, a, STEP) for a in steps])
    quiet = av < np.percentile(av, 50) - 22
    gaps, i = [], 0
    while i < nsteps:
        if quiet[i]:
            j = i
            while j + 1 < nsteps and quiet[j + 1]:
                j += 1
            if j - i >= 3:
                gaps.append([round(float(steps[i]), 3), round(float(steps[j] + STEP), 3)])
            i = j + 1
        else:
            i += 1

    # ---- 贝斯 / 808 音符
    srb = 22050
    bb = librosa.resample(bass, orig_sr=sr, target_sr=srb)
    onb = librosa.onset.onset_detect(y=bb, sr=srb, hop_length=hop, units='time', backtrack=True, delta=.15, wait=6)
    f0, _, _ = librosa.pyin(bb, fmin=30, fmax=250, sr=srb, hop_length=hop, frame_length=4096)
    tt = librosa.times_like(f0, sr=srb, hop_length=hop)
    rdb = librosa.amplitude_to_db(librosa.feature.rms(y=bb, hop_length=hop, frame_length=2048)[0], ref=np.max)
    notes = []
    for i, o in enumerate(onb):
        nxt = onb[i + 1] if i + 1 < len(onb) else o + 1
        m = (tt >= o + .03) & (tt < min(nxt, o + .4)); fv = f0[m]; fv = fv[~np.isnan(fv)]
        lm = (tt >= o) & (tt < o + .1)
        if len(fv) < 4 or not lm.any() or rdb[lm].max() < -30:
            continue
        p0, p1 = float(np.median(fv[:5])), float(np.median(fv[-5:]))
        nt = {'t': round(float(o), 3), 'midi': round(float(librosa.hz_to_midi(p0)), 1)}
        if abs(12 * np.log2(p1 / p0)) >= 2:
            nt['glide'] = round(float(librosa.hz_to_midi(p1)), 1)
        notes.append(nt)

    # ---- 每小节：鼓的密度（0 = 没鼓，1 = 满）、有没有贝斯
    Sb = np.abs(librosa.stft(bass, n_fft=2048, hop_length=512)); fb = librosa.fft_frequencies(sr=sr, n_fft=2048)
    bt = librosa.frames_to_time(np.arange(Sb.shape[1]), sr=sr, hop_length=512)
    BL = band_db(Sb, fb, 25, 250)
    bars = []
    for b in range(nsteps // 16 + 1):
        a = bar0 + b * 16 * STEP
        nk = sum(1 for k in kicks if a - .02 <= k < a + 16 * STEP - .02)
        ns = sum(1 for k in snares if a - .02 <= k < a + 16 * STEP - .02)
        mb = (bt >= a) & (bt < a + 16 * STEP)
        bars.append({'t': round(a, 3), 'kicks': nk, 'snares': ns, 'bass': bool(mb.any() and BL[mb].mean() > 10)})

    # ---- 人声：原曲减去三条分轨
    vocal = song - drums - bass - music
    Sv = np.abs(librosa.stft(vocal, n_fft=2048, hop_length=512)); fv_ = librosa.fft_frequencies(sr=sr, n_fft=2048)
    vt = librosa.frames_to_time(np.arange(Sv.shape[1]), sr=sr, hop_length=512)
    voc = band_db(Sv, fv_, 200, 4000)

    return {
        'bar0': round(float(bar0), 4), 'step': STEP,
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

    dr, (vt, voc) = analyze_stems(duration)
    vocal = norm(np.interp(grid, vt, voc))
    vocal_hr = norm(voc)
    beats = [dr['bar0'] + k * 4 * STEP for k in range(int((duration - dr['bar0']) / (4 * STEP)) + 1)]

    meta, lyrics = parse_lrc(LRC, duration)
    for l in lyrics:
        l['words'] = word_times(l, vt, vocal_hr)
    data = {
        'title': meta.get('ti') or 'Isolated',
        'artist': meta.get('ar') or 'YuKIluv7 / dangao_w',
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
          f"踩镲 {len(dr['hats'])}  静音 {len(dr['gaps'])}  贝斯音符 {len(dr['notes808'])}  歌词 {len(lyrics)} 句")
    print('每小节 底鼓/军鼓/贝斯：')
    for i, b in enumerate(dr['bars']):
        print(f"  {i:2d} {b['t']:7.2f}s  K{b['kicks']:2d} S{b['snares']:2d} {'B' if b['bass'] else '-'}")
    print('静音：' + ' '.join(f'{a:.2f}-{b:.2f}' for a, b in dr['gaps']))


if __name__ == '__main__':
    main()
