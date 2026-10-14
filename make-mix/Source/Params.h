#pragma once
// 参数表：三个角色共用一套 APVTS 布局（界面只显示当前角色的那几个）。不依赖 JUCE。
// 只做人声染色和混音：没有混响、没有回声。
#include "Shared.h"

namespace mm
{
enum class PKind { Db, Hz, Amount, Percent };
struct ParamDef { const char* id; const char* name; Role role; PKind kind; float min, max, def, skew; };

// skew：JUCE NormalisableRange 的 centre（<= 0 表示线性）
inline constexpr ParamDef kParams[] = {
    { "v_input",    "人聲 輸入",    Role::Vocal, PKind::Db,     -24, 24, 0, 0 },
    { "v_hpf",      "人聲 低切",  Role::Vocal, PKind::Hz,      40, 300, 90, 110 },
    { "v_mud",      "人聲 去渾濁",      Role::Vocal, PKind::Db,      -9, 3, -2, 0 },
    { "v_pres",     "人聲 存在感", Role::Vocal, PKind::Db,      -3, 8, 2, 0 },
    { "v_air",      "人聲 空氣感",      Role::Vocal, PKind::Db,      -3, 8, 3, 0 },
    { "v_comp",     "人聲 壓縮", Role::Vocal, PKind::Amount,   0, 1, 0.4f, 0 },
    { "v_deess",    "人聲 去齒音",   Role::Vocal, PKind::Amount,   0, 1, 0.4f, 0 },
    { "v_warmth",   "人聲 溫暖",   Role::Vocal, PKind::Amount,   0, 1, 0.2f, 0 },
    { "v_smooth",   "人聲 圓潤",   Role::Vocal, PKind::Amount,   0, 1, 0.3f, 0 },
    { "v_level",    "人聲 電平",    Role::Vocal, PKind::Db,     -24, 24, 0, 0 },
    { "b_hpf",      "伴奏 低切",   Role::Beat,  PKind::Hz,      20, 200, 25, 60 },
    { "b_pocket",   "伴奏 人聲口袋",    Role::Beat,  PKind::Db,      -6, 0, -1.5f, 0 },
    { "b_unmask",   "伴奏 讓位",    Role::Beat,  PKind::Db,       0, 12, 6, 0 },
    { "b_glue",     "伴奏 粘合",    Role::Beat,  PKind::Amount,   0, 1, 0.3f, 0 },
    { "b_level",    "伴奏 電平",     Role::Beat,  PKind::Db,     -24, 12, 0, 0 },
    { "m_low",      "總線 低頻",        Role::Bus,   PKind::Db,      -6, 6, 0, 0 },
    { "m_high",     "總線 高頻",       Role::Bus,   PKind::Db,      -6, 6, 0, 0 },
    { "m_glue",     "總線 粘合",       Role::Bus,   PKind::Amount,   0, 1, 0.35f, 0 },
    { "m_lift",     "總線 提升",       Role::Bus,   PKind::Amount,   0, 1, 0.5f, 0 },
    { "m_width",    "總線 寬度",      Role::Bus,   PKind::Percent, 50, 150, 100, 0 },
    { "m_drive",    "總線 推子",      Role::Bus,   PKind::Db,       0, 18, 0, 0 },
    { "m_ceiling",  "總線 天花板",    Role::Bus,   PKind::Db,      -3, 0, -1, 0 },
};
constexpr int kNumParams = (int) (sizeof (kParams) / sizeof (kParams[0]));

enum PIdx { V_INPUT, V_HPF, V_MUD, V_PRES, V_AIR, V_COMP, V_DEESS, V_WARMTH, V_SMOOTH, V_LEVEL,
            B_HPF, B_POCKET, B_UNMASK, B_GLUE, B_LEVEL, M_LOW, M_HIGH, M_GLUE, M_LIFT, M_WIDTH, M_DRIVE, M_CEILING };

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
