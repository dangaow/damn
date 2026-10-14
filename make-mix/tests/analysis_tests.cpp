// 离线分析的单元测试（10.1，WP3）。【WP0 桩】只做接口冒烟测试；WP3 替换为完整测试。
#include <juce_core/juce_core.h>
#include "../Source/Analysis.h"

namespace
{
class AnalysisSmokeTests final : public juce::UnitTest
{
public:
    AnalysisSmokeTests() : juce::UnitTest ("Analysis (smoke)", "analysis") {}

    void runTest() override
    {
        beginTest ("silence measures as (near) nothing");
        std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
        const float* ch[2] = { l.data(), r.data() };
        expect (mm::analysis::integratedLufs (ch, 2, 48000, 48000.0) <= -70.0f);
        expect (mm::analysis::truePeakDb (ch, 2, 48000, 48000.0) <= -70.0f);
        expectEquals ((int) mm::analysis::sibilancePercentiles (l.data(), 0, {}).size(), 3);
    }
};

AnalysisSmokeTests analysisSmokeTests;
} // namespace
