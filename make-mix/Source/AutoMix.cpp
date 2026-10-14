#include "AutoMix.h"
#include <cstdio>
#include <map>

namespace mm::automix
{
using analysis::Stereo;
namespace
{
constexpr int kSimBlock = 512;
// 专业人声的大致倍频程谱形（相对 500/1k/2k 平均，dB），用来判断录音偏浑 / 偏暗 / 偏亮
constexpr float kRefVocal[kOctBands] = { -30, -24, -12, -4, 1, 1, -1, -5, -11, -22 };

template <typename F> Stereo runChain (const Stereo& in, double fs, F&& proc)
{
    const int L = latencyFor (fs, kSimBlock);
    const size_t n = in.size(), total = n + (size_t) L;
    Stereo out; out.l.assign (total, 0.0f); out.r.assign (total, 0.0f);
    std::copy (in.l.begin(), in.l.end(), out.l.begin()); std::copy (in.r.begin(), in.r.end(), out.r.begin());
    for (size_t s = 0; s < total; s += kSimBlock)
    {
        const int m = (int) std::min ((size_t) kSimBlock, total - s);
        float* ch[2] = { out.l.data() + s, out.r.data() + s };
        proc (ch, m, (int64_t) s);
    }
    out.l.erase (out.l.begin(), out.l.begin() + L); out.r.erase (out.r.begin(), out.r.begin() + L);
    return out;
}
struct KeyStore : KeySink, KeySource           // 模拟时的人声 key：按 hop 存，读的时候把各路人声的功率加起来
{
    std::map<int64_t, std::array<float, kKeyBands>> pw;
    void publish (int64_t hop, const float* db) noexcept override
    {
        auto& a = pw[hop];
        for (int k = 0; k < kKeyBands; ++k) a[(size_t) k] += std::pow (10.0f, db[k] * 0.1f);
    }
    bool read (int64_t hop, bool, float* db) noexcept override
    {
        auto it = pw.find (hop); if (it == pw.end()) return false;
        for (int k = 0; k < kKeyBands; ++k) db[k] = powToDb (it->second[(size_t) k]);
        return true;
    }
};
Stereo sum (const std::vector<Stereo>& xs)
{
    Stereo s; if (xs.empty()) return s;
    size_t n = SIZE_MAX; for (auto& x : xs) n = std::min (n, x.size());
    s.l.assign (n, 0.0f); s.r.assign (n, 0.0f);
    for (auto& x : xs) for (size_t i = 0; i < n; ++i) { s.l[i] += x.l[i]; s.r[i] += x.r[i]; }
    return s;
}
std::string fmt (const char* f, double a, double b = 0, double c = 0) { char buf[160]; std::snprintf (buf, sizeof buf, f, a, b, c); return buf; }
} // namespace

Stereo runVocal (const Stereo& in, const VocalChain::P& p, StyleId st, double fs, double bpm, int64_t start, KeySink* sink)
{
    VocalChain c; c.setParams (p, st, bpm); c.prepare (fs, kSimBlock, latencyFor (fs, kSimBlock));
    return runChain (in, fs, [&] (float** ch, int n, int64_t s) { c.process (ch, 2, n, start + s, sink); });
}
Stereo runBeat (const Stereo& in, const BeatChain::P& p, StyleId st, double fs, int64_t start, KeySource* src)
{
    BeatChain c; c.setParams (p, st); c.prepare (fs, kSimBlock, latencyFor (fs, kSimBlock));
    return runChain (in, fs, [&] (float** ch, int n, int64_t s) { c.process (ch, 2, n, start + s, true, src); });
}
Stereo runBus (const Stereo& in, const BusChain::P& p, StyleId st, double fs, float* meanGlueGr)
{
    BusChain c; c.setParams (p, st); c.prepare (fs, kSimBlock, latencyFor (fs, kSimBlock));
    double gr = 0; int nb = 0;
    auto out = runChain (in, fs, [&] (float** ch, int n, int64_t) { c.process (ch, 2, n); gr += c.grGlue; ++nb; });
    if (meanGlueGr) *meanGlueGr = nb ? (float) (gr / nb) : 0.0f;
    return out;
}

Out solve (const In& in)
{
    Out o; const auto& st = mm::style (in.style); const double fs = in.fs;
    if (in.vocals.empty() && in.beats.empty() && ! in.hasBus) { o.error = "NOTHING TO MIX"; return o; }
    // 1) 对齐：只用所有录音共同覆盖的那段内容时间
    int64_t a = INT64_MIN, b = INT64_MAX;
    for (auto* v : { &in.vocals, &in.beats }) for (auto& c : *v) { a = std::max (a, c.start); b = std::min (b, c.start + (int64_t) c.audio.size()); }
    if (a == INT64_MIN) { a = 0; b = 0; }
    if (! in.vocals.empty() || ! in.beats.empty())
        if (b - a < (int64_t) (5 * fs)) { o.error = "NOT ENOUGH AUDIO (PLAY 15 s)"; return o; }
    auto trim = [&] (const Capture& c) { Stereo s; const size_t off = (size_t) (a - c.start), n = (size_t) (b - a);
        s.l.assign (c.audio.l.begin() + (long) off, c.audio.l.begin() + (long) (off + n)); s.r.assign (c.audio.r.begin() + (long) off, c.audio.r.begin() + (long) (off + n)); return s; };
    std::vector<Stereo> vin, bin;
    for (auto& c : in.vocals) vin.push_back (trim (c));
    for (auto& c : in.beats) bin.push_back (trim (c));
    o.m.seconds = (float) ((b - a) / fs);
    for (auto& v : vin) if (analysis::integrated (v, fs, true) < -55) { o.error = "NOT ENOUGH VOCAL"; return o; }

    // 2) 人声音色和动态（每路人声各自解）
    std::vector<VocalChain::P> vp;
    for (size_t i = 0; i < vin.size(); ++i)
    {
        auto p = VocalChain::fromSet (in.vocals[i].current);
        const Stereo& v = vin[i];
        const float Lin = analysis::integrated (v, fs, true);
        p.input = std::clamp (-20.0f - Lin, -24.0f, 24.0f);
        p.hpf = std::clamp (0.5f * (st.vHpfMin + st.vHpfMax), 40.0f, 300.0f);
        const auto lt = analysis::octaveLtas (v, fs);
        auto dev = [&] (int k) { return lt[(size_t) k] - kRefVocal[k]; };
        p.mud  = std::clamp (st.vMudG - st.vMatch * 0.45f * std::max (0.0f, dev (3)), -6.0f, 0.0f);
        p.pres = std::clamp (st.vPresG - st.vMatch * 0.6f * 0.5f * (dev (6) + dev (7)), -3.0f, 8.0f);
        p.air  = std::clamp (st.vAirG - st.vMatch * 0.6f * dev (8), -3.0f, 8.0f);
        p.warmth = std::clamp (st.vWarmth, 0.0f, 1.0f);
        const float sib = analysis::sibilance (v, fs);
        p.deess = std::clamp (0.3f + (sib - 12.0f) / 10.0f + 0.08f * st.vDeessMargin, 0.1f, 0.85f);
        // 压缩：用真实链子模拟，两步牛顿把动态（P90-P10）拉到风格目标
        auto dry = p; dry.echo = 0; dry.space = 0;
        const float s0 = analysis::spread (v, fs);
        float k = std::clamp (0.25f + (s0 - st.vSpread) / 8.0f, 0.05f, 0.95f);
        for (int it = 0; it < 2; ++it)
        {
            dry.comp = k;
            const float s1 = analysis::spread (runVocal (v, dry, in.style, fs, in.bpm, a, nullptr), fs);
            k = std::clamp (k + (s1 - st.vSpread) / 8.0f, 0.0f, 1.0f);
        }
        p.comp = k;
        p.echo = std::clamp ((st.vEchoWdr + 30.0f) / 24.0f, 0.0f, 1.0f); p.echoTime = std::clamp (st.vEchoTime, 0, 5);
        p.space = std::clamp ((st.vRevWdr + 32.0f) / 24.0f, 0.0f, 1.0f);
        p.level = 0;
        vp.push_back (p);
        ClientOut co; co.client = in.vocals[i].client; co.set = in.vocals[i].current;
        co.note = fmt ("INPUT %+.1f dB to reference\n", p.input) + fmt ("MUD %.1f dB  PRESENCE %+.1f dB  AIR %+.1f dB\n", p.mud, p.pres, p.air)
                + fmt ("COMPRESS %.0f%%  (spread %.1f LU -> target %.1f)\n", k * 100, s0, st.vSpread) + fmt ("DE-ESS %.0f%%  WARMTH %.0f%%\n", p.deess * 100, p.warmth * 100);
        o.clients.push_back (co);
    }
    // 3) 伴奏：低切 + 固定的人声口袋 + 动态让位深度（风格）
    std::vector<BeatChain::P> bp;
    for (auto& c : in.beats)
    {
        auto p = BeatChain::fromSet (c.current);
        p.hpf = std::clamp (st.bHpf, 20.0f, 200.0f);
        p.pocket = std::clamp (st.bPocketStatic + (in.vocals.empty() ? 0.0f : st.bPocketLinked), -6.0f, 0.0f);
        p.unmask = in.vocals.empty() ? 0.0f : std::clamp (st.bDmax, 0.0f, 12.0f);
        bp.push_back (p);
        ClientOut co; co.client = c.client; co.set = c.current;
        co.note = fmt ("LOW CUT %.0f Hz  POCKET %.1f dB @ %.0f Hz\n", p.hpf, p.pocket, st.bPocketF) + fmt ("UNMASK up to %.1f dB where the vocal sings\n", p.unmask);
        o.clients.push_back (co);
    }
    // 4) 人声电平：按风格的「人声比伴奏」（人声频段响度）定；伴奏让位又和人声电平有关，所以迭代两次
    std::vector<Stereo> vout (vin.size()), bout (bin.size());
    auto simAll = [&]
    {
        KeyStore keys;
        for (size_t i = 0; i < vin.size(); ++i) vout[i] = runVocal (vin[i], vp[i], in.style, fs, in.bpm, a, &keys);
        for (size_t j = 0; j < bin.size(); ++j) bout[j] = runBeat (bin[j], bp[j], in.style, fs, a, vin.empty() ? nullptr : &keys);
    };
    const Stereo rawBeat = sum (bin), rawVocal = sum (vin);
    if (! vin.empty() && ! bin.empty())
    {
        o.m.vbrBefore = analysis::integrated (rawVocal, fs, true) - analysis::integrated (rawBeat, fs, true);
        o.m.maskBefore = analysis::maskedFraction (rawVocal, rawBeat, fs);
    }
    for (int it = 0; it < 2; ++it)
    {
        simAll();
        const float Lb = bin.empty() ? 0.0f : analysis::integrated (sum (bout), fs, true);
        for (size_t i = 0; i < vin.size(); ++i)
        {
            const float Lv = analysis::integrated (vout[i], fs, true);
            // 没有伴奏可比时保持原来的响度（只改音色）
            const float want = bin.empty() ? analysis::integrated (vin[i], fs, true) : Lb + st.vbr;
            vp[i].level = std::clamp (vp[i].level + want - Lv, -24.0f, 24.0f);
        }
    }
    simAll();
    if (! vin.empty() && ! bin.empty())
    {
        o.m.vbrAfter = analysis::integrated (sum (vout), fs, true) - analysis::integrated (sum (bout), fs, true);
        o.m.maskAfter = analysis::maskedFraction (sum (vout), sum (bout), fs);
    }
    for (size_t i = 0; i < vin.size(); ++i)
    {
        auto& s = o.clients[i].set; const auto& p = vp[i];
        s[V_INPUT] = p.input; s[V_HPF] = p.hpf; s[V_MUD] = p.mud; s[V_PRES] = p.pres; s[V_AIR] = p.air; s[V_COMP] = p.comp; s[V_DEESS] = p.deess;
        s[V_WARMTH] = p.warmth; s[V_ECHO] = p.echo; s[V_ECHOTIME] = (float) p.echoTime; s[V_SPACE] = p.space; s[V_LEVEL] = p.level;
        o.clients[i].note += bin.empty() ? fmt ("LEVEL %+.1f dB (kept loudness, no BEAT linked)\n", p.level)
                                         : fmt ("LEVEL %+.1f dB  vocal vs beat %+.1f dB (style %+.1f)\n", p.level, o.m.vbrAfter, st.vbr);
    }
    for (size_t j = 0; j < bin.size(); ++j)
    {
        auto& s = o.clients[vin.size() + j].set; const auto& p = bp[j];
        s[B_HPF] = p.hpf; s[B_POCKET] = p.pocket; s[B_UNMASK] = p.unmask;
        if (! vin.empty()) o.clients[vin.size() + j].note += fmt ("masked cells %.0f%% -> %.0f%%\n", o.m.maskBefore * 100, o.m.maskAfter * 100);
    }
    // 5) 总线：音色向风格曲线靠、粘合到风格的平均压缩量、推到风格响度（-1 dBTP 天花板）
    if (in.hasBus && (! vout.empty() || ! bout.empty()))
    {
        std::vector<Stereo> all = vout; all.insert (all.end(), bout.begin(), bout.end());
        const Stereo mix = sum (all);
        auto p = BusChain::fromSet (in.busCurrent);
        const auto lt = analysis::octaveLtas (mix, fs);
        p.low  = std::clamp (0.6f * 0.5f * ((st.busCurve[1] - lt[1]) + (st.busCurve[2] - lt[2])), -4.0f, 4.0f);
        p.high = std::clamp (0.6f * 0.5f * ((st.busCurve[8] - lt[8]) + (st.busCurve[9] - lt[9])), -4.0f, 4.0f);
        p.width = 100; p.ceiling = std::clamp (st.ceiling, -3.0f, 0.0f); p.drive = 0;
        float lo = 0, hi = 1, gr = 0;                    // 粘合：二分到风格的平均压缩量
        for (int it = 0; it < 7; ++it) { p.glue = 0.5f * (lo + hi); runBus (mix, p, in.style, fs, &gr); (gr < st.glueGr ? lo : hi) = p.glue; }
        p.glue = 0.5f * (lo + hi);
        lo = 0; hi = 18;                                 // 推子：二分到风格响度
        Stereo out;
        for (int it = 0; it < 9; ++it)
        {
            p.drive = 0.5f * (lo + hi); out = runBus (mix, p, in.style, fs, &gr);
            (analysis::integrated (out, fs) < st.lufsStyle ? lo : hi) = p.drive;
        }
        p.drive = 0.5f * (lo + hi); out = runBus (mix, p, in.style, fs, &gr);
        o.m.busLufs = analysis::integrated (out, fs); o.m.busTp = analysis::truePeakDb (out); o.m.glueGr = gr;
        ClientOut co; co.client = in.busClient; co.set = in.busCurrent;
        co.set[M_LOW] = p.low; co.set[M_HIGH] = p.high; co.set[M_GLUE] = p.glue; co.set[M_WIDTH] = p.width; co.set[M_DRIVE] = p.drive; co.set[M_CEILING] = p.ceiling;
        co.note = fmt ("TONE low %+.1f dB  high %+.1f dB toward the style curve\n", p.low, p.high) + fmt ("GLUE %.0f%% (avg %.1f dB)\n", p.glue * 100, gr)
                + fmt ("DRIVE %+.1f dB -> %.1f LUFS, ", p.drive, o.m.busLufs) + fmt ("peak %.1f dBTP\n", o.m.busTp);
        o.clients.push_back (co);
    }
    o.ok = true;
    o.summary = std::string ("MIX DONE · ") + st.name;
    return o;
}
} // namespace mm::automix
