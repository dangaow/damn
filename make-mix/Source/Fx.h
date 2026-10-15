#pragma once
// 风格化效果：电话 / 收音机 / 对讲机 / 扩音器 / 失真 / Lo-fi / 地下 / 录音带 / 黑胶 / 游戏机。只染色，不加混响、不加回声。
// 三个角色共用。每帧处理，不分配内存。固定延迟 latency()（录音带 / 黑胶 / Lo-fi 的音高抖动要用），关掉时也走同样的延迟，
// 所以整条链的延迟不随模式变。湿声自动对齐干声的响度（300 ms），换模式不会忽大忽小，解算器量出来的电平也稳定。
#include "Dsp.h"

namespace mm
{
enum FxMode : int { FxOff, FxPhone, FxRadio, FxWalkie, FxMega, FxDist, FxLofi, FxUnder, FxTape, FxVinyl, FxGame, kNumFx };
inline constexpr const char* kFxNames[kNumFx] = { "關閉", "電話", "收音機", "對講機", "擴音器", "失真", "Lo-fi", "地下 UDG", "錄音帶", "黑膠", "遊戲機 8-bit" };

// 每个模式的配方（0 = 不用这一级）
struct FxDef
{
    float hp, lp;            // 高通 / 低通（Hz，24 dB/oct）
    float bellF, bellDb;     // 中频凸起
    float lowDb;             // 100 Hz 低架（录音带的「磁头凸起」、地下的厚）
    float drive, asym, hard; // 饱和：推多少 dB、偶次谐波（不对称）、硬削波比例
    float bits, rateHz;      // 降位深 / 降采样（采样保持，不做抗混叠：要的就是那个脏）
    float wowMs, flutMs;     // 音高抖动：慢（0.6 Hz）/ 快（7 Hz）
    float mono;              // 收成单声道的比例
};
inline constexpr FxDef kFx[kNumFx] = {
    //  hp     lp    bellF bellDb lowDb drive  asym  hard  bits  rate    wow   flut  mono
    {    0,     0,     0,  0,     0,    0,     0,    0,    0,     0,     0,    0,    0 },     // 关闭
    {  350,  3400,  1700,  4,     0,    8,     0.10f, 0,   0,  8000,     0,    0,    1 },     // 电话：窄带 + 一点失真 + 8 kHz 采样感
    {  220,  5000,  1300,  3,     0,    5,     0.05f, 0,   0,     0,     0,    0,    1 },     // 收音机：AM 的中频鼻音
    {  500,  2800,  1500,  5,     0,   16,     0,  0.7f,   0, 11025,     0,    0,    1 },     // 对讲机：更窄、硬削波
    {  600,  4200,  2200,  8,     0,   20,  0.15f, 0.4f,   0,     0,     0,    0,    1 },     // 扩音器：喇叭的尖和破
    {   40,  9000,   900,  2,     0,   18,  0.20f, 0.3f,   0,     0,     0,    0,    0 },     // 失真
    {   60,  6500,     0,  0,  1.5f,    4,  0.05f,    0,  10, 22050,  0.6f, 0.08f,    0 },     // Lo-fi：暗、10 bit、抖
    {   30,  9500,   250,  3,     2,   10,  0.10f,    0,   0,     0,     0,    0,    0 },     // 地下 UDG：暗、脏、低中厚
    {   30, 11000,     0,  0,  2.5f,    5,  0.05f,    0,   0,     0, 0.35f, 0.06f,    0 },     // 录音带：磁头低频凸起 + 抖动
    {   45, 12000,     0,  0,     1,    3,  0.02f,    0,   0,     0,  0.2f, 0.03f, 0.2f },     // 黑胶
    {    0,     0,     0,  0,     0,    2,     0,     0,   6,  8000,     0,    0,    0 },     // 游戏机：6 bit + 8 kHz
};

class StyleFx
{
public:
    static int latency (double fs) noexcept { return (int) std::ceil (0.001 * fs); }   // 1 ms

    void prepare (double sampleRate)
    {
        fs = sampleRate; D = latency (fs);
        for (auto& b : ring) b.assign ((size_t) (D + 4), 0.0f);
        envCoef = (float) std::exp (-1.0 / (0.3 * fs)); gCoef = (float) std::exp (-1.0 / (0.05 * fs));
        reset(); set (mode, amount);
    }
    void reset()
    {
        for (auto& b : ring) std::fill (b.begin(), b.end(), 0.0f);
        w = 0; hold[0] = hold[1] = 0; holdAcc = 1; eDry = eWet = 1e-9f; gain = 1; t = 0;
        for (auto* f : { &hp1, &hp2, &lp1, &lp2, &bell, &lowSh }) f->reset();
    }
    // 消息线程 / setParams 里调（只改系数）
    void set (int m, float amt)
    {
        mode = std::clamp (m, 0, kNumFx - 1); amount = std::clamp (amt, 0.0f, 1.0f);
        const auto& d = kFx[(size_t) mode];
        if (d.hp > 0) { hp1.set (vox::Svf::HighPass, fs, d.hp, 0.54); hp2.set (vox::Svf::HighPass, fs, d.hp, 1.31); }
        if (d.lp > 0) { lp1.set (vox::Svf::LowPass, fs, std::min (d.lp, 0.45f * (float) fs), 0.54); lp2.set (vox::Svf::LowPass, fs, std::min (d.lp, 0.45f * (float) fs), 1.31); }
        if (std::abs (d.bellDb) > 0.01f) bell.set (vox::Svf::Bell, fs, d.bellF, 1.0, d.bellDb);
        if (std::abs (d.lowDb) > 0.01f) lowSh.set (vox::Svf::LowShelf, fs, 100, 0.7, d.lowDb);
        g = dbToGain (d.drive); q = d.bits > 0 ? std::pow (2.0f, d.bits - 1) : 0; holdStep = d.rateHz > 0 ? (float) (d.rateHz / fs) : 1;
        biasOut = std::tanh (d.asym);
    }
    bool active() const noexcept { return mode != FxOff && amount > 0.001f; }

    void process (float& l, float& r) noexcept
    {
        ring[0][(size_t) w] = l; ring[1][(size_t) w] = r;
        const int R = (int) ring[0].size();
        const float dl = tap (0, (float) D), dr = tap (1, (float) D);         // 干声：固定 D
        if (! active()) { advance (R); l = dl; r = dr; return; }
        const auto& d = kFx[(size_t) mode];
        float x0 = dl, x1 = dr;
        if (d.wowMs > 0 || d.flutMs > 0)                                      // 音高抖动：在 [0, D] 里摆动读取位置
        {
            const double tt = t / fs;
            const float m = (float) (d.wowMs * std::sin (juce::MathConstants<double>::twoPi * 0.6 * tt) + d.flutMs * std::sin (juce::MathConstants<double>::twoPi * 7.0 * tt));
            const float pos = std::clamp ((float) D - m * 0.001f * (float) fs, 0.0f, (float) D);
            x0 = tap (0, pos); x1 = tap (1, pos);
        }
        advance (R);
        if (d.mono > 0) { const float mid = 0.5f * (x0 + x1), k = d.mono; x0 += k * (mid - x0); x1 += k * (mid - x1); }
        float y[2] = { x0, x1 };
        for (int c = 0; c < 2; ++c)
        {
            float s = y[c];
            if (d.hp > 0) s = hp2.process (hp1.process (s, c), c);
            if (std::abs (d.lowDb) > 0.01f) s = lowSh.process (s, c);
            if (std::abs (d.bellDb) > 0.01f) s = bell.process (s, c);
            if (d.drive > 0)                                                  // tanh（不对称）+ 硬削波，按推的量还原电平
            {
                const float u = s * g, soft = std::tanh (u + d.asym) - biasOut, hard = std::clamp (u, -1.0f, 1.0f);
                s = (soft + d.hard * (hard - soft)) / std::sqrt (g);
            }
            if (d.lp > 0) s = lp2.process (lp1.process (s, c), c);
            if (q > 0) s = std::round (s * q) / q;
            y[c] = s;
        }
        if (holdStep < 1)                                                     // 降采样：采样保持
        {
            holdAcc += holdStep;
            if (holdAcc >= 1) { holdAcc -= 1; hold[0] = y[0]; hold[1] = y[1]; }
            y[0] = hold[0]; y[1] = hold[1];
        }
        // 湿声响度跟着干声走（±12 dB 内）
        eDry = envCoef * eDry + (1 - envCoef) * (dl * dl + dr * dr);
        eWet = envCoef * eWet + (1 - envCoef) * (y[0] * y[0] + y[1] * y[1]);
        const float want = std::clamp (std::sqrt ((eDry + 1e-12f) / (eWet + 1e-12f)), 0.25f, 4.0f);
        gain = gCoef * gain + (1 - gCoef) * want;
        l = dl + amount * (y[0] * gain - dl); r = dr + amount * (y[1] * gain - dr);
    }

private:
    double fs = 44100; int D = 44, w = 0, mode = FxOff; float amount = 0; int64_t t = 0;
    std::array<std::vector<float>, 2> ring;
    vox::Svf hp1, hp2, lp1, lp2, bell, lowSh;
    float g = 1, q = 0, holdStep = 1, holdAcc = 1, hold[2] {}, biasOut = 0;
    float eDry = 1e-9f, eWet = 1e-9f, gain = 1, envCoef = 0.9999f, gCoef = 0.999f;
    float tap (int c, float delay) const noexcept                           // 线性插值读环形缓冲（delay 样本之前）
    {
        const int R = (int) ring[0].size();
        float p = (float) w - delay; while (p < 0) p += (float) R;
        const int i0 = (int) p; const float f = p - (float) i0;
        const auto& b = ring[(size_t) c];
        return b[(size_t) (i0 % R)] + f * (b[(size_t) ((i0 + 1) % R)] - b[(size_t) (i0 % R)]);
    }
    void advance (int R) noexcept { w = w + 1 >= R ? 0 : w + 1; ++t; }
};
} // namespace mm
