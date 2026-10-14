#pragma once
// 外观（第 8 节）：MixLook（v0 VoxLook 演变而来）、颜色、MeterBar、BandView、MixButton、SegmentedRole。
// 【WP0 桩】空类，桩处理器不引用。WP4 替换。
#include <juce_gui_basics/juce_gui_basics.h>

class MixLook : public juce::LookAndFeel_V4
{
public:
    MixLook();
};
