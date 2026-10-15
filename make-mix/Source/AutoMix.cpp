#include "AutoMix.h"
#include <cstdio>
#include <map>

namespace mm::automix
{
using analysis::Stereo;
namespace
{
constexpr int kSimBlock = 512;
// 从发行版里量出来的目标（tools/mixrender --measure，4 首歌的平均）：人声被伴奏盖住的比例、人声齿音
// 遮蔽 / 齿音的目标按风格训练（Styles.h 的 kStyleMask / kStyleSib）
// 发行版人声量出来的动态（P90-P10）比风格表（来自资料）大约 1.5 LU：按这个校正，免得把人声压扁
constexpr float kSpreadCal = 1.5f;

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
    std::map<int64_t, std::array<float, kKeyVals>> pw;
    void publish (int64_t hop, const float* db) noexcept override
    {
        auto& a = pw[hop];
        for (int k = 0; k < kKeyVals; ++k) a[(size_t) k] += std::pow (10.0f, db[k] * 0.1f);
    }
    bool read (int64_t hop, bool, float* db) noexcept override
    {
        auto it = pw.find (hop); if (it == pw.end()) return false;
        for (int k = 0; k < kKeyVals; ++k) db[k] = powToDb (it->second[(size_t) k]);
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
// 「跟随伴奏」的基准：处理后的人声 / 伴奏的 3 秒人声频段响度（和插件里完全同一个计算，多路伴奏按功率相加）。
// 先找主唱段的人声响度（3 秒响度的 75 % 分位），再只在主唱段（权重同插件）平均人声比伴奏 → {比值基准, 主唱响度}
std::pair<float, float> rideReference (const Stereo& vocal, const std::vector<Stereo>& beats, double fs)
{
    if (beats.empty()) return { -100.0f, -100.0f };
    const int hop = keyHop (fs); const size_t n = std::min (vocal.size(), beats[0].size());
    const double coef = std::exp (-(double) hop / (fs * 3.0));
    std::vector<std::pair<float, float>> fr;                        // 每帧 (人声电平, 伴奏电平)
    {
        BandMeter vm; vm.prepare (fs);
        std::vector<BandMeter> ms (beats.size()); for (auto& m : ms) m.prepare (fs);
        for (size_t i = 0; i < n; ++i)
        {
            vm.push (0.5f * (vocal.l[i] + vocal.r[i]));
            for (size_t b = 0; b < beats.size(); ++b) ms[b].push (0.5f * (beats[b].l[i] + beats[b].r[i]));
            if ((i + 1) % (size_t) hop != 0) continue;
            float pw = 0, db[kKeyVals];
            for (auto& m : ms) { m.take (db); pw += std::pow (10.0f, db[kKeyLoud] * 0.1f); }
            vm.take (db); fr.push_back ({ db[kKeyLoud], powToDb (pw) });
        }
    }
    const size_t skip = (size_t) (3 * fs / hop);                     // 前 3 秒平滑还没稳
    std::vector<float> vl; double ev = 0, eb = 0, w = 0;
    for (size_t k = 0; k < fr.size(); ++k) { VocalChain::rideRatio (ev, eb, fr[k].first, fr[k].second, coef, 0, w); if (k >= skip) vl.push_back ((float) (10 * std::log10 (ev + 1e-30))); }
    if (vl.empty()) return { -100.0f, -100.0f };
    const float vRef = analysis::percentile (vl, 0.75f);
    double acc = 0, wsum = 0; ev = eb = 0;
    for (size_t k = 0; k < fr.size(); ++k)
    {
        const double r = VocalChain::rideRatio (ev, eb, fr[k].first, fr[k].second, coef, vRef, w);
        if (k >= skip) { acc += w * r; wsum += w; }
    }
    return wsum > 1 ? std::pair<float, float> { (float) (acc / wsum), vRef } : std::pair<float, float> { -100.0f, -100.0f };
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
    if (in.vocals.empty() && in.beats.empty() && ! in.hasBus) { o.error = "沒有可混的軌"; return o; }
    // 1) 对齐：只用所有录音共同覆盖的那段内容时间
    int64_t a = INT64_MIN, b = INT64_MAX;
    for (auto* v : { &in.vocals, &in.beats }) for (auto& c : *v) { a = std::max (a, c.start); b = std::min (b, c.start + (int64_t) c.audio.size()); }
    if (a == INT64_MIN) { a = 0; b = 0; }
    if (! in.vocals.empty() || ! in.beats.empty())
        if (b - a < (int64_t) (5 * fs)) { o.error = "聲音不夠長（請播放 15 秒）"; return o; }
    auto trim = [&] (const Capture& c) { Stereo s; const size_t off = (size_t) (a - c.start), n = (size_t) (b - a);
        s.l.assign (c.audio.l.begin() + (long) off, c.audio.l.begin() + (long) (off + n)); s.r.assign (c.audio.r.begin() + (long) off, c.audio.r.begin() + (long) (off + n)); return s; };
    std::vector<Stereo> vin, bin;
    for (auto& c : in.vocals) vin.push_back (trim (c));
    for (auto& c : in.beats) bin.push_back (trim (c));
    o.m.seconds = (float) ((b - a) / fs);
    for (auto& v : vin) if (analysis::integrated (v, fs, true) < -55) { o.error = "沒聽到人聲"; return o; }

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
        auto dev = [&] (int k) { return lt[(size_t) k] - analysis::kRefVocal[k]; };
        // EQ：风格的方向 + 一半的「离专业人声多远」修正，幅度收窄（宁可少动，不要假）
        p.mud  = std::clamp (st.vMudG - st.vMatch * 0.4f * std::max (0.0f, dev (3)), -5.0f, 0.0f);
        p.pres = std::clamp (st.vPresG - st.vMatch * 0.5f * 0.5f * (dev (6) + dev (7)), -2.0f, 5.0f);
        p.air  = std::clamp (st.vAirG - st.vMatch * 0.5f * dev (8), -2.0f, 5.0f);
        p.warmth = std::clamp (st.vWarmth, 0.0f, 1.0f);
        // 圆润（动态去刺）：2–4 kHz 越比专业人声突出，开得越多
        p.smooth = std::clamp (0.3f + 0.08f * (0.5f * (dev (6) + dev (7))), 0.15f, 0.75f);
        const float sib = analysis::sibilance (v, fs);
        // 压缩：用真实链子模拟，两步牛顿把动态（P90-P10）拉到风格目标
        auto dry = p;
        const float s0 = analysis::spread (v, fs);
        // 只修正一半的差距：20 秒的学习段量出来的动态常比整首大（主歌 / 副歌交界），全修就压扁了
        const float spreadTarget = std::max (st.vSpread + kSpreadCal, s0 - 0.5f * (s0 - (st.vSpread + kSpreadCal)));
        // 学习段只有 20 秒，动态估计有噪声：步子小一点、最多 70 %，宁可少压（压扁就假了）
        // 至少 15 %：就算动态本来就稳，也让电平器轻轻带着，人声更有录音室的密度
        float k = std::clamp (0.25f + (s0 - spreadTarget) / 10.0f, 0.15f, 0.7f);
        for (int it = 0; it < 2; ++it)
        {
            dry.comp = k;
            const float s1 = analysis::spread (runVocal (v, dry, in.style, fs, in.bpm, a, nullptr), fs);
            k = std::clamp (k + (s1 - spreadTarget) / 10.0f, 0.15f, 0.7f);
        }
        p.comp = k;
        // 去齿音：模拟着二分，把齿音压到发行版人声的水平；本来就不重就只轻轻带一点
        float sibOut = sib; const float sibTarget = kStyleSib[std::clamp (in.style, 0, kNumStyles - 1)];
        {
            dry.comp = k; float lo = 0.05f, hi = 0.9f;
            dry.deess = lo; const float sLo = analysis::sibilance (runVocal (v, dry, in.style, fs, in.bpm, a, nullptr), fs);
            dry.deess = hi; const float sHi = analysis::sibilance (runVocal (v, dry, in.style, fs, in.bpm, a, nullptr), fs);
            if (sLo <= sibTarget + st.vDeessMargin * 0.5f) { p.deess = lo; sibOut = sLo; }
            else if (sLo - sHi < 1.0f) { p.deess = 0.25f; sibOut = sLo; }   // 去齿音压不下这种齿音：不硬开（只会把高频压暗）
            else
            {
                for (int it = 0; it < 5; ++it)
                {
                    dry.deess = 0.5f * (lo + hi);
                    sibOut = analysis::sibilance (runVocal (v, dry, in.style, fs, in.bpm, a, nullptr), fs);
                    (sibOut > sibTarget + st.vDeessMargin * 0.5f ? lo : hi) = dry.deess;
                }
                p.deess = 0.5f * (lo + hi);
            }
        }
        p.level = 0;
        vp.push_back (p);
        ClientOut co; co.client = in.vocals[i].client; co.set = in.vocals[i].current;
        co.note = fmt ("輸入 %+.1f dB（歸一到參考電平）\n", p.input) + fmt ("去渾濁 %.1f dB  存在感 %+.1f dB  空氣感 %+.1f dB\n", p.mud, p.pres, p.air)
                + fmt ("壓縮 %.0f%%（動態 %.1f LU → 目標 %.1f）\n", k * 100, s0, spreadTarget) + fmt ("去齒音 %.0f%%（齒音 %.1f → %.1f dB）  ", p.deess * 100, sib, sibOut) + fmt ("溫暖 %.0f%%  圓潤 %.0f%%\n", p.warmth * 100, p.smooth * 100);
        o.clients.push_back (co);
    }
    // 3) 伴奏：低切 + 固定的人声口袋 + 动态让位深度（风格）
    std::vector<BeatChain::P> bp;
    for (auto& c : in.beats)
    {
        auto p = BeatChain::fromSet (c.current);
        p.hpf = std::clamp (st.bHpf, 20.0f, 200.0f);
        p.pocket = std::clamp (st.bPocketStatic + (in.vocals.empty() ? 0.0f : st.bPocketLinked), -6.0f, 0.0f);
        p.glue = kStyleBeatGlue[std::clamp (in.style, 0, kNumStyles - 1)];
        p.unmask = in.vocals.empty() ? 0.0f : std::clamp (st.bDmax, 0.0f, 12.0f);
        bp.push_back (p);
        ClientOut co; co.client = c.client; co.set = c.current;
        co.note = fmt ("低切 %.0f Hz  人聲口袋 %.1f dB @ %.0f Hz\n", p.hpf, p.pocket, st.bPocketF) + fmt ("人聲唱的時候在對應頻段讓位，最多 %.1f dB\n", p.unmask);
        o.clients.push_back (co);
    }
    // 3b) 伴奏电平管理：整组伴奏太响（例如分轨直接相加、推子全开）就一起降下来，分轨之间的比例不变。
    //     人声电平是按伴奏定的，所以整个混音跟着回到正常的电平，总线不用硬压
    if (! bin.empty())
    {
        const float Lb = analysis::integrated (sum (bin), fs), off = Lb > -14.0f ? std::max (-18.0f, -16.0f - Lb) : 0.0f;
        if (off < -0.05f)
            for (size_t j = 0; j < bin.size(); ++j)
            {
                bp[j].level = std::clamp (bp[j].level + off, -24.0f, 12.0f);
            }
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
    auto setLevels = [&]
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
    };
    setLevels();
    // 伴奏让位深度：二分到「发行版那样的遮蔽比例」——不把伴奏挖空（那样很假），只让到和成品差不多
    if (! vin.empty() && ! bin.empty())
    {
        KeyStore keys; std::vector<Stereo> vtmp (vin.size());
        for (size_t i = 0; i < vin.size(); ++i) vtmp[i] = runVocal (vin[i], vp[i], in.style, fs, in.bpm, a, &keys);
        const Stereo vs = sum (vtmp);
        auto maskAt = [&] (float depth) { std::vector<Stereo> bt (bin.size());
            for (size_t j = 0; j < bin.size(); ++j) { auto q = bp[j]; q.unmask = depth; bt[j] = runBeat (bin[j], q, in.style, fs, a, &keys); }
            return analysis::maskedFraction (vs, sum (bt), fs); };
        float depth = 0;
        const float maskTarget = kStyleMask[std::clamp (in.style, 0, kNumStyles - 1)];
        if (maskAt (0) > maskTarget)
        {
            float lo = 0, hi = std::clamp (st.bDmax, 2.0f, 12.0f);
            if (maskAt (hi) > maskTarget) depth = hi;
            else { for (int it = 0; it < 6; ++it) { const float m = 0.5f * (lo + hi); (maskAt (m) > maskTarget ? lo : hi) = m; } depth = 0.5f * (lo + hi); }
        }
        for (auto& q : bp) q.unmask = depth;
        setLevels(); setLevels();
    }
    else setLevels();
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
        s[V_WARMTH] = p.warmth; s[V_SMOOTH] = p.smooth; s[V_LEVEL] = p.level;
        // 跟随伴奏：记住这段伴奏的电平，之后伴奏比它响 / 轻，人声就慢慢跟着推上 / 收回
        { const auto rr = bout.empty() || vout.size() <= i ? std::pair<float, float> { -100.0f, -100.0f } : rideReference (vout[i], bout, fs);   // 和插件一样：比处理后的伴奏
          s[V_RIDEREF] = clampParam (V_RIDEREF, rr.first); s[V_RIDEV] = clampParam (V_RIDEV, rr.second); } s[V_RIDE] = kStyleRide[std::clamp (in.style, 0, kNumStyles - 1)];
        o.clients[i].note += bin.empty() ? fmt ("電平 %+.1f dB（沒連上伴奏，保持原響度）\n", p.level)
                                         : fmt ("電平 %+.1f dB  人聲比伴奏 %+.1f dB（風格 %+.1f）\n", p.level, o.m.vbrAfter, st.vbr)
                                           + fmt ("跟隨伴奏 %.0f%%：副歌伴奏變響時人聲跟著推上去\n", s[V_RIDE] * 100);
    }
    for (size_t j = 0; j < bin.size(); ++j)
    {
        auto& s = o.clients[vin.size() + j].set; const auto& p = bp[j];
        s[B_HPF] = p.hpf; s[B_POCKET] = p.pocket; s[B_UNMASK] = p.unmask; s[B_GLUE] = p.glue; s[B_LEVEL] = p.level;
        o.clients[vin.size() + j].note = fmt ("低切 %.0f Hz  人聲口袋 %.1f dB @ %.0f Hz\n", p.hpf, p.pocket, st.bPocketF)
                                       + fmt ("人聲唱的時候在對應頻段讓位，最多 %.1f dB\n", p.unmask)
                                       + (std::abs (p.level - in.beats[j].current[B_LEVEL]) > 0.05f ? fmt ("伴奏太響：整組降 %.1f dB，分軌比例不變\n", in.beats[j].current[B_LEVEL] - p.level) : std::string());
        if (! vin.empty()) o.clients[vin.size() + j].note += fmt ("人聲被蓋住的部分 %.0f%% → %.0f%%\n", o.m.maskBefore * 100, o.m.maskAfter * 100);
    }
    // 5) 总线：音色向风格曲线靠、粘合到风格的平均压缩量、推到风格响度（-1 dBTP 天花板）
    if (in.hasBus && (! vout.empty() || ! bout.empty()))
    {
        std::vector<Stereo> all = vout; all.insert (all.end(), bout.begin(), bout.end());
        const Stereo mix = sum (all);
        auto p = BusChain::fromSet (in.busCurrent);
        const auto lt = analysis::octaveLtas (mix, fs);
        // 期望的音色改动：只往风格曲线靠一部分，保留这首歌自己的音色
        //   低频修五成（±3），中低频（125–250 Hz 发闷 / 发虚，最影响干不干净）修六成（±3），高频修三成（±2）
        auto dev2 = [&] (int k) { return 0.5f * ((st.busCurve[(size_t) k] - lt[(size_t) k]) + (st.busCurve[(size_t) k + 1] - lt[(size_t) k + 1])); };
        //   风格里各首歌本来就差很多的频段（训练时量的四分位距）修得更少
        const auto& iqr = kStyleCurveIqr[(size_t) std::clamp (in.style, 0, kNumStyles - 1)];
        auto trust = [] (float q) { return std::clamp (2.5f / std::max (q, 0.1f), 0.3f, 1.0f); };
        const float wantLo = std::clamp (0.5f * trust (iqr[0]) * dev2 (0), -3.0f, 3.0f), wantMid = std::clamp (0.6f * trust (iqr[1]) * dev2 (2), -3.0f, 3.0f),
                    wantHi = std::clamp (0.3f * trust (iqr[2]) * dev2 (8), -2.0f, 2.0f);
        p.low = wantLo; p.mid = wantMid; p.high = wantHi;
        p.width = kStyleWidth[std::clamp (in.style, 0, kNumStyles - 1)]; p.ceiling = std::clamp (st.ceiling, -3.0f, 0.0f); p.drive = 0;
        p.lift = kStyleLift[std::clamp (in.style, 0, kNumStyles - 1)];
        float lo = 0, hi = 1, gr = 0;                    // 粘合：二分到风格的平均压缩量
        for (int it = 0; it < 7; ++it) { p.glue = 0.5f * (lo + hi); runBus (mix, p, in.style, fs, &gr); (gr < st.glueGr ? lo : hi) = p.glue; }
        p.glue = 0.5f * (lo + hi);
        lo = -12; hi = 18;                               // 推子：二分到风格响度（太响的时候也会往下收）
        Stereo out;
        for (int it = 0; it < 9; ++it)
        {
            p.drive = 0.5f * (lo + hi); out = runBus (mix, p, in.style, fs, &gr);
            (analysis::integrated (out, fs) < st.lufsStyle ? lo : hi) = p.drive;
        }
        p.drive = 0.5f * (lo + hi); out = runBus (mix, p, in.style, fs, &gr);
        // 闭环：提升（低频泛音、压缩补偿）、磁带、限幅也会改音色。量出总线实际改了多少，把三个 EQ 旋钮补到期望值
        for (int pass = 0; pass < 2; ++pass)
        {
            const auto lo2 = analysis::octaveLtas (out, fs);
            auto got = [&] (int k) { return 0.5f * ((lo2[(size_t) k] - lt[(size_t) k]) + (lo2[(size_t) k + 1] - lt[(size_t) k + 1])); };
            p.low  = std::clamp (p.low  + wantLo  - got (0), -6.0f, 6.0f);
            p.mid  = std::clamp (p.mid  + wantMid - got (2), -6.0f, 6.0f);
            p.high = std::clamp (p.high + wantHi  - got (8), -6.0f, 6.0f);
            lo = p.drive - 3; hi = p.drive + 3;                // EQ 变了响度也变：在附近重新找推子
            for (int it = 0; it < 6; ++it)
            {
                p.drive = std::clamp (0.5f * (lo + hi), -12.0f, 18.0f); out = runBus (mix, p, in.style, fs, &gr);
                (analysis::integrated (out, fs) < st.lufsStyle ? lo : hi) = p.drive;
            }
            p.drive = std::clamp (0.5f * (lo + hi), -12.0f, 18.0f); out = runBus (mix, p, in.style, fs, &gr);
        }
        o.m.busLufs = analysis::integrated (out, fs); o.m.busTp = analysis::truePeakDb (out); o.m.glueGr = gr;
        ClientOut co; co.client = in.busClient; co.set = in.busCurrent;
        co.set[M_LOW] = p.low; co.set[M_HIGH] = p.high; co.set[M_MID] = p.mid; co.set[M_GLUE] = p.glue; co.set[M_LIFT] = p.lift; co.set[M_WIDTH] = p.width; co.set[M_DRIVE] = p.drive; co.set[M_CEILING] = p.ceiling;
        co.note = fmt ("音色往風格曲線靠：低頻 %+.1f  中低頻 %+.1f  高頻 %+.1f dB\n", p.low, p.mid, p.high) + fmt ("提升 %.0f%%（低頻壓緊加泛音、中頻磁帶密度、高頻動態空氣、加寬）\n", p.lift * 100) + fmt ("粘合 %.0f%%（平均 %.1f dB）\n", p.glue * 100, gr)
                + fmt ("推子 %+.1f dB → %.1f LUFS，", p.drive, o.m.busLufs) + fmt ("峰值 %.1f dBTP\n", o.m.busTp);
        o.clients.push_back (co);
    }
    o.ok = true;
    o.summary = std::string ("混音完成 · ") + st.name;
    return o;
}
} // namespace mm::automix
