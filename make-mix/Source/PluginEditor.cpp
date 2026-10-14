#include "PluginEditor.h"

using namespace mm;
namespace
{
const juce::Colour bg (0xff0a0b10), panel (0xff12141b), line (0xff23262f), red (0xffe0112b), silver (0xffc9ced6), dim (0xff6b7080);
// 旋钮下面的短名字（和 kParams 顺序一致）
const char* kShort[kNumParams] = { "INPUT", "LOW CUT", "MUD", "PRESENCE", "AIR", "COMPRESS", "DE-ESS", "WARMTH", "LEVEL",
                                   "LOW CUT", "POCKET", "UNMASK", "LEVEL",
                                   "LOW", "HIGH", "GLUE", "WIDTH", "DRIVE", "CEILING" };
const char* kRoleName[3] = { "VOCAL", "BEAT", "BUS" };
const char* kRoleHint[3] = { "on the vocal track (last slot)", "on the instrumental track", "on the master (last slot)" };
}

// ====================================================================== 外观
MixLook::MixLook()
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
    setColour (juce::TextButton::textColourOffId, silver);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, red.withAlpha (0.4f));
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::outlineWhenEditingColourId, red);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
}

juce::Font MixLook::ui (float size, bool bold)
{
   #if JUCE_WINDOWS
    return juce::FontOptions ("Microsoft YaHei UI", size, bold ? juce::Font::bold : juce::Font::plain);   // Windows 默认字体不带中文
   #else
    return juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain);
   #endif
}
juce::Font MixLook::getComboBoxFont (juce::ComboBox&) { return ui (14.0f, true); }
juce::Font MixLook::getPopupMenuFont() { return ui (15.0f); }
juce::Font MixLook::getTextButtonFont (juce::TextButton& b, int h) { return ui (std::min (16.0f, h * 0.42f), b.getProperties()["big"] ? true : true); }

void MixLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6);
    const float rad = juce::jmin (r.getWidth(), r.getHeight()) / 2, cx = r.getCentreX(), cy = r.getCentreY(), ang = a0 + pos * (a1 - a0);
    const float tw = juce::jmax (3.0f, rad * 0.09f);
    juce::Path track; track.addCentredArc (cx, cy, rad - tw, rad - tw, 0, a0, a1, true);
    g.setColour (line); g.strokePath (track, juce::PathStrokeType (tw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path val; val.addCentredArc (cx, cy, rad - tw, rad - tw, 0, a0, ang, true);
    g.setColour (red); g.strokePath (val, juce::PathStrokeType (tw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float kr = rad - tw * 2.6f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2d36), cx, cy - kr, juce::Colour (0xff111318), cx, cy + kr, false));
    g.fillEllipse (cx - kr, cy - kr, kr * 2, kr * 2);
    g.setColour (juce::Colour (0xff3a3e49)); g.drawEllipse (cx - kr, cy - kr, kr * 2, kr * 2, 1.0f);
    juce::Path ptr; ptr.addRoundedRectangle (-1.5f, -kr + 4, 3.0f, kr * 0.42f, 1.5f);
    g.setColour (silver); g.fillPath (ptr, juce::AffineTransform::rotation (ang).translated (cx, cy));
}

void MixLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId)); g.fillRoundedRectangle (0, 0, (float) w, (float) h, 4);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId)); g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, (float) h - 1, 4, 1);
    juce::Path tri; const float ax = (float) w - 16, ay = (float) h * 0.5f;
    tri.addTriangle (ax - 5, ay - 3, ax + 5, ay - 3, ax, ay + 4);
    g.setColour (red); g.fillPath (tri);
}

void MixLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool big = b.getProperties()["big"], on = b.getToggleState();
    juce::Colour fill = big ? red : (on ? red : panel);
    if (! b.isEnabled()) fill = panel.darker (0.3f);
    if (over) fill = fill.brighter (0.12f);
    if (down) fill = fill.darker (0.15f);
    g.setColour (fill); g.fillRoundedRectangle (r, 5);
    g.setColour (on || big ? fill.brighter (0.2f) : line); g.drawRoundedRectangle (r, 5, 1);
}

// ====================================================================== 编辑器
MakeMixEditor::MakeMixEditor (MakeMixProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);
    for (int i = 0; i < kNumParams; ++i)
    {
        auto& k = knobs[(size_t) i]; k.param = i;
        k.s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 18);
        k.s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        k.s.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        k.s.setDoubleClickReturnValue (true, kParams[i].def);
        addChildComponent (k.s);
        k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, kParams[i].id, k.s);
    }

    for (int r = 0; r < 3; ++r)
    {
        roleBtn[r].setButtonText (kRoleName[r]); roleBtn[r].setClickingTogglesState (false);
        roleBtn[r].onClick = [this, r] { proc.setRole ((Role) r); showRole(); };
        addAndMakeVisible (roleBtn[r]);
    }
    for (int s = 0; s < kNumStyles; ++s) styleBox.addItem (juce::String::fromUTF8 (mm::style (s).name), s + 1);
    styleBox.onChange = [this] { if (styleBox.getSelectedId() > 0) proc.setStyle (styleBox.getSelectedId() - 1, true); };
    addAndMakeVisible (styleBox);

    mixBtn.getProperties().set ("big", true); mixBtn.setButtonText ("MIX"); mixBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    mixBtn.onClick = [this] { proc.requestMix(); };
    undoBtn.setButtonText ("UNDO MIX"); undoBtn.onClick = [this] { proc.requestUndo(); };
    defaultsBtn.setButtonText ("STYLE DEFAULTS"); defaultsBtn.onClick = [this] { proc.loadStyleDefaults(); };
    for (auto* b : { &mixBtn, &undoBtn, &defaultsBtn }) addAndMakeVisible (*b);

    styleBox.setSelectedId (proc.style() + 1, juce::dontSendNotification); shownStyle = proc.style();
    setSize (960, 540);
    showRole();
    startTimerHz (30);
}

MakeMixEditor::~MakeMixEditor() { setLookAndFeel (nullptr); }

void MakeMixEditor::showRole()
{
    shownRole = (int) proc.role();
    for (int r = 0; r < 3; ++r) roleBtn[r].setToggleState (r == shownRole, juce::dontSendNotification);
    for (int i = 0; i < kNumParams; ++i) knobs[(size_t) i].s.setVisible ((int) kParams[i].role == shownRole);
    resized(); repaint();
}

void MakeMixEditor::timerCallback()
{
    if ((int) proc.role() != shownRole) showRole();
    if (proc.style() != shownStyle) { shownStyle = proc.style(); styleBox.setSelectedId (shownStyle + 1, juce::dontSendNotification); }
    auto fall = [] (float& shown, float v, float k) { shown = v > shown ? v : shown * k + v * (1 - k); };
    fall (mIn, proc.inPeak.load(), 0.86f); fall (mOut, proc.outPeak.load(), 0.86f);
    fall (mG1, proc.gr1.load(), 0.8f); fall (mG2, proc.gr2.load(), 0.8f);
    for (int k = 0; k < kKeyBands; ++k) fall (mCut[(size_t) k], proc.cut[(size_t) k].load(), 0.8f);
    const auto st = juce::String::fromUTF8 (proc.hub().statusText().c_str());
    if (st != status) { status = st; mixBtn.setButtonText (st); }
    undoBtn.setEnabled (proc.hub().canUndo());
    link = proc.isBridged() ? juce::String ("LINK OFF - BRIDGED: Wrapper > Processing > untick 'Make bridged'") : juce::String::fromUTF8 (proc.hub().linkSummary().c_str());
    noteText = proc.note();
    repaint();
}

void MakeMixEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (silver); g.setFont (MixLook::ui (30.0f, true)); g.drawText ("MAKE MIX", 28, 16, 240, 34, juce::Justification::left);
    g.setColour (dim); g.setFont (MixLook::ui (12.0f)); g.drawText (juce::String (juce::CharPointer_UTF8 ("VOCAL \xc2\xb7 BEAT \xc2\xb7 BUS  \xc2\xb7  MAKE STUDIO")), 30, 50, 300, 16, juce::Justification::left);
    g.setColour (red); g.fillRect (30, 72, 46, 3);
    g.setColour (dim); g.setFont (MixLook::ui (11.0f, true));
    g.drawText ("ROLE", 330, 6, 200, 14, juce::Justification::left);
    g.drawText ("STYLE", 640, 6, 200, 14, juce::Justification::left);
    g.setFont (MixLook::ui (12.0f)); g.drawText (juce::String ("put this one ") + kRoleHint[std::max (0, shownRole)], 330, 60, 300, 16, juce::Justification::left);
    g.setColour (link.startsWith ("LINK OFF") ? red : silver.withAlpha (0.8f)); g.setFont (MixLook::ui (12.0f, true));
    g.drawText (link, 640, 60, 300, 16, juce::Justification::left);

    // 旋钮的名字
    g.setFont (MixLook::ui (12.0f, true));
    for (int i = 0; i < kNumParams; ++i)
    {
        const bool vis = knobs[(size_t) i].s.isVisible();
        if (! vis) continue;
        const auto b = knobs[(size_t) i].s.getBounds();
        g.setColour (silver); g.drawText (kShort[i], b.getX() - 10, b.getY() - 18, b.getWidth() + 20, 14, juce::Justification::centred);
    }

    // 底栏：说明 + 电平表
    g.setColour (panel); g.fillRoundedRectangle (20, 380, (float) getWidth() - 40, 144, 8);
    g.setColour (line); g.drawRoundedRectangle (20.5f, 380.5f, (float) getWidth() - 41, 143, 8, 1);
    g.setColour (dim); g.setFont (MixLook::ui (11.0f, true)); g.drawText ("WHAT MIX DID", 260, 390, 200, 14, juce::Justification::left);
    g.setColour (silver); g.setFont (MixLook::ui (12.0f));
    g.drawFittedText (noteText.isEmpty() ? juce::String ("Play the song, then press MIX on any instance.\nKeep vocal and beat faders at 0 dB; use LEVEL here.") : noteText,
                      260, 406, 380, 112, juce::Justification::topLeft, 8);

    auto meter = [&] (int y, const char* name, float norm, bool reduction, const juce::String& txt)
    {
        const int x0 = 712, w = 150;
        g.setColour (dim); g.setFont (MixLook::ui (10.0f, true)); g.drawText (name, x0 - 62, y - 1, 58, 12, juce::Justification::right);
        g.setColour (line); g.fillRoundedRectangle ((float) x0, (float) y, (float) w, 10, 3);
        const float v = juce::jlimit (0.0f, 1.0f, norm);
        g.setColour (reduction ? red : silver.withAlpha (0.85f)); g.fillRoundedRectangle ((float) x0, (float) y, w * v, 10, 3);
        g.setColour (silver); g.setFont (MixLook::ui (10.0f)); g.drawText (txt, x0 + w + 6, y - 1, 60, 12, juce::Justification::left);
    };
    auto dbNorm = [] (float lin) { return (gainToDb (lin) + 48.0f) / 48.0f; };
    auto dbTxt = [] (float lin) { return lin < 1.0e-4f ? juce::String ("-inf") : juce::String (gainToDb (lin), 1); };
    meter (394, "IN", dbNorm (mIn), false, dbTxt (mIn));
    if (shownRole == 0) { meter (414, "COMP", mG1 / 18, true, "-" + juce::String (mG1, 1)); meter (434, "DE-ESS", mG2 / 12, true, "-" + juce::String (mG2, 1)); }
    else if (shownRole == 2) { meter (414, "GLUE", mG1 / 8, true, "-" + juce::String (mG1, 1)); meter (434, "LIMIT", mG2 / 8, true, "-" + juce::String (mG2, 1));
                               const float lu = proc.loud.load(); meter (454, "LUFS-S", (lu + 30) / 30, false, juce::String (lu, 1)); }
    else for (int k = 0; k < kKeyBands; ++k)
    {
        const char* hz[] = { "280", "560", "1.1k", "2.2k", "3.6k", "6k" };
        meter (412 + k * 15, hz[k], mCut[(size_t) k] / 12, true, "-" + juce::String (mCut[(size_t) k], 1));
    }
    meter (506, "OUT", dbNorm (mOut), false, dbTxt (mOut));
}

void MakeMixEditor::resized()
{
    for (int r = 0; r < 3; ++r) roleBtn[r].setBounds (330 + r * 96, 22, 90, 34);
    styleBox.setBounds (640, 22, 290, 34);
    // 当前角色的旋钮：最多 6 个一排
    std::vector<juce::Component*> vis; std::vector<int> ids;
    for (int i = 0; i < kNumParams; ++i)
        if ((int) kParams[i].role == shownRole) { vis.push_back (&knobs[(size_t) i].s); ids.push_back (i); }
    const int cols = 6, kw = 110, kh = 110, gap = (getWidth() - 40 - cols * kw) / (cols - 1);
    for (size_t j = 0; j < vis.size(); ++j)
    {
        const int col = (int) j % cols, row = (int) j / cols, x = 20 + col * (kw + gap), y = vis.size() <= (size_t) cols ? 170 : 104 + row * 146;
        vis[j]->setBounds (x, y, kw, kh);
    }
    mixBtn.setBounds (36, 396, 200, 74);
    undoBtn.setBounds (36, 478, 96, 32);
    defaultsBtn.setBounds (140, 478, 96, 32);
}
