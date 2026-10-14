// DSP 和处理链的单元测试（10.1，WP1）。【WP0 桩】只做接口冒烟测试；WP1 替换为完整测试。
#include <juce_core/juce_core.h>
#include "../Source/Chains.h"

namespace
{
class DspSmokeTests final : public juce::UnitTest
{
public:
    DspSmokeTests() : juce::UnitTest ("DSP / chains (smoke)", "dsp") {}

    void runTest() override
    {
        beginTest ("every chain prepares and processes default parameters without NaN");
        const mm::ChainSpec spec { 48000.0, 512, 2, mm::latencyFor (48000.0, 512) };
        mm::VocalChain v; mm::BeatChain b; mm::BusChain m;
        v.prepare (spec); b.prepare (spec); m.prepare (spec);
        const auto comp = mm::defaultCompProfile (mm::Role::Vocal);
        v.setParams (mm::VocalParams {}, comp);
        b.setParams (mm::BeatParams {}, mm::defaultCompProfile (mm::Role::Beat));
        m.setParams (mm::BusParams {}, mm::defaultCompProfile (mm::Role::Bus));
        std::vector<float> l (512), r (512);
        juce::Random rng (getRandom().nextInt64());
        bool finite = true;
        for (int blockIndex = 0; blockIndex < 8; ++blockIndex)
        {
            for (int pass = 0; pass < 3; ++pass)
            {
                for (size_t i = 0; i < l.size(); ++i) { l[i] = 0.25f * (rng.nextFloat() * 2 - 1); r[i] = 0.25f * (rng.nextFloat() * 2 - 1); }
                float* ch[2] = { l.data(), r.data() };
                const mm::ProcessContext ctx { blockIndex * 512, 120.0 };
                if (pass == 0) v.process (ch, 2, 512, ctx, nullptr);
                else if (pass == 1) b.process (ch, 2, nullptr, 0, 512, ctx, nullptr);
                else m.process (ch, 2, 512, ctx);
                for (size_t i = 0; i < l.size(); ++i) finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
            }
        }
        expect (finite);
    }
};

DspSmokeTests dspSmokeTests;
} // namespace
