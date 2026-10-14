#pragma once
// 进程内的实例中心（第 3 节）：注册、ID、分组 / 风格广播、key 环形缓冲、采样握手、学习会话、解算线程。不含 DSP。
// 【WP0 桩】公开接口是冻结的（3.3）；private 部分和 Hub.cpp 由 WP2 替换。
#include "Shared.h"
#include <juce_events/juce_events.h>
#include <mutex>

namespace mm
{
// PluginProcessor 实现；所有回调都在消息线程，且在 hub 的注册锁（std::recursive_mutex，允许宿主同步回调时重入）之内调用
class HubClient
{
public:
    virtual ~HubClient() = default;
    virtual ParamSet hubSnapshotParams() const = 0;
    virtual LearnedProfile hubSnapshotProfile() const = 0;
    virtual void hubApply (const ParamSet&, const LearnedProfile&) = 0;          // 用 gesture 写参数
    virtual void hubSetStyle (StyleId) = 0;                                       // 同组广播
    virtual std::shared_ptr<CaptureData> hubMakeCapture (int pass, double seconds) = 0;  // 分配并发布给音频线程
    virtual void hubReleaseCapture() = 0;                                         // 确认音频线程已停后释放
    virtual bool hubSidechainConnected() const = 0;
};

enum class CapState : uint32_t { Idle = 0, Armed, Running, StopReq, Stopped };

struct alignas (64) KeySlot
{
    std::atomic<uint64_t> tag { 0 };                       // 0 = 空 / 正在写；否则 = hop + 1
    std::array<std::atomic<float>, kKeyBands> bandDb {};
    std::atomic<float> vad { 0 };
};

struct alignas (64) SlotShared
{
    // 注册信息：消息线程写（锁内），任何线程读
    std::atomic<uint32_t> generation { 0 };                // 偶数 = 空闲，奇数 = 在用；每次 attach / detach +1
    std::atomic<uint8_t> role { 0 }, subRole { 0 }, group { 0 }, style { 0 };
    // 实时数据：只有槽主人的音频线程写
    std::atomic<int64_t> lastHop { -1 };                   // 最近发布的 key hop
    std::atomic<uint32_t> lastMs { 0 };                    // juce::Time::getMillisecondCounter()，只给界面判断“活跃”
    std::array<std::atomic<float>, kKeyBands> displayDb {}; // 平滑后的频段电平（VOCAL：key；BEAT：伴奏）
    std::array<std::atomic<float>, kKeyBands> cutDb {};    // BEAT 当前避让量（≤ 0）
    std::atomic<float> loudM { -100 };                     // 输出瞬时响度
    std::atomic<uint32_t> syncMiss { 0 };                  // BEAT：读 key 迟到次数
    std::atomic<uint8_t> keyState { 0 };                   // BEAT：0 无 1 LINK 2 SIDECHAIN
    // 学习采样握手
    std::atomic<uint32_t> capState { 0 };
    std::atomic<CaptureData*> capture { nullptr };         // 消息线程在 Armed 之前 release 发布
    // key 环形缓冲（只有 VOCAL 写）
    std::array<KeySlot, kKeyRing> ring;
};

struct PeerInfo { InstanceId id; Role role; SubRole sub; std::string name; bool active; int slot; };
struct LinkSummary
{
    int vocals = 0, beats = 0, buses = 0;
    bool bridgedHost = false, alone = false, full = false, styleMixed = false;
    std::vector<PeerInfo> peers;               // 按 (role, sub, id) 排序，稳定
    std::string text;                          // "LINK A · VOCAL ×2 · BEAT ×1 · BUS"
    std::string warning;                       // "BRIDGED — untick Wrapper ▸ Processing ▸ Make bridged"
};
struct SessionView
{
    enum class Phase { Idle, Listen1, Solve1, Settle2, Listen2, Solve2, Done, Failed, Cancelled };
    Phase phase = Phase::Idle;
    float progress = 0;                        // 当前阶段 0..1
    std::string status;                        // "LISTENING 1/2 · 37%" / "PRESS PLAY" / "NEED VOCAL"
    std::vector<std::string> summary;          // 会话总结
    bool canUndo = false, canRemix = false;
    uint32_t sessionId = 0;
};

class Hub : private juce::Timer
{
public:
    static constexpr uint32_t abiVersion = 1;
    Hub();
    ~Hub() override;

    // —— 消息线程 ——
    int attach (HubClient&, InstanceId& id);                 // 返回槽号（满了 = -1）；id 与在用实例冲突时就地换新
    void detach (int slot);
    enum class Restore { Same, Adopted, Clone };
    Restore restoreId (int slot, InstanceId& current, const InstanceId& saved);
    void setInfo (int slot, Role, SubRole, int group);
    void setName (int slot, const std::string&);
    void broadcastStyle (int fromSlot, StyleId);             // 同组其它实例 hubSetStyle()
    LinkSummary linkSummary (int slot) const;
    bool beginMix (int initiatorSlot, StyleId);              // MIX 按钮；返回 false = 已在进行
    void stop (int slot, bool cancel);                       // 第二次按 MIX = 现在就解算；× = 取消
    bool undoMix (int slot);
    bool remix (int slot, StyleId);                          // 用保留的采样重新解算
    SessionView session (int slot) const;
    void tick();                                             // Timer 10 Hz；测试工具手动调用
    void setSynchronousSolve (bool);                         // 测试：tick() 里直接调用 runSolve

    // —— 任何线程（无锁、不分配）——
    SlotShared& shared (int slot) noexcept;
    uint64_t liveMask() const noexcept;                      // acquire
    void publishKey (int slot, int64_t hop, const float* bandDb, float vad) noexcept;
    KeyRead readVocalKey (int group, int selfSlot, int64_t hop, float* bandDbOut) const noexcept;

private:
    void timerCallback() override { tick(); }
    // 实现细节见 3.4–3.9（std::recursive_mutex registryMutex；std::array<SlotShared, kMaxSlots> slots；
    // HubClient* owners[kMaxSlots]；InstanceId ids[kMaxSlots]；std::string names[kMaxSlots]；
    // std::atomic<uint64_t> live；Session sessions[kMaxGroups]；SolveWorker worker；…）
    // —— 桩只需要这些 ——
    std::array<SlotShared, kMaxSlots> slots;
    std::atomic<uint64_t> live { 0 };

    JUCE_DECLARE_NON_COPYABLE (Hub)
};
} // namespace mm
