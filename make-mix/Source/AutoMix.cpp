// 【WP0 桩】styleDefaults 返回该角色的参数表默认值；其它函数原样返回输入或默认值。WP3 换成第 5 节的实现。
#include "AutoMix.h"

namespace mm::automix
{
VocalParams vocalStatic (const StyleDef&, const VocalIn&, const JointIn*, SubRole, bool, bool, const BeatIn*, double) { return {}; }
EqFit fitVocalEq (const std::array<float, kAnaBands>&, const std::array<float, kAnaBands>&, float, int, double)
{
    return { kParams[v_lowF].def, 0, kParams[v_midF].def, 0, kParams[v_presF].def, 0, kParams[v_airF].def, 0, 0 };
}
float compAmountFor (const std::vector<float>&, float, float) { return kParams[v_comp].def; }
void ballisticsFromCrest (float, float& attackMs, float& releaseMs) { attackMs = kParams[v_compAtk].def; releaseMs = kParams[v_compRel].def; }
float deessAmountFor (float, float) { return kParams[v_deess].def; }
float sendAmountCorrect (float amount, float, float) { return amount; }
float levelForVbr (float, float) { return 0.0f; }
BeatParams beatStatic (const StyleDef&, bool) { return {}; }
float adaptUnmask (float amount, float) { return amount; }
BusParams busStatic (const StyleDef&, const BusIn&, float, float) { return {}; }
float glueThresholdFor (const std::vector<float>&, float, float, float) { return kParams[m_glue].def; }
float clipFor (const StyleDef&, float) { return 0.0f; }

ParamSet styleDefaults (StyleId, Role role)
{
    ParamSet s;
    for (int i = 0; i < kNumParams; ++i)
        if (kParams[i].role == role) s.set (kParams[i].id, kParams[i].def);
    return s;
}

float staticGrDb (float, float, float, float) { return 0.0f; }
} // namespace mm::automix
