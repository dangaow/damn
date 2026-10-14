#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Hub.h"

// MAKE MIX：一个插件，三个角色。人声轨上选 VOCAL，伴奏轨上选 BEAT，总线（主控）上选 BUS。
// 同一个 FL 工程里的实例通过 Hub 互相看得见：按 MIX 一起听一段 → 解算 → 把结果写进各自的参数。
class MakeMixProcessor : public juce::AudioProcessor, public mm::HubClient, private mm::KeySink, private mm::KeySource
{
public:
    MakeMixProcessor();
    ~MakeMixProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "MAKE MIX"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override { return mm::kNumStyles; }
    int getCurrentProgram() override { return style(); }
    void setCurrentProgram (int i) override { setStyle (i, true); }
    const juce::String getProgramName (int i) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // —— 给界面和测试工具（消息线程）——
    mm::Role role() const noexcept { return (mm::Role) roleA.load(); }
    void setRole (mm::Role);
    mm::StyleId style() const noexcept { return styleA.load(); }
    void setStyle (mm::StyleId, bool broadcast);
    void requestMix() { hubRef->requestMix (style()); }
    void requestUndo() { hubRef->requestUndo(); }
    void loadStyleDefaults();
    mm::Hub& hub() noexcept { return *hubRef; }
    juce::String note() const { const juce::ScopedLock sl (noteLock); return lastNote; }
    bool isBridged() const noexcept { return bridged; }

    // 一键分析（不需要别的实例）：听 10 秒自己的输入，后台算报告
    enum class Analyze { Idle, Listening, Working, Ready };
    void requestAnalyze();                    // 正在听时再按 = 用已有的（至少 3 秒）提前出结果
    void tickAnalyze();                       // 界面定时器调
    Analyze analyzeState() const noexcept { return (Analyze) aState.load(); }
    float analyzeProgress() const noexcept { return (float) aLen.load() / (float) std::max (1.0, 10.0 * fs); }
    std::shared_ptr<const mm::analysis::Report> report() const { const juce::ScopedLock sl (noteLock); return aReport; }

    // 电平表（音频线程写，界面读）
    std::atomic<float> inPeak { 0 }, outPeak { 0 }, gr1 { 0 }, gr2 { 0 }, loud { -100 };
    std::array<std::atomic<float>, mm::kKeyBands> cut {};

    juce::AudioProcessorValueTreeState apvts;
    double fallbackBpm = 120.0;

    // HubClient（消息线程）
    mm::Role hubRole() const override { return role(); }
    mm::ParamSet hubParams() const override;
    void hubApply (const mm::ParamSet&, const std::string& note) override;
    void hubSetStyle (mm::StyleId s) override { styleA.store (std::clamp (s, 0, mm::kNumStyles - 1)); }
    double hubSampleRate() const override { return fs; }
    double hubBpm() const override { return lastBpm.load(); }

private:
    void publish (int64_t hop, const float* db) noexcept override { hubRef->publishKey (slot, hop, db); }
    bool read (int64_t hop, bool exact, float* db) noexcept override { return hubRef->readVocalKey (hop, exact, db); }
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void applyParams (const mm::ParamSet&, mm::StyleId, double bpm);

    juce::SharedResourcePointer<mm::Hub> hubRef;
    int slot = -1; bool bridged = false;
    std::atomic<int> roleA { 0 }, styleA { 0 };
    std::atomic<double> lastBpm { 120.0 };
    std::array<std::atomic<float>*, mm::kNumParams> raw {};
    mm::VocalChain vocal; mm::BeatChain beat; mm::BusChain bus;
    mm::ParamSet applied; mm::StyleId appliedStyle = -1; double appliedBpm = 0; int lastRole = -1;
    double fs = 44100; int maxBlock = 512, L = 1024; bool prepared = false; int64_t freeCounter = 0;
    juce::CriticalSection noteLock; juce::String lastNote;
    std::vector<float> aL, aR; std::atomic<int> aLen { 0 }, aState { 0 }; std::atomic<bool> aArmed { false };
    std::thread aWorker; std::shared_ptr<const mm::analysis::Report> aReport;
    void startAnalyzeWorker();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MakeMixProcessor)
};
