// 【WP0 桩】所有函数返回默认值；integratedLufs / truePeakDb → −100。WP3 换成 4.2 的实现。
#include "Analysis.h"
#include "Dsp.h"

namespace mm::analysis
{
Features analyse (const CaptureData&, Role, int64_t) { return {}; }
Features analyseBuffer (const float* const*, int, int64_t, double, const std::vector<CaptureData::Segment>&, Role) { return {}; }
float integratedLufs (const float* const*, int, int64_t, double) { return -100.0f; }
float truePeakDb (const float* const*, int, int64_t, double) { return -100.0f; }
Joint joint (const std::vector<const Features*>&, const std::vector<float>&, const std::vector<const Features*>&, int) { return {}; }
std::vector<float> sibilancePercentiles (const float*, int64_t, const std::vector<uint8_t>&) { return std::vector<float> (3, 0.0f); }
} // namespace mm::analysis
