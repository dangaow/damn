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

// —— 整首歌量测（训练用）——————————————————————————————————————————————————
Stereo slice (const Stereo& x, size_t a, size_t b)
{
    b = std::min (b, x.size()); a = std::min (a, b);
    Stereo s; s.l.assign (x.l.begin() + (long) a, x.l.begin() + (long) b); s.r.assign (x.r.begin() + (long) a, x.r.begin() + (long) b);
    return s;
}
std::string jarr (const std::array<float, kOctBands>& v)
{
    std::string t = "["; for (int k = 0; k < kOctBands; ++k) t += (k ? "," : "") + juce::String (v[(size_t) k], 2).toStdString(); return t + "]";
}
std::string jnum (double x) { return std::isfinite (x) ? juce::String (x, 4).toStdString() : "null"; }
// 一段的全部指标（插件和解算器用的同一套算法）
std::string metricsJson (const Stereo& v, const Stereo& b, const Stereo& m, double fs)
{
    const float vI = analysis::integrated (v, fs, true), bI = analysis::integrated (b, fs, true);
    const bool hasV = vI > -55;
    std::string j = "{";
    j += "\"mix_lufs\":" + jnum (analysis::integrated (m, fs)) + ",\"mix_tp\":" + jnum (analysis::truePeakDb (m)) + ",\"mix_corr\":" + jnum (analysis::correlation (m));
    j += ",\"mix_spread\":" + jnum (analysis::spread (m, fs)) + ",\"mix_ltas\":" + jarr (analysis::octaveLtas (m, fs));
    j += ",\"beat_lufs\":" + jnum (analysis::integrated (b, fs)) + ",\"beat_low\":" + jnum (analysis::lowShare (b, fs)) + ",\"beat_corr\":" + jnum (analysis::correlation (b));
    j += ",\"beat_spread\":" + jnum (analysis::spread (b, fs)) + ",\"beat_ltas\":" + jarr (analysis::octaveLtas (b, fs));
    j += ",\"has_vocal\":" + std::string (hasV ? "true" : "false");
    if (hasV)
    {
        j += ",\"vbr\":" + jnum (vI - bI) + ",\"vocal_lufs\":" + jnum (analysis::integrated (v, fs)) + ",\"vocal_spread\":" + jnum (analysis::spread (v, fs));
        j += ",\"vocal_sib\":" + jnum (analysis::sibilance (v, fs)) + ",\"masked\":" + jnum (analysis::maskedFraction (v, b, fs)) + ",\"vocal_ltas\":" + jarr (analysis::octaveLtas (v, fs));
    }
    return j + "}";
}
// 分段：每秒一帧。人声在不在（人声频段瞬时响度 > 整首 -8 LU）+ 整体响度（4 秒平滑）。
// 有人声且比有人声部分的中位数响 0.7 LU 以上 = 副歌，其余有人声 = 主歌；没人声：第一句之前 = 前奏，最后一句之后 = 尾奏，中间 = 间奏
std::vector<std::tuple<int, int, std::string>> sections (const Stereo& v, const Stereo& m, double fs)
{
    const auto vm = analysis::momentary (v, fs, true), mm_ = analysis::momentary (m, fs);
    const float vI = analysis::integrated (v, fs, true);
    const int secs = (int) (m.size() / fs);
    std::vector<int> act ((size_t) secs, 0); std::vector<float> loud ((size_t) secs, -100);
    for (int s = 0; s < secs; ++s)
    {
        float vmax = -100, e = 0; int n = 0;
        for (int k = s * 10; k < std::min ((int) vm.size(), s * 10 + 10); ++k) vmax = std::max (vmax, vm[(size_t) k]);
        for (int k = s * 10; k < std::min ((int) mm_.size(), s * 10 + 10); ++k) { e += std::pow (10.0f, mm_[(size_t) k] / 10); ++n; }
        act[(size_t) s] = vmax > vI - 8; loud[(size_t) s] = n ? 10 * std::log10 (std::max (e / (float) n, 1e-12f)) : -100;
    }
    std::vector<int> act2 = act;                                     // 人声：前后 2 秒内有就算有（句子之间的换气不切段）
    for (int s = 0; s < secs; ++s) for (int d = -2; d <= 2; ++d) if (s + d >= 0 && s + d < secs && act[(size_t) (s + d)]) act2[(size_t) s] = 1;
    std::vector<float> sm ((size_t) secs);
    for (int s = 0; s < secs; ++s) { float e = 0; int n = 0; for (int d = -2; d <= 2; ++d) if (s + d >= 0 && s + d < secs) { e += std::pow (10.0f, loud[(size_t) (s + d)] / 10); ++n; } sm[(size_t) s] = 10 * std::log10 (std::max (e / (float) n, 1e-12f)); }
    std::vector<float> vl; for (int s = 0; s < secs; ++s) if (act2[(size_t) s]) vl.push_back (sm[(size_t) s]);
    const float med = vl.empty() ? 0 : analysis::percentile (vl, 0.5f);
    int first = secs, last = -1; for (int s = 0; s < secs; ++s) if (act2[(size_t) s]) { first = std::min (first, s); last = s; }
    std::vector<int> cls ((size_t) secs);                           // 0 前奏 1 主歌 2 副歌 3 间奏 4 尾奏
    for (int s = 0; s < secs; ++s) cls[(size_t) s] = act2[(size_t) s] ? (sm[(size_t) s] > med + 0.7f ? 2 : 1) : (s < first ? 0 : s > last ? 4 : 3);
    {                                                                // 前后 4 秒多数表决，去掉碎段
        std::vector<int> c2 = cls;
        for (int s = 0; s < secs; ++s)
        {
            int cnt[5] = {}; for (int d = -4; d <= 4; ++d) if (s + d >= 0 && s + d < secs) ++cnt[cls[(size_t) (s + d)]];
            c2[(size_t) s] = (int) (std::max_element (cnt, cnt + 5) - cnt);
        }
        cls = c2;
    }
    std::vector<std::tuple<int, int, std::string>> out; const char* names[] = { "intro", "verse", "chorus", "break", "outro" };
    std::vector<std::pair<int, int>> runs;                           // (开始秒, 类别)
    for (int s = 0; s < secs; ++s) if (runs.empty() || cls[(size_t) s] != runs.back().second) runs.push_back ({ s, cls[(size_t) s] });
    std::vector<std::pair<int, int>> merged;                         // 短于 6 秒的段并进前一段
    for (size_t i = 0; i < runs.size(); ++i)
    {
        const int end = i + 1 < runs.size() ? runs[i + 1].first : secs;
        if (! merged.empty() && end - runs[i].first < 6) continue;
        if (! merged.empty() && merged.back().second == runs[i].second) continue;
        merged.push_back (runs[i]);
    }
    for (size_t i = 0; i < merged.size(); ++i) out.push_back ({ merged[i].first, i + 1 < merged.size() ? merged[i + 1].first : secs, names[merged[i].second] });
    return out;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::vector<juce::File> vocals, beats; juce::File outDir ("mix_out"), refFile; std::vector<std::string> sets; std::string styleName = "trap"; double bpm = 120, learnStart = 0; bool noBus = false, randomBlocks = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i]; auto next = [&] { return i + 1 < argc ? std::string (argv[++i]) : std::string(); };
        if (a == "--vocal") vocals.emplace_back (juce::File::getCurrentWorkingDirectory().getChildFile (next()));
        else if (a == "--beat") beats.emplace_back (juce::File::getCurrentWorkingDirectory().getChildFile (next()));
        else if (a == "--style") styleName = next();
        else if (a == "--bpm") bpm = std::stod (next());
        else if (a == "--no-bus") noBus = true;
        else if (a == "--set") sets.push_back (next());                   // MIX 之后强制改参数：--set m_lift=0
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
        else if (a == "--measure-song")         // 训练：--measure-song vocal.wav inst.wav mix.wav → JSON（整首 + 分段 + 每 5 秒一个 20 秒窗）
        {
            double sr = 0; const Stereo v = readWav (juce::File::getCurrentWorkingDirectory().getChildFile (next()), sr);
            const Stereo b = readWav (juce::File::getCurrentWorkingDirectory().getChildFile (next()), sr);
            const Stereo m = readWav (juce::File::getCurrentWorkingDirectory().getChildFile (next()), sr);
            const size_t n = std::min ({ v.size(), b.size(), m.size() });
            const auto key = analysis::detectKey (b, sr);
            std::cout << "{\"dur\":" << jnum ((double) n / sr) << ",\"key\":\"" << key.name() << "\",\"key_conf\":" << jnum (key.confidence)
                      << ",\"whole\":" << metricsJson (v, b, m, sr) << ",\"sections\":[";
            bool first = true;
            for (auto& [a0, a1, kind] : sections (v, m, sr))
            {
                const size_t s0 = (size_t) (a0 * sr), s1 = (size_t) (a1 * sr);
                std::cout << (first ? "" : ",") << "{\"t0\":" << a0 << ",\"t1\":" << a1 << ",\"kind\":\"" << kind << "\",\"m\":"
                          << metricsJson (slice (v, s0, s1), slice (b, s0, s1), slice (m, s0, s1), sr) << "}";
                first = false;
            }
            std::cout << "],\"windows\":[";
            first = true;
            for (size_t s0 = 0; s0 + (size_t) (20 * sr) <= n; s0 += (size_t) (5 * sr))
            {
                const size_t s1 = s0 + (size_t) (20 * sr);
                std::cout << (first ? "" : ",") << "{\"t0\":" << jnum ((double) s0 / sr) << ",\"m\":" << metricsJson (slice (v, s0, s1), slice (b, s0, s1), slice (m, s0, s1), sr) << "}";
                first = false;
            }
            std::cout << "]}\n";
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

    for (auto& kv : sets)                                // --set id=value：所有实例里有这个参数的都改
    {
        const auto eq = kv.find ('='); if (eq == std::string::npos) continue;
        for (auto& p : procs) if (auto* prm = p->apvts.getParameter (kv.substr (0, eq)))
            prm->setValueNotifyingHost (prm->convertTo0to1 (std::stof (kv.substr (eq + 1))));
    }
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
