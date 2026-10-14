#include "PluginEditor.h"

namespace
{
const juce::Colour bg (0xff0a0b10), panel (0xff12141b), line (0xff23262f), red (0xffe0112b), silver (0xffc9ced6), dim (0xff6b7080);
const char* bigIds[]    = { "clean", "compress", "deess", "presence", "air", "warmth", "echo", "space" };
const char* bigLabels[] = { "CLEAN", "COMPRESS", "DE-ESS", "PRESENCE", "AIR", "WARMTH", "ECHO", "SPACE" };
const char* bigHints[]  = { "low cut + mud", "level it out", "tame the S", "cut through", "top sheen", "tape drive", "synced delay", "reverb" };
}

VoxLook::VoxLook()
{
    setColour (juce::Slider::textBoxTextColourId, silver);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, panel);
    setColour (juce::ComboBox::textColourId, silver);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::arrowColourId, red);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, silver);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, red);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

void VoxLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6);
    const float rad = juce::jmin (r.getWidth(), r.getHeight()) / 2, cx = r.getCentreX(), cy = r.getCentreY(), ang = a0 + pos * (a1 - a0);
    const float tw = juce::jmax (3.0f, rad * 0.09f);
    juce::Path track; track.addCentredArc (cx, cy, rad - tw, rad - tw, 0, a0, a1, true);
    g.setColour (line); g.strokePath (track, juce::PathStrokeType (tw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path val; val.addCentredArc (cx, cy, rad - tw, rad - tw, 0, a0, ang, true);
    g.setColour (red); g.strokePath (val, juce::PathStrokeType (tw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float kr = rad - tw * 2.6f;                                              // 旋钮本体：很暗的金属
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2d36), cx, cy - kr, juce::Colour (0xff111318), cx, cy + kr, false));
    g.fillEllipse (cx - kr, cy - kr, kr * 2, kr * 2);
    g.setColour (juce::Colour (0xff3a3e49)); g.drawEllipse (cx - kr, cy - kr, kr * 2, kr * 2, 1.0f);
    juce::Path ptr; ptr.addRoundedRectangle (-1.5f, -kr + 4, 3.0f, kr * 0.42f, 1.5f);
    g.setColour (silver); g.fillPath (ptr, juce::AffineTransform::rotation (ang).translated (cx, cy));
}

void VoxLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId)); g.fillRoundedRectangle (0, 0, (float) w, (float) h, 4);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId)); g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, (float) h - 1, 4, 1);
    juce::Path tri; const float ax = (float) w - 16, ay = (float) h * 0.5f;
    tri.addTriangle (ax - 5, ay - 3, ax + 5, ay - 3, ax, ay + 4);
    g.setColour (red); g.fillPath (tri);
}

MakeVoxEditor::MakeVoxEditor (MakeVoxProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);
    auto setup = [this] (Knob& k, const char* id, juce::String label, juce::String hint, bool small)
    {
        k.label = label; k.hint = hint;
        k.s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, small ? 64 : 72, 18);
        k.s.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        k.s.setDoubleClickReturnValue (true, proc.apvts.getParameter (id)->convertFrom0to1 (proc.apvts.getParameter (id)->getDefaultValue()));
        addAndMakeVisible (k.s);
        k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.s);
    };
    for (int i = 0; i < 8; ++i) setup (big[(size_t) i], bigIds[i], bigLabels[i], bigHints[i], false);
    setup (input, "input", "INPUT", "", true);
    setup (output, "output", "OUTPUT", "", true);

    for (int i = 0; i < proc.getNumPrograms(); ++i) preset.addItem (proc.getProgramName (i), i + 1);
    preset.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    preset.onChange = [this] { if (preset.getSelectedId() > 0) proc.setCurrentProgram (preset.getSelectedId() - 1); };
    addAndMakeVisible (preset);

    echoTime.addItemList (dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("echoTime"))->choices, 1);
    addAndMakeVisible (echoTime);
    echoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "echoTime", echoTime);

    setSize (960, 460);
    startTimerHz (30);
}

MakeVoxEditor::~MakeVoxEditor() { setLookAndFeel (nullptr); }

void MakeVoxEditor::timerCallback()
{
    auto fall = [] (float& shown, float v, float k) { shown = v > shown ? v : shown * k + v * (1 - k); };
    fall (mIn, proc.inPeak.load(), 0.86f); fall (mOut, proc.outPeak.load(), 0.86f);
    fall (mComp, proc.compGr.load(), 0.8f); fall (mDe, proc.deessGr.load(), 0.8f);
    if (proc.getCurrentProgram() != shownProgram) { shownProgram = proc.getCurrentProgram(); preset.setSelectedId (shownProgram + 1, juce::dontSendNotification); }
    repaint (0, 330, getWidth(), 130);
}

void MakeVoxEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    // 顶栏：品牌 + 一道红线
    g.setColour (silver); g.setFont (juce::FontOptions (30.0f, juce::Font::bold)); g.drawText ("MAKE VOX", 28, 16, 240, 34, juce::Justification::left);
    g.setColour (dim); g.setFont (juce::FontOptions (12.0f)); g.drawText ("VOCAL STRIP  ·  MAKE STUDIO", 30, 50, 300, 16, juce::Justification::left);
    g.setColour (red); g.fillRect (30, 72, 46, 3);
    g.setColour (dim); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("PRESET", 560, 18, 200, 14, juce::Justification::left);
    g.drawText ("ECHO TIME", 800, 18, 120, 14, juce::Justification::left);

    // 大旋钮的标签
    for (size_t i = 0; i < big.size(); ++i)
    {
        const auto b = big[i].s.getBounds();
        g.setColour (silver); g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (big[i].label, b.getX() - 10, b.getY() - 22, b.getWidth() + 20, 16, juce::Justification::centred);
        g.setColour (dim); g.setFont (juce::FontOptions (11.0f));
        g.drawText (big[i].hint, b.getX() - 10, b.getBottom() + 2, b.getWidth() + 20, 14, juce::Justification::centred);
    }

    // 底栏：输入输出 + 电平表
    g.setColour (panel); g.fillRoundedRectangle (20, 330, (float) getWidth() - 40, 112, 8);
    g.setColour (line); g.drawRoundedRectangle (20.5f, 330.5f, (float) getWidth() - 41, 111, 8, 1);
    for (auto* k : { &input, &output })
    {
        const auto b = k->s.getBounds();
        g.setColour (silver); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (k->label, b.getX(), b.getY() - 14, b.getWidth(), 12, juce::Justification::centred);
    }
    auto meter = [&] (int y, const char* name, float norm, bool reduction, const juce::String& txt)
    {
        const int x0 = 220, w = 520;
        g.setColour (dim); g.setFont (juce::FontOptions (11.0f, juce::Font::bold)); g.drawText (name, x0 - 92, y - 1, 86, 14, juce::Justification::right);
        g.setColour (line); g.fillRoundedRectangle ((float) x0, (float) y, (float) w, 12, 3);
        const float v = juce::jlimit (0.0f, 1.0f, norm);
        g.setColour (reduction ? red : silver.withAlpha (0.85f));
        if (reduction) g.fillRoundedRectangle ((float) x0, (float) y, w * v, 12, 3);
        else { g.fillRoundedRectangle ((float) x0, (float) y, w * v, 12, 3); if (v > 0.97f) { g.setColour (red); g.fillRoundedRectangle ((float) (x0 + w - 14), (float) y, 14, 12, 3); } }
        g.setColour (silver); g.setFont (juce::FontOptions (11.0f)); g.drawText (txt, x0 + w + 8, y - 1, 70, 14, juce::Justification::left);
    };
    auto dbNorm = [] (float lin) { return (vox::gainToDb (lin) + 48.0f) / 48.0f; };
    auto dbTxt = [] (float lin) { return lin < 1.0e-4f ? juce::String ("-inf") : juce::String (vox::gainToDb (lin), 1) + " dB"; };
    meter (346, "IN", dbNorm (mIn), false, dbTxt (mIn));
    meter (370, "COMPRESS", mComp / 18.0f, true, "-" + juce::String (mComp, 1) + " dB");
    meter (394, "DE-ESS", mDe / 12.0f, true, "-" + juce::String (mDe, 1) + " dB");
    meter (418, "OUT", dbNorm (mOut), false, dbTxt (mOut));
}

void MakeVoxEditor::resized()
{
    preset.setBounds (560, 34, 220, 30);
    echoTime.setBounds (800, 34, 130, 30);
    const int kw = 104, gap = (getWidth() - 40 - kw * 8) / 7;
    for (size_t i = 0; i < big.size(); ++i) big[i].s.setBounds (20 + (int) i * (kw + gap), 120, kw, 150);
    input.s.setBounds (34, 352, 76, 86);
    output.s.setBounds (getWidth() - 112, 352, 76, 86);
}
