#pragma once
// 三条处理链：普通 C++ 类。插件在音频线程里用它们，解算器离线用同一份代码模拟（模拟 = 实际效果）。
#include "Params.h"
#include "Styles.h"
#include "Dsp.h"

namespace mm
{
// 人声 key：每 hop 一帧，6 个频段的能量（dB）。hop 按「内容时间」编号：floor(内容样本位置 / hop)
struct KeySink   { virtual ~KeySink() = default;   virtual void publish (int64_t hop, const float* db) noexcept = 0; };
struct KeySource { virtual ~KeySource() = default; virtual bool read (int64_t hop, bool exact, float* db) noexcept = 0; };

// 整数样本延迟（两个声道一起走）
struct Pad
{
    std::vector<float> buf[2]; int w = 0, d = 0;
    void prepare (int delay) { d = std::max (0, delay); for (auto& b : buf) b.assign ((size_t) d + 1, 0.0f); w = 0; }
    void reset() { for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f); w = 0; }
    void frame (float& l, float& r) noexcept
    {
        if (d == 0) return;
        buf[0][(size_t) w] = l; buf[1][(size_t) w] = r; w = w + 1 > d ? 0 : w + 1;
        l = buf[0][(size_t) w]; r = buf[1][(size_t) w];
    }
};

// 一组带通 + 包络：每 hop 输出 6 个频段的平均能量（dB）
struct BandMeter
{
    vox::Svf bp[kKeyBands]; double acc[kKeyBands] {}; int count = 0;
    void prepare (double fs) { for (int k = 0; k < kKeyBands; ++k) { bp[k].set (vox::Svf::BandPass, fs, kKeyHz[k], 1.2); bp[k].reset(); } reset(); }
    void reset() { for (auto& a : acc) a = 0; count = 0; for (auto& b : bp) b.reset(); }
    void push (float x) noexcept { for (int k = 0; k < kKeyBands; ++k) { const float y = bp[k].process (x, 0); acc[k] += (double) y * y; } ++count; }
    void take (float* db) noexcept
    {
        for (int k = 0; k < kKeyBands; ++k) { db[k] = powToDb ((float) (acc[k] / std::max (1, count)) * 2.0f); acc[k] = 0; }
        count = 0;
    }
};

class VocalChain
{
public:
    struct P { float input = 0, hpf = 90, mud = -2, pres = 2, air = 3, comp = 0.4f, deess = 0.4f, warmth = 0.2f, smooth = 0.3f, level = 0; };
    static P fromSet (const ParamSet&);
    void prepare (double fs, int maxBlock, int latency);
    void reset();
    void setParams (const P&, StyleId, double bpm);
    // ch：nCh（1 或 2）个声道，就地处理 n 个样本；pos：这一块第一个样本的内容位置
    void process (float* const* ch, int nCh, int n, int64_t pos, KeySink* sink) noexcept;
    float grComp = 0, grDeess = 0;

private:
    double fs = 44100, bpm = 120; int hop = 256, osLat = 0;
    P p; StyleId style = 0;
    vox::Biquad hpA, hpB, mud, pres, air; vox::Compressor lev, comp; vox::DeEsser deess; vox::Saturator sat;
    float levMs = 0, levCoef = 0.999f;                  // 电平器（慢、RMS）+ 峰值压缩（快）：录音室常见的两级串联
    vox::Svf harshBp, harshBell; float envHarsh = 0, envFull = 0, envCoef = 0.999f, harshCut = 0, harshAtk = 0, harshRel = 0; int harshCount = 0;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    Pad pad; BandMeter key; float keyDb[kKeyBands] {};
    juce::SmoothedValue<float> inGain, outGain;
    float makeupDb = 0;
};

class BeatChain
{
public:
    struct P { float hpf = 25, pocket = -1.5f, unmask = 6, glue = 0.3f, level = 0; };
    static P fromSet (const ParamSet&);
    void prepare (double fs, int maxBlock, int latency);
    void reset();
    void setParams (const P&, StyleId);
    // playing = false 时宿主时间不可靠：读最新的人声 key（近似）
    void process (float* const* ch, int nCh, int n, int64_t pos, bool playing, KeySource* src) noexcept;
    float cutDb[kKeyBands] {};                         // 当前每段让位量（界面显示）
    int lateHops = 0;                                   // 读 key 时那一帧还没写好的次数（诊断）

private:
    double fs = 44100; int hop = 256, L = 1024;
    P p; StyleId style = 0;
    Pad pad; vox::Biquad hpA, hpB; vox::Svf pocket, bell[kKeyBands]; BandMeter meter;
    vox::Compressor glue; float glueMakeup = 1;          // 伴奏自己的轻粘合：鼓和 808、旋律更像一体
    std::vector<std::array<float, kKeyBands>> beatRing; int64_t lastHop = INT64_MIN;
    float target[kKeyBands] {}, cur[kKeyBands] {}, atk = 0, rel = 0; int coefCount = 0;
    juce::SmoothedValue<float> outGain;
    void startHop (int64_t h, bool playing, KeySource* src) noexcept;
};

class BusChain
{
public:
    struct P { float low = 0, high = 0, glue = 0.35f, lift = 0.5f, width = 100, drive = 0, ceiling = -1; };
    static P fromSet (const ParamSet&);
    void prepare (double fs, int maxBlock, int latency);
    void reset();
    void setParams (const P&, StyleId);
    void process (float* const* ch, int nCh, int n) noexcept;
    float grGlue = 0, grLimit = 0, loudShort = -100;   // 电平表：粘合 / 限幅压缩量，短期响度（3 秒）

private:
    double fs = 44100; P p; StyleId style = 0;
    vox::Svf low, high, scHp; vox::Compressor glue; vox::Limiter lim; Pad pad; vox::KWeight kw;
    // 提升：三段（120 Hz / 6 kHz，LR4 分频，相加平直）——低频压紧 + 泛音 + 收成单声道，中高频加宽，高频动态空气感，整体 2 倍超采样磁带饱和
    juce::dsp::LinkwitzRileyFilter<float> x1, x2; vox::Compressor lowComp; vox::Svf airShelf; vox::Saturator tape;
    std::unique_ptr<juce::dsp::Oversampling<float>> os; int osLat = 0;
    float envHi = 1e-9f, envMid = 1e-9f, envCoef = 0.999f, airDb = 0, dcX = 0, dcY = 0, lowMakeup = 1; int airCount = 0;
    juce::SmoothedValue<float> drive, width; double loudAcc = 0; int loudN = 0; float loudCoef = 0.999f, loudState = 1e-10f;
};
} // namespace mm
