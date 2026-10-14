#pragma once
// MAKE VOX 的各个处理模块。全部在 prepare() 里分配好内存，process 里不分配、不加锁（实时安全）。
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

// ------------------------------------------------------------------ 环形缓冲的延迟线（线性插值读）
struct DelayLine
{
    std::vector<float> buf; int w = 0;
    void prepare (int maxSamples) { buf.assign ((size_t) maxSamples + 4, 0.0f); w = 0; }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    void push (float x) noexcept { buf[(size_t) w] = x; if (++w >= (int) buf.size()) w = 0; }
    float read (float delaySamples) const noexcept
    {
        const int n = (int) buf.size();
        float r = (float) w - 1.0f - delaySamples; while (r < 0) r += (float) n;
        const int i0 = (int) r; const float f = r - (float) i0; const int i1 = (i0 + 1) % n;
        return buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * f;
    }
};

// ------------------------------------------------------------------ 回声：跟着宿主 BPM 的乒乓延迟，反馈里带高通/低通；人声大的时候自动压低（ducking），唱完才浮出来
struct Echo
{
    DelayLine dl[2]; Biquad hp, lp; double fs = 44100;
    float send = 0, feedback = 0.3f, duck = 0.65f, env = 0, envRelease = 0, envAttack = 0;
    juce::SmoothedValue<float> delaySamples;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& d : dl) d.prepare ((int) (fs * 2.5));
        hp.highPass (fs, 320, 0.7); lp.lowPass (fs, 4800, 0.7); hp.reset(); lp.reset();
        envAttack = (float) std::exp (-1.0 / (fs * 0.005)); envRelease = (float) std::exp (-1.0 / (fs * 0.28));
        delaySamples.reset (fs, 0.25); delaySamples.setCurrentAndTargetValue ((float) (fs * 0.5));
        env = 0;
    }
    void reset() { for (auto& d : dl) d.reset(); hp.reset(); lp.reset(); env = 0; }
    void setTime (double seconds) { delaySamples.setTargetValue ((float) juce::jlimit (0.02 * fs, 2.4 * fs, seconds * fs)); }
    // dry：这一帧干声（单声道检测用），in：送进回声的单声道信号；输出加到 outL / outR
    void process (float dry, float in, float& outL, float& outR) noexcept
    {
        const float a = std::abs (dry), c = a > env ? envAttack : envRelease;
        env = c * env + (1 - c) * a;
        const float d = delaySamples.getNextValue();
        const float rl = dl[0].read (d), rr = dl[1].read (d);
        const float fbL = lp.process (hp.process (rr, 0), 0) * feedback;   // 右边回到左边，左边回到右边：乒乓
        const float fbR = lp.process (hp.process (rl, 1), 1) * feedback;
        dl[0].push (in * send + fbL);
        dl[1].push (fbR);
        const float duckGain = 1.0f - duck * juce::jlimit (0.0f, 1.0f, env * 5.0f);
        outL += rl * duckGain; outR += rr * duckGain;
    }
};

// ------------------------------------------------------------------ 空间：预延迟 + 滤掉低频和最高频的混响送出
struct Space
{
    juce::Reverb verb; DelayLine pre[2]; Biquad hp, lp; double fs = 44100; float amount = 0, preDelay = 0;
    void prepare (double sampleRate)
    {
        fs = sampleRate; verb.setSampleRate (fs);
        juce::Reverb::Parameters p; p.roomSize = 0.78f; p.damping = 0.42f; p.wetLevel = 1.0f; p.dryLevel = 0.0f; p.width = 1.0f; p.freezeMode = 0.0f;
        verb.setParameters (p); verb.reset();
        for (auto& d : pre) d.prepare ((int) (fs * 0.1));
        preDelay = (float) (fs * 0.028);
        hp.highPass (fs, 240, 0.7); lp.lowPass (fs, 8500, 0.7); hp.reset(); lp.reset();
    }
    void reset() { verb.reset(); for (auto& d : pre) d.reset(); hp.reset(); lp.reset(); }
};
} // namespace vox
