// 离线渲染：把一个音频文件过一遍 MAKE VOX，写成 24 bit WAV，并打印 CPU 占用和电平。
//   vox_render in.wav out.wav [预设序号=0] [BPM=120] [参数=值 ...]      例：vox_render dry.wav wet.wav 1 94 compress=0.9 echo=0
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/PluginProcessor.h"
#include <chrono>
#include <iostream>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3) { std::cerr << "usage: vox_render in.wav out.wav [preset] [bpm] [param=value ...]\n"; return 1; }
    const juce::File in = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (in));
    if (reader == nullptr) { std::cerr << "cannot read " << argv[1] << "\n"; return 1; }

    const double fs = reader->sampleRate; const int block = 512;
    MakeVoxProcessor proc;
    proc.fallbackBpm = argc > 4 ? std::atof (argv[4]) : 120.0;
    proc.setPlayConfigDetails (2, 2, fs, block);
    proc.setCurrentProgram (argc > 3 ? std::atoi (argv[3]) : 0);
    for (int a = 5; a < argc; ++a)
    {
        const juce::String kv (argv[a]); auto* p = proc.apvts.getParameter (kv.upToFirstOccurrenceOf ("=", false, false));
        if (p == nullptr) { std::cerr << "unknown parameter " << argv[a] << "\n"; return 1; }
        p->setValueNotifyingHost (p->convertTo0to1 (kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
    }
    proc.prepareToPlay (fs, block);
    const int latency = proc.getLatencySamples();

    const juce::int64 total = reader->lengthInSamples + (juce::int64) (fs * 3.0);      // 多渲 3 秒，把回声和混响的尾巴留下来
    juce::AudioBuffer<float> buf (2, block), outAll (2, (int) total);
    juce::MidiBuffer midi;
    double cpu = 0; float pkIn = 0, pkOut = 0, maxComp = 0, maxDe = 0;
    for (juce::int64 pos = 0; pos < total; pos += block)
    {
        const int n = (int) std::min<juce::int64> (block, total - pos);
        buf.setSize (2, n, false, false, true); buf.clear();
        if (pos < reader->lengthInSamples) reader->read (&buf, 0, (int) std::min<juce::int64> (n, reader->lengthInSamples - pos), pos, true, reader->numChannels > 1);
        if (reader->numChannels == 1) buf.copyFrom (1, 0, buf, 0, 0, n);
        for (int c = 0; c < 2; ++c) pkIn = std::max (pkIn, buf.getMagnitude (c, 0, n));
        const auto t0 = std::chrono::steady_clock::now();
        proc.processBlock (buf, midi);
        cpu += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        maxComp = std::max (maxComp, proc.compGr.load()); maxDe = std::max (maxDe, proc.deessGr.load());
        for (int c = 0; c < 2; ++c) { outAll.copyFrom (c, (int) pos, buf, c, 0, n); pkOut = std::max (pkOut, buf.getMagnitude (c, 0, n)); }
        for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) if (! std::isfinite (buf.getSample (c, i))) { std::cerr << "NaN/Inf at " << pos + i << "\n"; return 2; }
    }

    // 去掉插件的延迟，让输出和输入对齐
    const int keep = (int) total - latency;
    juce::AudioBuffer<float> aligned (2, keep);
    for (int c = 0; c < 2; ++c) aligned.copyFrom (c, 0, outAll, c, latency, keep);
    out.deleteFile();
    std::unique_ptr<juce::OutputStream> os (out.createOutputStream());
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (fs).withNumChannels (2).withBitsPerSample (24));
    if (w == nullptr) { std::cerr << "cannot write " << argv[2] << "\n"; return 1; }
    w->writeFromAudioSampleBuffer (aligned, 0, keep);

    const double secs = (double) total / fs;
    std::cout << "preset " << proc.getProgramName (proc.getCurrentProgram()) << "  latency " << latency << " samples\n"
              << "CPU " << juce::String (cpu / secs * 100.0, 2) << "% of one core (" << juce::String (cpu, 3) << " s for " << juce::String (secs, 1) << " s)\n"
              << "peak in " << juce::String (vox::gainToDb (pkIn), 1) << " dBFS, out " << juce::String (vox::gainToDb (pkOut), 1) << " dBFS, max comp GR "
              << juce::String (maxComp, 1) << " dB, max de-ess GR " << juce::String (maxDe, 1) << " dB\n";
    return 0;
}
