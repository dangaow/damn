#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// 黑底 + 一种红（和 MAKE STUDIO / MV 的配色一样）。上面一排 8 个大旋钮，下面是输入输出和电平表。
class VoxLook : public juce::LookAndFeel_V4
{
public:
    VoxLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (14.0f, juce::Font::bold); }
};

class MakeVoxEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MakeVoxEditor (MakeVoxProcessor&);
    ~MakeVoxEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    struct Knob { juce::Slider s; juce::String label, hint; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att; };

    MakeVoxProcessor& proc;
    VoxLook look;
    std::array<Knob, 8> big;
    Knob input, output;
    juce::ComboBox preset, echoTime;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> echoAtt;
    float mIn = 0, mOut = 0, mComp = 0, mDe = 0;
    int shownProgram = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MakeVoxEditor)
};
