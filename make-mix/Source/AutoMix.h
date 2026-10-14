#pragma once
// 自动混音的纯数学（第 5 节）：只用 std，不碰音频和 JUCE。
// 【WP0 桩】公开接口是冻结的（5.1）；AutoMix.cpp 由 WP3 替换。
#include "Params.h"
#include "Styles.h"

namespace mm::automix
{
// —— 输入：都由 Solver 从 analysis::Features 填 ——
struct VocalIn  { float iActive, activeSec, voicedSec, f0P5, crestC2, crestDb, hfTilt; std::array<float, kAnaBands> ltasPink; std::vector<float> momActive; };
struct BeatIn   { float hfTilt; bool keyAvailable; };
struct JointIn  { bool valid; std::array<float, kAnaBands> medBeatMinusVoc; float maskedFraction; };
struct BusIn    { float iIn; std::array<float, kOctBands> octMidNorm; float widthLu; std::vector<float> rmsGlue; };

struct EqFit { float lowF, lowG, midF, midG, presF, presG, airF, airG; float residualDb; };

VocalParams vocalStatic (const StyleDef&, const VocalIn&, const JointIn*, SubRole, bool beatLinked, bool unmaskActive,
                         const BeatIn* beatSum, double fs);         // 5.3 步骤 V1–V7
EqFit fitVocalEq (const std::array<float, kAnaBands>& targetMinusMeasured, const std::array<float, kAnaBands>& weight,
                  float hpfHz, int hpfSlope, double fs);
float compAmountFor (const std::vector<float>& momInternal, float crestDb, float targetSpread);   // 二分
void  ballisticsFromCrest (float crestC2, float& attackMs, float& releaseMs);
float deessAmountFor (float p90, float margin);                    // 返回 0..1
float sendAmountCorrect (float amount, float wdrTarget, float wdrMeasured);
float levelForVbr (float vbrTarget, float vbrMeasured);            // 限幅 [-24, 24]
BeatParams beatStatic (const StyleDef&, bool keyAvailable);
float adaptUnmask (float amount, float maskedAfter);
BusParams busStatic (const StyleDef&, const BusIn&, float targetLufs, float ceilingDb);   // EQ、glue 比例/时间、宽度
float glueThresholdFor (const std::vector<float>& rmsDb, float ratio, float kneeDb, float targetMeanGr);
float clipFor (const StyleDef&, float overshootP97Db);
ParamSet styleDefaults (StyleId, Role);                            // 7.3
// 静态曲线（和 Dsp.h 的 Compressor 完全一样的软拐点）
float staticGrDb (float levelDb, float thrDb, float ratio, float kneeDb);
} // namespace mm::automix
