#pragma once
// 参数表：三个角色共用一份 APVTS 布局（6.1）。ID 和范围一旦发布就冻结，只能在末尾追加。不依赖 JUCE。
#include "Shared.h"

namespace mm
{
enum class PKind : uint8_t { Float, Choice };
struct ParamInfo
{
    const char* id; const char* name; Role role; PKind kind;
    float min, max, def, centre, step;      // centre <= 0 → 线性；step 0 = 连续
    const char* unit; bool automatable;
    const char* const* choices; int numChoices;
};
extern const ParamInfo kParams[]; extern const int kNumParams;   // 56

// 和 kParams 的顺序完全一致（单元测试逐个核对）
enum PIdx : int
{
    // VOCAL（26）
    v_input, v_hpf, v_hpfSlope, v_lowF, v_lowG, v_midF, v_midG, v_presF, v_presG, v_airF, v_airG, v_lpf,
    v_comp, v_compAtk, v_compRel, v_deess, v_deessF, v_warmth, v_echo, v_echoTime, v_echoFb, v_space, v_spaceDecay,
    v_preDelay, v_wetDuck, v_level,
    // BEAT（14）
    b_hpf, b_monoBelow, b_pocketF, b_pocketG, b_pocketQ, b_unmask, b_sens, b_focusLo, b_focusHi, b_release, b_side, b_ride,
    b_key, b_level,
    // BUS（16）
    m_lowG, m_lowMidG, m_presG, m_airG, m_lpf, m_glue, m_glueRatio, m_glueAtk, m_glueRel, m_glueScHpf, m_width, m_monoBelow,
    m_drive, m_clip, m_ceiling, m_target,
    kPIdxCount
};
static_assert (kPIdxCount == 56);

// 默认值 = kParams 的 def（= TRAP 的 styleDefaults），这样 VocalParams{} 也是一份合法参数
struct VocalParams { float input = 0, hpf = 105; int hpfSlope = 1; float lowF = 320, lowG = -3, midF = 1200, midG = 0, presF = 4000, presG = 2.5f,
                     airF = 11000, airG = 4, lpf = 22000, comp = 0.5f, compAtk = 15, compRel = 180, deess = 0.4f, deessF = 6000,
                     warmth = 0.25f, echo = 0.2f; int echoTime = 2; float echoFb = 0.25f, space = 0.3f, spaceDecay = 1.2f,
                     preDelay = 35, wetDuck = 6, level = 0; bool operator== (const VocalParams&) const = default; };
struct BeatParams  { float hpf = 20, monoBelow = 120, pocketF = 3000, pocketG = 0, pocketQ = 1, unmask = 0.67f, sens = 6, focusLo = 250,
                     focusHi = 5000, release = 160, side = 0.3f, ride = 0.5f;
                     int key = 0; float level = 0; bool operator== (const BeatParams&) const = default; };
struct BusParams   { float lowG = 0, lowMidG = 0, presG = 0, airG = 0, lpf = 22000, glue = -14, glueRatio = 2.5f, glueAtk = 20, glueRel = 120,
                     glueScHpf = 120, width = 1, monoBelow = 120,
                     drive = 0, clip = 0, ceiling = -1, target = -9; bool operator== (const BusParams&) const = default; };
VocalParams vocalFrom (const float* raw);   BeatParams beatFrom (const float* raw);   BusParams busFrom (const float* raw);  // raw[kPIdxCount]
ParamSet toSet (const VocalParams&); ParamSet toSet (const BeatParams&); ParamSet toSet (const BusParams&);
VocalParams vocalFrom (const ParamSet&);  BeatParams beatFrom (const ParamSet&);  BusParams busFrom (const ParamSet&);   // 缺的用默认值
std::string formatValue (int pidx, float v);   // "+2.0 dB" "120 Hz" "3.8 kHz" "45%" "1/8D"

// —— WP0 补充的小工具（只追加）——
int paramIndex (const std::string& id);        // id → PIdx；没有返回 -1
float clampParam (int pidx, float v);          // 限制到范围；选项取整；有 step 的按 step 取整
bool isLevelOwning (int pidx) noexcept;        // v_input / v_level / b_level / m_drive / m_glue / m_target（LOAD DEFAULTS 和预设不动它们）

// v_echoTime 各选项对应的拍数：1/16, 1/8, 1/8D, 1/4, 1/4D, 1/2
constexpr float kEchoBeats[6] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
enum class KeyChoice : int { Auto = 0, Link = 1, Sidechain = 2, Off = 3 };   // b_key 的选项序号
} // namespace mm
