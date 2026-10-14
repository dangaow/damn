#include "Params.h"
#include <cstdio>

namespace mm
{
namespace
{
const char* const kSlopeChoices[] = { "12 dB", "24 dB" };
const char* const kEchoTimeChoices[] = { "1/16", "1/8", "1/8D", "1/4", "1/4D", "1/2" };
const char* const kKeyChoices[] = { "AUTO", "LINK", "SIDECHAIN", "OFF" };

constexpr ParamInfo F (const char* id, const char* name, Role r, float lo, float hi, float def, float centre, float step,
                       const char* unit, bool automatable = true)
{
    return { id, name, r, PKind::Float, lo, hi, def, centre, step, unit, automatable, nullptr, 0 };
}
template <size_t N>
constexpr ParamInfo C (const char* id, const char* name, Role r, const char* const (&choices)[N], int def, bool automatable)
{
    return { id, name, r, PKind::Choice, 0.0f, (float) (N - 1), (float) def, 0.0f, 1.0f, "", automatable, choices, (int) N };
}
constexpr Role V = Role::Vocal, B = Role::Beat, M = Role::Bus;
} // namespace

// 顺序 = PIdx。改动只能在末尾追加（宿主按 ID 存自动化，ID 和范围不能变）
const ParamInfo kParams[] = {
    // —— VOCAL ——
    F ("v_input",      "Vox Input",          V, -24, 24, 0, 0, 0.1f, "dB"),
    F ("v_hpf",        "Vox Low Cut",        V, 20, 400, 105, 100, 0, "Hz"),
    C ("v_hpfSlope",   "Vox Low Cut Slope",  V, kSlopeChoices, 1, false),
    F ("v_lowF",       "Vox Mud Freq",       V, 120, 800, 320, 320, 0, "Hz"),
    F ("v_lowG",       "Vox Mud",            V, -12, 6, -3, 0, 0.1f, "dB"),
    F ("v_midF",       "Vox Mid Freq",       V, 500, 3000, 1200, 1200, 0, "Hz"),
    F ("v_midG",       "Vox Mid",            V, -8, 6, 0, 0, 0, "dB"),
    F ("v_presF",      "Vox Presence Freq",  V, 1500, 8000, 4000, 3500, 0, "Hz"),
    F ("v_presG",      "Vox Presence",       V, -6, 8, 2.5f, 0, 0, "dB"),
    F ("v_airF",       "Vox Air Freq",       V, 6000, 16000, 11000, 10000, 0, "Hz"),
    F ("v_airG",       "Vox Air",            V, -6, 10, 4, 0, 0, "dB"),
    F ("v_lpf",        "Vox High Cut",       V, 3000, 22000, 22000, 9000, 0, "Hz"),
    F ("v_comp",       "Vox Compress",       V, 0, 1, 0.5f, 0, 0, "%"),
    F ("v_compAtk",    "Vox Comp Attack",    V, 1, 80, 15, 15, 0, "ms"),
    F ("v_compRel",    "Vox Comp Release",   V, 30, 800, 180, 180, 0, "ms"),
    F ("v_deess",      "Vox De-ess",         V, 0, 1, 0.4f, 0, 0, "%"),
    F ("v_deessF",     "Vox De-ess Freq",    V, 3000, 10000, 6000, 6000, 0, "Hz"),
    F ("v_warmth",     "Vox Warmth",         V, 0, 1, 0.25f, 0, 0, "%"),
    F ("v_echo",       "Vox Echo",           V, 0, 1, 0.2f, 0, 0, "%"),
    C ("v_echoTime",   "Vox Echo Time",      V, kEchoTimeChoices, 2, true),
    F ("v_echoFb",     "Vox Echo Feedback",  V, 0, 0.8f, 0.25f, 0, 0, "%"),
    F ("v_space",      "Vox Space",          V, 0, 1, 0.3f, 0, 0, "%"),
    F ("v_spaceDecay", "Vox Space Decay",    V, 0.3f, 4, 1.2f, 1.5f, 0, "s"),
    F ("v_preDelay",   "Vox Pre-delay",      V, 0, 150, 35, 0, 0, "ms"),
    F ("v_wetDuck",    "Vox Wet Duck",       V, 0, 12, 6, 0, 0, "dB"),
    F ("v_level",      "Vox Level",          V, -24, 24, 0, 0, 0.1f, "dB"),
    // —— BEAT ——
    F ("b_hpf",        "Beat Low Cut",       B, 10, 120, 20, 30, 0, "Hz"),
    F ("b_monoBelow",  "Beat Mono Below",    B, 0, 300, 120, 0, 1, "Hz"),
    F ("b_pocketF",    "Beat Pocket Freq",   B, 500, 8000, 3000, 2500, 0, "Hz"),
    F ("b_pocketG",    "Beat Pocket",        B, -9, 0, 0, 0, 0, "dB"),
    F ("b_pocketQ",    "Beat Pocket Q",      B, 0.4f, 3, 1, 1, 0, ""),
    F ("b_unmask",     "Beat Unmask",        B, 0, 1, 0.67f, 0, 0, "%"),
    F ("b_sens",       "Beat Unmask Sens",   B, 0, 12, 6, 0, 0, "dB"),
    F ("b_focusLo",    "Beat Focus Low",     B, 120, 2000, 250, 400, 0, "Hz"),
    F ("b_focusHi",    "Beat Focus High",    B, 1000, 9000, 5000, 3500, 0, "Hz"),
    F ("b_release",    "Beat Unmask Release",B, 40, 600, 160, 160, 0, "ms"),
    F ("b_side",       "Beat Unmask Side",   B, 0, 1, 0.3f, 0, 0, "%"),
    F ("b_ride",       "Beat Ride",          B, 0, 4, 0.5f, 0, 0, "dB"),
    C ("b_key",        "Beat Key Source",    B, kKeyChoices, 0, false),
    F ("b_level",      "Beat Level",         B, -24, 12, 0, 0, 0, "dB"),
    // —— BUS ——
    F ("m_lowG",       "Bus Low",            M, -4, 4, 0, 0, 0, "dB"),
    F ("m_lowMidG",    "Bus Low-Mid",        M, -4, 4, 0, 0, 0, "dB"),
    F ("m_presG",      "Bus Presence",       M, -4, 4, 0, 0, 0, "dB"),
    F ("m_airG",       "Bus Air",            M, -4, 4, 0, 0, 0, "dB"),
    F ("m_lpf",        "Bus High Cut",       M, 6000, 22000, 22000, 12000, 0, "Hz"),
    F ("m_glue",       "Bus Glue Threshold", M, -36, 0, -14, 0, 0, "dB"),
    F ("m_glueRatio",  "Bus Glue Ratio",     M, 1, 6, 2.5f, 2, 0, ":1"),
    F ("m_glueAtk",    "Bus Glue Attack",    M, 1, 60, 20, 15, 0, "ms"),
    F ("m_glueRel",    "Bus Glue Release",   M, 50, 600, 120, 150, 0, "ms"),
    F ("m_glueScHpf",  "Bus Glue SC HPF",    M, 20, 300, 120, 100, 0, "Hz"),
    F ("m_width",      "Bus Width",          M, 0, 2, 1, 0, 0, "%"),
    F ("m_monoBelow",  "Bus Mono Below",     M, 0, 300, 120, 0, 0, "Hz"),
    F ("m_drive",      "Bus Drive",          M, 0, 24, 0, 0, 0.1f, "dB"),
    F ("m_clip",       "Bus Clip",           M, 0, 6, 0, 0, 0.1f, "dB"),
    F ("m_ceiling",    "Bus Ceiling",        M, -3, 0, -1, 0, 0.1f, "dBTP"),
    F ("m_target",     "Bus Target",         M, -18, -6, -9, 0, 0.1f, "LUFS", false),
};
const int kNumParams = (int) (sizeof (kParams) / sizeof (kParams[0]));
static_assert (sizeof (kParams) / sizeof (kParams[0]) == (size_t) kPIdxCount, "kParams 和 PIdx 的数量不一致");

//==============================================================================
// 结构体 ↔ 参数：每个字段只在这里列一次，三种转换共用
namespace
{
template <typename P, typename Fn> void fields (P& p, Fn&& f)
{
    if constexpr (std::is_same_v<std::remove_const_t<P>, VocalParams>)
    {
        f (v_input, p.input); f (v_hpf, p.hpf); f (v_hpfSlope, p.hpfSlope); f (v_lowF, p.lowF); f (v_lowG, p.lowG);
        f (v_midF, p.midF); f (v_midG, p.midG); f (v_presF, p.presF); f (v_presG, p.presG); f (v_airF, p.airF); f (v_airG, p.airG);
        f (v_lpf, p.lpf); f (v_comp, p.comp); f (v_compAtk, p.compAtk); f (v_compRel, p.compRel); f (v_deess, p.deess);
        f (v_deessF, p.deessF); f (v_warmth, p.warmth); f (v_echo, p.echo); f (v_echoTime, p.echoTime); f (v_echoFb, p.echoFb);
        f (v_space, p.space); f (v_spaceDecay, p.spaceDecay); f (v_preDelay, p.preDelay); f (v_wetDuck, p.wetDuck); f (v_level, p.level);
    }
    else if constexpr (std::is_same_v<std::remove_const_t<P>, BeatParams>)
    {
        f (b_hpf, p.hpf); f (b_monoBelow, p.monoBelow); f (b_pocketF, p.pocketF); f (b_pocketG, p.pocketG); f (b_pocketQ, p.pocketQ);
        f (b_unmask, p.unmask); f (b_sens, p.sens); f (b_focusLo, p.focusLo); f (b_focusHi, p.focusHi); f (b_release, p.release);
        f (b_side, p.side); f (b_ride, p.ride); f (b_key, p.key); f (b_level, p.level);
    }
    else
    {
        static_assert (std::is_same_v<std::remove_const_t<P>, BusParams>);
        f (m_lowG, p.lowG); f (m_lowMidG, p.lowMidG); f (m_presG, p.presG); f (m_airG, p.airG); f (m_lpf, p.lpf); f (m_glue, p.glue);
        f (m_glueRatio, p.glueRatio); f (m_glueAtk, p.glueAtk); f (m_glueRel, p.glueRel); f (m_glueScHpf, p.glueScHpf);
        f (m_width, p.width); f (m_monoBelow, p.monoBelow); f (m_drive, p.drive); f (m_clip, p.clip); f (m_ceiling, p.ceiling);
        f (m_target, p.target);
    }
}

// 选项参数存的是序号（float），四舍五入并限制在选项范围内
void assign (int pidx, float& dst, float v) noexcept { dst = std::isfinite (v) ? v : kParams[pidx].def; }
void assign (int pidx, int& dst, float v) noexcept
{
    const auto& info = kParams[pidx];
    const float r = std::isfinite (v) ? std::round (v) : info.def;
    dst = (int) std::clamp (r, info.min, info.max);
}

template <typename P> P fromRaw (const float* raw)
{
    P p {};
    fields (p, [raw] (int i, auto& field) { assign (i, field, raw[i]); });
    return p;
}
template <typename P> P fromSet (const ParamSet& s)
{
    P p {};
    fields (p, [&s] (int i, auto& field)
    {
        const float* v = s.find (kParams[i].id);
        assign (i, field, v != nullptr ? *v : kParams[i].def);
    });
    return p;
}
template <typename P> ParamSet toSetImpl (const P& p)
{
    ParamSet s;
    s.values.reserve (26);
    fields (p, [&s] (int i, const auto& field) { s.values.emplace_back (kParams[i].id, (float) field); });
    return s;
}
} // namespace

VocalParams vocalFrom (const float* raw) { return fromRaw<VocalParams> (raw); }
BeatParams  beatFrom  (const float* raw) { return fromRaw<BeatParams> (raw); }
BusParams   busFrom   (const float* raw) { return fromRaw<BusParams> (raw); }
VocalParams vocalFrom (const ParamSet& s) { return fromSet<VocalParams> (s); }
BeatParams  beatFrom  (const ParamSet& s) { return fromSet<BeatParams> (s); }
BusParams   busFrom   (const ParamSet& s) { return fromSet<BusParams> (s); }
ParamSet toSet (const VocalParams& p) { return toSetImpl (p); }
ParamSet toSet (const BeatParams& p)  { return toSetImpl (p); }
ParamSet toSet (const BusParams& p)   { return toSetImpl (p); }

int paramIndex (const std::string& id)
{
    for (int i = 0; i < kNumParams; ++i)
        if (id == kParams[i].id) return i;
    return -1;
}

float clampParam (int pidx, float v)
{
    if (pidx < 0 || pidx >= kNumParams) return v;
    const auto& info = kParams[pidx];
    if (! std::isfinite (v)) return info.def;
    v = std::clamp (v, info.min, info.max);
    if (info.step > 0)
        v = std::clamp (info.min + std::round ((v - info.min) / info.step) * info.step, info.min, info.max);
    return v;
}

bool isLevelOwning (int pidx) noexcept
{
    return pidx == v_input || pidx == v_level || pidx == b_level || pidx == m_drive || pidx == m_glue || pidx == m_target;
}

//==============================================================================
// 显示用的文字（界面、宿主、解释文字都用这一个）
namespace
{
std::string fmt (const char* f, double v)
{
    char buf[48];
    std::snprintf (buf, sizeof (buf), f, v);
    return buf;
}
// 先按显示精度取整，再决定正负号，避免出现 "-0.0"
double tidy (double v, double scale) { return std::round (v * scale) / scale + 0.0; }   // + 0.0 把 −0 变成 +0

bool isOff (int pidx, float v)
{
    switch (pidx)
    {
        case v_lpf: case m_lpf:             return v >= 21000.0f;
        case b_hpf:                         return v <= 12.0f;
        case b_monoBelow: case m_monoBelow: return v < 20.0f;
        default:                            return false;
    }
}
} // namespace

std::string formatValue (int pidx, float v)
{
    if (pidx < 0 || pidx >= kNumParams) return {};
    const auto& info = kParams[pidx];
    if (! std::isfinite (v)) v = info.def;

    if (info.kind == PKind::Choice)
    {
        const int i = std::clamp ((int) std::lround (v), 0, info.numChoices - 1);
        return info.choices[i];
    }
    if (isOff (pidx, v)) return "OFF";

    const std::string unit = info.unit;
    if (unit == "dB" || unit == "dBTP" || unit == "LUFS")
    {
        const double r = tidy (v, 10);
        const bool signedGain = unit == "dB" && info.min < 0;          // 增益类显示 "+2.0 dB"；量类（0 起步）不带 +
        return fmt (signedGain && r > 0 ? "+%.1f" : "%.1f", r) + " " + unit;
    }
    if (unit == "Hz")
    {
        if (v < 999.5f) return fmt ("%.0f Hz", tidy (v, 1));
        return fmt ("%.1f kHz", tidy (v / 1000.0, 10));
    }
    if (unit == "%")  return fmt ("%.0f%%", tidy (v * 100.0, 1));
    if (unit == "ms") return v < 9.95f ? fmt ("%.1f ms", tidy (v, 10)) : fmt ("%.0f ms", tidy (v, 1));
    if (unit == "s")  return v < 0.995f ? fmt ("%.2f s", tidy (v, 100)) : fmt ("%.1f s", tidy (v, 10));
    if (unit == ":1") return fmt ("%.1f:1", tidy (v, 10));
    return fmt ("%.2f", tidy (v, 100));                                  // 无单位（Q）
}

//==============================================================================
// 没学习过时的默认补偿曲线（7.3 的占位值；解算器会直接测量这些量，它们只影响第一次 MIX 之前）
CompProfile defaultCompProfile (Role role)
{
    CompProfile p;
    p.levelLoDb = -80.0f;

    // 30 ms RMS 直方图：高斯，μ = −22 dBFS，σ = 6 dB
    double sumW = 0;
    for (size_t i = 0; i < p.levelW.size(); ++i)
    {
        const double c = p.levelLoDb + (double) i + 0.5, z = (c + 22.0) / 6.0;
        p.levelW[i] = (float) std::exp (-0.5 * z * z);
        sumW += p.levelW[i];
    }
    double meanPow = 0;                                       // 直方图的功率平均（LTAS 的总功率对齐到它）
    for (size_t i = 0; i < p.levelW.size(); ++i)
    {
        p.levelW[i] = (float) (p.levelW[i] / sumW);
        meanPow += p.levelW[i] * std::pow (10.0, (p.levelLoDb + (double) i + 0.5) / 10.0);
    }

    // 频谱形状：每倍频程的相对电平（粉噪 = 平），再换成每个 ERB 频段的绝对功率
    auto shapeDb = [role] (double f)
    {
        if (role == Role::Vocal)                              // 150 Hz 以上 −1.5 dB/oct；120 Hz 以下 −12 dB/oct
        {
            const double top = -1.5 * std::log2 (std::max (f, 150.0) / 1000.0);
            return f >= 120.0 ? top : top - 12.0 * std::log2 (120.0 / f);
        }
        // BEAT / BUS：TRAP 的总线倍频程曲线（表 E），在 log f 上线性插值，两端外推取端点值
        constexpr double curve[kOctBands] = { 8, 12, 7, 3, 2, 0, -2, -3, -3, -7 };
        const double x = std::log2 (f / kOctCentresHz[0]);    // 倍频程中心正好相隔 1 oct
        if (x <= 0) return curve[0];
        if (x >= kOctBands - 1) return curve[kOctBands - 1];
        const int i = (int) x; const double t = x - i;
        return curve[i] + (curve[i + 1] - curve[i]) * t;
    };
    double sumP = 0;
    std::array<double, kAnaBands> pw {};
    for (int k = 0; k < kAnaBands; ++k)
    {
        const double fc = kAnaCentresHz[k], bw = kAnaEdgesHz[k + 1] - kAnaEdgesHz[k];
        pw[(size_t) k] = std::pow (10.0, shapeDb (fc) / 10.0) * bw / fc;   // 等每倍频程功率 → 频段功率 ∝ 带宽 / 中心频率
        sumP += pw[(size_t) k];
    }
    for (int k = 0; k < kAnaBands; ++k)
        p.ltasDb[(size_t) k] = (float) (10.0 * std::log10 (pw[(size_t) k] / sumP * meanPow));

    p.satGainDb = { 0.0f, 0.1f, 0.25f, 0.45f, 0.7f, 0.95f, 1.2f, 1.45f, 1.7f };
    p.crestDb = 12.0f;
    p.scAlignRaw = 0;
    p.valid = 0;
    return p;
}

//==============================================================================
std::string InstanceId::toString() const
{
    char buf[40];
    std::snprintf (buf, sizeof (buf), "%016llx%016llx", (unsigned long long) hi, (unsigned long long) lo);
    return buf;
}

InstanceId InstanceId::fromString (const std::string& s)
{
    if (s.size() != 32) return {};
    uint64_t part[2] = { 0, 0 };
    for (size_t i = 0; i < 32; ++i)
    {
        const char c = s[i];
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        if (d < 0) return {};
        part[i / 16] = (part[i / 16] << 4) | (uint64_t) d;
    }
    return { part[0], part[1] };
}

//==============================================================================
// 在消息线程分配并清零：清零会让操作系统真正分配内存页，音频线程追加时不会缺页
CaptureData::CaptureData (double fs_, int numCh_, bool withKey, double seconds)
    : fs (fs_), numCh (std::clamp (numCh_, 1, 2)),
      capacity (std::max<int64_t> (0, (int64_t) std::ceil (std::max (0.0, fs_ * seconds))))
{
    for (int c = 0; c < numCh; ++c) ch[(size_t) c].assign ((size_t) capacity, 0.0f);
    if (withKey) key.assign ((size_t) capacity, 0.0f);
}
} // namespace mm
