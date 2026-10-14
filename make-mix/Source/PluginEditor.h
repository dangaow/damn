#pragma once
// MAKE MIX 编辑器（第 8 节）。
// 【WP0 桩】空类，桩处理器不引用（createEditor 返回通用编辑器）。WP4 替换；WP2 之后在 createEditor 里 new MakeMixEditor (*this)。
#include "PluginProcessor.h"
#include "Look.h"

class MakeMixEditor : public juce::AudioProcessorEditor
{
public:
    explicit MakeMixEditor (MakeMixProcessor&);
    ~MakeMixEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MakeMixProcessor& proc;
    MixLook look;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MakeMixEditor)
};
