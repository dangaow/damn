#include "Analysis.h"
#include "Dsp.h"
#include "Styles.h"
#include <cstdio>

namespace mm::analysis
{
namespace
{
// K 计权（+ 可选 150 Hz 高通）后的单帧功率（两声道相加）
std::vector<float> kPower (const Stereo& x, double fs, bool hpf150)
{
    vox::KWeight kw; kw.prepare (fs);
    vox::Biquad h1, h2; h1.highPass (fs, 150, 0.5412); h2.highPass (fs, 150, 1.3066); h1.reset(); h2.reset();
    std::vector<float> p (x.size());
    for (size_t i = 0; i < x.size(); ++i)
    {
        float l = x.l[i], r = x.r[i];
        if (hpf150) { l = h2.process (h1.process (l, 0), 0); r = h2.process (h1.process (r, 1), 1); }
        l = kw.process (l, 0); r = kw.process (r, 1);
        p[i] = l * l + r * r;
    }
    return p;
}
std::vector<float> blocks (const std::vector<float>& p, double fs)       // 400 ms 块、100 ms 步进的均方
{
    const size_t win = (size_t) (0.4 * fs), stp = (size_t) (0.1 * fs);
    std::vector<float> out;
    if (p.size() < win) return out;
    std::vector<double> cum (p.size() + 1, 0.0);
    for (size_t i = 0; i < p.size(); ++i) cum[i + 1] = cum[i] + p[i];
    for (size_t s = 0; s + win <= p.size(); s += stp) out.push_back ((float) ((cum[s + win] - cum[s]) / (double) win));
    return out;
}
float gatedMean (const std::vector<float>& ms)
{
    double sum = 0; int n = 0;
    for (float m : ms) if (-0.691f + powToDb (m) > -70) { sum += m; ++n; }
    if (n == 0) return -100;
    const float rel = -0.691f + powToDb ((float) (sum / n)) - 10;
    sum = 0; n = 0;
    for (float m : ms) if (-0.691f + powToDb (m) > rel) { sum += m; ++n; }
    return n == 0 ? -100.0f : -0.691f + powToDb ((float) (sum / n));
}
// 活跃帧（1024 点）的起点：瞬时响度高于积分 - 20
std::vector<size_t> activeFrames (const Stereo& x, double fs, size_t frame)
{
    const auto mom = momentary (x, fs, true);
    const float I = integrated (x, fs, true), gate = std::max (-60.0f, I - 20);
    std::vector<size_t> out;
    const size_t stp = (size_t) (0.1 * fs), win = (size_t) (0.4 * fs);
    for (size_t s = 0; s + frame <= x.size(); s += frame)
    {
        const size_t centre = s + frame / 2, bi = centre > win / 2 ? (centre - win / 2) / stp : 0;
        if (bi < mom.size() && mom[bi] > gate) out.push_back (s);
    }
    return out;
}
} // namespace

float integrated (const Stereo& x, double fs, bool hpf150) { return gatedMean (blocks (kPower (x, fs, hpf150), fs)); }

std::vector<float> momentary (const Stereo& x, double fs, bool hpf150)
{
    auto b = blocks (kPower (x, fs, hpf150), fs);
    for (auto& v : b) v = -0.691f + powToDb (v);
    return b;
}

float percentile (std::vector<float> v, float q)
{
    if (v.empty()) return -100;
    const size_t k = (size_t) std::clamp ((double) q * (double) (v.size() - 1), 0.0, (double) (v.size() - 1));
    std::nth_element (v.begin(), v.begin() + (long) k, v.end());
    return v[k];
}

float spread (const Stereo& x, double fs)
{
    const auto mom = momentary (x, fs, true);
    const float I = integrated (x, fs, true);
    std::vector<float> act;
    for (float m : mom) if (m > I - 20) act.push_back (m);
    if (act.size() < 10) return 0;
    return percentile (act, 0.9f) - percentile (act, 0.1f);
}

namespace
{
template <typename F> void forFrames (const Stereo& x, double fs, F&& f)        // 对每个活跃帧做 FFT，回调功率谱
{
    constexpr int order = 11, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> win (N), buf (2 * N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / N);
    for (size_t s : activeFrames (x, fs, N))
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = 0.5f * (x.l[s + (size_t) i] + x.r[s + (size_t) i]) * win[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        f (buf.data(), N);
    }
}
} // namespace

std::array<float, kOctBands> octaveLtas (const Stereo& x, double fs)
{
    std::array<double, kOctBands> acc {};
    forFrames (x, fs, [&] (const float* mag, int N)
    {
        for (int b = 1; b < N / 2; ++b)
        {
            const double f = b * fs / N;
            for (int k = 0; k < kOctBands; ++k)
                if (f >= kOctCentresHz[k] / std::sqrt (2.0) && f < kOctCentresHz[k] * std::sqrt (2.0)) { acc[(size_t) k] += (double) mag[b] * mag[b]; break; }
        }
    });
    std::array<float, kOctBands> db {};
    for (int k = 0; k < kOctBands; ++k) db[(size_t) k] = powToDb ((float) acc[(size_t) k] + 1e-20f);
    const float ref = (db[4] + db[5] + db[6]) / 3;
    for (auto& d : db) d -= ref;
    return db;
}

float sibilance (const Stereo& x, double fs)
{
    std::vector<float> ratio;
    forFrames (x, fs, [&] (const float* mag, int N)
    {
        double hi = 0, all = 0;
        for (int b = 1; b < N / 2; ++b) { const double f = b * fs / N, p = (double) mag[b] * mag[b]; all += p; if (f >= 5000 && f < 10000) hi += p; }
        if (all > 1e-12) ratio.push_back (powToDb ((float) (hi / all)));
    });
    if (ratio.size() < 10) return 0;
    return percentile (ratio, 0.95f) - percentile (ratio, 0.5f);
}

float truePeakDb (const Stereo& x)
{
    vox::TruePeak tp; float m = 0;
    for (size_t i = 0; i < x.size(); ++i) m = std::max (m, tp.push (x.l[i], x.r[i]));
    return gainToDb (m);
}
float samplePeakDb (const Stereo& x)
{
    float m = 0;
    for (size_t i = 0; i < x.size(); ++i) m = std::max ({ m, std::abs (x.l[i]), std::abs (x.r[i]) });
    return gainToDb (m);
}

float maskedFraction (const Stereo& vocal, const Stereo& beat, double fs)
{
    constexpr int order = 11, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> win (N), bv (2 * N), bb (2 * N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / N);
    const size_t n = std::min (vocal.size(), beat.size());
    int masked = 0, total = 0;
    Stereo v { std::vector<float> (vocal.l.begin(), vocal.l.begin() + (long) n), std::vector<float> (vocal.r.begin(), vocal.r.begin() + (long) n) };
    for (size_t s : activeFrames (v, fs, N))
    {
        std::fill (bv.begin(), bv.end(), 0.0f); std::fill (bb.begin(), bb.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            bv[(size_t) i] = 0.5f * (vocal.l[s + (size_t) i] + vocal.r[s + (size_t) i]) * win[(size_t) i];
            bb[(size_t) i] = 0.5f * (beat.l[s + (size_t) i] + beat.r[s + (size_t) i]) * win[(size_t) i];
        }
        fft.performFrequencyOnlyForwardTransform (bv.data(), true); fft.performFrequencyOnlyForwardTransform (bb.data(), true);
        // 1–5 kHz 分成 1/3 倍频程小格，数伴奏比人声大的格子
        for (double f0 = 1000; f0 < 5000; f0 *= std::pow (2.0, 1.0 / 3))
        {
            double pv = 0, pb = 0;
            for (int b = (int) (f0 * N / fs); b < (int) (f0 * std::pow (2.0, 1.0 / 3) * N / fs); ++b) { pv += (double) bv[(size_t) b] * bv[(size_t) b]; pb += (double) bb[(size_t) b] * bb[(size_t) b]; }
            ++total; if (pb > pv) ++masked;
        }
    }
    return total == 0 ? 0.0f : (float) masked / (float) total;
}

// ---------------------------------------------------------------------------------------- 调性 / 宽度 / 低频
namespace
{
const char* kNotes[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
constexpr float kMajor[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
constexpr float kMinor[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
float pearson (const float* a, const float* b, int n)
{
    float ma = 0, mb = 0; for (int i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; } ma /= n; mb /= n;
    float sab = 0, saa = 0, sbb = 0;
    for (int i = 0; i < n; ++i) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); }
    return sab / std::sqrt (std::max (1e-12f, saa * sbb));
}
std::string f1 (float v) { char b[32]; std::snprintf (b, sizeof b, "%.1f", v); return b; }
std::string f0 (float v) { char b[32]; std::snprintf (b, sizeof b, "%.0f", v); return b; }
std::string sgn (float v) { char b[32]; std::snprintf (b, sizeof b, "%+.1f", v); return b; }
}

std::string Key::name() const { return std::string (kNotes[tonic % 12]) + (minor ? " minor" : " major"); }
std::string Key::relative() const
{
    return minor ? std::string (kNotes[(tonic + 3) % 12]) + " major" : std::string (kNotes[(tonic + 9) % 12]) + " minor";
}

Key detectKey (const Stereo& x, double fs)
{
    constexpr int order = 13, N = 1 << order;            // 8192 点：低音区也分得清半音
    juce::dsp::FFT fft (order);
    std::vector<float> win (N), buf (2 * N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / N);
    float chroma[12] {};
    for (size_t s = 0; s + N <= x.size(); s += N / 2)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = 0.5f * (x.l[s + (size_t) i] + x.r[s + (size_t) i]) * win[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        // 只取局部峰（谐波），按能量记到音级上；每帧归一，别让响的段落 / 808 一个音统治
        float frame[12] {}; double tot = 0;
        for (int b = 2; b < N / 2 - 1; ++b)
        {
            const double f = b * fs / N;
            if (f < 65 || f > 2500) continue;
            const float m = buf[(size_t) b];
            if (m <= buf[(size_t) b - 1] || m < buf[(size_t) b + 1]) continue;
            const int pc = ((int) std::lround (12.0 * std::log2 (f / 440.0)) + 9 + 1200) % 12;
            const float w = (float) std::sqrt (m) * (f < 160 ? 0.5f : 1.0f);
            frame[pc] += w; tot += w;
        }
        if (tot > 0) for (int i = 0; i < 12; ++i) chroma[i] += (float) (frame[i] / tot);
    }
    Key best; float bestR = -2, second = -2;
    for (int t = 0; t < 12; ++t)
        for (int m = 0; m < 2; ++m)
        {
            float prof[12]; for (int i = 0; i < 12; ++i) prof[i] = (m ? kMinor : kMajor)[(i - t + 12) % 12];
            const float r = pearson (chroma, prof, 12);
            if (r > bestR) { second = bestR; bestR = r; best.tonic = t; best.minor = m == 1; }
            else if (r > second) second = r;
        }
    best.confidence = std::clamp ((bestR - second) * 4.0f + bestR * 0.5f, 0.0f, 1.0f);
    return best;
}

float correlation (const Stereo& x)
{
    double lr = 0, ll = 0, rr = 0;
    for (size_t i = 0; i < x.size(); ++i) { lr += (double) x.l[i] * x.r[i]; ll += (double) x.l[i] * x.l[i]; rr += (double) x.r[i] * x.r[i]; }
    return (float) (lr / std::sqrt (std::max (1e-12, ll * rr)));
}

float lowShare (const Stereo& x, double fs)
{
    vox::KWeight kw; kw.prepare (fs); vox::Biquad lp1, lp2; lp1.lowPass (fs, 150, 0.5412); lp2.lowPass (fs, 150, 1.3066); lp1.reset(); lp2.reset();
    double lo = 0, all = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const float m = kw.process (0.5f * (x.l[i] + x.r[i]), 0), l = lp2.process (lp1.process (m, 0), 0);
        lo += (double) l * l; all += (double) m * m;
    }
    return all > 0 ? (float) (lo / all) : 0.0f;
}

// ---------------------------------------------------------------------------------------- 一键分析
Report analyse (int role, const Stereo& x, double fs, int style, double bpm)
{
    Report r; r.role = role; r.seconds = (float) (x.size() / fs);
    r.lufs = integrated (x, fs); r.tp = truePeakDb (x); r.ltas = octaveLtas (x, fs); r.width = 1.0f - correlation (x);
    auto& L = r.lines;
    if (r.lufs < -69) { L.push_back ("沒有聲音。先播放，再按一鍵分析。"); return r; }
    L.push_back ("響度 " + f1 (r.lufs) + " LUFS   峰值 " + f1 (r.tp) + " dBTP");
    if (r.tp > -0.3f) L.push_back ("! 峰值頂到 0 dBFS：在這個插件之前把音量調低一點");

    if (role == 0)                                      // —— 人声
    {
        for (int k = 0; k < kOctBands; ++k) r.target[(size_t) k] = kRefVocal[k];
        const float vb = integrated (x, fs, true); r.spread = spread (x, fs);
        auto dev = [&] (int k) { return r.ltas[(size_t) k] - kRefVocal[k]; };
        L.push_back ("動態 " + f1 (r.spread) + " LU（P90-P10）   齒音 " + f1 (sibilance (x, fs)) + " dB");
        if (vb < -32) L.push_back ("錄音偏小聲：一鍵混音會把輸入提高 " + sgn (-20 - vb) + " dB");
        if (r.spread > 8) L.push_back ("音量忽大忽小：需要多一點壓縮");
        else if (r.spread < 4) L.push_back ("已經很密了：只需要很少的壓縮");
        if (lowShare (x, fs) > 0.08f) L.push_back ("150 Hz 以下有轟聲 / 噴麥：把低切調高");
        if (dev (3) > 3) L.push_back ("250 Hz 附近發悶發渾（" + sgn (dev (3)) + " dB）：加大去渾濁");
        const float pres = 0.5f * (dev (6) + dev (7));
        if (pres < -3) L.push_back ("偏悶、躲在伴奏後面（2-4 kHz " + sgn (pres) + " dB）：加存在感");
        else if (pres > 3) L.push_back ("2-4 kHz 偏刺（" + sgn (pres) + " dB）：少一點存在感");
        if (dev (8) < -4) L.push_back ("高頻偏暗（8 kHz " + sgn (dev (8)) + " dB）：加空氣感");
        else if (dev (8) > 4) L.push_back ("高頻很亮（8 kHz " + sgn (dev (8)) + " dB）：少一點空氣感、多一點去齒音");
        if (sibilance (x, fs) > 14) L.push_back ("齒音（ㄙ、ㄘ、ㄕ）很重：加去齒音");
        if (L.size() <= 2) L.push_back ("人聲狀態健康：一鍵混音主要調電平和風格");
    }
    else if (role == 1)                                 // —— 伴奏
    {
        const float low = lowShare (x, fs);
        r.key = detectKey (x, fs); r.hasKey = true;
        L.push_back ("調性 " + r.key.name() + "（或關係調 " + r.key.relative() + "）" + (r.key.confidence < 0.35f ? "  - 不太確定" : ""));
        L.push_back ("低頻 " + f0 (low * 100) + "% 的能量在 150 Hz 以下" + (low > 0.6f ? "（808 / 底鼓很重）" : low < 0.25f ? "（低頻偏輕）" : ""));
        const float busy = 0.5f * (r.ltas[6] + r.ltas[7]);
        L.push_back (busy > 0.5f ? "人聲頻段很擠（2-4 kHz " + sgn (busy) + " dB）：伴奏讓位很重要"
                     : busy < -4 ? "給人聲留了空間（2-4 kHz " + sgn (busy) + " dB）"
                                 : "人聲頻段密度中等（2-4 kHz " + sgn (busy) + " dB）");
        const float corr = 1.0f - r.width;
        L.push_back ("立體聲 " + std::string (corr > 0.95f ? "接近單聲道" : corr > 0.6f ? "正常寬度" : corr > 0.3f ? "偏寬" : "很寬") + "（相關 " + f1 (corr) + "）");
        // 最接近的风格：谱形和风格曲线的距离 + 速度是否在这个风格的范围里（半速 / 倍速也算）
        static const float kTempo[kNumStyles][2] = { { 125, 170 }, { 138, 148 }, { 75, 170 }, { 80, 98 }, { 60, 105 }, { 70, 130 },
                                                     { 60, 88 }, { 68, 92 }, { 95, 130 }, { 100, 170 }, { 118, 132 }, { 96, 118 }, { 0, 0 },
                                                     { 78, 125 }, { 125, 160 } };
        float bestScore = 1e9;
        for (int s = 0; s < kNumStyles; ++s)
        {
            if (std::string_view (mm::style (s).key) == "makestudio") continue;   // 不推荐 MAKE STUDIO（那是自己的风格，不是猜出来的）
            // 训练过的风格用真实伴奏的谱形；没训练的用总线曲线近似
            const auto& bc = kStyleBeatCurve[(size_t) s]; const bool trained = std::abs (bc[5]) + std::abs (bc[1]) > 0.01f;
            float d = 0; for (int k = 1; k < kOctBands; ++k) { const float e = r.ltas[(size_t) k] - (trained ? bc[(size_t) k] : mm::style (s).busCurve[(size_t) k]); d += e * e; }
            d = std::sqrt (d / (kOctBands - 1));
            bool inTempo = false;
            for (double t : { bpm, bpm * 2, bpm / 2 }) inTempo |= t >= kTempo[s][0] && t <= kTempo[s][1];
            // 808 / 底鼓很重的伴奏更像说唱类（trap / drill / melodic / edm）
            const bool heavy = s == 0 || s == 1 || s == 2 || s == 10;
            const float score = d + (inTempo ? 0.0f : 3.0f) + (low > 0.7f ? (heavy ? -1.5f : 0.0f) : (heavy ? 0.5f : 0.0f));
            if (score < bestScore) { bestScore = score; r.suggestedStyle = s; }
        }
        if (r.suggestedStyle >= 0) L.push_back ("最接近的風格 " + std::string (mm::style (r.suggestedStyle).name) + "（" + f0 ((float) bpm) + " BPM）");
        for (int k = 0; k < kOctBands; ++k) r.target[(size_t) k] = mm::style (std::max (0, r.suggestedStyle)).busCurve[(size_t) k];
    }
    else                                                // —— 总线（整首混音）
    {
        const auto& st = mm::style (style);
        for (int k = 0; k < kOctBands; ++k) r.target[(size_t) k] = st.busCurve[(size_t) k];
        auto region = [&] (int a, int b) { float d = 0; for (int k = a; k <= b; ++k) d += r.ltas[(size_t) k] - st.busCurve[(size_t) k]; return d / (b - a + 1); };
        L.push_back ("目標 " + std::string (st.name) + "：" + f1 (st.lufsStyle) + " LUFS，" + f1 (st.ceiling) + " dBTP（現在差 " + sgn (r.lufs - st.lufsStyle) + " LU）");
        const float lo = region (0, 2), mid = region (3, 6), hi = region (7, 9);
        L.push_back ("對比風格曲線   低頻 " + sgn (lo) + "   中頻 " + sgn (mid) + "   高頻 " + sgn (hi) + " dB");
        if (lo > 2.5f) L.push_back ("對這個風格來說低頻太多"); else if (lo < -2.5f) L.push_back ("對這個風格來說低頻偏薄");
        if (hi > 2.5f) L.push_back ("對這個風格來說高頻偏亮 / 偏刺"); else if (hi < -2.5f) L.push_back ("對這個風格來說高頻偏悶");
        const float corr = correlation (x);
        L.push_back ("立體聲相關 " + f1 (corr) + (corr < 0.2f ? "  ! 檢查單聲道兼容" : ""));
        if (r.tp > st.ceiling) L.push_back ("峰值超過 " + f1 (st.ceiling) + " dBTP 天花板：一鍵混音會設好限幅");
    }
    return r;
}
} // namespace mm::analysis
