// 【WP0 桩】三条链都只是一条 spec.latency 的延迟：没有 key 帧，电平表为零。WP1 换成 6.2–6.4 的实现。
#include "Chains.h"
#include "Dsp.h"

namespace mm
{
namespace detail
{
void StubDelay::prepare (int latency)
{
    len = std::max (0, latency);
    for (auto& b : buf) b.assign ((size_t) len, 0.0f);
    pos = 0;
}
void StubDelay::reset() noexcept
{
    for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
    pos = 0;
}
void StubDelay::process (float* const* ch, int numCh, int n) noexcept
{
    if (len == 0 || n <= 0) return;
    int p = pos;
    for (int c = 0; c < std::min (numCh, 2); ++c)
    {
        auto* b = buf[(size_t) c].data();
        p = pos;
        for (int i = 0; i < n; ++i)
        {
            const float y = b[p];
            b[p] = ch[c][i];
            ch[c][i] = y;
            if (++p == len) p = 0;
        }
    }
    pos = p;
}
} // namespace detail

//==============================================================================
void VocalChain::prepare (const ChainSpec& s)              { pad.prepare (s.latency); m = {}; }
void VocalChain::reset() noexcept                          { pad.reset(); m = {}; }
void VocalChain::setParams (const VocalParams&, const CompProfile&) noexcept {}
void VocalChain::process (float* const* ch, int numCh, int n, const ProcessContext&, KeySink*, VocalTaps*) noexcept { pad.process (ch, numCh, n); }
void VocalChain::processBypassed (float* const* ch, int numCh, int n, const ProcessContext&, KeySink*) noexcept { pad.process (ch, numCh, n); }
int VocalChain::coreLatency() const noexcept               { return 0; }
float VocalChain::autoCompDb() const noexcept              { return 0.0f; }
const ChainMeters& VocalChain::meters() const noexcept     { return m; }

//==============================================================================
void BeatChain::prepare (const ChainSpec& s)               { pad.prepare (s.latency); m = {}; cut.fill (0.0f); beat.fill (-120.0f); }
void BeatChain::reset() noexcept                           { pad.reset(); m = {}; cut.fill (0.0f); beat.fill (-120.0f); }
void BeatChain::setParams (const BeatParams&, const CompProfile&) noexcept {}
void BeatChain::process (float* const* ch, int numCh, const float* const*, int, int n, const ProcessContext&, KeySource*, BeatTaps*) noexcept
{
    pad.process (ch, numCh, n);
}
void BeatChain::processBypassed (float* const* ch, int numCh, int n) noexcept { pad.process (ch, numCh, n); }
KeyMode BeatChain::keyMode() const noexcept                { return KeyMode::Off; }
uint32_t BeatChain::lateCount() const noexcept             { return 0; }
const std::array<float, kKeyBands>& BeatChain::cutDb() const noexcept  { return cut; }
const std::array<float, kKeyBands>& BeatChain::beatDb() const noexcept { return beat; }
const ChainMeters& BeatChain::meters() const noexcept      { return m; }

//==============================================================================
void BusChain::prepare (const ChainSpec& s)                { pad.prepare (s.latency); m = {}; }
void BusChain::reset() noexcept                            { pad.reset(); m = {}; }
void BusChain::setParams (const BusParams&, const CompProfile&) noexcept {}
void BusChain::process (float* const* ch, int numCh, int n, const ProcessContext&, BusTaps*) noexcept { pad.process (ch, numCh, n); }
void BusChain::processBypassed (float* const* ch, int numCh, int n) noexcept { pad.process (ch, numCh, n); }
int BusChain::coreLatency() const noexcept                 { return 0; }
void BusChain::resetIntegrated() noexcept                  {}
const ChainMeters& BusChain::meters() const noexcept       { return m; }
} // namespace mm
