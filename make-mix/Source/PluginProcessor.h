#pragma once
// MAKE MIX 插件：一个二进制，三个角色（VOCAL / BEAT / BUS），通过进程内的 Hub 互相看见。
// 【WP0 桩】公开 API 是冻结的（第 11 节，界面和 mixrender 依赖它）；实现由 WP2 替换。
// 桩的行为：完整的 APVTS 布局，按角色调用（桩）链 = 恒定延迟 L 的直通，保存 / 读取根属性，通用编辑器。
#include <juce_audio_processors/juce_audio_processors.h>
#include "Shared.h"
#include "Params.h"
#include "Chains.h"
#include "Hub.h"
#include <optional>

// 设计文档写的是类内的 struct Options；嵌套类带默认成员初始化时不能在同一个类里当 "= {}" 默认实参，所以放到类外再 using 进去
struct MakeMixOptions { bool privateHub = false; };          // 测试：模拟桥接（独立 hub）

class MakeMixProcessor : public juce::AudioProcessor, public mm::HubClient
{
public:
    using Options = MakeMixOptions;
    explicit MakeMixProcessor (Options = {});
    ~MakeMixProcessor() override;

    // 角色等（消息线程）
    mm::Role role() const noexcept;       void setRole (mm::Role, bool byUser);
    mm::SubRole subRole() const noexcept; void setSubRole (mm::SubRole);
    int group() const noexcept;           void setGroup (int);
    mm::StyleId style() const noexcept;   void setStyle (mm::StyleId, bool broadcast);
    void requestMix(); void requestStop (bool cancel); void requestUndo(); void requestRemix(); void loadStyleDefaults();
    mm::Hub* hub() const noexcept;        int slot() const noexcept;   const mm::InstanceId& instanceId() const noexcept;
    const mm::LearnedProfile& learned() const noexcept;               // 消息线程
    bool isBridgedHost() const noexcept;  std::string trackName() const;
    // 电平表（音频线程写，界面读）
    struct MeterAtomics { std::atomic<float> inPeak { 0 }, outPeak { 0 }, gr1 { 0 }, gr2 { 0 }, gr3 { 0 },
                          loudM { -100 }, loudS { -100 }, loudI { -100 }, truePeak { -100 }, autoComp { 0 }; };
    MeterAtomics meters;
    juce::AudioProcessorValueTreeState apvts;
    double fallbackBpm = 120.0;                                        // 没有宿主速度时

    // —— AudioProcessor ——
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MAKE MIX"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override;                                     // 程序 = 12 个风格
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    void updateTrackProperties (const TrackProperties&) override;

    // —— HubClient（消息线程）——
    mm::ParamSet hubSnapshotParams() const override;
    mm::LearnedProfile hubSnapshotProfile() const override;
    void hubApply (const mm::ParamSet&, const mm::LearnedProfile&) override;
    void hubSetStyle (mm::StyleId) override;
    std::shared_ptr<mm::CaptureData> hubMakeCapture (int pass, double seconds) override;
    void hubReleaseCapture() override;
    bool hubSidechainConnected() const override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void applyParams (const mm::ParamSet&, bool keepLevelOwning);     // 消息线程，用 gesture 写
    void runChains (juce::AudioBuffer<float>&, bool bypassed);

    // 实例状态（不是宿主参数）
    std::atomic<uint8_t> roleA { 0 }, subA { 0 }, groupA { 0 };
    std::atomic<int> styleA { 0 };
    bool roleUserSet = false;
    std::string name, cloneOf;
    mm::InstanceId id;
    mm::LearnedProfile learnedProfile;
    bool bridged = false;

    // Hub
    std::optional<juce::SharedResourcePointer<mm::Hub>> sharedHub;
    std::unique_ptr<mm::Hub> ownHub;
    mm::Hub* hubPtr = nullptr;
    int slotIndex = -1;

    // 音频线程
    std::array<std::atomic<float>*, mm::kPIdxCount> raw {};
    mm::SeqBox<mm::CompProfile> compBox;
    mm::CompProfile compAudio;
    uint32_t compSeq = 0;
    mm::VocalChain vocal;
    mm::BeatChain beat;
    mm::BusChain bus;
    int activeRole = -1, maxBlock = 512;
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MakeMixProcessor)
};
