#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Dsp.h"

// MAKE VOX：一键人声链。CLEAN → COMPRESS → DE-ESS → PRESENCE / AIR → WARMTH，再并联 ECHO 和 SPACE 的送出。
class MakeVoxProcessor : public juce::AudioProcessor
{
public:
    MakeVoxProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MAKE VOX"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // 给界面的电平表（线性峰值 / dB 压缩量），音频线程写，界面线程读
    std::atomic<float> inPeak { 0 }, outPeak { 0 }, compGr { 0 }, deessGr { 0 };

    // 测试工具用：没有宿主时假定的速度
    double fallbackBpm = 120.0;

private:
    struct Meters { float in = 0, out = 0, gr = 0; };
    void updateDsp();
    void processChunk (juce::AudioBuffer<float>&, int start, int n, int nCh, Meters&);
    double tempo() const;

    double fs = 44100;
    int maxBlock = 512;
    int currentProgram = 0;
    vox::Biquad hpA, hpB, mud, presence, air;
    vox::Compressor comp;
    vox::DeEsser deess;
    vox::Saturator sat;
    juce::dsp::Oversampling<float> oversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    vox::Echo echo;
    vox::Space space;
    juce::AudioBuffer<float> sendL, sendR;             // 混响的送出（整块一起处理）
    juce::SmoothedValue<float> inGain, outGain, echoSend, spaceSend;
    std::array<float, 9> cache {};                      // 上一块的参数值，变了才重算滤波器系数

    std::atomic<float>* p[11] {};
    enum { pInput, pClean, pPresence, pAir, pDeess, pCompress, pWarmth, pEcho, pEchoTime, pSpace, pOutput };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MakeVoxProcessor)
};
