#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace
{
const char* ids[] = { "input", "clean", "presence", "air", "deess", "compress", "warmth", "echo", "echoTime", "space", "output" };
const juce::StringArray echoTimes { "1/8", "1/8 D", "1/4", "1/4 D", "1/2" };
const double echoBeats[] = { 0.5, 0.75, 1.0, 1.5, 2.0 };

std::unique_ptr<juce::AudioParameterFloat> knob (const char* id, const char* name, float def)
{
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f), def,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100)) + "%"; })
                                             .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue() / 100.0f; }));
}
std::unique_ptr<juce::AudioParameterFloat> decibels (const char* id, const char* name, float lo, float hi)
{
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (lo, hi, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout MakeVoxProcessor::createLayout()
{
    const auto& d = vox::presets[0];
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (decibels ("input", "Input", -24, 24));
    l.add (knob ("clean", "Clean", d.clean), knob ("presence", "Presence", d.presence), knob ("air", "Air", d.air), knob ("deess", "De-ess", d.deess),
           knob ("compress", "Compress", d.compress), knob ("warmth", "Warmth", d.warmth), knob ("echo", "Echo", d.echo));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "echoTime", 1 }, "Echo Time", echoTimes, d.echoTime));
    l.add (knob ("space", "Space", d.space));
    l.add (decibels ("output", "Output", -24, 12));
    return l;
}

MakeVoxProcessor::MakeVoxProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MAKEVOX", createLayout())
{
    for (int i = 0; i < 11; ++i) p[i] = apvts.getRawParameterValue (ids[i]);
    cache.fill (-1.0f);
}

bool MakeVoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void MakeVoxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    for (auto* f : { &hpA, &hpB, &mud, &presence, &air }) f->reset();
    comp.prepare (fs, 5.0f, 90.0f);
    deess.prepare ({ fs, (juce::uint32) juce::jmax (1, samplesPerBlock), 2 });
    oversampling.initProcessing ((size_t) juce::jmax (1, samplesPerBlock));
    oversampling.reset();
    sat.prepare (fs * 2);
    echo.prepare (fs);
    space.prepare (fs);
    maxBlock = juce::jmax (1, samplesPerBlock);
    sendL.setSize (1, maxBlock); sendR.setSize (1, maxBlock);
    for (auto* s : { &inGain, &outGain, &echoSend, &spaceSend }) s->reset (fs, 0.03);
    inGain.setCurrentAndTargetValue (vox::dbToGain (p[pInput]->load()));
    outGain.setCurrentAndTargetValue (vox::dbToGain (p[pOutput]->load()));
    cache.fill (-1.0f);
    updateDsp();
    echoSend.setCurrentAndTargetValue (echoSend.getTargetValue());
    spaceSend.setCurrentAndTargetValue (spaceSend.getTargetValue());
    setLatencySamples (juce::roundToInt (oversampling.getLatencyInSamples()));
}

double MakeVoxProcessor::tempo() const
{
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                if (*bpm > 20 && *bpm < 400) return *bpm;
    return fallbackBpm;
}

// 旋钮 → 各模块的实际参数（只在旋钮动了的时候重算）
static bool moved (float a, float b) { return std::abs (a - b) > 1.0e-7f; }
void MakeVoxProcessor::updateDsp()
{
    const float v[9] = { p[pClean]->load(), p[pPresence]->load(), p[pAir]->load(), p[pDeess]->load(), p[pCompress]->load(),
                         p[pWarmth]->load(), p[pEcho]->load(), p[pSpace]->load(), p[pEchoTime]->load() };
    const float clean = v[0], pres = v[1], airAmt = v[2], de = v[3], k = v[4], w = v[5], e = v[6], s = v[7];
    if (moved (clean, cache[0]))
    {
        const double f = 30 + 120 * clean;                                           // 低切：30 → 150 Hz，4 阶巴特沃斯（24 dB/oct）
        hpA.highPass (fs, f, 0.5412); hpB.highPass (fs, f, 1.3066);
        mud.peak (fs, 300, 1.1, -5.0 * clean);                                        // 去浑浊：300 Hz 最多 -5 dB
    }
    if (moved (pres, cache[1])) presence.peak (fs, 3800, 0.9, 6.0 * pres);                  // 存在感：3.8 kHz 最多 +6 dB
    if (moved (airAmt, cache[2])) air.highShelf (fs, juce::jmin (10000.0, fs * 0.42), 0.707, 8.0 * airAmt);   // 空气感：10 kHz 高架最多 +8 dB
    if (moved (de, cache[3]))
    {
        deess.active = de > 0.01f;
        deess.thresholdDb = -16 - 24 * de; deess.ratio = 3 + 5 * de; deess.rangeDb = 3 + 9 * de;
    }
    if (moved (k, cache[4]))
    {
        if (k < 0.01f) { comp.ratio = 1; comp.makeupDb = 0; }
        else
        {
            comp.thresholdDb = -6 - 26 * k; comp.ratio = 1.5f + 4.5f * k; comp.kneeDb = 6;
            comp.makeupDb = -comp.thresholdDb * (1 - 1 / comp.ratio) * 0.45f;         // 自动补偿，压完音量差不多
        }
    }
    if (moved (w, cache[5])) sat.set (1 + 7 * w, 0.08f * w, juce::jmin (1.0f, 2.2f * w));
    if (moved (e, cache[6])) { echo.send = 0.55f * e; echo.feedback = 0.22f + 0.33f * e; }
    echoSend.setTargetValue (e > 0.0005f ? 1.0f : 0.0f);
    spaceSend.setTargetValue (0.5f * s);
    for (int i = 0; i < 9; ++i) cache[(size_t) i] = v[i];
}

void MakeVoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), nCh = juce::jmin (2, buffer.getNumChannels());
    for (int c = nCh; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    if (n == 0 || nCh == 0) return;

    updateDsp();
    inGain.setTargetValue (vox::dbToGain (p[pInput]->load()));
    outGain.setTargetValue (vox::dbToGain (p[pOutput]->load()));
    const int et = juce::jlimit (0, 4, (int) p[pEchoTime]->load());
    echo.setTime (echoBeats[et] * 60.0 / tempo());

    Meters m;
    for (int start = 0; start < n; start += maxBlock)                          // 宿主给的块比 prepareToPlay 说的大时，分段处理
        processChunk (buffer, start, juce::jmin (maxBlock, n - start), nCh, m);
    inPeak.store (m.in); outPeak.store (m.out); compGr.store (-m.gr); deessGr.store (-deess.grDb);
}

void MakeVoxProcessor::processChunk (juce::AudioBuffer<float>& buffer, int start, int n, int nCh, Meters& m)
{
    float* ch[2] = { buffer.getWritePointer (0, start), buffer.getWritePointer (nCh > 1 ? 1 : 0, start) };

    // 1) 串联：输入 → 低切 / 去浑浊 → 压缩 → 去齒音 → 存在感 / 空气感
    for (int i = 0; i < n; ++i)
    {
        const float g = inGain.getNextValue();
        float x[2], level = 0;
        for (int c = 0; c < nCh; ++c)
        {
            float s = ch[c][i] * g;
            m.in = std::max (m.in, std::abs (s));
            s = mud.process (hpB.process (hpA.process (s, c), c), c);
            x[c] = s; level = std::max (level, std::abs (s));
        }
        const float cg = comp.gainFor (level);
        m.gr = std::min (m.gr, comp.grDb);
        for (int c = 0; c < nCh; ++c) x[c] *= cg;
        deess.processFrame (x, nCh);
        for (int c = 0; c < nCh; ++c) ch[c][i] = air.process (presence.process (x[c], c), c);
    }

    // 2) 温暖：2 倍超采样里饱和（不加饱和时也照样走超采样，延迟保持不变）
    {
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) nCh, (size_t) start, (size_t) n);
        auto up = oversampling.processSamplesUp (block);
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            auto* d = up.getChannelPointer (c);
            for (size_t i = 0; i < up.getNumSamples(); ++i) d[i] = sat.process (d[i], (int) c);
        }
        oversampling.processSamplesDown (block);
    }

    // 3) 并联送出：回声（乒乓、躲开人声）和空间（预延迟 + 混响），最后输出增益
    auto* sl = sendL.getWritePointer (0); auto* sr = sendR.getWritePointer (0);
    for (int i = 0; i < n; ++i)
    {
        const float mono = nCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
        float wl = 0, wr = 0;
        echo.process (mono, mono * echoSend.getNextValue(), wl, wr);
        const float pre = space.pre[0].read (space.preDelay);
        space.pre[0].push (mono);
        const float sv = space.lp.process (space.hp.process (pre, 0), 0) * spaceSend.getNextValue();
        sl[i] = sv; sr[i] = sv;
        if (nCh > 1) { ch[0][i] += wl; ch[1][i] += wr; }
        else ch[0][i] += 0.5f * (wl + wr);
    }
    space.verb.processStereo (sl, sr, n);
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        if (nCh > 1) { ch[0][i] = (ch[0][i] + sl[i]) * g; ch[1][i] = (ch[1][i] + sr[i]) * g; }
        else ch[0][i] = (ch[0][i] + 0.5f * (sl[i] + sr[i])) * g;
        for (int c = 0; c < nCh; ++c) m.out = std::max (m.out, std::abs (ch[c][i]));
    }
}

int MakeVoxProcessor::getNumPrograms() { return (int) vox::presets.size(); }
const juce::String MakeVoxProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, (int) vox::presets.size()) ? juce::String (vox::presets[(size_t) index].name) : juce::String();
}
void MakeVoxProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) vox::presets.size())) return;
    currentProgram = index;
    const auto& pr = vox::presets[(size_t) index];
    const float values[] = { pr.input, pr.clean, pr.presence, pr.air, pr.deess, pr.compress, pr.warmth, pr.echo, (float) pr.echoTime, pr.space, pr.output };
    for (int i = 0; i < 11; ++i)
        if (auto* param = apvts.getParameter (ids[i]))
            param->setValueNotifyingHost (param->convertTo0to1 (values[i]));
}

void MakeVoxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}
void MakeVoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = (int) state.getProperty ("program", 0);
            apvts.replaceState (state);
        }
}

juce::AudioProcessorEditor* MakeVoxProcessor::createEditor() { return new MakeVoxEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MakeVoxProcessor(); }
