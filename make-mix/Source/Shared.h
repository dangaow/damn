#pragma once
// 公共类型：不依赖 JUCE，音频线程 / 消息线程 / 解算线程都会用到
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace mm
{
enum class Role : uint8_t { Vocal = 0, Beat = 1, Bus = 2 };
enum class SubRole : uint8_t { Lead = 0, Double = 1, Adlib = 2 };      // 只有 VOCAL 用
enum class KeyRead : uint8_t { Ok, Silent, Late };                     // 读人声 key 的结果（Hub 和 Chains 共用）
using StyleId = int;                                                    // kStyles 的下标

constexpr int kNumStyles = 12, kMaxSlots = 32, kMaxGroups = 4;
constexpr int kKeyBands = 10, kKeyRing = 512, kAnaBands = 32, kOctBands = 10;
constexpr float kKeyCentresHz[kKeyBands] = { 180, 271, 409, 616, 927, 1397, 2105, 3172, 4779, 7200 };
constexpr float kOctCentresHz[kOctBands] = { 31.5f, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
constexpr float kInternalRefLufs = -20.0f;     // VOCAL 输入归一化后的内部响度
constexpr float kVocalWeight[3] = { 1.0f, 0.5f, 0.5f };   // key 功率和权重：LEAD / DOUBLE / ADLIB

// —— 以下三个表是 4.2.2 / 6.3.3 的常数（WP0 补充，供 Analysis / Chains / AutoMix 共用，只能追加不能改）——
// 32 个 ERB 频段的边界和中心（Hz），40 Hz–16 kHz；高于 0.45 fs 的频段由使用者丢弃
constexpr float kAnaEdgesHz[kAnaBands + 1] = { 40, 77, 119, 166, 220, 281, 351, 430, 521, 623, 739, 872, 1022, 1193, 1388, 1609, 1860,
                                               2145, 2470, 2839, 3258, 3735, 4277, 4893, 5593, 6389, 7294, 8323, 9492, 10821, 12331, 14048, 16000 };
constexpr float kAnaCentresHz[kAnaBands] = { 58, 97, 142, 192, 250, 315, 389, 474, 570, 679, 803, 945, 1105, 1287, 1495, 1730,
                                             1998, 2303, 2649, 3042, 3489, 3997, 4575, 5232, 5979, 6827, 7792, 8888, 10135, 11552, 13162, 14993 };
// 10 个 key 频段的人声重要度 I_k（BEAT 的 need 计算和掩蔽统计用）
constexpr float kKeyImportance[kKeyBands] = { 0.25f, 0.40f, 0.55f, 0.70f, 0.85f, 0.95f, 1.0f, 1.0f, 0.85f, 0.45f };

inline int rateFactor (double fs) noexcept    { return fs <= 50000.0 ? 1 : fs <= 100000.0 ? 2 : 4; }
inline int keyHop (double fs) noexcept        { return 128 * rateFactor (fs); }   // key 帧长（样本）
inline int keyLookahead (double fs) noexcept  { return 128 * rateFactor (fs); }
// 所有角色报告同一个延迟；只在 prepareToPlay 里算一次
inline int latencyFor (double fs, int maxBlock) noexcept
{
    const int r = rateFactor (fs), b = (maxBlock + r - 1) / r;
    return (b <= 736 ? 1024 : 2048) * r;
}
inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept  { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float powToDb (float p) noexcept   { return 10.0f * std::log10 (std::max (p, 1.0e-12f)); }

struct InstanceId                      // 128 位随机 ID，存进 state
{
    uint64_t hi = 0, lo = 0;
    bool isNull() const noexcept { return hi == 0 && lo == 0; }
    bool operator== (const InstanceId& o) const noexcept { return hi == o.hi && lo == o.lo; }
    bool operator!= (const InstanceId& o) const noexcept { return ! (*this == o); }
    bool operator< (const InstanceId& o) const noexcept { return hi != o.hi ? hi < o.hi : lo < o.lo; }
    std::string toString() const;                     // 32 位十六进制（Params.cpp 实现，只用 std）
    static InstanceId fromString (const std::string&); // 解析失败返回 null（Params.cpp）
    static InstanceId random();                       // Hub.cpp 实现（juce::Uuid）
};

struct ParamSet                         // 参数的“真实值”（不是 0..1），只在消息 / 解算线程用；方法在本头文件 inline 实现
{
    std::vector<std::pair<std::string, float>> values;
    void set (const std::string& id, float v);
    const float* find (const std::string& id) const;
};

// 音频线程用来预测响度变化的学习结果（定长，可平凡复制）
struct CompProfile
{
    float levelLoDb = -80.0f;                 // 直方图第 0 格下沿（dBFS，链输入处，未加 INPUT）
    std::array<float, 80> levelW {};          // 30 ms RMS 直方图，1 dB 一格，归一化（和 = 1）
    std::array<float, kAnaBands> ltasDb {};   // 链输入的频段功率（绝对 dB，非粉噪参考）
    std::array<float, 9> satGainDb {};        // WARMTH = 0, 1/8 … 1 时饱和级的响度变化（dB）
    float crestDb = 12.0f;                    // 10 ms 峰值相对 30 ms RMS 的典型差值
    uint8_t scAlignRaw = 0;                   // BEAT：侧链没有经过宿主延迟补偿（见 6.3.4）
    uint8_t valid = 0;                        // 0 = 用默认曲线
};
CompProfile defaultCompProfile (Role);        // Params.cpp 实现（默认人声 / 伴奏 / 混音曲线）

struct LearnedProfile                         // 存进实例 state
{
    CompProfile comp;
    uint32_t sessionId = 0;
    StyleId style = -1;
    std::string learnedAt;                    // ISO 时间，仅显示
    std::vector<std::string> notes;           // 这个实例的解释文字
};

// 一写多读的小盒子：消息线程写，音频线程读到的一定是完整的一份（模板，本头文件 inline 实现）
template <typename T> class SeqBox
{
    static_assert (std::is_trivially_copyable_v<T>);
    static constexpr size_t W = (sizeof (T) + 3) / 4;
    std::atomic<uint32_t> seq { 0 };
    std::array<std::atomic<uint32_t>, W> w {};
public:
    void write (const T& v) noexcept;                          // seq 变奇数 → 写 → seq 变偶数（release）
    bool read (T& out, uint32_t& lastSeq) const noexcept;      // 版本没变返回 false；最多重试 4 次，失败保留旧值
};

// 学习时的原始采样：消息线程分配，音频线程只追加，解算线程只读前缀
struct CaptureData
{
    CaptureData (double fs, int numCh, bool withKey, double seconds);   // 可能抛 std::bad_alloc（Params.cpp 实现）
    const double fs;
    const int numCh;                           // 1 或 2
    const int64_t capacity;                    // 帧数
    std::array<std::vector<float>, 2> ch;      // 链之前的主输入
    std::vector<float> key;                    // BEAT 侧链（单声道和），没有就为空
    struct Segment { int64_t at; int64_t timeline; int32_t pass; float bpm; };
    static constexpr int kMaxSegments = 256;
    std::array<Segment, kMaxSegments> seg {};
    std::atomic<int> numSeg { 0 };             // 先写 seg[numSeg] 再 release +1
    std::atomic<int64_t> written { 0 };        // 已写帧数（release）
    std::atomic<int64_t> activeSamples { 0 };  // 进度：有信号的样本数
    std::atomic<bool> keyActive { false };     // 采样期间侧链出现过信号
    // —— 以下只有音频线程读写 ——
    int64_t expectTimeline = -1;
    int32_t pass = 0;
    int64_t skip = 0;                          // 开始前还要跳过的样本（BUS 第 2 遍的稳定时间）
    float vadMs = 0;
};

struct SolveSource
{
    InstanceId id;
    Role role = Role::Vocal;
    SubRole sub = SubRole::Lead;
    std::string name;                                  // 轨道名，仅显示
    std::shared_ptr<const CaptureData> capture;        // 可能为空
    ParamSet params;                                   // 当前参数
    CompProfile comp;
    bool sidechainConnected = false;
    bool keyUserForced = false;                        // 用户把 KEY 设成了非 AUTO
};
struct SolveRequest
{
    uint32_t sessionId = 0;
    int pass = 1;                                      // 1 = 人声 + 伴奏，2 = 总线
    StyleId style = 0;
    double bpm = 120;
    std::vector<SolveSource> sources;
    bool linkKeyAvailable = false;                     // 同组有 VOCAL → BEAT 的 AUTO 会用 hub key
    float busTargetLufs = -9.0f, busCeilingDb = -1.0f;
    const std::atomic<bool>* cancel = nullptr;         // 解算线程定期检查
};
struct SolveOutput { InstanceId id; ParamSet params; CompProfile comp; std::vector<std::string> notes; };
struct SolveResult
{
    bool ok = false;
    std::string error;                                 // 给用户看的短句（大写英文），如 "NOT ENOUGH VOCAL"
    std::vector<SolveOutput> outputs;
    std::vector<std::string> summary;                  // 会话总结（BUS 显示全部）
    std::vector<std::pair<std::string, double>> metrics;   // mixrender 的 JSON 用
};

//==============================================================================
// inline 实现

inline void ParamSet::set (const std::string& id, float v)
{
    for (auto& p : values)
        if (p.first == id) { p.second = v; return; }      // 已有就覆盖，保持原来的顺序
    values.emplace_back (id, v);
}

inline const float* ParamSet::find (const std::string& id) const
{
    for (auto& p : values)
        if (p.first == id) return &p.second;
    return nullptr;
}

// 顺序锁（Boehm 的写法）：写端 seq+1 → release 栅栏 → 逐字写 → seq+1（release）；
// 读端 acquire 读 seq → 逐字读 → acquire 栅栏 → 再读 seq，两次相同且为偶数才算完整。
// lastSeq 由读端保存，初值 0（= 还没写过，读到的永远是“没变”，读端保留自己的默认值）。
template <typename T> void SeqBox<T>::write (const T& v) noexcept
{
    std::array<uint32_t, W> tmp {};
    std::memcpy (tmp.data(), &v, sizeof (T));
    const uint32_t s = seq.load (std::memory_order_relaxed);
    seq.store (s + 1, std::memory_order_relaxed);
    std::atomic_thread_fence (std::memory_order_release);
    for (size_t i = 0; i < W; ++i) w[i].store (tmp[i], std::memory_order_relaxed);
    seq.store (s + 2, std::memory_order_release);
}

template <typename T> bool SeqBox<T>::read (T& out, uint32_t& lastSeq) const noexcept
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const uint32_t s1 = seq.load (std::memory_order_acquire);
        if (s1 == lastSeq) return false;                   // 版本没变
        if ((s1 & 1u) != 0) continue;                      // 正在写
        std::array<uint32_t, W> tmp;
        for (size_t i = 0; i < W; ++i) tmp[i] = w[i].load (std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_acquire);
        if (seq.load (std::memory_order_relaxed) != s1) continue;   // 读的时候被改了，重来
        std::memcpy (static_cast<void*> (&out), tmp.data(), sizeof (T));   // T 可平凡复制（static_assert）
        lastSeq = s1;
        return true;
    }
    return false;                                          // 一直在写：保留旧值，下一块再读
}
} // namespace mm
