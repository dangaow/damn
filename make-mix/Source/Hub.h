#pragma once
// Hub：同一个插件 DLL 里所有实例共享的一个对象（juce::SharedResourcePointer）。
// 负责：实例登记、风格广播、人声 key（音频线程无锁读写）、学习时的录音、一键混音的流程和解算线程、撤销。
// 规则：音频线程只碰原子量和预先分配好的内存；登记 / 会话 / 应用参数都在消息线程。
#include "AutoMix.h"
#include <juce_events/juce_events.h>
#include <mutex>
#include <thread>

namespace mm
{
// 实例（插件处理器）给 hub 的接口：全部在消息线程调用
struct HubClient
{
    virtual ~HubClient() = default;
    virtual Role hubRole() const = 0;
    virtual ParamSet hubParams() const = 0;
    virtual void hubApply (const ParamSet&, const std::string& note) = 0;   // 用手势写参数（可撤销、宿主看得见）
    virtual void hubSetStyle (StyleId) = 0;
    virtual double hubSampleRate() const = 0;
    virtual double hubBpm() const = 0;
};

class Hub : private juce::Timer
{
public:
    Hub();
    ~Hub() override;
    static constexpr int abiVersion = 1;

    // —— 消息线程 ——
    int attach (HubClient*);                 // 返回槽位（满了返回 -1）
    void detach (int slot);
    void broadcastStyle (StyleId);
    void requestMix (StyleId);               // 正在听的时候再按一次 = 提前结束、用已有的录音解算
    void requestUndo();
    void tick();                             // 10 Hz（测试工具里手动调）
    void setSynchronousSolve (bool s) { synchronous = s; }
    enum class State { Idle, Listening, Solving, Done, Failed };
    State state() const noexcept { return st.load(); }
    std::string statusText() const;          // 按钮上的字
    std::string linkSummary() const;         // "LINK · VOCAL ×2 · BEAT ×1 · BUS"
    bool canUndo() const;
    automix::Metrics lastMetrics() const;

    // —— 音频线程 ——
    void setRole (int slot, Role) noexcept;
    void publishKey (int slot, int64_t hop, const float* db) noexcept;
    bool readVocalKey (int64_t hop, bool exact, float* db) noexcept;      // 所有在线人声的功率和
    // 录音：学习期间把这一块输入追加进去（内容时间不连续就从头录）
    void capture (int slot, const float* l, const float* r, int n, int64_t pos, bool playing) noexcept;

private:
    struct CaptureBuf { std::vector<float> l, r; double fs = 0; std::atomic<int> len { 0 }, gen { 0 }; std::atomic<int64_t> start { 0 }; std::atomic<bool> armed { false }; };
    struct Slot
    {
        std::atomic<bool> live { false }; std::atomic<int> role { 0 };
        std::array<std::atomic<int64_t>, kKeyRing> stamp; std::array<std::array<std::atomic<float>, kKeyBands>, kKeyRing> db;
        std::atomic<int64_t> latest { -1 };
        std::atomic<CaptureBuf*> cap { nullptr };
        HubClient* client = nullptr;                          // 只在消息线程用
        std::vector<std::unique_ptr<CaptureBuf>> owned;      // 这个槽分配过的录音缓冲（detach 时释放）
    };
    std::array<Slot, kMaxSlots> slots;
    mutable std::mutex mtx;                                  // 登记表（消息线程）

    std::atomic<State> st { State::Idle };
    std::atomic<bool> heard { true }; bool waitVocal = false;   // 有人声轨时：等人声真的唱起来才开始录（前奏不算）
    std::vector<int> participants; StyleId sessionStyle = 0; double listenStart = 0; std::string message;
    std::thread worker; std::atomic<bool> resultReady { false }; automix::Out result; automix::Metrics metrics;
    std::vector<std::pair<int, ParamSet>> undo; std::vector<int> resultSlots;
    bool synchronous = false; double doneAt = 0;

    void timerCallback() override { tick(); }
    void startSolve();
    void applyResult();
    static double now();
};
} // namespace mm
