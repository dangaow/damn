// 【WP0 桩】WP4 替换。
#include "PluginEditor.h"

MakeMixEditor::MakeMixEditor (MakeMixProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setSize (980, 560);
}
MakeMixEditor::~MakeMixEditor() = default;
void MakeMixEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colour (0xff0a0b10)); }
void MakeMixEditor::resized() {}
