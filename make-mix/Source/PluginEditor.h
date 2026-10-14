#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// 黑底 + 一种红（和 MAKE STUDIO / MV 一样）。顶上：角色 + 风格；中间：当前角色的旋钮；底下：MIX、说明、电平表。
class MixLook : public juce::LookAndFeel_V4
{
public:
    MixLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    static juce::Font ui (float size, bool bold = false);   // 带中文回退字体
};

class MakeMixEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MakeMixEditor (MakeMixProcessor&);
    ~MakeMixEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showRole();
    struct Knob { juce::Slider s; int param = 0; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att; };

    MakeMixProcessor& proc;
    MixLook look;
    std::array<Knob, mm::kNumParams> knobs;
    juce::ComboBox styleBox;
    juce::TextButton roleBtn[3], mixBtn, undoBtn, defaultsBtn, analyzeBtn, closeBtn, useStyleBtn;
    std::shared_ptr<const mm::analysis::Report> rep; bool showReport = false;
    void setReportVisible (bool);
    void paintReport (juce::Graphics&);
    juce::String status, link, noteText;
    float mIn = 0, mOut = 0, mG1 = 0, mG2 = 0; std::array<float, mm::kKeyBands> mCut {};
    int shownRole = -1, shownStyle = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MakeMixEditor)
};
