// Hub 的并发 / 生命周期测试（10.1，WP2；也用 TSan / ASan 编译）。【WP0 桩】只做接口冒烟测试；WP2 替换为完整测试。
#include <juce_core/juce_core.h>
#include "../Source/Hub.h"

namespace
{
class HubSmokeTests final : public juce::UnitTest
{
public:
    HubSmokeTests() : juce::UnitTest ("Hub (smoke)", "hub") {}

    void runTest() override
    {
        beginTest ("a fresh private hub has no live slots and reads silence");
        auto hub = std::make_unique<mm::Hub>();
        expectEquals ((juce::int64) hub->liveMask(), (juce::int64) 0);
        float bands[mm::kKeyBands];
        expect (hub->readVocalKey (0, -1, 100, bands) == mm::KeyRead::Silent);
        expect (hub->session (0).phase == mm::SessionView::Phase::Idle);
        expectEquals ((int) mm::Hub::abiVersion, 1);
    }
};

HubSmokeTests hubSmokeTests;
} // namespace
