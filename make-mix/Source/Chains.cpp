#include "Chains.h"

namespace mm
{
// ====================================================================== VOCAL
VocalChain::P VocalChain::fromSet (const ParamSet& s)
{
    P p; p.input = s[V_INPUT]; p.hpf = s[V_HPF]; p.mud = s[V_MUD]; p.pres = s[V_PRES]; p.air = s[V_AIR]; p.comp = s[V_COMP];
    p.deess = s[V_DEESS]; p.warmth = s[V_WARMTH]; p.smooth = s[V_SMOOTH]; p.level = s[V_LEVEL];
    return p;
}

void VocalChain::prepare (double sampleRate, int maxBlock, int latency)
{
    fs = sampleRate; hop = keyHop (fs);
    os = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    os->initProcessing ((size_t) maxBlock);
    osLat = (int) std::lround (os->getLatencyInSamples());
    pad.prepare (latency - osLat);
    sat.prepare (fs * 2);
    deess.prepare ({ fs, (juce::uint32) maxBlock, 2 });
    key.prepare (fs);
    for (auto* s : { &inGain, &outGain }) s->reset (fs, 0.03);
    lev.prepare (fs, 20.0f, 280.0f); comp.prepare (fs, 2.0f, 70.0f);
    levCoef = (float) std::exp (-1.0 / (fs * 0.012)); envCoef = (float) std::exp (-1.0 / (fs * 0.008));
    harshAtk = (float) std::exp (-1.0 / (fs * 0.003)); harshRel = (float) std::exp (-1.0 / (fs * 0.06));
    harshBp.set (vox::Svf::BandPass, fs, 3400, 0.9); harshBell.set (vox::Svf::Bell, fs, 3400, 1.3, 0);
    setParams (p, style, bpm);
    for (auto* s : { &inGain, &outGain }) s->setCurrentAndTargetValue (s->getTargetValue());
    reset();
}

void VocalChain::reset()
{
    for (auto* b : { &hpA, &hpB, &mud, &pres, &air }) b->reset();
    comp.grDb = 0; lev.grDb = 0; levMs = 0; deess.reset(); harshBp.reset(); harshBell.reset(); envHarsh = envFull = 0; harshCut = 0; harshCount = 0; sat.prepare (fs * 2); if (os) os->reset(); pad.reset(); key.reset();
    grComp = grDeess = 0;
}

void VocalChain::setParams (const P& np, StyleId sid, double tempo)
{
    const auto& st = mm::style (sid);
    p = np; style = sid; bpm = tempo;   // bpm 目前没用（以前给回声同步）
    hpA.highPass (fs, p.hpf, st.vHpfSlope >= 24 ? 0.5412 : 0.7071);
    if (st.vHpfSlope >= 24) hpB.highPass (fs, p.hpf, 1.3066); else hpB.set (1, 0, 0, 1, 0, 0);
    mud.peak (fs, st.vMudF, 1.1, p.mud);
    pres.peak (fs, st.vPresF, 0.9, p.pres);
    air.highShelf (fs, std::min (st.vAirF, (float) (0.42 * fs)), 0.7, p.air);
    // 压缩（两级串联，录音室常见做法）：输入已经按 v_input 归一到内部参考电平（约 -20 LUFS），阈值用绝对值
    //   1) 电平器：RMS 检测、慢启动慢释放、软膝 —— 把整句拉平，听不出在压
    //   2) 峰值压缩：快、只抓冒出来的字头
    const float k = p.comp;
    lev.thresholdDb = -20 - 8 * k; lev.ratio = k < 0.01f ? 1.0f : 1.5f + 2.0f * k; lev.kneeDb = 10; lev.makeupDb = 0;
    comp.thresholdDb = -9 - 9 * k; comp.ratio = k < 0.01f ? 1.0f : 3.0f + 3.0f * k; comp.kneeDb = 4; comp.makeupDb = 0;
    {   // 估计补偿：参考电平附近两级的静态压缩量的 60%，手动拧压缩时音量大致不变（MIX 会再精确设 LEVEL）
        auto gr = [] (const vox::Compressor& c, float lvl) { const float over = lvl - c.thresholdDb; return over > 0 ? over * (1.0f - 1.0f / c.ratio) : 0.0f; };
        makeupDb = 0.6f * (gr (lev, -17.0f) + gr (comp, -10.0f));
    }
    deess.active = p.deess > 0.01f;
    deess.thresholdDb = -24 - 18 * p.deess; deess.ratio = 3 + 5 * p.deess; deess.rangeDb = 3 + 9 * p.deess;
    sat.set (1 + 7 * p.warmth, 0.08f * p.warmth, std::min (1.0f, 2.2f * p.warmth));
    inGain.setTargetValue (dbToGain (p.input));
    outGain.setTargetValue (dbToGain (p.level + makeupDb));
}

void VocalChain::process (float* const* ch, int nCh, int n, int64_t pos, KeySink* sink) noexcept
{
    float* c0 = ch[0]; float* c1 = nCh > 1 ? ch[1] : ch[0];
    float grC = 0, grD = 0;
    // 1) 串联：输入 → 低切 → 去浑浊 → 压缩 → 去齿音 → 存在感 → 空气感
    for (int i = 0; i < n; ++i)
    {
        const float g = inGain.getNextValue();
        float x[2], level = 0;
        for (int c = 0; c < nCh; ++c)
        {
            float s = (c ? c1[i] : c0[i]) * g;
            s = mud.process (hpB.process (hpA.process (s, c), c), c);
            x[c] = s; level = std::max (level, std::abs (s));
        }
        // 电平器用 RMS（12 ms），峰值压缩用峰值
        float ms = 0; for (int c = 0; c < nCh; ++c) ms += x[c] * x[c] / (float) nCh;
        levMs = levCoef * levMs + (1 - levCoef) * ms;
        const float lg = lev.gainFor (std::sqrt (levMs) * 1.41f);
        for (int c = 0; c < nCh; ++c) x[c] *= lg;
        const float cg = comp.gainFor (level * lg);
        grC = std::min (grC, comp.grDb + lev.grDb);
        for (int c = 0; c < nCh; ++c) x[c] *= cg;
        deess.processFrame (x, nCh); grD = std::min (grD, deess.grDb);
        // 圆润：2.5–5 kHz 比整体突出时才压一下（刺的瞬间），平时不动
        {
            const float mono = nCh > 1 ? 0.5f * (x[0] + x[1]) : x[0], b = harshBp.process (mono, 0);
            envHarsh = envCoef * envHarsh + (1 - envCoef) * b * b; envFull = envCoef * envFull + (1 - envCoef) * mono * mono;
            const float over = powToDb (envHarsh) - powToDb (envFull) + 4.0f - 6.0f * p.smooth;   // 越圆润，越早开始压
            const float want = p.smooth > 0.01f ? -std::clamp (over * 0.6f, 0.0f, 2.0f + 5.0f * p.smooth) : 0.0f;
            const float cc = want < harshCut ? harshAtk : harshRel; harshCut = cc * harshCut + (1 - cc) * want;
            if (++harshCount >= 16) { harshCount = 0; harshBell.set (vox::Svf::Bell, fs, 3400, 1.3, harshCut); }
        }
        for (int c = 0; c < nCh; ++c) (c ? c1 : c0)[i] = air.process (pres.process (harshBell.process (x[c], c), c), c);
    }
    // 2) 温暖：2 倍超采样里饱和（不加饱和也照样走超采样，延迟不变）
    {
        juce::dsp::AudioBlock<float> block (ch, (size_t) nCh, 0, (size_t) n);
        auto up = os->processSamplesUp (block);
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            auto* d = up.getChannelPointer (c);
            for (size_t i = 0; i < up.getNumSamples(); ++i) d[i] = sat.process (d[i], (int) c);
        }
        os->processSamplesDown (block);
    }
    // 3) 电平 → key → 补齐固定延迟
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        float l = c0[i] * g, r = nCh > 1 ? c1[i] * g : 0.0f;
        key.push (nCh > 1 ? 0.5f * (l + r) : l);
        if (sink != nullptr && (pos + i + 1) % hop == 0) { key.take (keyDb); sink->publish ((pos + i) / hop, keyDb); }
        pad.frame (l, r);
        c0[i] = l; if (nCh > 1) c1[i] = r;
    }
    grComp = -grC; grDeess = -grD;
}

// ====================================================================== BEAT
BeatChain::P BeatChain::fromSet (const ParamSet& s)
{
    P p; p.hpf = s[B_HPF]; p.pocket = s[B_POCKET]; p.unmask = s[B_UNMASK]; p.glue = s[B_GLUE]; p.level = s[B_LEVEL];
    return p;
}

void BeatChain::prepare (double sampleRate, int, int latency)
{
    fs = sampleRate; hop = keyHop (fs); L = latency;
    pad.prepare (L); meter.prepare (fs);
    beatRing.assign ((size_t) (L / hop + 8), {});
    for (int k = 0; k < kKeyBands; ++k) bell[k].set (vox::Svf::Bell, fs, kKeyHz[k], 1.3, 0);
    outGain.reset (fs, 0.03);
    setParams (p, style); outGain.setCurrentAndTargetValue (outGain.getTargetValue());
    reset();
}

void BeatChain::reset()
{
    pad.reset(); meter.reset(); hpA.reset(); hpB.reset(); pocket.reset(); glue.grDb = 0;
    for (auto& b : bell) b.reset();
    for (auto& r : beatRing) r.fill (-100);
    for (int k = 0; k < kKeyBands; ++k) { target[k] = cur[k] = cutDb[k] = 0; }
    lastHop = INT64_MIN; coefCount = 0; lateHops = 0;
}

void BeatChain::setParams (const P& np, StyleId sid)
{
    const auto& st = mm::style (sid);
    p = np; style = sid;
    hpA.highPass (fs, p.hpf, 0.5412); hpB.highPass (fs, p.hpf, 1.3066);
    pocket.set (vox::Svf::Bell, fs, st.bPocketF, 0.8, p.pocket);
    atk = (float) std::exp (-1.0 / (fs * 0.012));
    rel = (float) std::exp (-1.0 / (fs * std::max (40.0f, st.bRelMs) * 0.001));
    // 轻粘合：2:1、慢启动（让鼓头过去）、中等释放；阈值随旋钮。补偿 -12 dBFS 处静态压缩量的一半，伴奏响度基本不变
    glue.prepare (fs, 25.0f, 160.0f);
    glue.thresholdDb = -6 - 16 * p.glue; glue.ratio = p.glue < 0.01f ? 1.0f : 1.6f + 0.8f * p.glue; glue.kneeDb = 8; glue.makeupDb = 0;
    { const float over = -12.0f - glue.thresholdDb; glueMakeup = dbToGain (over > 0 ? 0.5f * over * (1.0f - 1.0f / glue.ratio) : 0.0f); }
    outGain.setTargetValue (dbToGain (p.level));
}

// 进入新的一帧（延迟后的内容时间 h）：按人声 key 和伴奏自己在这段的能量算每段让位量
void BeatChain::startHop (int64_t h, bool playing, KeySource* src) noexcept
{
    float v[kKeyBands];
    const auto& st = mm::style (style);
    const bool have = src != nullptr && p.unmask > 0.01f && src->read (h, playing, v);
    const auto& b = beatRing[(size_t) (((h % (int64_t) beatRing.size()) + (int64_t) beatRing.size()) % (int64_t) beatRing.size())];
    for (int k = 0; k < kKeyBands; ++k)
    {
        float t = 0;
        if (have && kKeyHz[k] >= st.bLo * 0.7f && kKeyHz[k] <= st.bHi * 1.3f)
        {
            const float act = std::clamp ((v[k] + 62.0f) / 12.0f, 0.0f, 1.0f);       // 这一段人声确实在唱
            const float mask = std::clamp ((b[(size_t) k] - v[k] + 6.0f) / 12.0f, 0.0f, 1.0f);  // 伴奏在这段盖住了人声多少
            t = p.unmask * kKeyImp[k] * act * mask;
        }
        target[k] = t;
    }
    if (! have && playing && src != nullptr && p.unmask > 0.01f) ++lateHops;
}

void BeatChain::process (float* const* ch, int nCh, int n, int64_t pos, bool playing, KeySource* src) noexcept
{
    float* c0 = ch[0]; float* c1 = nCh > 1 ? ch[1] : ch[0];
    const int64_t R = (int64_t) beatRing.size();
    for (int i = 0; i < n; ++i)
    {
        const int64_t q = pos + i;
        float l = c0[i], r = nCh > 1 ? c1[i] : c0[i];
        // 未延迟的输入：量伴奏自己每段的能量（比要用它的时候早 L 个样本，肯定来得及）
        meter.push (0.5f * (l + r));
        if ((q + 1) % hop == 0) { float db[kKeyBands]; meter.take (db); auto& slot = beatRing[(size_t) (((q / hop) % R + R) % R)]; for (int k = 0; k < kKeyBands; ++k) slot[(size_t) k] = db[k]; }
        pad.frame (l, r);
        const int64_t d = q - L;
        if (d % hop == 0 || lastHop == INT64_MIN) { const int64_t h = (int64_t) std::floor ((double) d / hop); if (h != lastHop) { lastHop = h; startHop (h, playing, src); } }
        for (int k = 0; k < kKeyBands; ++k) { const float t = -target[k], c = t < cur[k] ? atk : rel; cur[k] = c * cur[k] + (1 - c) * t; }
        if (++coefCount >= 16) { coefCount = 0; for (int k = 0; k < kKeyBands; ++k) bell[k].set (vox::Svf::Bell, fs, kKeyHz[k], 1.3, cur[k]); }
        const float g = outGain.getNextValue();
        float y[2] {}, pk = 0;
        for (int c = 0; c < nCh; ++c)
        {
            float s = c ? r : l;
            s = pocket.process (hpB.process (hpA.process (s, c), c), c);
            for (int k = 0; k < kKeyBands; ++k) s = bell[k].process (s, c);
            y[c] = s; pk = std::max (pk, std::abs (s));
        }
        const float gg = glue.gainFor (pk) * glueMakeup;
        for (int c = 0; c < nCh; ++c) (c ? c1 : c0)[i] = y[c] * gg * g;
    }
    for (int k = 0; k < kKeyBands; ++k) cutDb[k] = -cur[k];
}

// ====================================================================== BUS
BusChain::P BusChain::fromSet (const ParamSet& s)
{
    P p; p.low = s[M_LOW]; p.high = s[M_HIGH]; p.mid = s[M_MID]; p.glue = s[M_GLUE]; p.lift = s[M_LIFT]; p.width = s[M_WIDTH]; p.drive = s[M_DRIVE]; p.ceiling = s[M_CEILING];
    return p;
}

void BusChain::prepare (double sampleRate, int maxBlock, int latency)
{
    fs = sampleRate;
    const int la = 64 * rateFactor (fs);
    os = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    os->initProcessing ((size_t) maxBlock);
    osLat = (int) std::lround (os->getLatencyInSamples());
    lim.prepare (fs, la); pad.prepare (latency - lim.latency() - osLat); kw.prepare (fs);
    const juce::dsp::ProcessSpec spec { fs, (juce::uint32) maxBlock, 2 };
    x1.prepare (spec); x1.setCutoffFrequency (120.0f); x2.prepare (spec); x2.setCutoffFrequency (6000.0f);
    lowComp.prepare (fs, 30.0f, 120.0f); tape.prepare (fs * 2);
    envCoef = (float) std::exp (-1.0 / (fs * 0.01));
    drive.reset (fs, 0.05); width.reset (fs, 0.05);
    loudCoef = (float) std::exp (-1.0 / (fs * 3.0));
    setParams (p, style);
    drive.setCurrentAndTargetValue (drive.getTargetValue()); width.setCurrentAndTargetValue (width.getTargetValue());
    reset();
}

void BusChain::reset()
{
    low.reset(); high.reset(); body.reset(); scHp.reset(); glue.grDb = 0; lim.reset(); pad.reset(); kw.reset(); x1.reset(); x2.reset();
    lowComp.grDb = 0; airShelf.reset(); tape.prepare (fs * 2); if (os) os->reset();
    envHi = envMid = 1e-9f; airDb = 0; dcX = dcY = 0; airCount = 0;
    loudState = 1e-10f; grGlue = grLimit = 0; loudShort = -100;
}

void BusChain::setParams (const P& np, StyleId sid)
{
    const auto& st = mm::style (sid);
    p = np; style = sid;
    low.set (vox::Svf::LowShelf, fs, 120, 0.7, p.low);
    high.set (vox::Svf::HighShelf, fs, 7000, 0.7, p.high);
    body.set (vox::Svf::Bell, fs, 180, 0.8, p.mid);
    scHp.set (vox::Svf::HighPass, fs, std::max (20.0f, st.glueScHpf), 0.7);
    glue.prepare (fs, std::max (1.0f, st.glueAtk), std::max (20.0f, st.glueRel));
    glue.thresholdDb = -4 - 20 * p.glue; glue.ratio = p.glue < 0.01f ? 1.0f : st.glueRatio; glue.kneeDb = 6; glue.makeupDb = 0;
    // 提升：低频压紧（比例随旋钮），整体磁带饱和（-12 dBFS 参考电平进出一样大）
    const float L = p.lift;
    lowComp.thresholdDb = -16; lowComp.ratio = 1.0f + 1.2f * L; lowComp.kneeDb = 10; lowComp.makeupDb = 0;
    { const float over = -8.0f - lowComp.thresholdDb; lowMakeup = dbToGain (over > 0 ? 0.9f * over * (1.0f - 1.0f / lowComp.ratio) : 0.0f); }
    tape.set (1.0f + 3.5f * L, 0.05f * L, std::min (1.0f, 0.85f * L));
    lim.ceiling = dbToGain (p.ceiling - 0.05f);
    drive.setTargetValue (dbToGain (p.drive)); width.setTargetValue (p.width * 0.01f);
}

void BusChain::process (float* const* ch, int nCh, int n) noexcept
{
    float* c0 = ch[0]; float* c1 = nCh > 1 ? ch[1] : ch[0];
    const auto& st = mm::style (style);
    const float L = p.lift, dcR = 0.9995f;
    float grG = 0, grL = 0;
    // 1) 音色 → 提升（三段）→ 粘合
    for (int i = 0; i < n; ++i)
    {
        float in[2] = { c0[i], nCh > 1 ? c1[i] : c0[i] }, lo[2], mid[2], hi[2];
        for (int c = 0; c < 2; ++c)
        {
            const float x = body.process (high.process (low.process (in[c], c), c), c);
            float rest; x1.processSample (c, x, lo[c], rest); x2.processSample (c, rest, mid[c], hi[c]);
        }
        // 低频：压紧 → 加一点高八度泛音（小音箱 / 手机也听得到 808）→ 收成单声道
        const float gl = lowComp.gainFor (std::max (std::abs (lo[0]), std::abs (lo[1]))) * lowMakeup;
        float lm = 0.5f * (lo[0] + lo[1]) * gl, ls = 0.5f * (lo[0] - lo[1]) * gl * (1.0f - L);
        { const float rect = std::abs (lm), d = rect - dcX + dcR * dcY; dcX = rect; dcY = d; lm += 0.6f * L * d; }
        // 中频：稍微加宽；高频：动态空气感（高频已经比中频突出时自动收，不会刺）+ 更宽
        const float mm_ = 0.5f * (mid[0] + mid[1]), ms = 0.5f * (mid[0] - mid[1]) * (1.0f + 0.2f * L);
        envHi = envCoef * envHi + (1 - envCoef) * 0.5f * (hi[0] * hi[0] + hi[1] * hi[1]);
        envMid = envCoef * envMid + (1 - envCoef) * 0.5f * (mid[0] * mid[0] + mid[1] * mid[1]);
        if (++airCount >= 16)
        {
            airCount = 0;
            const float ratio = powToDb (envHi) - powToDb (envMid), target = std::clamp (5.0f * L - std::max (0.0f, ratio + 7.0f) * 0.5f, 0.0f, 5.0f * L);
            airDb += 0.02f * (target - airDb);
            airShelf.set (vox::Svf::HighShelf, fs, 9000, 0.7, airDb);
        }
        const float h0 = airShelf.process (hi[0], 0), h1 = airShelf.process (hi[1], 1);
        const float hm = 0.5f * (h0 + h1), hs = 0.5f * (h0 - h1) * (1.0f + 0.45f * L);
        float l = (lm + ls) + (mm_ + ms) + (hm + hs), r = (lm - ls) + (mm_ - ms) + (hm - hs);
        const float det = std::max (std::abs (scHp.process (l, 0)), std::abs (scHp.process (r, 1)));
        const float gg = glue.gainFor (det); grG = std::min (grG, glue.grDb);
        c0[i] = l * gg; if (nCh > 1) c1[i] = r * gg; else c0[i] = 0.5f * (l + r) * gg;
    }
    // 2) 磁带饱和（2 倍超采样；不加也照样走，延迟不变）
    {
        juce::dsp::AudioBlock<float> block (ch, (size_t) nCh, 0, (size_t) n);
        auto up = os->processSamplesUp (block);
        if (L > 0.005f)
            for (size_t c = 0; c < up.getNumChannels(); ++c)
            {
                auto* d = up.getChannelPointer (c);
                for (size_t i = 0; i < up.getNumSamples(); ++i) d[i] = tape.process (d[i], (int) c);
            }
        os->processSamplesDown (block);
    }
    // 3) 宽度 → 推子 → 软削波 → 真峰值限幅 → 补齐延迟
    const float clipDb = std::max (st.clipDb, 2.5f * L), cc = lim.ceiling * dbToGain (clipDb), knee = cc * (0.6f - 0.1f * L);
    for (int i = 0; i < n; ++i)
    {
        float l = c0[i], r = nCh > 1 ? c1[i] : c0[i];
        const float m = 0.5f * (l + r), s = 0.5f * (l - r) * width.getNextValue();
        l = m + s; r = m - s;
        const float dr = drive.getNextValue(); l *= dr; r *= dr;
        if (clipDb > 0.01f)                              // 软削波：峰值先被圆掉一点，限幅器少干活、更响更结实
            for (float* x : { &l, &r })
            {
                const float a = std::abs (*x);
                if (a > knee) *x = std::copysign (knee + (cc - knee) * std::tanh ((a - knee) / (cc - knee)), *x);
            }
        lim.process (l, r); grL = std::min (grL, lim.grDb);
        pad.frame (l, r);
        const float kl = kw.process (l, 0), kr = kw.process (r, 1);
        loudState = loudCoef * loudState + (1 - loudCoef) * (kl * kl + kr * kr);
        c0[i] = nCh > 1 ? l : 0.5f * (l + r); if (nCh > 1) c1[i] = r;
    }
    grGlue = -grG; grLimit = -grL; loudShort = -0.691f + powToDb (loudState);
}
} // namespace mm
