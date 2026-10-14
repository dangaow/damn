#pragma once
// MAKE MIX 的各个处理模块。全部在 prepare() 里分配好内存，process 里不分配、不加锁（实时安全）。
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

namespace vox
{
inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)  { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

// ------------------------------------------------------------------ 双二阶滤波器（RBJ cookbook，转置直接 II 型），每个声道一份状态
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    std::array<float, 2> z1 {}, z2 {};

    void reset() { z1.fill (0); z2.fill (0); }
    float process (float x, int ch) noexcept
    {
        const float y = b0 * x + z1[(size_t) ch];
        z1[(size_t) ch] = b1 * x - a1 * y + z2[(size_t) ch];
        z2[(size_t) ch] = b2 * x - a2 * y;
        return y;
    }
    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
    {
        b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
        a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
    }
    void highPass (double fs, double f, double q)
    {
        const double w = juce::MathConstants<double>::twoPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void lowPass (double fs, double f, double q)
    {
        const double w = juce::MathConstants<double>::twoPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void peak (double fs, double f, double q, double db)
    {
        const double A = std::pow (10.0, db / 40), w = juce::MathConstants<double>::twoPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void highShelf (double fs, double f, double q, double db)
    {
        const double A = std::pow (10.0, db / 40), w = juce::MathConstants<double>::twoPi * f / fs, c = std::cos (w), s = std::sin (w);
        const double al = s / (2 * q), sq = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
             (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
};

// ------------------------------------------------------------------ 压缩器：峰值检测，增益在 dB 域按起音/释放平滑，软拐点
struct Compressor
{
    float thresholdDb = 0, ratio = 1, kneeDb = 6, makeupDb = 0;
    float attackCoef = 0, releaseCoef = 0, grDb = 0;

    void prepare (double fs, float attackMs, float releaseMs)
    {
        attackCoef  = (float) std::exp (-1.0 / (fs * attackMs * 0.001));
        releaseCoef = (float) std::exp (-1.0 / (fs * releaseMs * 0.001));
        grDb = 0;
    }
    // 输入一个样本的检测电平（线性），返回这一刻的增益（线性，含补偿）
    float gainFor (float level) noexcept
    {
        float target = 0;
        if (ratio > 1.0001f)
        {
            const float over = gainToDb (level) - thresholdDb, slope = 1.0f / ratio - 1.0f;
            if (2 * over <= -kneeDb)        target = 0;
            else if (2 * std::abs (over) < kneeDb) target = slope * (over + kneeDb * 0.5f) * (over + kneeDb * 0.5f) / (2 * kneeDb);
            else                             target = slope * over;
        }
        const float coef = target < grDb ? attackCoef : releaseCoef;
        grDb = coef * grDb + (1 - coef) * target;
        return dbToGain (grDb + makeupDb);
    }
};

// ------------------------------------------------------------------ 去齒音：4 阶 Linkwitz-Riley 分频，只压高频那一段（低频原样），高低相加是全通，不改音色
struct DeEsser
{
    juce::dsp::LinkwitzRileyFilter<float> split;
    float thresholdDb = 0, ratio = 4, rangeDb = 0, attackCoef = 0, releaseCoef = 0, grDb = 0;
    bool active = false;

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        split.prepare (spec); split.setType (juce::dsp::LinkwitzRileyFilterType::lowpass); split.setCutoffFrequency (5200.0f);
        attackCoef  = (float) std::exp (-1.0 / (spec.sampleRate * 0.0004));
        releaseCoef = (float) std::exp (-1.0 / (spec.sampleRate * 0.045));
        grDb = 0;
    }
    void reset() { split.reset(); grDb = 0; }
    // 处理一帧（numCh 个声道），就地修改
    void processFrame (float* x, int numCh) noexcept
    {
        float lo[2], hi[2], peak = 0;
        for (int c = 0; c < numCh; ++c) { split.processSample (c, x[c], lo[c], hi[c]); peak = std::max (peak, std::abs (hi[c])); }
        float target = 0;
        if (active)
        {
            const float over = gainToDb (peak) - thresholdDb;
            if (over > 0) target = std::max (-rangeDb, over * (1.0f / ratio - 1.0f));
        }
        const float coef = target < grDb ? attackCoef : releaseCoef;
        grDb = coef * grDb + (1 - coef) * target;
        const float g = dbToGain (grDb);
        for (int c = 0; c < numCh; ++c) x[c] = lo[c] + hi[c] * g;
    }
};

// ------------------------------------------------------------------ 磁带式饱和（在 2 倍超采样里做），带一点不对称（偶次谐波），后面去直流
struct Saturator
{
    float drive = 1, bias = 0, mix = 0, norm = 1, offset = 0;
    std::array<float, 2> dcX {}, dcY {};
    float dcR = 0.9995f;

    void prepare (double fsOversampled) { dcR = (float) std::exp (-juce::MathConstants<double>::twoPi * 8.0 / fsOversampled); dcX.fill (0); dcY.fill (0); }
    // 补偿按 -12 dBFS 的参考电平算：这个电平进出一样大，更大的峰被压住，小信号略抬。
    // （按 0 dBFS 归一会让小信号被放大 drive 倍，加了温暖整体就猛地变响）
    void set (float d, float b, float m)
    {
        drive = d; bias = b; mix = m; offset = std::tanh (d * b);
        constexpr float ref = 0.25f;
        norm = ref / (std::tanh (d * (ref + b)) - offset);
    }
    float process (float x, int ch) noexcept
    {
        if (mix <= 0.0001f) return x;
        float y = (std::tanh (drive * (x + bias)) - offset) * norm;
        // 一阶高通去掉不对称带来的直流
        const float d = y - dcX[(size_t) ch] + dcR * dcY[(size_t) ch];
        dcX[(size_t) ch] = y; dcY[(size_t) ch] = d;
        return x + (d - x) * mix;
    }
};

// ------------------------------------------------------------------ TPT 状态变量滤波器（Cytomic）：增益可以每几个样本改一次而不出咔哒声
struct Svf
{
    enum Type { Bell, LowShelf, HighShelf, BandPass, HighPass };
    float g = 0, k = 1, a1 = 1, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0;
    std::array<float, 2> s1 {}, s2 {};
    void reset() { s1.fill (0); s2.fill (0); }
    void set (Type t, double fs, double f, double q, float db = 0)
    {
        const float A = std::pow (10.0f, db / 40.0f);
        g = (float) std::tan (juce::MathConstants<double>::pi * std::min (f, 0.49 * fs) / fs);
        switch (t)
        {
            case Bell:      k = (float) (1.0 / (q * A)); m0 = 1; m1 = k * (A * A - 1); m2 = 0; break;
            case LowShelf:  g /= std::sqrt (A); k = (float) (1.0 / q); m0 = 1; m1 = k * (A - 1); m2 = A * A - 1; break;
            case HighShelf: g *= std::sqrt (A); k = (float) (1.0 / q); m0 = A * A; m1 = k * (1 - A) * A; m2 = 1 - A * A; break;
            case BandPass:  k = (float) (1.0 / q); m0 = 0; m1 = 1; m2 = 0; break;
            case HighPass:  k = (float) (1.0 / q); m0 = 1; m1 = -k; m2 = -1; break;
        }
        a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    float process (float x, int ch) noexcept
    {
        auto& z1 = s1[(size_t) ch]; auto& z2 = s2[(size_t) ch];
        const float v3 = x - z2, v1 = a1 * z1 + a2 * v3, v2 = z2 + a2 * z1 + a3 * v3;
        z1 = 2 * v1 - z1; z2 = 2 * v2 - z2;
        return m0 * x + m1 * v1 + m2 * v2;
    }
};

// ------------------------------------------------------------------ K 计权（BS.1770 的两级：+4 dB 高架 + 38 Hz 高通），离线测响度和总线电平表用
struct KWeight
{
    Biquad shelf, hp;
    void prepare (double fs) { shelf.highShelf (fs, 1681.97, 0.7071, 4.0); hp.highPass (fs, 38.13, 0.5003); reset(); }
    void reset() { shelf.reset(); hp.reset(); }
    float process (float x, int ch) noexcept { return hp.process (shelf.process (x, ch), ch); }
};

// ------------------------------------------------------------------ 真峰值：4 倍多相插值（8 抽头加窗 sinc），只用来检测
struct TruePeak
{
    std::array<std::array<float, 8>, 2> hist {}; std::array<std::array<float, 8>, 4> taps {}; int pos = 0;
    TruePeak()
    {
        const double pi = juce::MathConstants<double>::pi;
        for (int ph = 0; ph < 4; ++ph)
            for (int i = 0; i < 8; ++i)
            {
                const double x = 3.0 + ph / 4.0 - i;          // 插值点在第 3、4 个样本之间
                const double sinc = std::abs (x) < 1e-9 ? 1.0 : std::sin (pi * x) / (pi * x);
                taps[(size_t) ph][(size_t) i] = (float) (sinc * (0.5 + 0.5 * std::cos (pi * x / 4.5)));
            }
    }
    void reset() { for (auto& h : hist) h.fill (0); pos = 0; }
    // 推入一帧（两个声道），返回最近这段插值后的最大绝对值
    float push (float l, float r) noexcept
    {
        hist[0][(size_t) pos] = l; hist[1][(size_t) pos] = r; pos = (pos + 1) & 7;
        float m = std::max (std::abs (l), std::abs (r));
        for (size_t c = 0; c < 2; ++c)
            for (size_t ph = 1; ph < 4; ++ph)
            {
                float acc = 0;
                for (int i = 0; i < 8; ++i) acc += taps[ph][(size_t) i] * hist[c][(size_t) ((pos + i) & 7)];
                m = std::max (m, std::abs (acc));
            }
        return m;
    }
};

// ------------------------------------------------------------------ 预读限幅器：要求的增益在预读窗口里取最小（单调队列），
// 再做一次略短的窗口平均 → 峰值到来之前增益已经平滑地压到位；最后硬限一次保险
struct Limiter
{
    int la = 64; float ceiling = 0.89f, relMul = 0.9995f, rel = 1, grDb = 0;
    std::vector<float> dl[2], box, dqVal; std::vector<int64_t> dqIdx;
    int dw = 0, bw = 0, bl = 56, head = 0, tail = 0, cap = 0; double boxSum = 0; TruePeak tp; int64_t t = 0;
    void prepare (double fs, int lookahead)
    {
        la = std::max (16, lookahead); bl = la - 8; cap = la + 4;
        for (auto& d : dl) d.assign ((size_t) la + 1, 0.0f);
        box.assign ((size_t) bl, 1.0f); dqIdx.assign ((size_t) cap, 0); dqVal.assign ((size_t) cap, 1.0f);
        relMul = (float) std::exp (-1.0 / (fs * 0.12)); reset();
    }
    void reset()
    {
        for (auto& d : dl) std::fill (d.begin(), d.end(), 0.0f);
        std::fill (box.begin(), box.end(), 1.0f); boxSum = bl; head = tail = 0; dw = bw = 0; rel = 1; grDb = 0; tp.reset(); t = 0;
    }
    int latency() const noexcept { return la; }
    void process (float& l, float& r) noexcept
    {
        const float pk = tp.push (l, r), target = pk > ceiling ? ceiling / pk : 1.0f;
        while (head != tail && dqVal[(size_t) ((tail + cap - 1) % cap)] >= target) tail = (tail + cap - 1) % cap;
        dqIdx[(size_t) tail] = t; dqVal[(size_t) tail] = target; tail = (tail + 1) % cap;
        while (t - dqIdx[(size_t) head] > la) head = (head + 1) % cap;
        rel = std::min (dqVal[(size_t) head], 1.0f - (1.0f - rel) * relMul);
        boxSum += rel - box[(size_t) bw]; box[(size_t) bw] = rel; bw = (bw + 1) % bl;
        const float g = (float) std::min (1.0, boxSum / bl);
        dl[0][(size_t) dw] = l; dl[1][(size_t) dw] = r; dw = (dw + 1) % (la + 1);
        l = juce::jlimit (-ceiling, ceiling, dl[0][(size_t) dw] * g);     // dw 现在指向 la 个样本之前写的那一格
        r = juce::jlimit (-ceiling, ceiling, dl[1][(size_t) dw] * g);
        ++t; grDb = gainToDb (g);
    }
};
} // namespace vox
