#pragma once
// 离线分析：解算器（和测试工具）在录下来的音频上算响度、频谱、动态、齿音。只在后台线程用，可以分配内存。
#include "Shared.h"

namespace mm::analysis
{
struct Stereo { std::vector<float> l, r; size_t size() const { return l.size(); } };

// BS.1770 积分响度（门限 -70 LUFS 绝对 / -10 LU 相对）。hpf150 = true 时先过 150 Hz 高通：「人声频段响度」，
// 用来比较人声和伴奏（不让 808 / 底鼓的低频能量主导）
float integrated (const Stereo&, double fs, bool hpf150 = false);
// 400 ms 窗、100 ms 步进的瞬时响度序列（同样可选 150 Hz 高通）
std::vector<float> momentary (const Stereo&, double fs, bool hpf150 = false);
// 活跃帧（瞬时响度高于积分响度 - 20 LU）上的分位数
float percentile (std::vector<float> v, float q);
float spread (const Stereo&, double fs);              // 人声动态：活跃帧上 P90 - P10（LU）
// 倍频程长期平均谱（dB，相对 500/1k/2k 三段的平均），只统计活跃帧
std::array<float, kOctBands> octaveLtas (const Stereo&, double fs);
// 齿音：每帧 5–10 kHz 占全频的能量比（dB）的 P95 - P50
float sibilance (const Stereo&, double fs);
float truePeakDb (const Stereo&);
float samplePeakDb (const Stereo&);
// 掩蔽：人声活跃的帧里，1–5 kHz 伴奏能量大于人声的比例
float maskedFraction (const Stereo& vocal, const Stereo& beat, double fs);
} // namespace mm::analysis
