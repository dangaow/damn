#pragma once
// STYLE 表（7.2 的表 A–E）。WP0 按设计文档录入，之后归 WP3 调。顺序按序号存进 state，只能在末尾追加。不依赖 JUCE。
#include "Shared.h"
#include <string_view>

namespace mm
{
struct StyleDef
{
    const char* key; const char* name;                  // "trap", "TRAP" / "MANDOPOP 华语流行"
    // 人声音色（表 A）
    float vTilt, vMudF, vMudG, vPresF, vPresG, vAirF, vAirG, vHpfK, vHpfMin, vHpfMax; int vHpfSlope; float vLpf, vMatch;
    // 人声动态 / 空间 / 电平（表 B）
    float vSpread, vDeessMargin, vSibTarget, vWarmth, vRevDecay, vPreDelayMs, vRevWdr; int vEchoTime;
    float vEchoFb, vEchoWdr, vWetDuck, vbr, dblOffset, adlibOffset;
    // 伴奏（表 C）
    float bDmax, bSens, bLo, bHi, bRelMs, bSide, bRide, bPocketF, bPocketStatic, bPocketLinked, bHpf, bMono;
    // 总线（表 D、E）
    float lufsStyle, lufsLoud, ceiling, glueRatio, glueAtk, glueRel, glueGr, glueScHpf, clipDb, width, busMono, busLpf, maxLimGr;
    std::array<float, kOctBands> busCurve;              // 相对 500/1k/2k 平均，dB
};
// 字段约定：
//   vHpfSlope = 12 或 24（dB/oct，和表一样）；参数 v_hpfSlope 的选项序号 = hpfSlopeChoice (vHpfSlope)
//   vEchoTime = 参数 v_echoTime 的选项序号：0 = 1/16, 1 = 1/8, 2 = 1/8D, 3 = 1/4, 4 = 1/4D, 5 = 1/2
//   vLpf / busLpf = 22000 表示关
//   dblOffset / adlibOffset 是相对 LEAD 的电平（负数 = 更小）
//   bPocketStatic / bPocketLinked、ceiling、vRevWdr / vEchoWdr 都是 dB（负数）
constexpr int hpfSlopeChoice (int dbPerOct) noexcept { return dbPerOct >= 24 ? 1 : 0; }

// （设计文档写的是 extern 声明 + 本头文件里 inline constexpr 定义；MSVC 对这种重复声明不稳定，所以只保留 inline constexpr 定义）
inline constexpr std::array<StyleDef, kNumStyles> kStyles {{
    //  key          name
    //  A: tilt  mudF  mudG  presF presG airF   airG  hpfK  min  max  slope lpf    match
    //  B: spread deessM sibT warmth rev  pre  revWdr echo fb    echoWdr duck vbr   dbl  adlib
    //  C: Dmax sens lo   hi    rel  side ride pocketF static linked hpf mono
    //  D: lufs  loud  ceil  ratio atk rel  gr   scHpf clip width  mono lpf   maxLim
    //  E: 31.5 63 125 250 500 1k 2k 4k 8k 16k
    { "trap", "TRAP",
      -1.0f, 320, -3.0f, 4000, 2.5f, 11000, 4.0f, 0.85f, 90, 120, 24, 22000, 0.70f,
      5.5f, 1.0f, 2.5f, 0.25f, 1.2f, 35, -20, 2, 0.25f, -18, 6, 1.0f, -8, -7,
      6, 6, 250, 5000, 160, 0.3f, 0.5f, 3000, -2.0f, 0, 20, 120,
      -9.0f, -8.0f, -1.0f, 2.5f, 20, 120, 1.5f, 120, 2.0f, -13.0f, 120, 22000, 6,
      { 8, 12, 7, 3, 2, 0, -2, -3, -3, -7 } },
    { "drill", "DRILL",
      -1.0f, 320, -2.5f, 3200, 3.0f, 10000, 2.0f, 0.85f, 100, 130, 24, 22000, 0.70f,
      5.0f, 1.5f, 3.0f, 0.45f, 0.5f, 15, -27, 1, 0.15f, -24, 8, 1.5f, -8, -6,
      6, 6, 200, 4000, 110, 0.3f, 0.0f, 2500, -1.5f, 0, 20, 100,
      -8.5f, -7.5f, -1.0f, 3.0f, 15, 100, 2.0f, 120, 3.0f, -12.0f, 100, 22000, 6,
      { 6, 11, 7, 3, 2, 0, -1.5f, -2.5f, -2.5f, -6 } },
    { "melodic", "MELODIC RAP",
      -1.0f, 400, -2.5f, 4000, 2.0f, 11000, 4.0f, 0.75f, 80, 110, 24, 22000, 0.70f,
      5.5f, 1.0f, 2.5f, 0.30f, 2.2f, 50, -15, 3, 0.38f, -15, 6, -0.5f, -7, -7,
      6, 6, 300, 5000, 200, 0.3f, 0.75f, 2500, -2.0f, 0, 20, 120,
      -9.5f, -8.5f, -1.0f, 2.0f, 20, 150, 1.5f, 120, 1.0f, -11.0f, 120, 22000, 5,
      { 5, 10, 6, 3.5f, 2.5f, 0, -2, -3.5f, -4.5f, -9 } },
    { "boombap", "BOOM BAP",
      -1.5f, 400, -1.5f, 2200, 2.5f, 10000, 1.0f, 0.75f, 70, 95, 12, 22000, 0.60f,
      5.5f, 1.5f, 3.0f, 0.30f, 0.7f, 20, -23, 0, 0.10f, -22, 4, 2.5f, -9, -8,
      7, 6, 400, 4000, 120, 0.3f, 1.5f, 2000, -2.5f, 0, 30, 100,
      -10.0f, -9.0f, -1.0f, 4.0f, 20, 200, 2.5f, 100, 1.0f, -14.0f, 100, 22000, 5,
      { 0, 7, 7, 4.5f, 2.5f, 0, -2.5f, -5, -7, -13 } },
    { "rnb", "R&B",
      -1.5f, 280, -2.0f, 3500, 1.5f, 12000, 4.5f, 0.60f, 70, 95, 12, 22000, 0.70f,
      6.0f, 0.0f, 2.5f, 0.12f, 2.0f, 35, -17, 3, 0.28f, -19, 4, 1.0f, -9, -9,
      4, 4, 200, 5000, 220, 0.3f, 0.5f, 3000, -1.5f, 0, 20, 120,
      -10.0f, -9.5f, -1.0f, 1.8f, 30, 200, 1.0f, 120, 0.0f, -9.5f, 120, 22000, 4,
      { 4, 9, 6.5f, 4, 2.5f, 0, -2.5f, -4, -5, -9 } },
    { "mandopop", "MANDOPOP 华语流行",
      -1.0f, 320, -2.5f, 3200, 2.0f, 11000, 3.0f, 0.70f, 70, 95, 24, 22000, 0.70f,
      5.5f, 0.0f, 2.5f, 0.08f, 2.3f, 60, -15, 3, 0.28f, -19, 3, 2.5f, -10, -10,
      5, 5, 250, 5000, 200, 0.3f, 0.75f, 2800, -2.0f, 0, 20, 120,
      -9.5f, -9.0f, -1.0f, 1.8f, 30, 200, 1.5f, 120, 0.5f, -9.5f, 120, 22000, 4,
      { 1, 6, 5.5f, 3.5f, 2, 0, -2, -3.5f, -5, -10 } },
    { "ballad", "BALLAD 华语抒情",
      -1.5f, 350, -1.0f, 3000, 1.5f, 10000, 3.0f, 0.60f, 60, 85, 12, 22000, 0.60f,
      7.0f, 0.5f, 3.0f, 0.05f, 2.8f, 75, -13, 3, 0.32f, -18, 2.5f, 3.5f, -11, -11,
      5, 5, 250, 4000, 250, 0.3f, 1.5f, 2200, -2.0f, -0.5f, 20, 100,
      -11.0f, -10.0f, -1.0f, 1.5f, 30, 250, 1.0f, 100, 0.0f, -9.5f, 100, 22000, 3,
      { -2, 4, 5, 4, 2.5f, 0, -2.5f, -4.5f, -6.5f, -12 } },
    { "lofi", "LO-FI",
      -3.0f, 500, -1.0f, 1500, 2.0f, 10000, 0.0f, 1.20f, 150, 300, 12, 7500, 0.50f,
      6.5f, 3.0f, 4.0f, 0.60f, 1.1f, 20, -17, 2, 0.35f, -16, 1, -2.0f, -8, -8,
      3, 3, 1500, 3000, 250, 0.3f, 0.0f, 2200, -1.0f, 0, 25, 150,
      -13.0f, -12.0f, -1.0f, 3.0f, 20, 200, 2.5f, 150, 0.0f, -10.5f, 150, 12000, 4,
      { 2, 7, 7, 5, 3, 0, -3.5f, -7, -11, -20 } },
    { "pop", "POP",
      -0.5f, 300, -2.5f, 4000, 3.0f, 11000, 5.0f, 0.80f, 90, 120, 24, 22000, 0.75f,
      5.0f, 0.0f, 2.5f, 0.20f, 1.6f, 30, -19, 1, 0.20f, -21, 6, 1.5f, -9, -8,
      5, 6, 200, 5000, 180, 0.3f, 0.75f, 3000, -2.0f, 0, 20, 120,
      -9.0f, -8.0f, -1.0f, 2.0f, 20, 150, 2.0f, 120, 1.0f, -7.5f, 120, 22000, 5,
      { 2, 8, 6, 3, 1.5f, 0, -1.5f, -2.5f, -3.5f, -7 } },
    { "rock", "ROCK",
      -1.0f, 400, -2.0f, 2200, 3.0f, 10000, 2.0f, 0.85f, 100, 130, 24, 22000, 0.70f,
      5.5f, 1.5f, 3.0f, 0.40f, 1.4f, 60, -21, 0, 0.05f, -15, 4, -1.0f, -6, -8,
      6, 6, 400, 4000, 150, 1.0f, 0.5f, 2200, -2.0f, 0, 25, 100,
      -8.5f, -8.0f, -1.0f, 4.0f, 20, 120, 3.0f, 100, 1.5f, -8.5f, 100, 22000, 5,
      { -2, 5, 5, 3.5f, 2.5f, 0, -1, -2, -3, -8 } },
    { "edm", "EDM",
      -0.5f, 300, -3.0f, 4500, 3.0f, 11000, 5.0f, 1.00f, 120, 180, 24, 22000, 0.75f,
      5.0f, 0.0f, 2.5f, 0.25f, 3.0f, 30, -15, 3, 0.40f, -16, 9, 1.5f, -8, -8,
      7, 6, 300, 6000, 180, 0.3f, 0.0f, 3000, -2.5f, 0, 20, 120,
      -8.0f, -7.0f, -1.0f, 3.0f, 15, 120, 2.0f, 120, 2.0f, -8.5f, 120, 22000, 6,
      { 5, 10, 6, 2.5f, 1, 0, -1, -1.5f, -2, -5 } },
    { "afrobeats", "AFROBEATS",
      -1.0f, 320, -2.0f, 4000, 2.5f, 11000, 3.0f, 0.80f, 90, 120, 24, 22000, 0.70f,
      5.5f, 1.0f, 2.5f, 0.15f, 1.6f, 30, -19, 2, 0.28f, -19, 4, 1.0f, -8, -7,
      4, 5, 300, 5000, 200, 0.3f, 0.0f, 3000, -1.5f, 0, 20, 120,
      -10.0f, -9.0f, -1.0f, 2.0f, 30, 200, 1.5f, 120, 0.5f, -8.5f, 120, 22000, 4,
      { 2, 8, 7, 3.5f, 1.5f, 0, -1.5f, -2.5f, -3, -7 } },
}};
inline const StyleDef& style (StyleId s) { return kStyles[(size_t) std::clamp (s, 0, kNumStyles - 1)]; }

// 按 key（"trap"）或名字（不分大小写，"MELODIC RAP"）找序号；找不到返回 -1。命令行工具用
inline StyleId styleIndex (std::string_view s)
{
    auto lower = [] (char c) { return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c; };
    auto same = [&] (std::string_view a, std::string_view b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) if (lower (a[i]) != lower (b[i])) return false;
        return true;
    };
    for (int i = 0; i < kNumStyles; ++i)
        if (same (s, kStyles[(size_t) i].key) || same (s, kStyles[(size_t) i].name)) return i;
    return -1;
}
} // namespace mm
