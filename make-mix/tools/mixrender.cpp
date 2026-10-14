// mm_mixrender：离线模拟 FL —— 人声、伴奏、总线三个 MAKE MIX 实例共用一个 hub。
//   mm_mixrender --vocal v.wav --beat b.wav [--vocal v2.wav] [--style trap] [--bpm 94] [--no-bus] [--blocks 512|random] --out DIR
// 流程：从头播放 → 在总线上按 MIX → 听满 15 秒 → 解算并写参数 → 从头完整渲染一遍（含 3 秒尾巴）→ 写 WAV 和指标。
// 还会用随机块长 + 随机处理顺序再渲染一次，检查结果和固定 512 的完全一样（导出 = 实时）。
#include "../Source/PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <chrono>
#include <iostream>
#include <random>

using namespace mm;
using analysis::Stereo;

namespace
{
struct PlayHead : juce::AudioPlayHead
{
    int64_t pos = 0; bool playing = true; double bpm = 120;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setTimeInSamples (pos); p.setIsPlaying (playing); p.setBpm (bpm);
        return p;
    }
};
Stereo readWav (const juce::File& f, double& fs)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (! r) { std::cerr << "cannot read " << f.getFullPathName() << "\n"; std::exit (2); }
    fs = r->sampleRate;
    juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
    Stereo s; s.l.assign (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples());
    const float* rr = b.getReadPointer (b.getNumChannels() > 1 ? 1 : 0); s.r.assign (rr, rr + b.getNumSamples());
    return s;
}
void writeWav (const juce::File& f, const Stereo& s, double fs)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions {}.withSampleRate (fs).withNumChannels (2).withBitsPerSample (24));
    const float* ch[2] = { s.l.data(), s.r.data() };
    w->writeFromFloatArrays (ch, 2, (int) s.size());
}
struct Track { MakeMixProcessor* p; const Stereo* in; Stereo out; double cpu = 0; };

// 跑 [0, len) 这一段（含尾巴），每个周期：人声 / 伴奏按随机顺序，总线吃它们的和
void run (std::vector<Track>& src, Track* busT, PlayHead& ph, size_t len, int maxBlock, bool randomBlocks, uint32_t seed,
          std::function<bool (size_t)> perCycle = {})
{
    std::mt19937 rng (seed);
    for (auto& t : src) { t.out.l.assign (len, 0); t.out.r.assign (len, 0); }
    if (busT) { busT->out.l.assign (len, 0); busT->out.r.assign (len, 0); }
    juce::AudioBuffer<float> buf (2, maxBlock * 2), sumBuf (2, maxBlock * 2); juce::MidiBuffer midi;
    size_t s = 0;
    while (s < len)
    {
        int n = maxBlock;
        if (randomBlocks) { const int r = (int) (rng() % 100); n = r < 2 ? 0 : r < 4 ? (int) (maxBlock * 1.45) : 1 + (int) (rng() % (uint32_t) maxBlock); }
        n = (int) std::min ((size_t) n, len - s);
        ph.pos = (int64_t) s;
        sumBuf.setSize (2, std::max (1, n), false, false, true); sumBuf.clear();
        std::vector<size_t> order (src.size()); for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        if (randomBlocks) std::shuffle (order.begin(), order.end(), rng);
        for (size_t oi : order)
        {
            auto& t = src[oi];
            buf.setSize (2, std::max (1, n), false, false, true);
            for (int c = 0; c < 2; ++c) { auto& x = c ? t.in->r : t.in->l; for (int i = 0; i < n; ++i) buf.setSample (c, i, s + (size_t) i < x.size() ? x[s + (size_t) i] : 0.0f); }
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
            const auto t0 = std::chrono::steady_clock::now();
            t.p->processBlock (view, midi);
            t.cpu += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) { const float v = buf.getSample (c, i); (c ? t.out.r : t.out.l)[s + (size_t) i] = v; sumBuf.addSample (c, i, v); }
        }
        if (busT)
        {
            juce::AudioBuffer<float> view (sumBuf.getArrayOfWritePointers(), 2, n);
            const auto t0 = std::chrono::steady_clock::now();
            busT->p->processBlock (view, midi);
            busT->cpu += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) (c ? busT->out.r : busT->out.l)[s + (size_t) i] = sumBuf.getSample (c, i);
        }
        s += (size_t) n;
        if (perCycle && ! perCycle (s)) break;
    }
}
Stereo shift (const Stereo& x, int L, size_t n)                 // 去掉延迟
{
    Stereo o; o.l.assign (n, 0); o.r.assign (n, 0);
    for (size_t i = 0; i < n && i + (size_t) L < x.size(); ++i) { o.l[i] = x.l[i + (size_t) L]; o.r[i] = x.r[i + (size_t) L]; }
    return o;
}
float maxDiffDb (const Stereo& a, const Stereo& b)
{
    float m = 0; for (size_t i = 0; i < std::min (a.size(), b.size()); ++i) m = std::max ({ m, std::abs (a.l[i] - b.l[i]), std::abs (a.r[i] - b.r[i]) });
    return gainToDb (m);
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::vector<juce::File> vocals, beats; juce::File outDir ("mix_out"), refFile; std::string styleName = "trap"; double bpm = 120, learnStart = 0; bool noBus = false, randomBlocks = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i]; auto next = [&] { return i + 1 < argc ? std::string (argv[++i]) : std::string(); };
        if (a == "--vocal") vocals.emplace_back (juce::File::getCurrentWorkingDirectory().getChildFile (next()));
        else if (a == "--beat") beats.emplace_back (juce::File::getCurrentWorkingDirectory().getChildFile (next()));
        else if (a == "--style") styleName = next();
        else if (a == "--bpm") bpm = std::stod (next());
        else if (a == "--no-bus") noBus = true;
        else if (a == "--learn-start") learnStart = std::stod (next());   // 从第几秒开始学（跳过没有人声的前奏）
        else if (a == "--reference") refFile = juce::File::getCurrentWorkingDirectory().getChildFile (next());
        else if (a == "--measure")              // 量一首成品：--measure vocal.wav beat.wav mix.wav（训练风格目标用）
        {
            double sr = 0; const Stereo v = readWav (juce::File::getCurrentWorkingDirectory().getChildFile (next()), sr);
            const Stereo b = readWav (juce::File::getCurrentWorkingDirectory().getChildFile (next()), sr);
            const Stereo m = readWav (juce::File::getCurrentWorkingDirectory().getChildFile (next()), sr);
            auto arr = [] (const std::array<float, kOctBands>& bands) { std::string t; for (float x : bands) t += " " + juce::String (x, 1).toStdString(); return t; };
            std::cout << "vocal_ltas" << arr (analysis::octaveLtas (v, sr)) << "\n"
                      << "mix_ltas" << arr (analysis::octaveLtas (m, sr)) << "\n"
                      << "beat_ltas" << arr (analysis::octaveLtas (b, sr)) << "\n"
                      << "vbr " << analysis::integrated (v, sr, true) - analysis::integrated (b, sr, true) << "\n"
                      << "vocal_spread " << analysis::spread (v, sr) << "\n"
                      << "vocal_sib " << analysis::sibilance (v, sr) << "\n"
                      << "masked " << analysis::maskedFraction (v, b, sr) << "\n"
                      << "mix_lufs " << analysis::integrated (m, sr) << "\n"
                      << "mix_tp " << analysis::truePeakDb (m) << "\n"
                      << "mix_corr " << analysis::correlation (m) << "\n"
                      << "beat_low " << analysis::lowShare (b, sr) << "\n";
            return 0;
        }
        else if (a == "--blocks") randomBlocks = next() == "random";
        else if (a == "--snapshot")             // 把三个角色的界面各截一张 PNG（检查排版）
        {
            const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (next()); dir.createDirectory();
            MakeMixProcessor p; p.prepareToPlay (44100, 512);
            for (int r = 0; r < 3; ++r)
            {
                p.setRole ((Role) r);
                std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
                ed->setVisible (true);
                auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                juce::PNGImageFormat png; juce::FileOutputStream fo (dir.getChildFile (juce::String ("ui_") + juce::String (r) + ".png"));
                fo.setPosition (0); fo.truncate(); png.writeImageToStream (img, fo);
            }
            return 0;
        }
        else if (a == "--analyze")              // 一键分析：--analyze vocal|beat|bus file.wav [snapshot.png]
        {
            const std::string rs = next(); const juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            const Role role = rs == "beat" ? Role::Beat : rs == "bus" ? Role::Bus : Role::Vocal;
            double sr = 0; const Stereo x = readWav (f, sr);
            MakeMixProcessor p; PlayHead hp; hp.bpm = bpm; p.setPlayHead (&hp); p.fallbackBpm = bpm; p.setRole (role); p.setStyle (styleIndex (styleName) < 0 ? 0 : styleIndex (styleName), false);
            p.prepareToPlay (sr, 512); p.requestAnalyze();
            juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer mb;
            for (size_t s0 = 0; s0 + 512 <= x.size() && p.analyzeState() == MakeMixProcessor::Analyze::Listening; s0 += 512)
            {
                hp.pos = (int64_t) s0;
                for (int j = 0; j < 512; ++j) { b.setSample (0, j, x.l[s0 + (size_t) j]); b.setSample (1, j, x.r[s0 + (size_t) j]); }
                p.processBlock (b, mb); p.tickAnalyze();
            }
            if (p.analyzeState() == MakeMixProcessor::Analyze::Listening) p.requestAnalyze();
            while (p.analyzeState() != MakeMixProcessor::Analyze::Ready) juce::Thread::sleep (20);
            for (auto& l : p.report()->lines) std::cout << l << "\n";
            std::cout << "LTAS"; for (float v : p.report()->ltas) std::cout << " " << juce::String (v, 1); std::cout << "\n";
            if (i + 1 < argc && argv[i + 1][0] != '-')
            {
                std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor()); ed->setVisible (true);
                auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                juce::PNGImageFormat png; const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (next());
                out.deleteFile(); juce::FileOutputStream fo (out); png.writeImageToStream (img, fo);
            }
            return 0;
        }
        else if (a == "--out") outDir = juce::File::getCurrentWorkingDirectory().getChildFile (next());
        else { std::cerr << "usage: mm_mixrender --vocal v.wav --beat b.wav [--style trap] [--bpm 94] [--no-bus] [--blocks 512|random] --out DIR\n"; return 2; }
    }
    if (vocals.empty() && beats.empty()) { std::cerr << "need --vocal and/or --beat\n"; return 2; }
    const StyleId sid = styleIndex (styleName);
    if (sid < 0) { std::cerr << "unknown style " << styleName << "\n"; return 2; }
    outDir.createDirectory();

    double fs = 0; std::vector<Stereo> vin, bin;
    for (auto& f : vocals) vin.push_back (readWav (f, fs));
    for (auto& f : beats) bin.push_back (readWav (f, fs));
    size_t songLen = SIZE_MAX; for (auto* v : { &vin, &bin }) for (auto& x : *v) songLen = std::min (songLen, x.size());

    const int maxBlock = 512; PlayHead ph; ph.bpm = bpm;
    std::vector<std::unique_ptr<MakeMixProcessor>> procs; std::vector<Track> src;
    auto make = [&] (Role r) { auto p = std::make_unique<MakeMixProcessor>(); p->setRole (r); p->setPlayHead (&ph); p->setStyle (sid, true);
                               p->fallbackBpm = bpm; p->prepareToPlay (fs, maxBlock); procs.push_back (std::move (p)); return procs.back().get(); };
    for (auto& v : vin) src.push_back ({ make (Role::Vocal), &v, {} });
    for (auto& b : bin) src.push_back ({ make (Role::Beat), &b, {} });
    Track busT { noBus ? nullptr : make (Role::Bus), nullptr, {} };
    auto& hub = procs.front()->hub(); hub.setSynchronousSolve (true);
    for (auto& p : procs) p->setStyle (sid, true);
    const int L = procs.front()->getLatencySamples();
    std::cout << "MAKE MIX harness  fs " << fs << "  style " << mm::style (sid).name << "  latency " << L << "  instances " << procs.size() << "\n"
              << hub.linkSummary() << "\n";

    // 1) 学习：从头播放，第一块之后按 MIX，听到会话结束（最多两遍歌）
    if (learnStart <= 0) procs.back()->requestMix();
    size_t sinceTick = 0; bool asked = learnStart <= 0;
    const auto tLearn = std::chrono::steady_clock::now();
    for (int pass = 0; pass < 2 && (! asked || hub.state() == Hub::State::Listening); ++pass)
        run (src, busT.p ? &busT : nullptr, ph, songLen, maxBlock, false, 1, [&] (size_t pos)
        {
            if (! asked && pos >= (size_t) (learnStart * fs)) { asked = true; procs.back()->requestMix(); }
            sinceTick += maxBlock;
            if (sinceTick >= (size_t) (0.1 * fs)) { sinceTick = 0; hub.tick(); }
            return ! asked || hub.state() == Hub::State::Listening;
        });
    if (hub.state() == Hub::State::Listening) procs.back()->requestMix();      // 歌比 15 秒短：用已有的
    const double solveSec = std::chrono::duration<double> (std::chrono::steady_clock::now() - tLearn).count();
    std::cout << "MIX: " << hub.statusText() << "  (" << juce::String (solveSec, 2) << " s wall incl. listening sim)\n";
    for (auto& p : procs) std::cout << "  [" << (p->role() == Role::Vocal ? "VOCAL" : p->role() == Role::Beat ? "BEAT" : "BUS") << "]\n    "
                                    << p->note().replace ("\n", "\n    ").toStdString() << "\n";

    // 2) 正式渲染（固定 512 块）+ 3 秒尾巴
    const size_t total = songLen + (size_t) (3 * fs) + (size_t) L;
    auto render = [&] (bool rnd, uint32_t seed)
    {
        for (auto& p : procs) p->prepareToPlay (fs, maxBlock);
        for (auto& t : src) t.cpu = 0;
        busT.cpu = 0;
        run (src, busT.p ? &busT : nullptr, ph, total, maxBlock, rnd, seed);
    };
    render (randomBlocks, 7);
    std::vector<Stereo> vout, bout; Stereo busOut;
    for (auto& t : src) { (t.p->role() == Role::Vocal ? vout : bout).push_back (shift (t.out, L, total - (size_t) L)); }
    if (busT.p) busOut = shift (busT.out, L, total - (size_t) L);
    auto sum = [] (const std::vector<Stereo>& xs, size_t n) { Stereo s; s.l.assign (n, 0); s.r.assign (n, 0);
        for (auto& x : xs) for (size_t i = 0; i < std::min (n, x.size()); ++i) { s.l[i] += x.l[i]; s.r[i] += x.r[i]; }
        return s; };
    const size_t n = total - (size_t) L;
    const Stereo rawV = sum (vin, n), rawB = sum (bin, n), raw = sum ({ rawV, rawB }, n), mixV = sum (vout, n), mixB = sum (bout, n);
    writeWav (outDir.getChildFile ("raw_sum.wav"), raw, fs);
    for (size_t i = 0; i < vout.size(); ++i) writeWav (outDir.getChildFile ("vocal_" + std::to_string (i) + "_out.wav"), vout[i], fs);
    for (size_t i = 0; i < bout.size(); ++i) writeWav (outDir.getChildFile ("beat_" + std::to_string (i) + "_out.wav"), bout[i], fs);
    if (busT.p) writeWav (outDir.getChildFile ("bus_out.wav"), busOut, fs);

    // 3) 指标
    auto m = hub.lastMetrics();
    auto I = [&] (const Stereo& x, bool h) { return analysis::integrated (x, fs, h); };
    std::cout << "\nMETRICS (whole file)\n";
    if (! vin.empty() && ! bin.empty())
        std::cout << "  vocal-vs-beat (vocal band)  raw " << juce::String (I (rawV, true) - I (rawB, true), 1) << " dB  -> mixed " << juce::String (I (mixV, true) - I (mixB, true), 1)
                  << " dB  (style " << juce::String (mm::style (sid).vbr, 1) << ")\n"
                  << "  masked cells 1-5 kHz       raw " << juce::String (analysis::maskedFraction (rawV, rawB, fs) * 100, 0) << "%  -> mixed "
                  << juce::String (analysis::maskedFraction (mixV, mixB, fs) * 100, 0) << "%\n"
                  << "  beat loudness change        " << juce::String (I (mixB, false) - I (rawB, false), 2) << " LU\n";
    for (size_t i = 0; i < vin.size(); ++i)
        std::cout << "  vocal " << i << " spread  " << juce::String (analysis::spread (vin[i], fs), 1) << " -> " << juce::String (analysis::spread (vout[i], fs), 1) << " LU (style "
                  << juce::String (mm::style (sid).vSpread, 1) << ")\n";
    std::cout << "  raw sum  " << juce::String (I (raw, false), 1) << " LUFS  TP " << juce::String (analysis::truePeakDb (raw), 1) << " dBTP\n";
    if (busT.p)
        std::cout << "  bus out  " << juce::String (I (busOut, false), 1) << " LUFS (style " << juce::String (mm::style (sid).lufsStyle, 1) << ")  TP "
                  << juce::String (analysis::truePeakDb (busOut), 2) << " dBTP  sample peak " << juce::String (analysis::samplePeakDb (busOut), 2) << " dBFS\n"
                  << "  solver: learned " << juce::String (m.seconds, 1) << " s, bus " << juce::String (m.busLufs, 1) << " LUFS, glue " << juce::String (m.glueGr, 1) << " dB\n";
    if (busT.p && refFile.existsAsFile())                // 和原曲母带比：频谱形状距离、响度差（越小越像发行版）
    {
        double rfs = 0; const Stereo ref = readWav (refFile, rfs);
        const auto a = analysis::octaveLtas (busOut, fs), b = analysis::octaveLtas (ref, rfs);
        float d = 0; for (int k = 1; k < kOctBands; ++k) d += (a[(size_t) k] - b[(size_t) k]) * (a[(size_t) k] - b[(size_t) k]);
        const Stereo rawCut = raw;
        const auto c = analysis::octaveLtas (rawCut, fs);
        float d0 = 0; for (int k = 1; k < kOctBands; ++k) d0 += (c[(size_t) k] - b[(size_t) k]) * (c[(size_t) k] - b[(size_t) k]);
        std::cout << "  vs released master: tone distance " << juce::String (std::sqrt (d / 9), 2) << " dB (raw sum " << juce::String (std::sqrt (d0 / 9), 2)
                  << ")  loudness " << juce::String (I (busOut, false) - I (ref, false), 1) << " LU\n    curve diff:";
        for (int k = 0; k < kOctBands; ++k) std::cout << " " << juce::String (a[(size_t) k] - b[(size_t) k], 1);
        std::cout << "\n";
    }
    const double songSec = (double) total / fs;
    for (auto& t : src) std::cout << "  CPU " << (t.p->role() == Role::Vocal ? "VOCAL " : "BEAT  ") << juce::String (100 * t.cpu / songSec, 2) << " % of one core\n";
    if (busT.p) std::cout << "  CPU BUS   " << juce::String (100 * busT.cpu / songSec, 2) << " % of one core\n";

    // 4) 确定性：随机块长 / 随机顺序 vs 固定 512
    render (! randomBlocks, 99);
    float worst = -200;
    { size_t vi = 0, bi = 0; for (auto& t : src) { auto o = shift (t.out, L, n); worst = std::max (worst, maxDiffDb (o, t.p->role() == Role::Vocal ? vout[vi++] : bout[bi++])); } }
    if (busT.p) worst = std::max (worst, maxDiffDb (shift (busT.out, L, n), busOut));
    std::cout << "  determinism (random vs fixed blocks): max diff " << juce::String (worst, 1) << " dBFS " << (worst <= -120 ? "OK" : "DIFFERS") << "\n";
    return hub.state() == Hub::State::Failed ? 1 : 0;
}
