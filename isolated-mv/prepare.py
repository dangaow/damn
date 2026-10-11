"""Prepare everything mv.html reads: audio analysis, lyrics and font subsets.

    python prepare.py          -> assets.js

Reads Isolated.mp3 (full mix, with the LRC translation tag inside), Isolated.lrc and the
three stems in stems/. Fonts come from Google Fonts on the first run and are cached in .fonts/; only the
glyphs the video uses are kept, so assets.js stays small and the page works offline from file://.
Re-run it after changing any Chinese text in mv.html so the subsets cover the new characters.
"""
import base64, io, json, os, re, subprocess, sys, urllib.request
import numpy as np
from scipy import signal
from fontTools import subset
from fontTools.ttLib import TTFont

here = os.path.dirname(os.path.abspath(__file__))
P = lambda *a: os.path.join(here, *a)
SR = 22050

# ---------------------------------------------------------------- audio
def decode(path):
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-map', '0:a:0', '-ac', '1', '-ar', str(SR), '-f', 'f32le', '-'],
                         check=True, capture_output=True).stdout
    return np.frombuffer(raw, dtype=np.float32).copy()

full = decode(P('Isolated.mp3'))
stems = {k: decode(P('stems', f'{k}.mp3')) for k in ('drums', 'bass', 'music')}
N = min(len(full), *(len(v) for v in stems.values()))
full = full[:N]; stems = {k: v[:N] for k, v in stems.items()}
DUR = N / SR

# the stems are sample-aligned with the mix; what the stems do not explain is (mostly) the voice
mix = sum(stems.values())
g = float(np.dot(full, mix) / np.dot(mix, mix))
voice = full - g * mix

ENV_HZ = 60
def envelope(x):
    w = SR // ENV_HZ
    n = len(x) // w
    r = np.sqrt((x[:n * w].reshape(n, w) ** 2).mean(1))
    r = signal.savgol_filter(r, 5, 2)
    r = np.clip(r / (np.percentile(r, 99) + 1e-9), 0, 1)
    return [int(round(v * 255)) for v in r]

env = {k: envelope(v) for k, v in [('full', full), ('voice', voice), *stems.items()]}

hop = 256
fps = SR / hop
f_, _, Z = signal.stft(stems['drums'], SR, nperseg=1024, noverlap=1024 - hop, boundary=None, padded=False)
M = np.abs(Z)
def onset(lo, hi):
    b = M[(f_ >= lo) & (f_ < hi)].sum(0)
    b = np.log1p(b / (np.percentile(b, 95) + 1e-9) * 10)
    o = np.maximum(0, np.diff(b, prepend=b[0]))
    return o / (np.percentile(o[o > 0], 99) + 1e-9)
o_kick, o_snare, o_hat = onset(20, 120), onset(180, 1500), onset(6000, 11000)

# beat grid: comb search on the snare+kick novelty, then a least-squares fit to the hits near the grid
nov = o_kick + o_snare
best = (-1, 0, 0)
for bpm in np.arange(80, 96, 0.02):
    per = 60 / bpm * fps
    for ph in np.linspace(0, per, 48, endpoint=False):
        idx = np.round(ph + np.arange(0, len(nov) - per, per)).astype(int)
        s = nov[idx].sum()
        if s > best[0]: best = (s, bpm, ph / fps)
_, bpm, phase = best
beat = 60 / bpm
pk, _ = signal.find_peaks(o_snare, height=.6, distance=int(fps * .09))
ts = pk / fps
k = np.round((ts - phase) / beat)
near = np.abs(ts - (phase + k * beat)) < .05
A = np.vstack([k[near], np.ones(near.sum())]).T
beat, phase = np.linalg.lstsq(A, ts[near], rcond=None)[0]
phase %= beat
bpm = 60 / beat

def peaks(o, h, dist):
    p, _ = signal.find_peaks(o, height=h, distance=int(fps * dist))
    return [[round(i / fps, 3), round(float(min(o[i], 1.5)), 2)] for i in p]

grid = []
for i in range(int((DUR - phase) / beat) + 1):
    t = phase + i * beat
    j0, j1 = int((t - .045) * fps), int((t + .045) * fps) + 1
    if j1 >= len(o_snare): break
    grid.append([round(t, 3), round(float(min(o_kick[j0:j1].max(), 1.5)), 2), round(float(min(o_snare[j0:j1].max(), 1.5)), 2)])

# ---------------------------------------------------------------- lyrics
def lrc_time(s):
    m, sec = s.split(':'); return round(int(m) * 60 + float(sec), 3)
lines = []
for ln in open(P('Isolated.lrc'), encoding='utf-8').read().splitlines():
    m = re.match(r'\[(\d+:\d+(?:\.\d+)?)\](.*)', ln.strip())
    if m and m.group(2).strip(): lines.append({'t': lrc_time(m.group(1)), 'text': m.group(2).strip()})
tags = json.loads(subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'format_tags', '-of', 'json', P('Isolated.mp3')],
                                 check=True, capture_output=True).stdout)['format'].get('tags', {})
trans = {}
for key, val in tags.items():
    if key.lower().startswith('lyrics'):
        for ln in val.splitlines():
            m = re.match(r'\[(\d+:\d+(?:\.\d+)?)\](.*?)\s*\(翻译：(.*)\)\s*$', ln.strip())
            if m: trans[lrc_time(m.group(1))] = m.group(3).strip()
for L in lines:
    tr = trans.get(L['t'], '')                                          # Chinese lines need no translation; the LRC is the authority for the words
    if tr and re.search(r'[A-Za-z]', L['text']) and tr.replace(' ', '') != L['text'].replace(' ', ''): L['tr'] = tr
meta = {k: tags.get(k, '') for k in ('title', 'artist', 'album', 'date')}

# ---------------------------------------------------------------- fonts
FONTS = {  # family in mv.html -> (file in .fonts, Google Fonts css2 query, CJK?)
    'Noto Serif SC': ('NotoSerifSC-Regular.ttf', 'Noto+Serif+SC:wght@400', True),
    'Noto Serif SC Black': ('NotoSerifSC-Black.ttf', 'Noto+Serif+SC:wght@900', True),
    'Zhi Mang Xing': ('ZhiMangXing.ttf', 'Zhi+Mang+Xing', True),
    'Cormorant': ('Cormorant-Italic.ttf', 'Cormorant+Garamond:ital,wght@1,500', False),
    'Anton': ('Anton.ttf', 'Anton', False),
    'Space Mono': ('SpaceMono-Regular.ttf', 'Space+Mono:wght@400', False),
    'Space Mono Bold': ('SpaceMono-Bold.ttf', 'Space+Mono:wght@700', False),
}
os.makedirs(P('.fonts'), exist_ok=True)
def fetch_font(fn, query):
    if os.path.exists(P('.fonts', fn)): return
    css = urllib.request.urlopen(f'https://fonts.googleapis.com/css2?family={query}').read().decode()  # no UA -> plain TTF
    url = re.search(r'url\((https://[^)]+\.ttf)\)', css).group(1)
    print(f'downloading {fn}')
    open(P('.fonts', fn), 'wb').write(urllib.request.urlopen(url).read())

html = open(P('mv.html'), encoding='utf-8').read() if os.path.exists(P('mv.html')) else ''
latin = ''.join(chr(c) for c in range(32, 127)) + '’‘“”—–…·•●○◀▶■□×※♡♥→←↺％'
cjk = set(''.join(L['text'] + L.get('tr', '') for L in lines) + ''.join(meta.values()) + re.sub(r'[\x00-\x7f]', '', html))
cjk |= set('，。、！？：；「」『』（）《》【】')
fonts = {}
for fam, (fn, query, is_cjk) in FONTS.items():
    fetch_font(fn, query)
    f = TTFont(P('.fonts', fn))
    cmap = f.getBestCmap()
    text = latin + (''.join(sorted(cjk)) if is_cjk else '')
    opts = subset.Options(); opts.layout_features = ['*']; opts.name_IDs = ['*']; opts.notdef_outline = True
    sub = subset.Subsetter(opts); sub.populate(unicodes=[ord(c) for c in text if ord(c) in cmap]); sub.subset(f)
    buf = io.BytesIO(); f.save(buf)
    fonts[fam] = base64.b64encode(buf.getvalue()).decode('ascii')
    missing = [c for c in sorted(cjk) if is_cjk and ord(c) not in cmap and not c.isspace()]
    if missing: print(f'  {fam}: no glyph for {"".join(missing)}')

data = {
    'meta': meta, 'duration': round(DUR, 3), 'bpm': round(float(bpm), 3), 'beat': round(float(beat), 5), 'phase': round(float(phase), 4),
    'envHz': ENV_HZ, 'env': env, 'grid': grid,
    'kicks': peaks(o_kick, .45, .09), 'snares': peaks(o_snare, .7, .09), 'hats': peaks(o_hat, .55, .06),
    'lyrics': lines,
}
with open(P('assets.js'), 'w', encoding='utf-8') as out:
    out.write('// generated by prepare.py, do not edit\n')
    out.write('window.ASSETS = ' + json.dumps(data, ensure_ascii=False, separators=(',', ':')) + ';\n')
    out.write('window.ASSETS.fonts = ' + json.dumps(fonts, separators=(',', ':')) + ';\n')
print(f'{DUR:.2f}s  {bpm:.2f} BPM (beat {beat:.4f}s, phase {phase:.3f}s)  mix gain {g:.3f}  '
      f'{len(lines)} lines ({sum("tr" in L for L in lines)} translated)  {len(cjk)} CJK chars')
print(f'wrote assets.js  ({os.path.getsize(P("assets.js")) / 1e6:.2f} MB)')
