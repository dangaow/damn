# 训练：量每首分离好的歌（整首 + 分段 + 20 秒窗），按风格统计，输出风格目标和伴奏分类器。
# 用法：python train.py CORPUS_DIR [MM_MIXRENDER]
#   CORPUS_DIR/manifest.json：[{id, title, artist, style, preview}]，分离结果在 CORPUS_DIR/sep/<id>/{vocals,inst,mix}.wav
# 输出：CORPUS_DIR/measure/<id>.json（每首的量测，缓存）、CORPUS_DIR/report.json（风格统计 + 分类器）、终端里的摘要
import json, os, subprocess, sys
import numpy as np

C = sys.argv[1]
MM = sys.argv[2] if len(sys.argv) > 2 else '/home/user/build-mix/mm_mixrender'
man = json.load(open(f'{C}/manifest.json'))
SEP = os.environ.get('SEP_DIR', f'{C}/sep')                          # 分离结果目录（MSST 或 htdemucs）
MEAS = f'{C}/measure_' + os.path.basename(SEP.rstrip('/'))            # 量测缓存跟着分离结果走
os.makedirs(MEAS, exist_ok=True)


def bpm_of(path):
    import librosa
    y, sr = librosa.load(path, sr=22050, mono=True)
    t, _ = librosa.beat.beat_track(y=y, sr=sr)
    t = float(np.atleast_1d(t)[0])
    while t < 70: t *= 2                                             # 说唱常见的「半速 / 倍速」统一到 70–180
    while t > 180: t /= 2
    return t


songs = []
for s in man:
    d = f'{SEP}/{s["id"]}'
    if not os.path.exists(f'{d}/inst.wav'): continue
    mp = f'{MEAS}/{s["id"]}.json'
    if not os.path.exists(mp):
        out = subprocess.run([MM, '--measure-song', f'{d}/vocals.wav', f'{d}/inst.wav', f'{d}/mix.wav'], capture_output=True, text=True, check=True).stdout
        m = json.loads(out); m['bpm'] = bpm_of(f'{d}/inst.wav')
        json.dump(m, open(mp, 'w'))
    s = dict(s); s['m'] = json.load(open(mp)); songs.append(s)

SCAL = ['vbr', 'vocal_spread', 'vocal_sib', 'masked', 'mix_lufs', 'mix_tp', 'mix_corr', 'mix_spread', 'beat_low', 'beat_corr', 'beat_spread']
VEC = ['mix_ltas', 'beat_ltas', 'vocal_ltas']


def stats(xs):
    xs = np.array([x for x in xs if x is not None], dtype=float)
    if len(xs) == 0: return None
    return dict(n=len(xs), med=float(np.median(xs)), p25=float(np.percentile(xs, 25)), p75=float(np.percentile(xs, 75)))


report = {'styles': {}}
for st in sorted({s['style'] for s in songs}):
    ss = [s for s in songs if s['style'] == st]
    r = {'songs': len(ss), 'preview_only': all(s.get('preview') for s in ss)}
    for k in SCAL: r[k] = stats([s['m']['whole'].get(k) for s in ss])
    for k in VEC:
        v = [s['m']['whole'][k] for s in ss if k in s['m']['whole']]
        if v: r[k] = [round(float(x), 2) for x in np.median(np.array(v), axis=0)]
    r['bpm'] = stats([s['m']['bpm'] for s in ss])
    # 副歌比主歌：人声比伴奏、整体响度差多少（有分段的歌才算）
    dv, dl = [], []
    for s in ss:
        sec = s['m']['sections']
        def mean_of(kind, key):
            xs = [x['m'][key] for x in sec if x['kind'] == kind and x['m'].get('has_vocal') and x['m'].get(key) is not None]
            return np.mean(xs) if xs else None
        cv, vv = mean_of('chorus', 'vbr'), mean_of('verse', 'vbr')
        if cv is not None and vv is not None: dv.append(cv - vv)
        cl, vl = mean_of('chorus', 'mix_lufs'), mean_of('verse', 'mix_lufs')
        if cl is not None and vl is not None: dl.append(cl - vl)
    r['chorus_minus_verse_vbr'] = stats(dv); r['chorus_minus_verse_lufs'] = stats(dl)
    # 插件只听 20 秒：每首里 20 秒窗和整首的偏差（中位数的绝对偏差）
    dev = {k: [] for k in ['vbr', 'vocal_spread', 'masked', 'mix_lufs']}
    for s in ss:
        w = [x['m'] for x in s['m']['windows'] if x['m'].get('has_vocal')]
        for k in dev:
            if s['m']['whole'].get(k) is None: continue
            dev[k] += [abs(x[k] - s['m']['whole'][k]) for x in w if x.get(k) is not None]
    r['window_abs_dev'] = {k: (float(np.median(v)) if v else None) for k, v in dev.items()}
    report['styles'][st] = r

# 伴奏分类：每个 20 秒窗的伴奏特征 → 最近的风格中心（特征先按全体标准化）。留一首交叉验证
def feats(m, bpm):
    return np.array(list(m['beat_ltas']) + [m['beat_low'] * 10, m['beat_corr'] * 5, m['beat_spread'], bpm / 10.0])

X, Y, G = [], [], []
for gi, s in enumerate(songs):
    for w in s['m']['windows'] or [{'m': s['m']['whole']}]:
        if w['m'].get('beat_ltas') is None or w['m'].get('beat_lufs', -100) < -50: continue
        X.append(feats(w['m'], s['m']['bpm'])); Y.append(s['style']); G.append(gi)
if X:
    X = np.array(X); Y = np.array(Y); G = np.array(G)
    mu, sd = X.mean(0), X.std(0) + 1e-6
    Z = (X - mu) / sd
    labels = sorted(set(Y))
    correct = 0
    for g in set(G):
        tr, te = G != g, G == g
        cen = {l: Z[tr & (Y == l)].mean(0) for l in labels if (tr & (Y == l)).any()}
        for z, y in zip(Z[te], Y[te]):
            pred = min(cen, key=lambda l: np.sum((z - cen[l]) ** 2))
            correct += pred == y
    report['beat_classifier'] = dict(labels=labels, mu=mu.round(4).tolist(), sd=sd.round(4).tolist(),
                                     centroids={l: Z[Y == l].mean(0).round(4).tolist() for l in labels},
                                     loso_accuracy=float(correct / len(Y)), windows=int(len(Y)))

json.dump(report, open(MEAS + '_report.json', 'w'), ensure_ascii=False, indent=1)


def f(x, k='med', d=1):
    return '-' if x is None else f'{x[k]:.{d}f}'


print(f'{len(songs)} 首歌')
for st, r in report['styles'].items():
    print(f'\n== {st}  {r["songs"]} 首' + ('（只有 30 秒片段）' if r['preview_only'] else ''))
    print(f'  人聲比伴奏 {f(r["vbr"])} dB（{f(r["vbr"], "p25")}..{f(r["vbr"], "p75")}）  人聲動態 {f(r["vocal_spread"])} LU  齒音 {f(r["vocal_sib"])}  遮蔽 {f(r["masked"], d=2)}')
    print(f'  響度 {f(r["mix_lufs"])} LUFS  峰值 {f(r["mix_tp"])} dBTP  相關 {f(r["mix_corr"], d=2)}  伴奏低頻 {f(r["beat_low"], d=2)}  BPM {f(r["bpm"], d=0)}')
    print(f'  總線曲線 {r.get("mix_ltas")}')
    print(f'  伴奏曲線 {r.get("beat_ltas")}')
    print(f'  人聲曲線 {r.get("vocal_ltas")}')
    print(f'  副歌-主歌 人聲比伴奏 {f(r["chorus_minus_verse_vbr"])}  響度 {f(r["chorus_minus_verse_lufs"])}   20 秒窗偏差 {r["window_abs_dev"]}')
if 'beat_classifier' in report:
    b = report['beat_classifier']; print(f'\n伴奏分類（留一首驗證）準確率 {b["loso_accuracy"] * 100:.0f}%，{b["windows"]} 個窗，類別 {b["labels"]}')
