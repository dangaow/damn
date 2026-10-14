#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace mm;

juce::AudioProcessorValueTreeState::ParameterLayout MakeMixProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& d : kParams)
    {
        const juce::ParameterID id { d.id, 1 };
        juce::NormalisableRange<float> r (d.min, d.max, d.kind == PKind::Percent ? 1.0f : 0.0f);
        if (d.skew > 0) r.setSkewForCentre (d.skew);
        auto attr = juce::AudioParameterFloatAttributes();
        switch (d.kind)
        {
            case PKind::Db:      attr = attr.withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; }); break;
            case PKind::Hz:      attr = attr.withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " Hz"; }); break;
            case PKind::Percent: attr = attr.withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; }); break;
            case PKind::Amount: attr = attr.withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100)) + "%"; }); break;
        }
        layout.add (std::make_unique<juce::AudioParameterFloat> (id, d.name, r, d.def, attr));
    }
    return layout;
}

MakeMixProcessor::MakeMixProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MAKEMIX", createLayout())
{
    for (int i = 0; i < kNumParams; ++i) raw[(size_t) i] = apvts.getRawParameterValue (kParams[i].id);
    // FL 的「桥接」模式把插件放进单独的 ilbridge 进程，那样就看不到别的实例
    bridged = juce::File::getSpecialLocation (juce::File::hostApplicationPath).getFileName().containsIgnoreCase ("ilbridge");
    slot = hubRef->attach (this);
}

MakeMixProcessor::~MakeMixProcessor() { hubRef->detach (slot); }

const juce::String MakeMixProcessor::getProgramName (int i) { return juce::String::fromUTF8 (mm::style (i).name); }

bool MakeMixProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void MakeMixProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate; maxBlock = std::max (32, samplesPerBlock); L = latencyFor (fs, maxBlock);
    const auto set = hubParams();
    applyParams (set, style(), lastBpm.load());
    vocal.prepare (fs, maxBlock, L); beat.prepare (fs, maxBlock, L); bus.prepare (fs, maxBlock, L);
    setLatencySamples (L);                                       // 三个角色一样，参数怎么改都不变
    freeCounter = 0; lastRole = -1; prepared = true;
}

void MakeMixProcessor::applyParams (const ParamSet& s, StyleId st, double bpm)
{
    vocal.setParams (VocalChain::fromSet (s), st, bpm);
    beat.setParams (BeatChain::fromSet (s), st);
    bus.setParams (BusChain::fromSet (s), st);
    applied = s; appliedStyle = st; appliedBpm = bpm;
}

void MakeMixProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), nCh = std::min (2, buffer.getNumChannels());
    for (int c = nCh; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    if (! prepared || n == 0 || nCh == 0) return;

    // 宿主时间：播放时用 timeInSamples（各条轨对得上），停着的时候用自己的计数
    bool playing = false; int64_t pos = freeCounter; double bpm = fallbackBpm;
    if (auto* ph = getPlayHead())
        if (auto info = ph->getPosition())
        {
            if (auto b = info->getBpm()) bpm = *b;
            playing = info->getIsPlaying();
            if (playing) if (auto t = info->getTimeInSamples()) pos = *t;
        }
    lastBpm.store (bpm);

    ParamSet now;
    for (int i = 0; i < kNumParams; ++i) now[i] = raw[(size_t) i]->load (std::memory_order_relaxed);
    const StyleId st = style();
    if (now.v != applied.v || st != appliedStyle || std::abs (bpm - appliedBpm) > 0.01) applyParams (now, st, bpm);

    const int r = roleA.load();
    if (r != lastRole) { hubRef->setRole (slot, (Role) r); if (r == 0) vocal.reset(); else if (r == 1) beat.reset(); else bus.reset(); lastRole = r; }

    float pin = 0; for (int c = 0; c < nCh; ++c) pin = std::max (pin, buffer.getMagnitude (c, 0, n));
    for (int start = 0; start < n; start += maxBlock)
    {
        const int m = std::min (maxBlock, n - start);
        float* ch[2] = { buffer.getWritePointer (0, start), buffer.getWritePointer (nCh > 1 ? 1 : 0, start) };
        if (r != (int) Role::Bus) hubRef->capture (slot, ch[0], ch[1], m, pos + start, playing);
        if (r == (int) Role::Vocal)     vocal.process (ch, nCh, m, pos + start, this);
        else if (r == (int) Role::Beat) beat.process (ch, nCh, m, pos + start, playing, this);
        else                            bus.process (ch, nCh, m);
    }
    freeCounter += n;

    float pout = 0; for (int c = 0; c < nCh; ++c) pout = std::max (pout, buffer.getMagnitude (c, 0, n));
    inPeak.store (pin); outPeak.store (pout);
    if (r == 0) { gr1.store (vocal.grComp); gr2.store (vocal.grDeess); }
    else if (r == 1) { for (int k = 0; k < kKeyBands; ++k) cut[(size_t) k].store (beat.cutDb[k]); }
    else { gr1.store (bus.grGlue); gr2.store (bus.grLimit); loud.store (bus.loudShort); }
}

// ---------------------------------------------------------------------------------------- 角色 / 风格 / 参数（消息线程）
void MakeMixProcessor::setRole (Role r)
{
    roleA.store ((int) r);
    hubRef->setRole (slot, r);
}

void MakeMixProcessor::setStyle (StyleId s, bool broadcast)
{
    s = std::clamp (s, 0, kNumStyles - 1);
    if (broadcast) hubRef->broadcastStyle (s); else styleA.store (s);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

ParamSet MakeMixProcessor::hubParams() const
{
    ParamSet s;
    for (int i = 0; i < kNumParams; ++i) s[i] = raw[(size_t) i]->load();
    return s;
}

void MakeMixProcessor::hubApply (const ParamSet& s, const std::string& n)
{
    for (int i = 0; i < kNumParams; ++i)
    {
        auto* p = apvts.getParameter (kParams[i].id);
        const float v = clampParam (i, s[i]);
        if (p == nullptr || std::abs (v - raw[(size_t) i]->load()) < 1e-4f) continue;   // 没变的不写，免得撤销记录太多
        p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture();
    }
    const juce::ScopedLock sl (noteLock); lastNote = juce::String::fromUTF8 (n.c_str());
}

void MakeMixProcessor::loadStyleDefaults()
{
    const auto& st = mm::style (style());
    auto s = hubParams();
    switch (role())
    {
        case Role::Vocal:
            s[V_HPF] = 0.5f * (st.vHpfMin + st.vHpfMax); s[V_MUD] = st.vMudG; s[V_PRES] = st.vPresG; s[V_AIR] = st.vAirG;
            s[V_COMP] = 0.4f; s[V_DEESS] = 0.4f; s[V_WARMTH] = st.vWarmth;
            break;
        case Role::Beat:
            s[B_HPF] = st.bHpf; s[B_POCKET] = st.bPocketStatic; s[B_UNMASK] = st.bDmax; break;
        case Role::Bus:
            s[M_LOW] = 0; s[M_HIGH] = 0; s[M_GLUE] = 0.35f; s[M_WIDTH] = 100; s[M_CEILING] = st.ceiling; break;
    }
    hubApply (s, std::string ("STYLE DEFAULTS: ") + st.name + "\n");
}

// ---------------------------------------------------------------------------------------- 状态
void MakeMixProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    xml->setAttribute ("role", roleA.load()); xml->setAttribute ("style", styleA.load()); xml->setAttribute ("version", 1);
    copyXmlToBinary (*xml, dest);
}

void MakeMixProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    roleA.store (std::clamp (xml->getIntAttribute ("role", 0), 0, 2));
    styleA.store (std::clamp (xml->getIntAttribute ("style", 0), 0, kNumStyles - 1));
    hubRef->setRole (slot, role());
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* MakeMixProcessor::createEditor() { return new MakeMixEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MakeMixProcessor(); }
