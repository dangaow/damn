#include "Analysis.h"
#include "Dsp.h"

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
} // namespace mm::analysis
