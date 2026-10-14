#pragma once
// 三个角色的处理链（6.2–6.4）。插件在音频线程用，解算器拿来离线仿真：只有一份实现，仿真 = 实际。
// 【WP0 桩】公开接口是冻结的（6.6）；private 部分和 Chains.cpp 由 WP1 替换。桩的行为：只延迟 spec.latency 个样本。
#include "Params.h"
#include <juce_dsp/juce_dsp.h>

namespace mm
{
struct ChainSpec { double fs = 48000; int maxBlock = 512; int numCh = 2; int latency = 0; };   // latency 0 = 不补齐（解算仿真用）
struct ProcessContext { int64_t timeline = -1; double bpm = 120; };   // timeline = 本块第一个输入样本的时间线位置；-1 = 未播放
struct KeyFrame { int64_t hop = 0; std::array<float, kKeyBands> bandDb {}; float vad = 0; };
class KeySink   { public: virtual ~KeySink() = default;   virtual void keyFrame (const KeyFrame&) noexcept = 0; };
class KeySource { public: virtual ~KeySource() = default; virtual KeyRead vocalKey (int64_t hop, float* bandDb) noexcept = 0; };

struct VocalTaps { float* dry = nullptr; float* echo = nullptr; float* space = nullptr; float* preSat = nullptr;
                   float* sibDb = nullptr; float* compGrDb = nullptr; };          // 每个 n 个样本（两声道平均），可为空
struct BeatTaps  { float* cutMeanDb = nullptr; };
struct BusTaps   { float* glueGrDb = nullptr; float* limGrDb = nullptr; float* preLimTpDb = nullptr; };
struct ChainMeters { float inPeak = 0, outPeak = 0, gr1 = 0, gr2 = 0, gr3 = 0, loudM = -100, loudS = -100, loudI = -100, truePeak = -100; };

namespace detail
{
// 桩用的补齐延迟（每声道一个环形缓冲；prepare 里分配）
struct StubDelay
{
    std::array<std::vector<float>, 2> buf;
    int len = 0, pos = 0;
    void prepare (int latency);
    void reset() noexcept;
    void process (float* const* ch, int numCh, int n) noexcept;
};
} // namespace detail

class VocalChain
{
public:
    void prepare (const ChainSpec&);                       // 分配（非实时）
    void reset() noexcept;
    void setParams (const VocalParams&, const CompProfile&) noexcept;   // 每块调用；变了才重算
    void process (float* const* ch, int numCh, int n, const ProcessContext&, KeySink*, VocalTaps* = nullptr) noexcept;
    void processBypassed (float* const* ch, int numCh, int n, const ProcessContext&, KeySink*) noexcept;
    int coreLatency() const noexcept;
    float autoCompDb() const noexcept;
    const ChainMeters& meters() const noexcept;            // 本块的值；处理器拷到原子量
private:
    detail::StubDelay pad;
    ChainMeters m;
};
enum class KeyMode : uint8_t { Off = 0, Link = 1, Sidechain = 2 };
class BeatChain
{
public:
    void prepare (const ChainSpec&);                       // spec.latency ≥ keyLookahead + keyHop（插件里 = L）
    void reset() noexcept;
    void setParams (const BeatParams&, const CompProfile&) noexcept;
    void process (float* const* ch, int numCh, const float* const* aux, int auxCh, int n,
                  const ProcessContext&, KeySource* link, BeatTaps* = nullptr) noexcept;   // link = nullptr：没有 VOCAL 同伴
    void processBypassed (float* const* ch, int numCh, int n) noexcept;
    KeyMode keyMode() const noexcept;   uint32_t lateCount() const noexcept;
    const std::array<float, kKeyBands>& cutDb() const noexcept;
    const std::array<float, kKeyBands>& beatDb() const noexcept;
    const ChainMeters& meters() const noexcept;
private:
    detail::StubDelay pad;
    ChainMeters m;
    std::array<float, kKeyBands> cut {}, beat {};
};
class BusChain
{
public:
    void prepare (const ChainSpec&);
    void reset() noexcept;
    void setParams (const BusParams&, const CompProfile&) noexcept;
    void process (float* const* ch, int numCh, int n, const ProcessContext&, BusTaps* = nullptr) noexcept;
    void processBypassed (float* const* ch, int numCh, int n) noexcept;
    int coreLatency() const noexcept;
    void resetIntegrated() noexcept;                        // 播放开始时清积分响度
    const ChainMeters& meters() const noexcept;
private:
    detail::StubDelay pad;
    ChainMeters m;
};
} // namespace mm
