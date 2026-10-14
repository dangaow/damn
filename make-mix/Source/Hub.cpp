#include "Hub.h"

namespace mm
{
namespace { constexpr double kListenSec = 15.0, kMaxCaptureSec = 24.0, kMinSec = 6.0; }

double Hub::now() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

Hub::Hub()
{
    for (auto& s : slots) { for (auto& t : s.stamp) t.store (-1); for (auto& f : s.db) for (auto& x : f) x.store (-100); }
    startTimerHz (10);
}

Hub::~Hub()
{
    stopTimer();
    if (worker.joinable()) worker.join();
}

int Hub::attach (HubClient* c)
{
    std::lock_guard<std::mutex> lk (mtx);
    for (int i = 0; i < kMaxSlots; ++i)
        if (slots[(size_t) i].client == nullptr)
        {
            auto& s = slots[(size_t) i];
            s.client = c; s.role.store ((int) c->hubRole()); s.latest.store (-1);
            for (auto& t : s.stamp) t.store (-1);
            s.live.store (true, std::memory_order_release);
            return i;
        }
    return -1;
}

void Hub::detach (int slot)
{
    if (! juce::isPositiveAndBelow (slot, kMaxSlots)) return;
    std::lock_guard<std::mutex> lk (mtx);
    auto& s = slots[(size_t) slot];
    s.live.store (false, std::memory_order_release);
    s.client = nullptr;
    // 实例析构时它的音频线程已经停了，录音缓冲可以放心释放（解算线程用的是拷贝）
    s.cap.store (nullptr); s.owned.clear();
    participants.erase (std::remove (participants.begin(), participants.end(), slot), participants.end());
    undo.erase (std::remove_if (undo.begin(), undo.end(), [slot] (auto& u) { return u.first == slot; }), undo.end());
}

void Hub::broadcastStyle (StyleId sid)
{
    std::vector<HubClient*> cs;
    { std::lock_guard<std::mutex> lk (mtx); for (auto& s : slots) if (s.client) cs.push_back (s.client); }
    for (auto* c : cs) c->hubSetStyle (sid);
}

std::string Hub::linkSummary() const
{
    std::lock_guard<std::mutex> lk (mtx);
    int n[3] {};
    for (auto& s : slots) if (s.client) ++n[(int) s.client->hubRole()];
    if (n[0] + n[1] + n[2] <= 1) return "單獨使用 · 在人聲、伴奏、Master 各插一個 MAKE MIX";
    std::string out = "已連接";
    const char* names[] = { "人聲", "伴奏", "總線" };
    for (int r = 0; r < 3; ++r) if (n[r]) out += std::string (" · ") + names[r] + (n[r] > 1 ? " ×" + std::to_string (n[r]) : "");
    return out;
}

// ---------------------------------------------------------------------------------------- 音频线程
void Hub::setRole (int slot, Role r) noexcept { if (juce::isPositiveAndBelow (slot, kMaxSlots)) slots[(size_t) slot].role.store ((int) r, std::memory_order_relaxed); }

void Hub::publishKey (int slot, int64_t hop, const float* db) noexcept
{
    if (! juce::isPositiveAndBelow (slot, kMaxSlots) || hop < 0) return;
    auto& s = slots[(size_t) slot]; const size_t i = (size_t) (hop % kKeyRing);
    s.stamp[i].store (-1, std::memory_order_relaxed);                // 写之前先把戳记作废（单写者的 seqlock）
    std::atomic_thread_fence (std::memory_order_release);
    for (int k = 0; k < kKeyBands; ++k) s.db[i][(size_t) k].store (db[k], std::memory_order_relaxed);
    s.stamp[i].store (hop, std::memory_order_release);
    s.latest.store (hop, std::memory_order_release);
}

bool Hub::readVocalKey (int64_t hop, bool exact, float* db) noexcept
{
    float pw[kKeyBands] {}; bool any = false;
    for (auto& s : slots)
    {
        if (! s.live.load (std::memory_order_acquire) || s.role.load (std::memory_order_relaxed) != (int) Role::Vocal) continue;
        const int64_t h = exact ? hop : s.latest.load (std::memory_order_acquire);
        if (h < 0) continue;
        const size_t i = (size_t) (h % kKeyRing);
        float v[kKeyBands];
        if (s.stamp[i].load (std::memory_order_acquire) != h) continue;
        for (int k = 0; k < kKeyBands; ++k) v[k] = s.db[i][(size_t) k].load (std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_acquire);
        if (s.stamp[i].load (std::memory_order_relaxed) != h) continue;    // 读的时候被改写了：这一路这帧不算
        for (int k = 0; k < kKeyBands; ++k) pw[k] += std::pow (10.0f, v[k] * 0.1f);
        any = true;
    }
    if (any) for (int k = 0; k < kKeyBands; ++k) db[k] = powToDb (pw[k]);
    return any;
}

void Hub::capture (int slot, const float* l, const float* r, int n, int64_t pos, bool playing) noexcept
{
    if (! juce::isPositiveAndBelow (slot, kMaxSlots) || n <= 0) return;
    auto* c = slots[(size_t) slot].cap.load (std::memory_order_acquire);
    if (c == nullptr || ! c->armed.load (std::memory_order_acquire) || ! playing) return;
    int len = c->len.load (std::memory_order_relaxed);
    if (len > 0 && c->start.load (std::memory_order_relaxed) + len != pos) len = 0;   // 跳了位置：从这里重新录
    if (len == 0) c->start.store (pos, std::memory_order_relaxed);
    const int m = std::min (n, (int) c->l.size() - len);
    if (m <= 0) return;
    std::copy (l, l + m, c->l.begin() + len); std::copy (r, r + m, c->r.begin() + len);
    c->len.store (len + m, std::memory_order_release);
}

// ---------------------------------------------------------------------------------------- 会话（消息线程）
void Hub::requestMix (StyleId sid)
{
    if (st == State::Solving) return;
    if (st == State::Listening) { startSolve(); return; }                 // 提前结束
    {
    std::lock_guard<std::mutex> lk (mtx);
    participants.clear(); sessionStyle = sid; message.clear();
    for (int i = 0; i < kMaxSlots; ++i)
    {
        auto& s = slots[(size_t) i]; if (! s.client) continue;
        participants.push_back (i);
        if (s.client->hubRole() == Role::Bus) continue;
        const double fs = s.client->hubSampleRate();
        CaptureBuf* c = s.cap.load();
        if (c == nullptr || std::abs (c->fs - fs) > 1)
        {
            auto nb = std::make_unique<CaptureBuf>(); nb->fs = fs;
            nb->l.assign ((size_t) (kMaxCaptureSec * fs), 0.0f); nb->r.assign ((size_t) (kMaxCaptureSec * fs), 0.0f);
            c = nb.get(); s.owned.push_back (std::move (nb));
        }
        c->len.store (0); c->armed.store (true, std::memory_order_release);
        s.cap.store (c, std::memory_order_release);
    }
    }
    listenStart = now();
    st = State::Listening;
    if (synchronous) tick();
}

void Hub::tick()
{
    if (st == State::Listening)
    {
        double minSec = 1e9; bool any = false;
        { std::lock_guard<std::mutex> lk (mtx);
          for (int i : participants) { auto& s = slots[(size_t) i]; if (! s.client || s.client->hubRole() == Role::Bus) continue;
              auto* c = s.cap.load(); if (! c) continue; any = true; minSec = std::min (minSec, c->len.load() / c->fs); } }
        if (! any) { message = "請在人聲或伴奏軌上也插 MAKE MIX"; st = State::Failed; doneAt = now(); return; }
        if (minSec >= kListenSec) startSolve();
    }
    else if (st == State::Solving)
    {
        if (resultReady.load (std::memory_order_acquire)) { if (worker.joinable()) worker.join(); applyResult(); }
    }
    else if ((st == State::Done || st == State::Failed) && now() - doneAt > 4.0) st = State::Idle;
}

void Hub::startSolve()
{
    automix::In in; in.style = sessionStyle;
    {
        std::lock_guard<std::mutex> lk (mtx);
        for (int i : participants)
        {
            auto& s = slots[(size_t) i]; if (! s.client) continue;
            in.fs = s.client->hubSampleRate(); in.bpm = s.client->hubBpm();
            const Role r = s.client->hubRole();
            if (r == Role::Bus) { if (! in.hasBus) { in.hasBus = true; in.busClient = i; in.busCurrent = s.client->hubParams(); } continue; }
            auto* c = s.cap.load(); if (! c) continue;
            c->armed.store (false, std::memory_order_release);
            const int len = c->len.load (std::memory_order_acquire);
            if (len < (int) (kMinSec * c->fs)) continue;
            automix::Capture cap; cap.client = i; cap.start = c->start.load(); cap.current = s.client->hubParams();
            cap.audio.l.assign (c->l.begin(), c->l.begin() + len); cap.audio.r.assign (c->r.begin(), c->r.begin() + len);
            (r == Role::Vocal ? in.vocals : in.beats).push_back (std::move (cap));
        }
    }
    if (in.vocals.empty() && in.beats.empty()) { message = in.hasBus ? "先播放歌曲，再按一鍵混音" : "聲音不夠長"; st = State::Failed; doneAt = now(); return; }
    st = State::Solving; resultReady.store (false);
    auto job = [this, in = std::move (in)]() mutable { result = automix::solve (in); resultReady.store (true, std::memory_order_release); };
    if (synchronous) { job(); applyResult(); }
    else { if (worker.joinable()) worker.join(); worker = std::thread (std::move (job)); }
}

void Hub::applyResult()
{
    resultReady.store (false);
    if (! result.ok) { message = result.error; st = State::Failed; doneAt = now(); return; }
    std::vector<std::pair<HubClient*, const automix::ClientOut*>> todo;
    {
        std::lock_guard<std::mutex> lk (mtx);
        undo.clear();
        for (auto& co : result.clients)
            if (juce::isPositiveAndBelow (co.client, kMaxSlots) && slots[(size_t) co.client].client)
            {
                auto* c = slots[(size_t) co.client].client;
                undo.push_back ({ co.client, c->hubParams() });
                todo.push_back ({ c, &co });
            }
    }
    for (auto& [c, co] : todo) c->hubApply (co->set, co->note);
    metrics = result.m; message = result.summary; st = State::Done; doneAt = now();
}

void Hub::requestUndo()
{
    std::vector<std::pair<HubClient*, ParamSet>> todo;
    { std::lock_guard<std::mutex> lk (mtx);
      for (auto& [slot, set] : undo) if (slots[(size_t) slot].client) todo.push_back ({ slots[(size_t) slot].client, set });
      undo.clear(); }
    for (auto& [c, set] : todo) c->hubApply (set, "已撤銷：恢復到混音之前的設置\n");
}

bool Hub::canUndo() const { std::lock_guard<std::mutex> lk (mtx); return ! undo.empty(); }
automix::Metrics Hub::lastMetrics() const { return metrics; }

std::string Hub::statusText() const
{
    switch (st.load())
    {
        case State::Listening:
        {
            double minSec = 1e9;
            { std::lock_guard<std::mutex> lk (mtx);
              for (int i : participants) { auto& s = slots[(size_t) i]; if (! s.client || s.client->hubRole() == Role::Bus) continue;
                  if (auto* c = s.cap.load()) minSec = std::min (minSec, c->len.load() / c->fs); } }
            if (minSec > 1e8 || (minSec < 0.05 && now() - listenStart > 1.0)) return "請先播放";
            return "聆聽中 " + std::to_string ((int) std::min (99.0, 100.0 * minSec / kListenSec)) + "%";
        }
        case State::Solving: return "混音中…";
        case State::Done:    return "完成";
        case State::Failed:  return message;
        case State::Idle: default: return "一鍵混音";
    }
}
} // namespace mm
