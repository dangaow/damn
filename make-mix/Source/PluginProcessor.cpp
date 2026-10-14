// 【WP0 桩】处理器骨架：完整参数布局、角色分派、分段处理、恒定延迟、状态根属性、程序 = 风格。WP2 换成完整实现（第 3、9 节）。
#include "PluginProcessor.h"
#include "AutoMix.h"
#include "Styles.h"

namespace
{
// 宿主里手动输入数值："+2.0 dB"、"3.8 kHz"、"45%"、"OFF"
float parseValue (int pidx, const juce::String& text)
{
    const auto& p = mm::kParams[pidx];
    const auto t = text.trim().toLowerCase();
    if (t.startsWith ("off")) return (pidx == mm::v_lpf || pidx == mm::m_lpf) ? p.max : p.min;
    float v = t.getFloatValue();
    if (juce::String (p.unit) == "%") v *= 0.01f;
    else if (t.contains ("khz") || t.endsWithChar ('k')) v *= 1000.0f;
    return mm::clampParam (pidx, v);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout MakeMixProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < mm::kNumParams; ++i)
    {
        const auto& p = mm::kParams[i];
        const juce::ParameterID pid { p.id, 1 };
        if (p.kind == mm::PKind::Choice)
        {
            juce::StringArray items;
            for (int c = 0; c < p.numChoices; ++c) items.add (p.choices[c]);
            layout.add (std::make_unique<juce::AudioParameterChoice> (pid, p.name, items, (int) p.def,
                                                                      juce::AudioParameterChoiceAttributes().withAutomatable (p.automatable)));
        }
        else
        {
            juce::NormalisableRange<float> range (p.min, p.max, p.step);
            if (p.centre > 0) range.setSkewForCentre (p.centre);
            const auto attr = juce::AudioParameterFloatAttributes()
                                  .withAutomatable (p.automatable)
                                  .withStringFromValueFunction ([i] (float v, int) { return juce::String::fromUTF8 (mm::formatValue (i, v).c_str()); })
                                  .withValueFromStringFunction ([i] (const juce::String& s) { return parseValue (i, s); });
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid, p.name, range, p.def, attr));
        }
    }
    return layout;
}

MakeMixProcessor::MakeMixProcessor (Options options)
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withInput ("Vox Key", juce::AudioChannelSet::stereo(), true)),   // BEAT 的侧链 key（1.5、6.3）
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int i = 0; i < mm::kNumParams; ++i) raw[(size_t) i] = apvts.getRawParameterValue (mm::kParams[i].id);

    if (options.privateHub) { ownHub = std::make_unique<mm::Hub>(); hubPtr = ownHub.get(); }
    else                    { sharedHub.emplace(); hubPtr = &sharedHub->get(); }
    slotIndex = hubPtr->attach (*this, id);
    if (id.isNull()) id = mm::InstanceId::random();
    bridged = juce::File::getSpecialLocation (juce::File::hostApplicationPath).getFileName().containsIgnoreCase ("ilbridge");
}

MakeMixProcessor::~MakeMixProcessor()
{
    if (hubPtr != nullptr && slotIndex >= 0) hubPtr->detach (slotIndex);
}

//==============================================================================
mm::Role MakeMixProcessor::role() const noexcept        { return (mm::Role) roleA.load (std::memory_order_relaxed); }
mm::SubRole MakeMixProcessor::subRole() const noexcept  { return (mm::SubRole) subA.load (std::memory_order_relaxed); }
int MakeMixProcessor::group() const noexcept            { return groupA.load (std::memory_order_relaxed); }
mm::StyleId MakeMixProcessor::style() const noexcept    { return styleA.load (std::memory_order_relaxed); }

void MakeMixProcessor::setRole (mm::Role r, bool byUser)
{
    roleA.store ((uint8_t) std::clamp ((int) r, 0, 2), std::memory_order_relaxed);
    if (byUser) roleUserSet = true;
    if (slotIndex >= 0) hubPtr->setInfo (slotIndex, role(), subRole(), group());
}
void MakeMixProcessor::setSubRole (mm::SubRole s)
{
    subA.store ((uint8_t) std::clamp ((int) s, 0, 2), std::memory_order_relaxed);
    if (slotIndex >= 0) hubPtr->setInfo (slotIndex, role(), subRole(), group());
}
void MakeMixProcessor::setGroup (int g)
{
    groupA.store ((uint8_t) std::clamp (g, 0, mm::kMaxGroups - 1), std::memory_order_relaxed);
    if (slotIndex >= 0) hubPtr->setInfo (slotIndex, role(), subRole(), group());
}
void MakeMixProcessor::setStyle (mm::StyleId s, bool broadcast)
{
    styleA.store (std::clamp (s, 0, mm::kNumStyles - 1), std::memory_order_relaxed);
    if (broadcast && slotIndex >= 0) hubPtr->broadcastStyle (slotIndex, style());
}

void MakeMixProcessor::requestMix()          { if (slotIndex >= 0) hubPtr->beginMix (slotIndex, style()); }
void MakeMixProcessor::requestStop (bool c)  { if (slotIndex >= 0) hubPtr->stop (slotIndex, c); }
void MakeMixProcessor::requestUndo()         { if (slotIndex >= 0) hubPtr->undoMix (slotIndex); }
void MakeMixProcessor::requestRemix()        { if (slotIndex >= 0) hubPtr->remix (slotIndex, style()); }
void MakeMixProcessor::loadStyleDefaults()   { applyParams (mm::automix::styleDefaults (style(), role()), true); }

mm::Hub* MakeMixProcessor::hub() const noexcept                       { return hubPtr; }
int MakeMixProcessor::slot() const noexcept                           { return slotIndex; }
const mm::InstanceId& MakeMixProcessor::instanceId() const noexcept   { return id; }
const mm::LearnedProfile& MakeMixProcessor::learned() const noexcept  { return learnedProfile; }
bool MakeMixProcessor::isBridgedHost() const noexcept                 { return bridged; }
std::string MakeMixProcessor::trackName() const                       { return name; }

// 消息线程：真实值 → 0..1，只写变了的，每个都包在 gesture 里（宿主能撤销、能看到旋钮动）
void MakeMixProcessor::applyParams (const mm::ParamSet& set, bool keepLevelOwning)
{
    for (const auto& [pid, v] : set.values)
    {
        const int i = mm::paramIndex (pid);
        if (i < 0 || (keepLevelOwning && mm::isLevelOwning (i))) continue;
        if (auto* p = apvts.getParameter (pid))
        {
            const float nv = p->convertTo0to1 (mm::clampParam (i, v));
            if (std::abs (nv - p->getValue()) > 1.0e-6f)
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (nv);
                p->endChangeGesture();
            }
        }
    }
}

//==============================================================================
void MakeMixProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = std::max (1, samplesPerBlock);
    const int latency = mm::latencyFor (sampleRate, maxBlock);
    const mm::ChainSpec spec { sampleRate, maxBlock, 2, latency };
    vocal.prepare (spec);
    beat.prepare (spec);
    bus.prepare (spec);
    activeRole = -1;
    prepared = true;
    setLatencySamples (latency);
}

void MakeMixProcessor::reset()
{
    vocal.reset();
    beat.reset();
    bus.reset();
}

bool MakeMixProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out || (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto key = layouts.inputBuses[1];
        if (! key.isDisabled() && key != juce::AudioChannelSet::mono() && key != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void MakeMixProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)         { runChains (buffer, false); }
void MakeMixProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { runChains (buffer, true); }

void MakeMixProcessor::runChains (juce::AudioBuffer<float>& buffer, bool bypassed)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < std::min (getTotalNumOutputChannels(), buffer.getNumChannels()); ++c) buffer.clear (c, 0, n);
    const int numCh = std::min ({ 2, getMainBusNumOutputChannels(), buffer.getNumChannels() });
    if (! prepared || numCh <= 0) return;                                  // 没 prepare 就直通（FL Patcher 会这样调）

    // 播放头：时间线位置和速度
    bool playing = false;
    int64_t timeline = -1;
    double bpm = fallbackBpm;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto t = pos->getTimeInSamples()) { playing = pos->getIsPlaying(); timeline = *t; }
            if (auto b = pos->getBpm()) if (*b > 20.0 && *b < 400.0) bpm = *b;
        }

    const int r = std::clamp ((int) roleA.load (std::memory_order_relaxed), 0, 2);
    if (r != activeRole)
    {
        if (r == 0) vocal.reset(); else if (r == 1) beat.reset(); else bus.reset();
        activeRole = r;
    }
    float values[mm::kPIdxCount];
    for (size_t i = 0; i < raw.size(); ++i) values[i] = raw[i]->load (std::memory_order_relaxed);
    compBox.read (compAudio, compSeq);
    if (r == 0)      vocal.setParams (mm::vocalFrom (values), compAudio);
    else if (r == 1) beat.setParams (mm::beatFrom (values), compAudio);
    else             bus.setParams (mm::busFrom (values), compAudio);
    if (n == 0) return;                                                    // 0 样本的块：参数已经读进链了，直接返回

    // 侧链 key（第二个输入总线）
    int auxCh = 0, auxOffset = 0;
    if (getBusCount (true) > 1)
        if (auto* key = getBus (true, 1); key != nullptr && key->isEnabled())
        {
            auxOffset = getChannelIndexInProcessBlockBuffer (true, 1, 0);
            auxCh = std::min (2, key->getNumberOfChannels());
            if (auxOffset + auxCh > buffer.getNumChannels()) auxCh = 0;
        }

    float inPeak = 0;
    for (int c = 0; c < numCh; ++c) inPeak = std::max (inPeak, buffer.getMagnitude (c, 0, n));

    for (int start = 0; start < n; start += maxBlock)                     // 宿主给的块比 prepareToPlay 说的大时分段处理
    {
        const int len = std::min (maxBlock, n - start);
        float* ch[2] = { buffer.getWritePointer (0, start), numCh > 1 ? buffer.getWritePointer (1, start) : nullptr };
        const float* aux[2] = { nullptr, nullptr };
        for (int c = 0; c < auxCh; ++c) aux[c] = buffer.getReadPointer (auxOffset + c, start);
        const mm::ProcessContext ctx { playing ? timeline + start : -1, bpm };
        if (r == 0)
        {
            if (bypassed) vocal.processBypassed (ch, numCh, len, ctx, nullptr);
            else          vocal.process (ch, numCh, len, ctx, nullptr);
        }
        else if (r == 1)
        {
            if (bypassed) beat.processBypassed (ch, numCh, len);
            else          beat.process (ch, numCh, aux, auxCh, len, ctx, nullptr);
        }
        else
        {
            if (bypassed) bus.processBypassed (ch, numCh, len);
            else          bus.process (ch, numCh, len, ctx);
        }
    }

    float outPeak = 0;
    for (int c = 0; c < numCh; ++c) outPeak = std::max (outPeak, buffer.getMagnitude (c, 0, n));
    meters.inPeak.store (inPeak, std::memory_order_relaxed);
    meters.outPeak.store (outPeak, std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessorEditor* MakeMixProcessor::createEditor() { return new juce::GenericAudioProcessorEditor (*this); }

int MakeMixProcessor::getNumPrograms()     { return mm::kNumStyles; }
int MakeMixProcessor::getCurrentProgram()  { return style(); }
void MakeMixProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, mm::kNumStyles)) return;
    setStyle (index, true);
    loadStyleDefaults();
}
const juce::String MakeMixProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, mm::kNumStyles) ? juce::String::fromUTF8 (mm::kStyles[(size_t) index].name) : juce::String();
}

//==============================================================================
// 9.1：<MAKEMIX stateVersion id cloneOf role subRole group style roleUserSet trackName><PARAMS …/></MAKEMIX>（桩不存 LEARNED）
void MakeMixProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("MAKEMIX");
    root.setProperty ("stateVersion", 1, nullptr);
    root.setProperty ("id", juce::String (id.toString()), nullptr);
    root.setProperty ("cloneOf", juce::String (cloneOf), nullptr);
    root.setProperty ("role", (int) role(), nullptr);
    root.setProperty ("subRole", (int) subRole(), nullptr);
    root.setProperty ("group", group(), nullptr);
    root.setProperty ("style", style(), nullptr);
    root.setProperty ("roleUserSet", roleUserSet ? 1 : 0, nullptr);
    root.setProperty ("trackName", juce::String::fromUTF8 (name.c_str()), nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    if (auto xml = root.createXml()) copyXmlToBinary (*xml, destData);
}

void MakeMixProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("MAKEMIX")) return;           // 不认识的（包括 v0）忽略
    const auto root = juce::ValueTree::fromXml (*xml);

    const auto saved = mm::InstanceId::fromString (root.getProperty ("id").toString().toStdString());
    cloneOf = root.getProperty ("cloneOf").toString().toStdString();
    if (! saved.isNull() && hubPtr->restoreId (slotIndex, id, saved) == mm::Hub::Restore::Clone)
        cloneOf = saved.toString();

    roleA.store ((uint8_t) std::clamp ((int) root.getProperty ("role", 0), 0, 2), std::memory_order_relaxed);
    subA.store ((uint8_t) std::clamp ((int) root.getProperty ("subRole", 0), 0, 2), std::memory_order_relaxed);
    groupA.store ((uint8_t) std::clamp ((int) root.getProperty ("group", 0), 0, mm::kMaxGroups - 1), std::memory_order_relaxed);
    styleA.store (std::clamp ((int) root.getProperty ("style", 0), 0, mm::kNumStyles - 1), std::memory_order_relaxed);
    roleUserSet = (int) root.getProperty ("roleUserSet", 0) != 0;
    if (name.empty()) name = root.getProperty ("trackName").toString().toStdString();
    if (slotIndex >= 0) hubPtr->setInfo (slotIndex, role(), subRole(), group());

    const auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid()) apvts.replaceState (params);
}

void MakeMixProcessor::updateTrackProperties (const TrackProperties& props)
{
    if (props.name.has_value()) name = props.name->toStdString();
    if (slotIndex >= 0) hubPtr->setName (slotIndex, name);
}

//==============================================================================
mm::ParamSet MakeMixProcessor::hubSnapshotParams() const
{
    mm::ParamSet s;
    s.values.reserve ((size_t) mm::kNumParams);
    for (int i = 0; i < mm::kNumParams; ++i) s.values.emplace_back (mm::kParams[i].id, raw[(size_t) i]->load (std::memory_order_relaxed));
    return s;
}
mm::LearnedProfile MakeMixProcessor::hubSnapshotProfile() const { return learnedProfile; }
void MakeMixProcessor::hubApply (const mm::ParamSet& set, const mm::LearnedProfile& profile)
{
    applyParams (set, false);
    learnedProfile = profile;
    compBox.write (profile.comp);
}
void MakeMixProcessor::hubSetStyle (mm::StyleId s)                          { setStyle (s, false); }
std::shared_ptr<mm::CaptureData> MakeMixProcessor::hubMakeCapture (int, double) { return nullptr; }   // 桩：不采样
void MakeMixProcessor::hubReleaseCapture()                                 {}
bool MakeMixProcessor::hubSidechainConnected() const                       { return false; }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MakeMixProcessor(); }
