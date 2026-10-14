// 离线渲染（单实例）：把一个音频文件过一遍 MAKE MIX，写成 24 bit WAV，并打印 CPU、响度和峰值。
//   mm_render in.wav out.wav [--role vocal|beat|bus] [--sub lead|double|adlib] [--style trap] [--bpm 94] [--block 512] [--mix] [id=value ...]
//   例：mm_render dry.wav wet.wav --role vocal --style trap --bpm 94 v_comp=0.8 v_echo=0
// 【WP0 最小移植】v0 的 vox_render 换成新 API；WP1 负责完善（--mix 需要 Hub + Solver）。
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/PluginProcessor.h"
#include "../Source/Analysis.h"
#include "../Source/Styles.h"
#include <chrono>
#include <iostream>

namespace
{
// 假的宿主播放头：一直在播放，时间线 = 已处理的样本数
struct RenderPlayHead final : juce::AudioPlayHead
{
    int64_t pos = 0; double bpm = 120, fs = 48000; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setTimeInSamples (pos);
        p.setTimeInSeconds ((double) pos / fs);
        p.setIsPlaying (playing);
        p.setBpm (bpm);
        return p;
    }
};

int usage()
{
    std::cerr << "usage: mm_render in.wav out.wav [--role vocal|beat|bus] [--sub lead|double|adlib] [--style trap] [--bpm 94]\n"
                 "                 [--block 512] [--mix] [id=value ...]\n";
    return 1;
}

double dbfs (float g) { return 20.0 * std::log10 (std::max (1.0e-9, (double) g)); }
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3) return usage();

    mm::Role role = mm::Role::Vocal;
    mm::SubRole sub = mm::SubRole::Lead;
    mm::StyleId styleId = 0;
    double bpm = 120.0;
    int block = 512;
    bool doMix = false;
    std::vector<std::pair<juce::String, float>> overrides;
    for (int a = 3; a < argc; ++a)
    {
        const juce::String arg (argv[a]);
        auto next = [&] { return a + 1 < argc ? juce::String (argv[++a]) : juce::String(); };
        if (arg == "--role")
        {
            const auto v = next().toLowerCase();
            role = v == "beat" ? mm::Role::Beat : v == "bus" ? mm::Role::Bus : mm::Role::Vocal;
        }
        else if (arg == "--sub")
        {
            const auto v = next().toLowerCase();
            sub = v == "double" ? mm::SubRole::Double : v == "adlib" ? mm::SubRole::Adlib : mm::SubRole::Lead;
        }
        else if (arg == "--style")
        {
            const auto v = next();
            styleId = mm::styleIndex (v.toStdString());
            if (styleId < 0) { std::cerr << "unknown style " << v << "\n"; return 1; }
        }
        else if (arg == "--bpm")   bpm = next().getDoubleValue();
        else if (arg == "--block") block = std::max (1, next().getIntValue());
        else if (arg == "--mix")   doMix = true;
        else if (arg.contains ("=")) overrides.emplace_back (arg.upToFirstOccurrenceOf ("=", false, false), arg.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
        else return usage();
    }

    const juce::File in = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (in));
    if (reader == nullptr) { std::cerr << "cannot read " << argv[1] << "\n"; return 1; }
    const double fs = reader->sampleRate;
    const int64_t len = reader->lengthInSamples;
    juce::AudioBuffer<float> src (2, (int) len);
    reader->read (&src, 0, (int) len, 0, true, true);
    if (reader->numChannels == 1) src.copyFrom (1, 0, src, 0, 0, (int) len);

    MakeMixProcessor proc;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.inputBuses.add (juce::AudioChannelSet::disabled());          // 这里不接侧链
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    if (! proc.setBusesLayout (layout)) { std::cerr << "bus layout refused\n"; return 1; }
    proc.setRateAndBufferSizeDetails (fs, block);
    proc.fallbackBpm = bpm;
    proc.setRole (role, true);
    proc.setSubRole (sub);
    proc.setStyle (styleId, false);
    proc.loadStyleDefaults();
    for (const auto& [pid, v] : overrides)
    {
        auto* p = proc.apvts.getParameter (pid);
        if (p == nullptr) { std::cerr << "unknown parameter " << pid << "\n"; return 1; }
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    }
    RenderPlayHead head;
    head.bpm = bpm;
    head.fs = fs;
    proc.setPlayHead (&head);
    proc.prepareToPlay (fs, block);
    const int latency = proc.getLatencySamples();

    juce::AudioBuffer<float> buf (proc.getTotalNumInputChannels() > 2 ? proc.getTotalNumInputChannels() : 2, block);
    juce::MidiBuffer midi;
    // 处理时间线 [from, to)：输入超出文件长度的部分是静音；dest 非空时按时间线位置存输出
    auto run = [&] (int64_t from, int64_t to, juce::AudioBuffer<float>* dest, double& cpu, float& pkOut) -> bool
    {
        for (int64_t pos = from; pos < to; pos += block)
        {
            const int n = (int) std::min<int64_t> (block, to - pos);
            buf.setSize (buf.getNumChannels(), n, false, false, true);
            buf.clear();
            if (pos < len)
                for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, src, c, (int) pos, (int) std::min<int64_t> (n, len - pos));
            head.pos = pos;
            const auto t0 = std::chrono::steady_clock::now();
            proc.processBlock (buf, midi);
            cpu += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            for (int c = 0; c < 2; ++c)
            {
                pkOut = std::max (pkOut, buf.getMagnitude (c, 0, n));
                for (int i = 0; i < n; ++i)
                    if (! std::isfinite (buf.getSample (c, i))) { std::cerr << "NaN/Inf at " << pos + i << "\n"; return false; }
                if (dest != nullptr) dest->copyFrom (c, (int) pos, buf, c, 0, n);
            }
        }
        return true;
    };

    if (doMix)
    {
        // 单实例 MIX：边播边让 hub 走学习会话（每 100 ms 音频 tick 一次），最多把文件循环 3 遍，结束后再从头渲染
        auto* hub = proc.hub();
        hub->setSynchronousSolve (true);
        proc.requestMix();
        auto finished = [&]
        {
            const auto p = hub->session (proc.slot()).phase;
            return p == mm::SessionView::Phase::Idle || p == mm::SessionView::Phase::Done
                || p == mm::SessionView::Phase::Failed || p == mm::SessionView::Phase::Cancelled;
        };
        if (finished())
            std::cout << "MIX: no session started (hub / solver not available in this build)\n";
        else
        {
            double learnCpu = 0; float learnPk = 0;
            const int64_t hop = std::max<int64_t> (block, (int64_t) (fs * 0.1));
            for (int loop = 0; loop < 3 && ! finished(); ++loop)
                for (int64_t pos = 0; pos < len && ! finished(); pos += hop)
                {
                    if (! run (pos, std::min (len, pos + hop), nullptr, learnCpu, learnPk)) return 2;
                    hub->tick();
                }
            const auto view = hub->session (proc.slot());
            std::cout << "MIX: " << view.status << "\n";
            for (const auto& line : view.summary) std::cout << "  " << line << "\n";
        }
        proc.reset();
    }

    const int64_t total = len + (int64_t) (fs * 3.0);                  // 多渲 3 秒，留住回声和混响的尾巴
    juce::AudioBuffer<float> outAll (2, (int) total);
    double cpu = 0; float pkOut = 0;
    if (! run (0, total, &outAll, cpu, pkOut)) return 2;

    // 去掉插件延迟，让输出和输入对齐
    const int keep = (int) (total - latency);
    juce::AudioBuffer<float> aligned (2, keep);
    for (int c = 0; c < 2; ++c) aligned.copyFrom (c, 0, outAll, c, latency, keep);
    out.deleteFile();
    std::unique_ptr<juce::OutputStream> os (out.createOutputStream());
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions {}.withSampleRate (fs).withNumChannels (2).withBitsPerSample (24));
    if (w == nullptr) { std::cerr << "cannot write " << argv[2] << "\n"; return 1; }
    w->writeFromAudioSampleBuffer (aligned, 0, keep);

    const float* inPtr[2] = { src.getReadPointer (0), src.getReadPointer (1) };
    const float* outPtr[2] = { aligned.getReadPointer (0), aligned.getReadPointer (1) };
    const double secs = (double) total / fs;
    const char* roleName[] = { "VOCAL", "BEAT", "BUS" };
    std::cout << "role " << roleName[(int) role] << "  style " << juce::String::fromUTF8 (mm::style (styleId).name)
              << "  latency " << latency << " samples\n"
              << "CPU " << juce::String (cpu / secs * 100.0, 2) << "% of one core (" << juce::String (cpu, 3) << " s for "
              << juce::String (secs, 1) << " s)\n"
              << "LUFS in " << juce::String (mm::analysis::integratedLufs (inPtr, 2, len, fs), 2)
              << ", out " << juce::String (mm::analysis::integratedLufs (outPtr, 2, keep, fs), 2) << "\n"
              << "peak in " << juce::String (dbfs (std::max (src.getMagnitude (0, 0, (int) len), src.getMagnitude (1, 0, (int) len))), 1)
              << " dBFS, out " << juce::String (dbfs (pkOut), 1) << " dBFS, TP out "
              << juce::String (mm::analysis::truePeakDb (outPtr, 2, keep, fs), 1) << " dBTP\n";
    return 0;
}
