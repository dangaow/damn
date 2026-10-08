"""分析歌曲和歌词，生成画面要用的时间轴数据。

    python analyze.py        -> data.js（mv.html 直接加载）、data.json（方便查看）

输出内容：
  - 时长、BPM、每一拍的时间、推测的小节重拍
  - 每 1/30 秒的能量（响度）和低频能量（鼓点），归一化到 0..1
  - 段落分界（按音色/和声变化自动切分）
  - 歌词：每句的开始、结束时间，以及逐字时间

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


def main():
    y, sr = librosa.load(SONG, sr=22050, mono=True)
    duration = len(y) / sr
    hop = 512

    tempo, beats = librosa.beat.beat_track(y=y, sr=sr, hop_length=hop, units='time')
    tempo = float(np.atleast_1d(tempo)[0])
    beats = np.asarray(beats, float)

    # 重拍：在 4 种相位里挑低频能量最强的那一组，当作每小节第一拍
    S = np.abs(librosa.stft(y, hop_length=hop))
    freqs = librosa.fft_frequencies(sr=sr)
    low = S[freqs < 150].sum(axis=0)
    low_at = np.interp(librosa.time_to_frames(beats, sr=sr, hop_length=hop), np.arange(len(low)), low)
    phase = int(np.argmax([low_at[p::4].sum() for p in range(4)])) if len(beats) >= 8 else 0
    downbeats = beats[phase::4]

    # 按视频帧率重采样的能量曲线
    rms = librosa.feature.rms(S=S)[0]
    ft = librosa.frames_to_time(np.arange(len(rms)), sr=sr, hop_length=hop)
    grid = np.arange(0, duration, 1 / FPS)
    energy = norm(np.interp(grid, ft, librosa.amplitude_to_db(rms, ref=np.max)))
    bass = norm(np.interp(grid, ft, low))

    # 段落：用色度 + MFCC 做聚类分段，段数按歌曲长度估
    feat = np.vstack([librosa.feature.chroma_cqt(y=y, sr=sr, hop_length=hop),
                      librosa.util.normalize(librosa.feature.mfcc(y=y, sr=sr, hop_length=hop, n_mfcc=13), axis=1)])
    k = int(np.clip(round(duration / 15), 4, 12))
    bounds = librosa.segment.agglomerative(feat, k)
    bt = librosa.frames_to_time(bounds, sr=sr, hop_length=hop)
    edges = list(bt) + [duration]
    sections = []
    for a, b_ in zip(edges[:-1], edges[1:]):
        m = (grid >= a) & (grid < b_)
        sections.append({'t': round(float(a), 3), 'end': round(float(b_), 3),
                         'energy': round(float(energy[m].mean()) if m.any() else 0, 3)})

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
        'bpm': round(tempo, 2),
        'fps': FPS,
        'beats': [round(b, 3) for b in beats],
        'downbeats': [round(b, 3) for b in downbeats],
        'energy': [round(float(v), 3) for v in energy],
        'bass': [round(float(v), 3) for v in bass],
        'sections': sections,
        'lyrics': lyrics,
    }
    js = json.dumps(data, ensure_ascii=False, separators=(',', ':'))
    open(os.path.join(here, 'data.js'), 'w', encoding='utf-8').write(f'window.MV_DATA = {js};\n')
    json.dump(data, open(os.path.join(here, 'data.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)

    print(f'时长 {duration:.2f}s  BPM {tempo:.1f}  拍数 {len(beats)}  首拍 {beats[0]:.2f}s  歌词 {len(lyrics)} 句')
    print('段落：' + '  '.join(f"{s['t']:.1f}-{s['end']:.1f}({s['energy']:.2f})" for s in sections))
    if fixed:
        print('逐字时间不可信、已插值：' + '；'.join(fixed))
    short = [l for l in lyrics if l['end'] - l['t'] < 0.8]
    for l in short:
        print(f"注意：{l['t']:.2f}s「{l['text']}」只有 {l['end'] - l['t']:.2f}s，时间戳可能需要校对")


if __name__ == '__main__':
    main()
