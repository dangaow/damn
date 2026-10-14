#pragma once
// 离线分析：解算器（和测试工具）在录下来的音频上算响度、频谱、动态、齿音。只在后台线程用，可以分配内存。
#include "Shared.h"

namespace mm::analysis
{
struct Stereo { std::vector<float> l, r; size_t size() const { return l.size(); } };

// 专业人声的倍频程谱形（每个倍频程的总能量，相对 500/1k/2k 平均，dB）：判断录音偏浑 / 偏暗 / 偏亮。
// 取自 谎话 / Isolated / 灰烬 / 枷锁 四首发行版里分离出的人声的平均（最低两段有分离串音，按经验压低）
inline constexpr float kRefVocal[kOctBands] = { -36, -28, -12, 1, 5, 0, -4, -2.5f, 0.5f, -4 };

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

// 调性：12 个音级的色度 + Krumhansl 调式模板。返回 0..11 = C..B，minor 表示小调，confidence 0..1
struct Key { int tonic = 0; bool minor = false; float confidence = 0; std::string name() const; std::string relative() const; };
Key detectKey (const Stereo&, double fs);
float correlation (const Stereo&);                    // 左右相关（1 = 单声道，0 = 很宽）
float lowShare (const Stereo&, double fs);            // 150 Hz 以下占 K 计权功率的比例

// 一键分析的报告（界面画的就是这些）
struct Report
{
    int role = 0; float seconds = 0, lufs = -100, tp = -100, spread = 0, width = 0;
    std::array<float, kOctBands> ltas {}, target {};    // 倍频程谱 / 参照（人声：专业人声谱形；其他：风格曲线）
    int suggestedStyle = -1; Key key; bool hasKey = false;
    std::vector<std::string> lines;                     // 结果和建议（英文，界面显示）
};
// 角色 0 人声 / 1 伴奏 / 2 总线；style 是当前风格（总线对比它的曲线），bpm 用来猜风格
Report analyse (int role, const Stereo&, double fs, int style, double bpm);
} // namespace mm::analysis
