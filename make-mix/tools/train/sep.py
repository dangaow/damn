# 人声分离（MSST 的 BS-Roformer viperx ep317，人声 SDR 12.98）。整首歌，CPU 上用 bf16（AMX）跑。
# 8 秒一块、重叠 1 秒、等功率交叉淡化；伴奏 = 原曲 - 人声（和 MSST 的 --extract_instrumental 一样）。
# 用法：python sep.py OUT_DIR file1 file2 ...   （已经分离过的跳过；输出 OUT_DIR/<名字>/{mix,vocals,inst}.wav）
import os, sys, time, subprocess, numpy as np, soundfile as sf, torch

MSST = os.environ.get('MSST_DIR', '/home/user/msst')
sys.path.insert(0, MSST)
from utils.settings import get_model_from_config

SR, CHUNK, OVL = 44100, 352800, 44100


def load_model():
    torch.set_num_threads(os.cpu_count() or 4)
    model, _ = get_model_from_config('bs_roformer', f'{MSST}/configs/viperx/model_bs_roformer_ep_317_sdr_12.9755.yaml')
    sd = torch.load(f'{MSST}/ckpt/model_bs_roformer_ep_317_sdr_12.9755.ckpt', map_location='cpu', weights_only=False)
    model.load_state_dict(sd)
    return model.eval()


def decode(path):
    # 任何格式 → 44.1 kHz 立体声 float32
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-f', 'f32le', '-ac', '2', '-ar', str(SR), '-'],
                         check=True, capture_output=True).stdout
    return np.frombuffer(raw, dtype=np.float32).reshape(-1, 2).T.copy()


def separate(model, mix):
    n = mix.shape[1]
    step = CHUNK - OVL
    out = np.zeros_like(mix); wsum = np.zeros(n, dtype=np.float32)
    fade = np.sin(np.linspace(0, np.pi / 2, OVL, dtype=np.float32)) ** 2
    for start in range(0, max(1, n - OVL), step):
        seg = mix[:, start:start + CHUNK]
        m = seg.shape[1]
        if m < CHUNK: seg = np.pad(seg, ((0, 0), (0, CHUNK - m)))
        with torch.inference_mode(), torch.autocast('cpu', dtype=torch.bfloat16):
            y = model(torch.from_numpy(seg).unsqueeze(0)).float()[0].reshape(2, -1).numpy()[:, :m]
        w = np.ones(m, dtype=np.float32)
        if start > 0: w[:OVL] = fade[:min(OVL, m)]
        if start + CHUNK < n: w[-OVL:] = fade[::-1][-min(OVL, m):]
        out[:, start:start + m] += y * w; wsum[start:start + m] += w
    return out / np.maximum(wsum, 1e-6)


def main():
    out_dir, files = sys.argv[1], sys.argv[2:]
    model = load_model()
    for i, f in enumerate(files):
        name = os.path.splitext(os.path.basename(f))[0]
        d = os.path.join(out_dir, name)
        if os.path.exists(os.path.join(d, 'inst.wav')): continue
        os.makedirs(d, exist_ok=True)
        t = time.time()
        mix = decode(f)
        voc = separate(model, mix)
        sf.write(os.path.join(d, 'mix.wav'), mix.T, SR, subtype='FLOAT')
        sf.write(os.path.join(d, 'vocals.wav'), voc.T, SR, subtype='FLOAT')
        sf.write(os.path.join(d, 'inst.wav'), (mix - voc).T, SR, subtype='FLOAT')
        print(f'[{i + 1}/{len(files)}] {name}  {mix.shape[1] / SR:.0f} s audio in {time.time() - t:.0f} s', flush=True)


if __name__ == '__main__':
    main()
