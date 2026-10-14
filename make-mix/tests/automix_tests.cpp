// AutoMix 纯数学的单元测试（10.1，WP3）。【WP0 桩】只做接口冒烟测试；WP3 替换为完整测试。
#include <juce_core/juce_core.h>
#include "../Source/AutoMix.h"

namespace
{
class AutoMixSmokeTests final : public juce::UnitTest
{
public:
    AutoMixSmokeTests() : juce::UnitTest ("AutoMix (smoke)", "automix") {}

    void runTest() override
    {
        beginTest ("styleDefaults returns only the requested role's parameters");
        for (int s = 0; s < mm::kNumStyles; ++s)
            for (auto role : { mm::Role::Vocal, mm::Role::Beat, mm::Role::Bus })
            {
                const auto set = mm::automix::styleDefaults (s, role);
                expect (! set.values.empty());
                for (const auto& [id, v] : set.values)
                {
                    const int i = mm::paramIndex (id);
                    expect (i >= 0 && mm::kParams[i].role == role, juce::String ("foreign parameter ") + id);
                    expect (std::isfinite (v));
                }
            }
    }
};

AutoMixSmokeTests autoMixSmokeTests;
} // namespace
