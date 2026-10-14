#pragma once
// 公共常数和小工具：不依赖 JUCE，音频线程 / 消息线程 / 解算线程都用
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace mm
{
enum class Role : int { Vocal = 0, Beat = 1, Bus = 2 };
using StyleId = int;                                   // kStyles 的下标

constexpr int kNumStyles = 12, kOctBands = 10, kMaxSlots = 32;
constexpr float kOctCentresHz[kOctBands] = { 31.5f, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };

// 人声 key 的 6 个频段（伴奏在这些频段给人声让位）和每段的重要度
constexpr int kKeyBands = 6;
constexpr float kKeyHz[kKeyBands]  = { 280, 560, 1100, 2200, 3600, 6000 };
constexpr float kKeyImp[kKeyBands] = { 0.45f, 0.7f, 0.9f, 1.0f, 1.0f, 0.7f };
constexpr int kKeyRing = 1024;                         // 每个人声槽保留的 key 帧数

inline int rateFactor (double fs) noexcept { return fs <= 50000.0 ? 1 : fs <= 100000.0 ? 2 : 4; }
inline int keyHop (double fs) noexcept     { return 256 * rateFactor (fs); }
// 所有角色报告同一个固定延迟，只在 prepareToPlay 里算一次。
// 伴奏要用「延迟 L 之前」的人声 key：块长 n 满足 n <= L - hop 时，不管 FL 先算哪条轨，那一帧都已经写好了。
inline int latencyFor (double fs, int maxBlock) noexcept
{
    const int r = rateFactor (fs), h = keyHop (fs);
    for (int L : { 1024, 2048, 4096 }) if (maxBlock <= L * r - h) return L * r;
    return 8192 * r;
}
inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept  { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float powToDb (float p) noexcept   { return 10.0f * std::log10 (std::max (p, 1.0e-12f)); }
} // namespace mm
