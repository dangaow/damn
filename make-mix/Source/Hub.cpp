// 【WP0 桩】attach 一律返回 −1（每个实例都单独工作），key 永远是 Silent，MIX 不会开始。WP2 换成第 3 节的实现。
#include "Hub.h"
#include "Solver.h"

namespace mm
{
InstanceId InstanceId::random()
{
    for (;;)
    {
        const juce::Uuid u;                                    // 系统随机源
        const auto* b = u.getRawData();
        InstanceId id;
        for (int i = 0; i < 8; ++i)  id.hi = (id.hi << 8) | (uint64_t) b[i];
        for (int i = 8; i < 16; ++i) id.lo = (id.lo << 8) | (uint64_t) b[i];
        if (! id.isNull()) return id;
    }
}

Hub::Hub() = default;
Hub::~Hub() { stopTimer(); }

int Hub::attach (HubClient&, InstanceId& id)
{
    if (id.isNull()) id = InstanceId::random();
    return -1;
}
void Hub::detach (int) {}
Hub::Restore Hub::restoreId (int, InstanceId& current, const InstanceId& saved)
{
    if (saved == current) return Restore::Same;
    if (! saved.isNull()) current = saved;
    return Restore::Adopted;
}
void Hub::setInfo (int, Role, SubRole, int) {}
void Hub::setName (int, const std::string&) {}
void Hub::broadcastStyle (int, StyleId) {}
LinkSummary Hub::linkSummary (int) const
{
    LinkSummary s;
    s.alone = true;
    s.text = "ALONE";
    return s;
}
bool Hub::beginMix (int, StyleId) { return false; }
void Hub::stop (int, bool) {}
bool Hub::undoMix (int) { return false; }
bool Hub::remix (int, StyleId) { return false; }
SessionView Hub::session (int) const { return {}; }
void Hub::tick() {}
void Hub::setSynchronousSolve (bool) {}

SlotShared& Hub::shared (int slot) noexcept { return slots[(size_t) std::clamp (slot, 0, kMaxSlots - 1)]; }
uint64_t Hub::liveMask() const noexcept { return live.load (std::memory_order_acquire); }
void Hub::publishKey (int, int64_t, const float*, float) noexcept {}
KeyRead Hub::readVocalKey (int, int, int64_t, float* bandDbOut) const noexcept
{
    if (bandDbOut != nullptr)
        for (int k = 0; k < kKeyBands; ++k) bandDbOut[k] = powToDb (0.0f);
    return KeyRead::Silent;
}
} // namespace mm
