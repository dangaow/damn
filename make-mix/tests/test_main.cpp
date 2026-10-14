// mm_tests：JUCE UnitTestRunner 的入口，外加 WP0 的基础测试（参数表、结构体转换、风格表、Shared.h 的工具）。
//   mm_tests [--category foundation|dsp|hub|automix|analysis] [--test "Name"] [--seed N] [--list]
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "../Source/Params.h"
#include "../Source/Styles.h"
#include "../Source/PluginProcessor.h"
#include <iostream>
#include <set>
#include <thread>

namespace
{
using namespace mm;

const char* const kExpectedIds[] = {
    "v_input", "v_hpf", "v_hpfSlope", "v_lowF", "v_lowG", "v_midF", "v_midG", "v_presF", "v_presG", "v_airF", "v_airG", "v_lpf",
    "v_comp", "v_compAtk", "v_compRel", "v_deess", "v_deessF", "v_warmth", "v_echo", "v_echoTime", "v_echoFb", "v_space",
    "v_spaceDecay", "v_preDelay", "v_wetDuck", "v_level",
    "b_hpf", "b_monoBelow", "b_pocketF", "b_pocketG", "b_pocketQ", "b_unmask", "b_sens", "b_focusLo", "b_focusHi", "b_release",
    "b_side", "b_ride", "b_key", "b_level",
    "m_lowG", "m_lowMidG", "m_presG", "m_airG", "m_lpf", "m_glue", "m_glueRatio", "m_glueAtk", "m_glueRel", "m_glueScHpf",
    "m_width", "m_monoBelow", "m_drive", "m_clip", "m_ceiling", "m_target",
};

class FoundationTests final : public juce::UnitTest
{
public:
    FoundationTests() : juce::UnitTest ("Foundation (Shared / Params / Styles)", "foundation") {}

    void runTest() override
    {
        paramTable();
        conversions();
        formatting();
        helpers();
        ids();
        seqBox();
        compProfile();
        capture();
        styles();
    }

private:
    bool inRange (int pidx, float v) { return v >= kParams[pidx].min - 1.0e-4f && v <= kParams[pidx].max + 1.0e-4f; }

    void paramTable()
    {
        beginTest ("parameter table matches 6.1");
        expectEquals (kNumParams, 56);
        expectEquals ((int) kPIdxCount, kNumParams);
        expectEquals ((int) (sizeof (kExpectedIds) / sizeof (kExpectedIds[0])), kNumParams);
        std::set<std::string> seen;
        int perRole[3] = { 0, 0, 0 };
        for (int i = 0; i < kNumParams; ++i)
        {
            const auto& p = kParams[i];
            const juce::String id (p.id);
            expect (id == kExpectedIds[i], "PIdx order: " + id);
            expect (seen.insert (p.id).second, "duplicate id " + id);
            const char prefix = p.role == Role::Vocal ? 'v' : p.role == Role::Beat ? 'b' : 'm';
            expect (id[0] == prefix && id[1] == '_', "role prefix " + id);
            ++perRole[(int) p.role];
            expect (p.min < p.max, "range " + id);
            expect (p.def >= p.min && p.def <= p.max, "default in range " + id);
            expect (p.centre <= 0 || (p.centre > p.min && p.centre < p.max), "skew centre inside range " + id);
            if (p.step > 0)
            {
                const float k = (p.def - p.min) / p.step;
                expectWithinAbsoluteError (k, std::round (k), 1.0e-3f, "default on step grid " + id);
            }
            if (p.kind == PKind::Choice)
            {
                expect (p.choices != nullptr && p.numChoices >= 2, "choices " + id);
                expectEquals ((int) p.max, p.numChoices - 1);
                expectEquals ((float) (int) p.def, p.def);
            }
            const bool shouldAutomate = ! (i == v_hpfSlope || i == b_key || i == m_target);
            expect (p.automatable == shouldAutomate, "automatable flag " + id);
            expect (p.name != nullptr && p.name[0] != 0 && p.unit != nullptr, "name / unit " + id);
        }
        expectEquals (perRole[0], 26);
        expectEquals (perRole[1], 14);
        expectEquals (perRole[2], 16);
    }

    void conversions()
    {
        beginTest ("struct <-> raw / ParamSet");
        float raw[kPIdxCount];
        for (int i = 0; i < kNumParams; ++i) raw[i] = kParams[i].def;
        expect (vocalFrom (raw) == VocalParams {}, "VocalParams{} = table defaults");
        expect (beatFrom (raw) == BeatParams {}, "BeatParams{} = table defaults");
        expect (busFrom (raw) == BusParams {}, "BusParams{} = table defaults");
        expect (vocalFrom (ParamSet {}) == VocalParams {});
        expect (beatFrom (ParamSet {}) == BeatParams {});
        expect (busFrom (ParamSet {}) == BusParams {});

        VocalParams v; v.lowG = -5.5f; v.echoTime = 4; v.hpfSlope = 0; v.level = 3.2f; v.spaceDecay = 2.25f;
        BeatParams b;  b.key = 2; b.unmask = 0.9f; b.focusHi = 7000;
        BusParams m;   m.drive = 7.4f; m.width = 1.06f; m.target = -14;
        expect (vocalFrom (toSet (v)) == v);
        expect (beatFrom (toSet (b)) == b);
        expect (busFrom (toSet (m)) == m);
        const auto vs = toSet (v), bs = toSet (b), ms = toSet (m);
        expectEquals ((int) vs.values.size(), 26);
        expectEquals ((int) bs.values.size(), 14);
        expectEquals ((int) ms.values.size(), 16);
        expect (vs.values.front().first == "v_input" && vs.values.back().first == "v_level");
        expect (bs.values.front().first == "b_hpf" && bs.values.back().first == "b_level");
        expect (ms.values.front().first == "m_lowG" && ms.values.back().first == "m_target");

        // 原始数组按 PIdx 取值，和 ParamSet 的路径一致
        float raw2[kPIdxCount];
        for (int i = 0; i < kNumParams; ++i) raw2[i] = kParams[i].def;
        for (const auto& [id, val] : vs.values) raw2[paramIndex (id)] = val;
        expect (vocalFrom (raw2) == v);

        ParamSet partial; partial.set ("v_lowG", -1.0f); partial.set ("not_a_param", 5.0f);
        VocalParams expected; expected.lowG = -1.0f;
        expect (vocalFrom (partial) == expected, "missing values fall back to defaults");

        raw[v_echoTime] = 3.4f; raw[b_key] = -2.0f;
        expectEquals (vocalFrom (raw).echoTime, 3);
        expectEquals (beatFrom (raw).key, 0);
        raw[v_echoTime] = 9.0f;
        expectEquals (vocalFrom (raw).echoTime, 5);
    }

    void formatting()
    {
        beginTest ("formatValue");
        auto is = [this] (int pidx, float v, const char* want)
        {
            const auto got = formatValue (pidx, v);
            expect (got == want, juce::String (kParams[pidx].id) + " " + juce::String (v) + " -> \"" + juce::String (got) + "\", want \"" + want + "\"");
        };
        is (v_presG, 2.0f, "+2.0 dB");
        is (v_lowG, -3.0f, "-3.0 dB");
        is (v_input, -0.01f, "0.0 dB");
        is (v_hpf, 120.0f, "120 Hz");
        is (v_presF, 3800.0f, "3.8 kHz");
        is (v_airF, 11000.0f, "11.0 kHz");
        is (v_comp, 0.45f, "45%");
        is (m_width, 1.0f, "100%");
        is (v_echoTime, 2.0f, "1/8D");
        is (v_hpfSlope, 0.0f, "12 dB");
        is (b_key, 2.0f, "SIDECHAIN");
        is (v_lpf, 22000.0f, "OFF");
        is (v_lpf, 9000.0f, "9.0 kHz");
        is (b_hpf, 10.0f, "OFF");
        is (b_monoBelow, 0.0f, "OFF");
        is (m_ceiling, -1.0f, "-1.0 dBTP");
        is (m_target, -9.0f, "-9.0 LUFS");
        is (m_glueRatio, 2.5f, "2.5:1");
        is (b_pocketQ, 1.0f, "1.00");
        is (v_compAtk, 15.0f, "15 ms");
        is (v_compAtk, 1.5f, "1.5 ms");
        is (v_spaceDecay, 1.2f, "1.2 s");
        is (v_spaceDecay, 0.35f, "0.35 s");
        is (v_wetDuck, 6.0f, "6.0 dB");
        is (m_drive, 7.4f, "7.4 dB");
        for (int i = 0; i < kNumParams; ++i)
            for (float v : { kParams[i].min, kParams[i].def, kParams[i].max })
                expect (! formatValue (i, v).empty(), juce::String ("empty text for ") + kParams[i].id);
        expect (formatValue (-1, 0).empty() && formatValue (kNumParams, 0).empty());
    }

    void helpers()
    {
        beginTest ("Shared helpers, clampParam, paramIndex, isLevelOwning");
        expectEquals (latencyFor (44100, 512), 1024);
        expectEquals (latencyFor (48000, 736), 1024);
        expectEquals (latencyFor (48000, 737), 2048);
        expectEquals (latencyFor (48000, 4096), 2048);
        expectEquals (latencyFor (96000, 1024), 2048);
        expectEquals (latencyFor (96000, 1600), 4096);
        expectEquals (latencyFor (192000, 512), 4096);
        expectEquals (latencyFor (192000, 8192), 8192);
        expectEquals (keyHop (48000), 128);
        expectEquals (keyHop (96000), 256);
        expectEquals (keyLookahead (192000), 512);
        expectEquals (rateFactor (50000), 1);
        expectEquals (rateFactor (88200), 2);
        expectWithinAbsoluteError (dbToGain (-6.0206f), 0.5f, 1.0e-4f);
        expectWithinAbsoluteError (gainToDb (0.1f), -20.0f, 1.0e-4f);
        expectWithinAbsoluteError (powToDb (0.01f), -20.0f, 1.0e-4f);
        expectWithinAbsoluteError (powToDb (0.0f), -120.0f, 1.0e-3f);
        expectWithinAbsoluteError (gainToDb (0.0f), -180.0f, 1.0e-3f);
        for (int k = 0; k < kAnaBands; ++k)
            expect (kAnaEdgesHz[k] < kAnaCentresHz[k] && kAnaCentresHz[k] < kAnaEdgesHz[k + 1], "ERB centre inside its band");

        expectEquals (paramIndex ("v_input"), (int) v_input);
        expectEquals (paramIndex ("m_target"), (int) m_target);
        expectEquals (paramIndex ("nope"), -1);
        expectWithinAbsoluteError (clampParam (v_level, 30.0f), 24.0f, 1.0e-6f);
        expectWithinAbsoluteError (clampParam (v_level, 1.234f), 1.2f, 1.0e-5f);
        expectWithinAbsoluteError (clampParam (v_echoTime, 2.6f), 3.0f, 1.0e-6f);
        expectWithinAbsoluteError (clampParam (b_monoBelow, 120.4f), 120.0f, 1.0e-6f);
        expectWithinAbsoluteError (clampParam (v_presG, std::nanf ("")), 2.5f, 1.0e-6f);
        int owning = 0;
        for (int i = 0; i < kNumParams; ++i) owning += isLevelOwning (i) ? 1 : 0;
        expectEquals (owning, 6);
        expect (isLevelOwning (v_input) && isLevelOwning (v_level) && isLevelOwning (b_level) && isLevelOwning (m_drive)
                && isLevelOwning (m_glue) && isLevelOwning (m_target));

        beginTest ("ParamSet");
        ParamSet s;
        expect (s.find ("a") == nullptr);
        s.set ("a", 1.0f); s.set ("b", 2.0f); s.set ("a", 3.0f);
        expectEquals ((int) s.values.size(), 2);
        expect (s.find ("a") != nullptr && *s.find ("a") == 3.0f);
        expect (s.values[0].first == "a", "set keeps the original order");
    }

    void ids()
    {
        beginTest ("InstanceId");
        const InstanceId a { 0x0123456789abcdefull, 0xfedcba9876543210ull };
        expect (a.toString() == "0123456789abcdeffedcba9876543210");
        expect (InstanceId::fromString (a.toString()) == a);
        expect (InstanceId::fromString ("0123456789ABCDEFFEDCBA9876543210") == a);
        expect (InstanceId::fromString ("").isNull());
        expect (InstanceId::fromString ("0123456789abcdeffedcba987654321").isNull());
        expect (InstanceId::fromString ("0123456789abcdeffedcba987654321g").isNull());
        expect (InstanceId {}.isNull() && ! a.isNull());
        const InstanceId b { a.hi, a.lo + 1 };
        expect (a < b && ! (b < a) && a != b);
        std::set<std::pair<uint64_t, uint64_t>> seen;
        for (int i = 0; i < 1000; ++i)
        {
            const auto r = InstanceId::random();
            expect (! r.isNull());
            seen.insert ({ r.hi, r.lo });
        }
        expectEquals ((int) seen.size(), 1000);
    }

    void seqBox()
    {
        beginTest ("SeqBox");
        SeqBox<CompProfile> box;
        CompProfile out; out.crestDb = 99.0f;
        uint32_t seq = 0;
        expect (! box.read (out, seq), "nothing written yet");
        expectEquals (out.crestDb, 99.0f);
        CompProfile in = defaultCompProfile (Role::Vocal); in.crestDb = 7.5f; in.valid = 1;
        box.write (in);
        expect (box.read (out, seq));
        expectEquals (out.crestDb, 7.5f);
        expect (std::memcmp (&out, &in, sizeof (CompProfile)) == 0);
        expect (! box.read (out, seq), "unchanged version reads false");

        // 并发：写端不停写“所有字段同一个数”，读端读到的必须是完整的一份
        struct Pod { std::array<float, 64> v; };
        SeqBox<Pod> pbox;
        std::atomic<bool> stop { false };
        std::atomic<int> torn { 0 }, reads { 0 };
        std::thread reader ([&]
        {
            Pod p {}; uint32_t s = 0;
            while (! stop.load (std::memory_order_relaxed))
                if (pbox.read (p, s))
                {
                    reads.fetch_add (1, std::memory_order_relaxed);
                    for (float x : p.v) if (x != p.v[0]) { torn.fetch_add (1); break; }
                }
        });
        Pod p;
        for (int k = 1; k < 50000000 && reads.load (std::memory_order_relaxed) < 20000; ++k) { p.v.fill ((float) k); pbox.write (p); }
        stop = true;
        reader.join();
        expectEquals (torn.load(), 0);
        expect (reads.load() >= 1000, "reader saw enough versions");
        logMessage ("SeqBox concurrent reads: " + juce::String (reads.load()));
    }

    void compProfile()
    {
        beginTest ("defaultCompProfile");
        for (auto role : { Role::Vocal, Role::Beat, Role::Bus })
        {
            const auto p = defaultCompProfile (role);
            expectEquals ((int) p.valid, 0);
            expectEquals (p.levelLoDb, -80.0f);
            expectEquals (p.crestDb, 12.0f);
            double sum = 0, mean = 0, meanPow = 0, sumP = 0;
            for (size_t i = 0; i < p.levelW.size(); ++i)
            {
                const double c = p.levelLoDb + (double) i + 0.5;
                sum += p.levelW[i]; mean += p.levelW[i] * c; meanPow += p.levelW[i] * std::pow (10.0, c / 10.0);
            }
            expectWithinAbsoluteError (sum, 1.0, 1.0e-5);
            expectWithinAbsoluteError (mean, -22.0, 0.05);
            for (float d : p.ltasDb) { expect (std::isfinite (d)); sumP += std::pow (10.0, d / 10.0); }
            expectWithinAbsoluteError (10.0 * std::log10 (sumP), 10.0 * std::log10 (meanPow), 0.01);
            const std::array<float, 9> sat { 0.0f, 0.1f, 0.25f, 0.45f, 0.7f, 0.95f, 1.2f, 1.45f, 1.7f };
            expect (p.satGainDb == sat);
        }
        // 粉噪参考：人声 150 Hz 以上 −1.5 dB/oct
        const auto v = defaultCompProfile (Role::Vocal);
        auto pink = [] (const CompProfile& p, int k)
        {
            return p.ltasDb[(size_t) k] - 10.0 * std::log10 (kAnaEdgesHz[k + 1] - kAnaEdgesHz[k]) + 10.0 * std::log10 (0.2316 * kAnaCentresHz[k]);
        };
        expectWithinAbsoluteError (pink (v, 22) - pink (v, 12), -1.5 * std::log2 (kAnaCentresHz[22] / kAnaCentresHz[12]), 0.01);
        expect (pink (v, 0) < pink (v, 3) - 10.0, "vocal default rolls off below 120 Hz");
        const auto b = defaultCompProfile (Role::Beat), m = defaultCompProfile (Role::Bus);
        expect (b.ltasDb == m.ltasDb && b.ltasDb != v.ltasDb);
        expect (pink (b, 1) > pink (b, 12) + 8.0, "beat default is bass-heavy (TRAP bus curve)");
    }

    void capture()
    {
        beginTest ("CaptureData");
        const CaptureData a (48000.0, 2, true, 30.0);
        expectEquals ((int) a.capacity, 1440000);
        expectEquals ((int) a.ch[0].size(), 1440000);
        expectEquals ((int) a.ch[1].size(), 1440000);
        expectEquals ((int) a.key.size(), 1440000);
        const CaptureData b (44100.0, 1, false, 0.5);
        expectEquals ((int) b.capacity, 22050);
        expectEquals (b.numCh, 1);
        expect (b.ch[1].empty() && b.key.empty());
        expectEquals (b.written.load(), (int64_t) 0);
    }

    void styles()
    {
        beginTest ("style table");
        const char* names[] = { "TRAP", "DRILL", "MELODIC RAP", "BOOM BAP", "R&B", "MANDOPOP 华语流行", "BALLAD 华语抒情",
                                "LO-FI", "POP", "ROCK", "EDM", "AFROBEATS" };
        std::set<std::string> keys;
        for (int i = 0; i < kNumStyles; ++i)
        {
            expect (std::string (kStyles[(size_t) i].name) == names[i], juce::String ("style order ") + juce::String (i));
            expect (keys.insert (kStyles[(size_t) i].key).second);
            expectEquals (styleIndex (kStyles[(size_t) i].key), i);
            expectEquals (styleIndex (kStyles[(size_t) i].name), i);
        }
        expectEquals (styleIndex ("Melodic Rap"), 2);
        expectEquals (styleIndex ("nope"), -1);
        expect (&style (-3) == &kStyles[0] && &style (99) == &kStyles[11]);

        for (const auto& s : kStyles)
        {
            const juce::String n (juce::CharPointer_UTF8 (s.name));
            auto in = [&] (int pidx, float v, const char* what) { expect (inRange (pidx, v), n + ": " + what + " = " + juce::String (v)); };
            in (v_lowF, s.vMudF, "vMudF");   in (v_lowG, s.vMudG, "vMudG");
            in (v_presF, s.vPresF, "vPresF"); in (v_presG, s.vPresG, "vPresG");
            in (v_airF, s.vAirF, "vAirF");   in (v_airG, s.vAirG, "vAirG");
            in (v_hpf, s.vHpfMin, "vHpfMin"); in (v_hpf, s.vHpfMax * 1.3f, "1.3 vHpfMax");
            expect (s.vHpfMin < s.vHpfMax && s.vHpfK > 0);
            expect (s.vHpfSlope == 12 || s.vHpfSlope == 24);
            in (v_lpf, s.vLpf, "vLpf");
            expect (s.vMatch > 0 && s.vMatch <= 1);
            expect (s.vSpread > 0 && s.vSibTarget > 0 && s.vDeessMargin >= 0);
            in (v_warmth, s.vWarmth + 0.1f, "vWarmth + adlib");
            in (v_spaceDecay, s.vRevDecay, "vRevDecay"); in (v_preDelay, s.vPreDelayMs, "vPreDelayMs");
            expect (s.vEchoTime >= 0 && s.vEchoTime <= 5);
            in (v_echoFb, s.vEchoFb, "vEchoFb"); in (v_wetDuck, s.vWetDuck, "vWetDuck");
            expect (s.vRevWdr < 0 && s.vEchoWdr < 0 && s.dblOffset < 0 && s.adlibOffset < 0);
            in (v_level, s.vbr, "vbr");
            in (b_unmask, s.bDmax / 9.0f, "bDmax / 9"); in (b_sens, s.bSens, "bSens");
            in (b_focusLo, s.bLo, "bLo"); in (b_focusHi, s.bHi, "bHi"); expect (s.bLo < s.bHi);
            in (b_release, s.bRelMs, "bRelMs"); in (b_side, s.bSide, "bSide"); in (b_ride, s.bRide, "bRide");
            in (b_pocketF, s.bPocketF, "bPocketF"); in (b_pocketG, s.bPocketStatic, "bPocketStatic"); in (b_pocketG, s.bPocketLinked, "bPocketLinked");
            in (b_hpf, s.bHpf, "bHpf"); in (b_monoBelow, s.bMono, "bMono");
            in (m_target, s.lufsStyle, "lufsStyle"); in (m_target, s.lufsLoud, "lufsLoud"); expect (s.lufsLoud > s.lufsStyle);
            in (m_ceiling, s.ceiling, "ceiling");
            in (m_glueRatio, s.glueRatio, "glueRatio"); in (m_glueAtk, s.glueAtk, "glueAtk"); in (m_glueRel, s.glueRel, "glueRel");
            in (m_glueScHpf, s.glueScHpf, "glueScHpf"); in (m_clip, s.clipDb, "clipDb"); in (m_monoBelow, s.busMono, "busMono");
            in (m_lpf, s.busLpf, "busLpf");
            expect (s.glueGr > 0 && s.maxLimGr > 0 && s.width < 0);
            for (float c : s.busCurve) expect (std::isfinite (c));
            expectEquals (s.busCurve[5], 0.0f);
        }

        // 7.3：TRAP 的 standalone 默认值 = 参数默认值（v_space / v_echo 依赖 WP1 的校准常数，这里不查）
        const auto& t = kStyles[0];
        auto same = [this] (int pidx, float v) { expectWithinAbsoluteError (v, kParams[pidx].def, 0.006f, juce::String ("TRAP default ") + kParams[pidx].id); };
        same (v_hpf, 0.5f * (t.vHpfMin + t.vHpfMax));
        same (v_hpfSlope, (float) hpfSlopeChoice (t.vHpfSlope));
        same (v_lpf, t.vLpf);
        same (v_lowF, t.vMudF); same (v_lowG, t.vMudG); same (v_presF, t.vPresF); same (v_presG, t.vPresG);
        same (v_airF, t.vAirF); same (v_airG, t.vAirG);
        same (v_comp, std::clamp (0.5f + 0.1f * (5.5f - t.vSpread), 0.2f, 0.8f));
        same (v_deess, std::clamp (0.45f - 0.05f * t.vDeessMargin, 0.1f, 0.8f));
        same (v_warmth, t.vWarmth); same (v_echoTime, (float) t.vEchoTime); same (v_echoFb, t.vEchoFb);
        same (v_spaceDecay, t.vRevDecay); same (v_preDelay, t.vPreDelayMs); same (v_wetDuck, t.vWetDuck);
        same (b_hpf, t.bHpf); same (b_monoBelow, t.bMono); same (b_pocketF, t.bPocketF); same (b_pocketG, t.bPocketLinked);
        same (b_unmask, t.bDmax / 9.0f); same (b_sens, t.bSens); same (b_focusLo, t.bLo); same (b_focusHi, t.bHi);
        same (b_release, t.bRelMs); same (b_side, t.bSide); same (b_ride, t.bRide);
        same (m_lpf, t.busLpf); same (m_glueRatio, t.glueRatio); same (m_glueAtk, t.glueAtk); same (m_glueRel, t.glueRel);
        same (m_glueScHpf, t.glueScHpf); same (m_monoBelow, t.busMono); same (m_ceiling, t.ceiling); same (m_target, t.lufsStyle);
    }
};

FoundationTests foundationTests;

// 处理器层面的约定（对 WP2 的完整实现也成立）：参数布局、恒定延迟、总线、状态往返
class ProcessorContractTests final : public juce::UnitTest
{
public:
    ProcessorContractTests() : juce::UnitTest ("Processor contracts", "foundation") {}

    void runTest() override
    {
        beginTest ("APVTS layout comes from kParams");
        {
            MakeMixProcessor p;
            const auto& params = p.getParameters();
            expectEquals (params.size(), kNumParams);
            for (int i = 0; i < kNumParams; ++i)
            {
                auto* rp = p.apvts.getParameter (kParams[i].id);
                expect (rp != nullptr, juce::String ("missing ") + kParams[i].id);
                if (rp == nullptr) continue;
                const float def = rp->convertFrom0to1 (rp->getDefaultValue());
                expectWithinAbsoluteError (def, kParams[i].def, 1.0e-3f * (kParams[i].max - kParams[i].min), juce::String ("default ") + kParams[i].id);
                expect (rp->isAutomatable() == kParams[i].automatable, juce::String ("automatable ") + kParams[i].id);
                expect (rp->getText (rp->getValue(), 64) == juce::String (formatValue (i, def)), juce::String ("text ") + kParams[i].id);
            }
            expectEquals (p.getNumPrograms(), kNumStyles);
            expect (p.getProgramName (5) == juce::String (juce::CharPointer_UTF8 ("MANDOPOP 华语流行")));
            expectEquals (p.getBusCount (true), 2);
            expectEquals (p.getBusCount (false), 1);
            expect (p.getBus (true, 1)->getName() == "Vox Key");
        }

        beginTest ("latency = latencyFor (fs, maxBlock), set in prepareToPlay");
        for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (int block : { 1, 64, 512, 1024, 4096 })
            {
                MakeMixProcessor p;
                p.setRateAndBufferSizeDetails (fs, block);
                p.prepareToPlay (fs, block);
                expectEquals (p.getLatencySamples(), latencyFor (fs, block));
                juce::AudioBuffer<float> buf (p.getTotalNumInputChannels(), block * 3 + 7);   // 比 maxBlock 大的块要分段
                buf.clear();
                buf.setSample (0, 0, 1.0f);
                juce::MidiBuffer midi;
                p.processBlock (buf, midi);
                bool finite = true;
                for (int c = 0; c < buf.getNumChannels(); ++c)
                    for (int i = 0; i < buf.getNumSamples(); ++i) finite = finite && std::isfinite (buf.getSample (c, i));
                expect (finite);
                juce::AudioBuffer<float> empty (p.getTotalNumInputChannels(), 0);
                p.processBlock (empty, midi);                                                  // 0 样本的块
            }

        beginTest ("state round trip keeps role / group / style / params");
        {
            MakeMixProcessor a;
            a.setRole (Role::Beat, true);
            a.setGroup (2);
            a.setStyle (5, false);
            auto* pq = a.apvts.getParameter ("b_pocketQ");
            pq->setValueNotifyingHost (pq->convertTo0to1 (2.2f));
            juce::MemoryBlock mb;
            a.getStateInformation (mb);
            MakeMixProcessor b;
            b.setStateInformation (mb.getData(), (int) mb.getSize());
            expect (b.role() == Role::Beat);
            expectEquals (b.group(), 2);
            expectEquals (b.style(), 5);
            expectWithinAbsoluteError (b.apvts.getRawParameterValue ("b_pocketQ")->load(), 2.2f, 1.0e-3f);
            expect (! b.instanceId().isNull());
            const auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
            expect (xml != nullptr && xml->hasTagName ("MAKEMIX") && xml->getIntAttribute ("stateVersion") == 1);
            expect (xml != nullptr && InstanceId::fromString (xml->getStringAttribute ("id").toStdString()) == a.instanceId());
            MakeMixProcessor c;
            const char junk[] = "not a state";
            c.setStateInformation (junk, (int) sizeof (junk));                                // 不认识的状态被忽略
            expect (c.role() == Role::Vocal);
        }

        beginTest ("LOAD DEFAULTS keeps the level-owning parameters");
        {
            MakeMixProcessor p;
            auto set = [&p] (const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
            auto get = [&p] (const char* id) { return p.apvts.getRawParameterValue (id)->load(); };
            set ("v_input", 6.0f); set ("v_level", -3.0f); set ("v_presG", -4.0f);
            p.loadStyleDefaults();
            expectWithinAbsoluteError (get ("v_input"), 6.0f, 1.0e-3f);
            expectWithinAbsoluteError (get ("v_level"), -3.0f, 1.0e-3f);
            expect (std::abs (get ("v_presG") + 4.0f) > 0.5f, "non-level parameter reset to the style default");
        }
    }
};

ProcessorContractTests processorContractTests;

struct Runner final : juce::UnitTestRunner
{
    void logMessage (const juce::String& m) override { std::cout << m << std::endl; }
};
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;                  // Hub 的 Timer 需要 MessageManager
    juce::String category, testName;
    juce::int64 seed = 0x4d4d4958;                         // 固定种子，结果可复现
    bool list = false;
    for (int a = 1; a < argc; ++a)
    {
        const juce::String arg (argv[a]);
        if ((arg == "--category" || arg == "-c") && a + 1 < argc) category = argv[++a];
        else if ((arg == "--test" || arg == "-t") && a + 1 < argc) testName = argv[++a];
        else if (arg == "--seed" && a + 1 < argc) seed = juce::String (argv[++a]).getLargeIntValue();
        else if (arg == "--list") list = true;
        else { std::cerr << "usage: mm_tests [--category C] [--test NAME] [--seed N] [--list]\n"; return 2; }
    }
    if (list)
    {
        for (auto* t : juce::UnitTest::getAllTests()) std::cout << "[" << t->getCategory() << "] " << t->getName() << "\n";
        return 0;
    }

    Runner runner;
    runner.setAssertOnFailure (false);
    if (testName.isNotEmpty())
    {
        juce::Array<juce::UnitTest*> chosen;
        for (auto* t : juce::UnitTest::getAllTests()) if (t->getName() == testName) chosen.add (t);
        if (chosen.isEmpty()) { std::cerr << "no test named " << testName << "\n"; return 2; }
        runner.runTests (chosen, seed);
    }
    else if (category.isNotEmpty()) runner.runTestsInCategory (category, seed);
    else runner.runAllTests (seed);

    int passes = 0, failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        if (auto* r = runner.getResult (i)) { passes += r->passes; failures += r->failures; }
    std::cout << "mm_tests: " << runner.getNumResults() << " groups, " << passes << " passes, " << failures << " failures (seed "
              << seed << ")" << std::endl;
    return failures > 0 || runner.getNumResults() == 0 ? 1 : 0;
}
