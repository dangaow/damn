#pragma once
// 离线分析（4.2）：从学习采样算特征，给解算器用。不受实时限制。
// 【WP0 桩】公开接口是冻结的（4.3）；Analysis.cpp 由 WP3 替换。
#include "Shared.h"

namespace mm::analysis
{
struct FrameSeries                                   // 短帧（时间线对齐）
{
    int hop = 0; double fs = 0;
    std::vector<int32_t> pass; std::vector<int64_t> index;          // 时间线帧号
    std::vector<std::array<float, kAnaBands>> bandDb;               // 绝对功率 dB
};
struct LoudSeries { std::vector<float> momLufs, wLufs; std::vector<int32_t> pass; std::vector<int64_t> index; };  // 100 ms 步长
struct Features
{
    float activeSec = 0, voicedSec = 0, iIn = -100, iActive = -100, tpIn = -100, f0P5 = 0, f0P50 = 0;
    float crestDb = 12, crestC2 = 10, hfTilt = 0, widthLu = -20;
    std::array<float, kAnaBands> ltasPink {}, ltasDb {};
    std::array<float, kOctBands> octMid {};
    std::array<float, 80> levelW {};
    std::vector<float> momActive;                     // 有声的 400 ms 响度
    std::vector<uint8_t> vad;                         // 对应 LoudSeries
    FrameSeries frames; LoudSeries loud;
    bool keyActive = false;
};
// 时间线信息来自 CaptureData::seg；n = written（只读前缀）
Features analyse (const CaptureData&, Role, int64_t n);
Features analyseBuffer (const float* const* ch, int numCh, int64_t n, double fs,
                        const std::vector<CaptureData::Segment>&, Role);       // 给仿真输出用
float integratedLufs (const float* const* ch, int numCh, int64_t n, double fs);
float truePeakDb (const float* const* ch, int numCh, int64_t n, double fs);
struct Joint { bool valid = false; float maskedFraction = 0, sii = 0, vbr = 0, larMid = 0; std::array<float, kAnaBands> medBeatMinusVoc {}; int frames = 0; };
// vocals / beats：每个实例的特征（要求 frames/loud 已算好）；leadIndex = 只统计该 LEAD 的 vad
Joint joint (const std::vector<const Features*>& vocals, const std::vector<float>& vocalWeights,
             const std::vector<const Features*>& beats, int leadIndex = -1);
std::vector<float> sibilancePercentiles (const float* sibDb, int64_t n, const std::vector<uint8_t>& vadPerSample);   // P50, P90, P99
} // namespace mm::analysis
