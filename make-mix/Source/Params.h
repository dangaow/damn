#pragma once
// 参数表：三个角色共用一套 APVTS 布局（界面只显示当前角色的那几个）。不依赖 JUCE。
#include "Shared.h"

namespace mm
{
enum class PKind { Db, Hz, Amount, Choice, Percent };
struct ParamDef { const char* id; const char* name; Role role; PKind kind; float min, max, def, skew; };

// skew：JUCE NormalisableRange 的 centre（<= 0 表示线性）
inline constexpr ParamDef kParams[] = {
    { "v_input",    "Vocal Input",    Role::Vocal, PKind::Db,     -24, 24, 0, 0 },
    { "v_hpf",      "Vocal Low Cut",  Role::Vocal, PKind::Hz,      40, 300, 90, 110 },
    { "v_mud",      "Vocal Mud",      Role::Vocal, PKind::Db,      -9, 3, -2, 0 },
    { "v_pres",     "Vocal Presence", Role::Vocal, PKind::Db,      -3, 8, 2, 0 },
    { "v_air",      "Vocal Air",      Role::Vocal, PKind::Db,      -3, 8, 3, 0 },
    { "v_comp",     "Vocal Compress", Role::Vocal, PKind::Amount,   0, 1, 0.4f, 0 },
    { "v_deess",    "Vocal De-ess",   Role::Vocal, PKind::Amount,   0, 1, 0.4f, 0 },
    { "v_warmth",   "Vocal Warmth",   Role::Vocal, PKind::Amount,   0, 1, 0.2f, 0 },
    { "v_echo",     "Vocal Echo",     Role::Vocal, PKind::Amount,   0, 1, 0.15f, 0 },
    { "v_echoTime", "Echo Time",      Role::Vocal, PKind::Choice,   0, 5, 2, 0 },
    { "v_space",    "Vocal Space",    Role::Vocal, PKind::Amount,   0, 1, 0.2f, 0 },
    { "v_level",    "Vocal Level",    Role::Vocal, PKind::Db,     -24, 24, 0, 0 },
    { "b_hpf",      "Beat Low Cut",   Role::Beat,  PKind::Hz,      20, 200, 25, 60 },
    { "b_pocket",   "Beat Pocket",    Role::Beat,  PKind::Db,      -6, 0, -1.5f, 0 },
    { "b_unmask",   "Beat Unmask",    Role::Beat,  PKind::Db,       0, 12, 6, 0 },
    { "b_level",    "Beat Level",     Role::Beat,  PKind::Db,     -24, 12, 0, 0 },
    { "m_low",      "Bus Low",        Role::Bus,   PKind::Db,      -6, 6, 0, 0 },
    { "m_high",     "Bus High",       Role::Bus,   PKind::Db,      -6, 6, 0, 0 },
    { "m_glue",     "Bus Glue",       Role::Bus,   PKind::Amount,   0, 1, 0.35f, 0 },
    { "m_width",    "Bus Width",      Role::Bus,   PKind::Percent, 50, 150, 100, 0 },
    { "m_drive",    "Bus Drive",      Role::Bus,   PKind::Db,       0, 18, 0, 0 },
    { "m_ceiling",  "Bus Ceiling",    Role::Bus,   PKind::Db,      -3, 0, -1, 0 },
};
constexpr int kNumParams = (int) (sizeof (kParams) / sizeof (kParams[0]));
inline constexpr const char* kEchoChoices[] = { "1/16", "1/8", "1/8 D", "1/4", "1/4 D", "1/2" };
inline constexpr float kEchoBeats[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };

enum PIdx { V_INPUT, V_HPF, V_MUD, V_PRES, V_AIR, V_COMP, V_DEESS, V_WARMTH, V_ECHO, V_ECHOTIME, V_SPACE, V_LEVEL,
            B_HPF, B_POCKET, B_UNMASK, B_LEVEL, M_LOW, M_HIGH, M_GLUE, M_WIDTH, M_DRIVE, M_CEILING };

inline int paramIndex (const std::string& id)
{
    for (int i = 0; i < kNumParams; ++i) if (id == kParams[i].id) return i;
    return -1;
}
// 一组完整的参数值（按 kParams 顺序）
struct ParamSet
{
    std::array<float, kNumParams> v {};
    ParamSet() { for (int i = 0; i < kNumParams; ++i) v[(size_t) i] = kParams[i].def; }
    float& operator[] (int i) { return v[(size_t) i]; }
    float operator[] (int i) const { return v[(size_t) i]; }
};
inline float clampParam (int i, float x) { return std::clamp (x, kParams[i].min, kParams[i].max); }
} // namespace mm
