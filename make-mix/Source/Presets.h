#pragma once
// 出厂预设：每个预设是一组旋钮位置（0..1），ECHO TIME 是选项的序号。第 0 个就是默认值（宿主新建插件时会先载入它）。
#include <array>

namespace vox
{
struct Preset { const char* name; float input, clean, presence, air, deess, compress, warmth, echo; int echoTime; float space, output; };

// ECHO TIME 选项：0 = 1/8，1 = 1/8 附点，2 = 1/4，3 = 1/4 附点，4 = 1/2
inline constexpr std::array<Preset, 6> presets {{
    { "Default",     0, 0.50f, 0.40f, 0.40f, 0.40f, 0.50f, 0.20f, 0.12f, 2, 0.15f, 0 },
    { "Trap Lead",   0, 0.60f, 0.60f, 0.55f, 0.50f, 0.75f, 0.35f, 0.22f, 1, 0.18f, 0 },
    { "Rap Dry",     0, 0.55f, 0.50f, 0.35f, 0.45f, 0.80f, 0.30f, 0.04f, 0, 0.05f, 0 },
    { "R&B Smooth",  0, 0.45f, 0.35f, 0.62f, 0.55f, 0.55f, 0.20f, 0.30f, 2, 0.40f, 0 },
    { "Clean Pop",   0, 0.50f, 0.45f, 0.50f, 0.50f, 0.60f, 0.15f, 0.15f, 2, 0.25f, 0 },
    { "Lo-fi Tape",  0, 0.85f, 0.20f, 0.00f, 0.30f, 0.70f, 0.80f, 0.35f, 2, 0.30f, 0 },
}};
} // namespace vox
