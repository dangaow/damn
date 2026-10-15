#pragma once
// 每个角色自己的预设（界面上的「預設」选单）。只改音色、动态和效果，不碰输入 / 电平 / 推子 / 天花板（那些由一键混音按歌算）。
// 载入时：这个角色的参数先回到默认值，再套预设里写的值。按一键混音会按歌重新算音色，效果保留。
// 列表只能在末尾追加（界面只存名字，不存序号，顺序随便改也行）。
#include "Params.h"
#include "Fx.h"

namespace mm
{
struct PV { int i; float v; };                           // 参数序号（PIdx）+ 值；i == 0（V_INPUT，预设从不改）表示结束
struct Preset { const char* name; Role role; PV v[12]; };

inline constexpr Preset kPresets[] = {
    // —— 人声 ——————————————————————————————————————————————————————————————
    { "自然乾淨",       Role::Vocal, { { V_HPF, 80 }, { V_MUD, -2 }, { V_PRES, 1.5f }, { V_AIR, 2 }, { V_COMP, 0.35f }, { V_DEESS, 0.35f }, { V_WARMTH, 0.1f }, { V_SMOOTH, 0.3f } } },
    { "流行明亮",       Role::Vocal, { { V_HPF, 100 }, { V_MUD, -3 }, { V_PRES, 3.5f }, { V_AIR, 5 }, { V_COMP, 0.55f }, { V_DEESS, 0.5f }, { V_WARMTH, 0.15f }, { V_SMOOTH, 0.35f } } },
    { "說唱貼臉",       Role::Vocal, { { V_HPF, 110 }, { V_MUD, -3.5f }, { V_PRES, 4 }, { V_AIR, 3 }, { V_COMP, 0.75f }, { V_DEESS, 0.45f }, { V_WARMTH, 0.35f }, { V_SMOOTH, 0.25f } } },
    { "旋律說唱",       Role::Vocal, { { V_HPF, 100 }, { V_MUD, -3 }, { V_PRES, 2.5f }, { V_AIR, 4.5f }, { V_COMP, 0.6f }, { V_DEESS, 0.5f }, { V_WARMTH, 0.25f }, { V_SMOOTH, 0.45f } } },
    { "抒情溫暖",       Role::Vocal, { { V_HPF, 75 }, { V_MUD, -1.5f }, { V_PRES, 1 }, { V_AIR, 2.5f }, { V_COMP, 0.4f }, { V_DEESS, 0.4f }, { V_WARMTH, 0.3f }, { V_SMOOTH, 0.55f } } },
    { "R&B 絲滑",       Role::Vocal, { { V_HPF, 85 }, { V_MUD, -2.5f }, { V_PRES, 1.5f }, { V_AIR, 5 }, { V_COMP, 0.5f }, { V_DEESS, 0.55f }, { V_WARMTH, 0.2f }, { V_SMOOTH, 0.6f } } },
    { "搖滾嘶吼",       Role::Vocal, { { V_HPF, 120 }, { V_MUD, -2 }, { V_PRES, 5 }, { V_AIR, 2 }, { V_COMP, 0.8f }, { V_DEESS, 0.3f }, { V_WARMTH, 0.6f }, { V_SMOOTH, 0.2f } } },
    { "耳語貼耳",       Role::Vocal, { { V_HPF, 90 }, { V_MUD, -2 }, { V_PRES, 2 }, { V_AIR, 6 }, { V_COMP, 0.65f }, { V_DEESS, 0.7f }, { V_WARMTH, 0.1f }, { V_SMOOTH, 0.4f } } },
    { "低沉男聲",       Role::Vocal, { { V_HPF, 70 }, { V_MUD, -3.5f }, { V_PRES, 3 }, { V_AIR, 3 }, { V_COMP, 0.5f }, { V_DEESS, 0.3f }, { V_WARMTH, 0.3f }, { V_SMOOTH, 0.3f } } },
    { "高亢女聲",       Role::Vocal, { { V_HPF, 120 }, { V_MUD, -1.5f }, { V_PRES, 1 }, { V_AIR, 3 }, { V_COMP, 0.5f }, { V_DEESS, 0.6f }, { V_WARMTH, 0.15f }, { V_SMOOTH, 0.6f } } },
    { "和聲襯底",       Role::Vocal, { { V_HPF, 150 }, { V_MUD, -4 }, { V_PRES, 0 }, { V_AIR, 1 }, { V_COMP, 0.7f }, { V_DEESS, 0.6f }, { V_WARMTH, 0.2f }, { V_SMOOTH, 0.6f } } },
    { "Adlib 點綴",     Role::Vocal, { { V_HPF, 200 }, { V_MUD, -3 }, { V_PRES, 4 }, { V_AIR, 3 }, { V_COMP, 0.7f }, { V_DEESS, 0.5f }, { V_WARMTH, 0.4f }, { V_SMOOTH, 0.3f }, { V_FX, FxRadio }, { V_FXAMT, 0.35f } } },
    { "地下 UDG",       Role::Vocal, { { V_HPF, 110 }, { V_MUD, -2 }, { V_PRES, 3 }, { V_AIR, 1.5f }, { V_COMP, 0.8f }, { V_DEESS, 0.4f }, { V_WARMTH, 0.7f }, { V_SMOOTH, 0.2f }, { V_FX, FxUnder }, { V_FXAMT, 0.6f } } },
    { "電話音",         Role::Vocal, { { V_HPF, 150 }, { V_COMP, 0.6f }, { V_DEESS, 0.3f }, { V_WARMTH, 0.3f }, { V_FX, FxPhone }, { V_FXAMT, 1 } } },
    { "收音機",         Role::Vocal, { { V_HPF, 120 }, { V_COMP, 0.6f }, { V_WARMTH, 0.3f }, { V_FX, FxRadio }, { V_FXAMT, 1 } } },
    { "對講機",         Role::Vocal, { { V_HPF, 200 }, { V_COMP, 0.7f }, { V_FX, FxWalkie }, { V_FXAMT, 1 } } },
    { "擴音器",         Role::Vocal, { { V_HPF, 200 }, { V_COMP, 0.7f }, { V_FX, FxMega }, { V_FXAMT, 1 } } },
    { "失真人聲",       Role::Vocal, { { V_HPF, 110 }, { V_COMP, 0.6f }, { V_WARMTH, 0.5f }, { V_FX, FxDist }, { V_FXAMT, 0.7f } } },
    { "Lo-fi 人聲",     Role::Vocal, { { V_HPF, 120 }, { V_AIR, 0 }, { V_COMP, 0.6f }, { V_WARMTH, 0.4f }, { V_FX, FxLofi }, { V_FXAMT, 0.8f } } },
    { "錄音帶人聲",     Role::Vocal, { { V_HPF, 90 }, { V_AIR, 2 }, { V_COMP, 0.5f }, { V_WARMTH, 0.5f }, { V_FX, FxTape }, { V_FXAMT, 0.7f } } },
    { "黑膠人聲",       Role::Vocal, { { V_HPF, 90 }, { V_AIR, 1.5f }, { V_COMP, 0.5f }, { V_WARMTH, 0.4f }, { V_FX, FxVinyl }, { V_FXAMT, 0.7f } } },
    { "遊戲機 8-bit",   Role::Vocal, { { V_HPF, 120 }, { V_COMP, 0.6f }, { V_FX, FxGame }, { V_FXAMT, 0.5f } } },

    // —— 伴奏 ——————————————————————————————————————————————————————————————
    { "原樣（不讓位）", Role::Beat,  { { B_HPF, 20 }, { B_POCKET, 0 }, { B_UNMASK, 0 }, { B_GLUE, 0 } } },
    { "輕讓位",         Role::Beat,  { { B_HPF, 25 }, { B_POCKET, -1 }, { B_UNMASK, 3 }, { B_GLUE, 0.2f } } },
    { "標準讓位",       Role::Beat,  { { B_HPF, 25 }, { B_POCKET, -1.5f }, { B_UNMASK, 6 }, { B_GLUE, 0.3f } } },
    { "人聲優先",       Role::Beat,  { { B_HPF, 30 }, { B_POCKET, -3 }, { B_UNMASK, 9 }, { B_GLUE, 0.35f } } },
    { "粘合緊實",       Role::Beat,  { { B_HPF, 25 }, { B_POCKET, -1.5f }, { B_UNMASK, 5 }, { B_GLUE, 0.6f } } },
    { "808 重低保留",   Role::Beat,  { { B_HPF, 20 }, { B_POCKET, -1 }, { B_UNMASK, 5 }, { B_GLUE, 0.25f } } },
    { "抒情留白",       Role::Beat,  { { B_HPF, 35 }, { B_POCKET, -2 }, { B_UNMASK, 7 }, { B_GLUE, 0.2f } } },
    { "地下 UDG 髒",    Role::Beat,  { { B_HPF, 25 }, { B_UNMASK, 5 }, { B_GLUE, 0.5f }, { B_FX, FxUnder }, { B_FXAMT, 0.6f } } },
    { "Lo-fi 舊錄音",   Role::Beat,  { { B_HPF, 40 }, { B_UNMASK, 5 }, { B_GLUE, 0.4f }, { B_FX, FxLofi }, { B_FXAMT, 0.8f } } },
    { "錄音帶",         Role::Beat,  { { B_UNMASK, 5 }, { B_GLUE, 0.35f }, { B_FX, FxTape }, { B_FXAMT, 0.6f } } },
    { "黑膠",           Role::Beat,  { { B_UNMASK, 5 }, { B_GLUE, 0.3f }, { B_FX, FxVinyl }, { B_FXAMT, 0.7f } } },
    { "收音機前奏",     Role::Beat,  { { B_UNMASK, 0 }, { B_FX, FxRadio }, { B_FXAMT, 1 } } },
    { "電話前奏",       Role::Beat,  { { B_UNMASK, 0 }, { B_FX, FxPhone }, { B_FXAMT, 1 } } },
    { "遊戲機 8-bit",   Role::Beat,  { { B_UNMASK, 5 }, { B_FX, FxGame }, { B_FXAMT, 0.6f } } },
    { "失真伴奏",       Role::Beat,  { { B_UNMASK, 5 }, { B_GLUE, 0.4f }, { B_FX, FxDist }, { B_FXAMT, 0.4f } } },

    // —— 总线 ——————————————————————————————————————————————————————————————
    { "透明",           Role::Bus,   { { M_LOW, 0 }, { M_HIGH, 0 }, { M_MID, 0 }, { M_GLUE, 0.2f }, { M_LIFT, 0.2f }, { M_WIDTH, 100 } } },
    { "流行響亮",       Role::Bus,   { { M_LOW, 1 }, { M_HIGH, 1.5f }, { M_MID, -1 }, { M_GLUE, 0.45f }, { M_LIFT, 0.6f }, { M_WIDTH, 110 } } },
    { "說唱重低",       Role::Bus,   { { M_LOW, 2 }, { M_HIGH, 0.5f }, { M_MID, -1.5f }, { M_GLUE, 0.5f }, { M_LIFT, 0.65f }, { M_WIDTH, 105 } } },
    { "抒情寬廣",       Role::Bus,   { { M_LOW, 0 }, { M_HIGH, 1 }, { M_MID, -0.5f }, { M_GLUE, 0.3f }, { M_LIFT, 0.35f }, { M_WIDTH, 120 } } },
    { "溫暖復古",       Role::Bus,   { { M_LOW, 1 }, { M_HIGH, -1.5f }, { M_MID, 0.5f }, { M_GLUE, 0.45f }, { M_LIFT, 0.5f }, { M_WIDTH, 95 } } },
    { "明亮現代",       Role::Bus,   { { M_LOW, 0.5f }, { M_HIGH, 2.5f }, { M_MID, -1.5f }, { M_GLUE, 0.4f }, { M_LIFT, 0.6f }, { M_WIDTH, 115 } } },
    { "動態保留",       Role::Bus,   { { M_GLUE, 0.15f }, { M_LIFT, 0.25f }, { M_WIDTH, 100 } } },
    { "寬立體聲",       Role::Bus,   { { M_GLUE, 0.35f }, { M_LIFT, 0.5f }, { M_WIDTH, 135 } } },
    { "地下 UDG",       Role::Bus,   { { M_LOW, 1.5f }, { M_HIGH, -1 }, { M_MID, 1 }, { M_GLUE, 0.6f }, { M_LIFT, 0.7f }, { M_WIDTH, 95 }, { M_FX, FxUnder }, { M_FXAMT, 0.35f } } },
    { "Lo-fi 母帶",     Role::Bus,   { { M_HIGH, -2 }, { M_GLUE, 0.4f }, { M_LIFT, 0.4f }, { M_FX, FxLofi }, { M_FXAMT, 0.5f } } },
    { "錄音帶母帶",     Role::Bus,   { { M_GLUE, 0.4f }, { M_LIFT, 0.5f }, { M_FX, FxTape }, { M_FXAMT, 0.5f } } },
    { "黑膠母帶",       Role::Bus,   { { M_GLUE, 0.35f }, { M_LIFT, 0.4f }, { M_FX, FxVinyl }, { M_FXAMT, 0.6f } } },
    { "收音機（整首）", Role::Bus,   { { M_LIFT, 0.3f }, { M_FX, FxRadio }, { M_FXAMT, 1 } } },
    { "電話（整首）",   Role::Bus,   { { M_LIFT, 0.3f }, { M_FX, FxPhone }, { M_FXAMT, 1 } } },
    { "遊戲機 8-bit",   Role::Bus,   { { M_LIFT, 0.4f }, { M_FX, FxGame }, { M_FXAMT, 0.4f } } },
};
constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));

// 预设不改的参数：增益结构由一键混音按歌决定
constexpr bool presetKeeps (int i) noexcept { return i == V_INPUT || i == V_LEVEL || i == B_LEVEL || i == M_DRIVE || i == M_CEILING; }

// 在 current 上套用预设：这个角色的参数（除了增益结构）回到默认值，再写预设里的值
inline ParamSet applyPreset (const Preset& pr, ParamSet current)
{
    for (int i = 0; i < kNumParams; ++i)
        if (kParams[i].role == pr.role && ! presetKeeps (i)) current[i] = kParams[i].def;
    for (const auto& pv : pr.v)
    {
        if (pv.i <= 0) break;
        current[pv.i] = clampParam (pv.i, pv.v);
    }
    return current;
}
} // namespace mm
