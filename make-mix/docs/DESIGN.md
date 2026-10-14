# MAKE MIX: Design v1

Status: implementation-ready design. Owner: architect. Applies to `/home/user/damn/make-mix/`.

The design draws on three research reports (host, genre and algorithms) and on the uncommitted v0 code ("MAKE VOX": `Source/Dsp.h`, `PluginProcessor.*`, `PluginEditor.*`, `Presets.h`, `tools/render.cpp`).

Conventions used throughout:
- Identifiers and UI text are in English.
- Code comments are in Simplified Chinese, matching v0.
- "dB" means 20·log10 of amplitude. "LU/LUFS" follow BS.1770-5.
- Unverified facts about hosts are marked **UNVERIFIED**. They are collected in Appendix A as manual FL checks.

---

## 0. Key decisions (one page)

1. **One binary, three roles.** One VST3, "MAKE MIX" (company MAKE STUDIO, codes `Mkst`/`Mmix`). It has a ROLE of VOCAL, BEAT or BUS.
   - ROLE, SUB-ROLE (LEAD/DOUBLE/ADLIB), LINK GROUP (A–D) and STYLE are **instance state, not host parameters**. This means FL automation, Randomize and Hybridize can never flip them.
   - All three role chains are prepared in every instance, so switching role is real-time safe.
2. **LEARN → SOLVE → FREEZE.** Pressing **MIX** on any instance starts a group-wide learn session. Every result is written into ordinary parameters on the message thread with `begin/setValueNotifyingHost/endChangeGesture`, which makes it visible, undoable and automatable.
   - After the session, no instance depends on cross-instance data for its sound, with one exception: the BEAT's live unmask key (point 4).
   - Export therefore equals realtime by construction. This also holds for "split mixer tracks" export, a missing BUS, or bridged instances.
3. **Learning captures raw audio, not statistics.** During a learn pass each instance's audio thread only appends its input samples to a preallocated buffer (memcpy).
   - The buffer is at most 30 s and is allocated on the message thread when MIX is pressed.
   - A background solver thread analyses the captures offline, at high resolution. It then **simulates the real DSP chains** on them, so it can *measure* loudness, spread, sibilance, wet levels and bus LUFS instead of predicting them.
   - This removes the root cause of the v0 +5…+13 LU jumps.
   - The audio-thread cost of learning is close to zero.
4. **贴伴奏 (making the vocal sit in the beat).** This is done by (a) the VOCAL level solved to a style vocal-to-beat ratio on a *vocal-band loudness* (K-weighting + 150 Hz HPF), plus (b) a **masking-driven dynamic unmask on the BEAT** (10 dynamic bells on the mid channel), keyed by the live vocal envelope.
   - The key comes from the hub (default) or from a host sidechain input "VOX KEY" (deterministic, and works when bridged).
   - Hub keys are read by timeline hop index behind a fixed latency budget, so the result is order-independent. Section 3.6 gives the sync condition.
5. **Constant, role-independent latency.** `L = latencyFor(fs, maxBlock)` = 1024·r samples, or 2048·r when the host block is larger than 736·r. Here r = 1/2/4 for ≤50k/≤100k/>100k. It is set once in `prepareToPlay` and never changed by any parameter.
6. **Hub.** `juce::SharedResourcePointer<Hub>` holds:
   - 32 preallocated slots that are never freed while the hub lives;
   - a generation counter per slot;
   - seqlocked key rings;
   - a per-slot capture handshake;
   - one solve worker thread.
   The registry is guarded by a mutex used only on the message thread. Audio threads touch atomics only.
7. **Loudness-safe chains.** Every gain stage of VOCAL and BEAT is compensated automatically on the audio thread from a learned *CompProfile* (level histogram + LTAS + saturator table). Compression, EQ and warmth therefore change tone, not level.
   - Level is owned by explicit LEVEL/INPUT parameters, which the solver sets.
   - The BUS owns the ceiling through a true-peak limiter.
8. **Instance IDs** are 128-bit and saved in state. On restore, if a *live* instance already holds the saved ID, the restore is treated as an FL clone/paste/preset-drag and keeps its fresh ID. Otherwise the saved ID is adopted.
9. **Every role works alone.** Without links:
   - VOCAL: tone, dynamics and space for the style; it keeps its own level.
   - BEAT: tidy, pocket EQ, and sidechain-keyed unmask.
   - BUS: tonal balance, glue, width and loudness to target.
   Bridged and sandboxed hosts are detected and explained in the UI.

---

## 1. Product overview

### 1.1 Roles

| Role | Inserted on | Processing | What it contributes to MIX |
|---|---|---|---|
| **VOCAL** (sub-role LEAD / DOUBLE / ADLIB) | each vocal mixer track, or one vocal sub-bus (not both) | input normalisation → low cut → 4-band EQ → leveler + peak compressor → de-esser → warmth (2× OS) → auto loudness compensation → LEVEL → echo + space sends (ducked) | Captures the raw vocal. Receives: tone, dynamics, de-ess, space, and level relative to the beat. Publishes its live band envelope (key) to the hub. |
| **BEAT** | instrumental track(s) or stems | low cut, mono-below, static pocket EQ, **dynamic unmask** keyed by vocal(s), ride, LEVEL | Captures the beat (and the sidechain, if connected). Receives: tidy settings, pocket, unmask depth/sensitivity/range. |
| **BUS** | master (or the vocal+beat sum bus), last insert | tonal-balance EQ → glue compressor (SC HPF) → width / mono-below → drive → soft clip → true-peak limiter; loudness meter | Captures the mix *after* pass 1 is applied. Receives: tone toward the style curve, glue, width, drive/clip to reach target LUFS at the ceiling. Shows the combined picture: link list, masking view, final loudness. |

### 1.2 User workflow in FL Studio

1. **Insert on the vocal track(s).** Open the mixer track that carries the vocal. Add MAKE MIX in the **last** FX slot.
   - The role is suggested from the track name (§9.4): "Vox", "Vocal", "人声", "Lead" → VOCAL/LEAD; "adlib", "ad-lib" → ADLIB; "dbl", "double", "back", "harm", "和声", "伴唱" → DOUBLE.
   - Otherwise it defaults to VOCAL/LEAD and the role chip pulses until the user clicks it.
   - Repeat for ad-libs and backing tracks, choosing DOUBLE or ADLIB.
2. **Insert on the beat track(s).** Add MAKE MIX last on the beat (or on each stem: drums, 808, melody) and pick **BEAT**.
3. **Insert on the master.** Add MAKE MIX as the last insert on the Master track and pick **BUS**. Remove any other limiter after it.
4. **Check the link line.** It should read `LINK A · VOCAL ×3 · BEAT ×1 · BUS`. If it reads `LINK OFF — BRIDGED`, follow the hint (Wrapper ▸ Processing ▸ untick *Make bridged*).
5. **Choose STYLE** on any instance (e.g. `TRAP`, `MANDOPOP 华语流行`). The style is broadcast to the whole group.
6. **Set faders to 0 dB.** Leave vocal and beat faders at 0 dB and use MAKE MIX's LEVEL knobs instead. The UI repeats this hint on first MIX.
7. **Start playback and press MIX.** Loop the hook, or play from the first verse. Then press **MIX** on any instance (the BUS is the natural place).
   - The button shows `LISTENING 1/2 · 37%`. It needs about 15 s of vocal-active audio. With the transport stopped it shows `PRESS PLAY`.
   - Next it shows `MIXING…` (solve, 1–4 s); vocal and beat settings are applied.
   - With a BUS present, `LISTENING 2/2` follows (about 12 s of the new mix), then `MIXING…`, then `DONE`.
8. **Read and tweak.** Each instance shows what it changed (§5.7). Knobs moved to their new positions, and all of them stay editable.
   - `UNDO MIX` restores the pre-MIX state of every participant. FL's own undo also works, since the changes were made as gestures.
   - Changing STYLE afterwards re-solves pass 1 instantly from the retained captures, then re-listens the bus (§3.9).
9. **Export normally.** Nothing about export is special.

### 1.3 Multiple vocals and beat stems

- **Vocals:**
  - Every LEAD gets its own level solved against the beat sum, computed over *its own* active frames. Alternating rappers (e.g. Isolated with Yuki) therefore each land on target.
  - DOUBLE and ADLIB are levelled relative to the LEAD loudness, using style offsets (§7, table B). They get a 20 % higher low cut and their own tone and dynamics solve.
  - The beat's unmask key is the weighted power sum of all VOCAL instances in the group: LEAD 1.0, DOUBLE 0.5, ADLIB 0.5.
- **Beat stems:**
  - Stem balance belongs to the producer and is never re-levelled.
  - Every BEAT unmasks itself against the same vocal key.
  - The beat sum (power) is used for the vocal-to-beat ratio (VBR) and the masking statistics. An 808 stem naturally receives almost no cut, because the unmask focus is 250 Hz–5 kHz.
- **Vocal group bus:** put VOCAL either on every vocal track **or** on the vocal group bus, never both. Doing both double-processes the vocal and double-counts the key. The link line warns when a VOCAL instance's input loudness is far above every other VOCAL's (heuristic, informational only).
- **More than one BUS in a group:** the link line shows `BUS ×2 — move one to group B`, and pass 2 is skipped. More than one BUS in a group is not supported.

### 1.4 Partial setups and no link

| Present / visible | What MIX does | Notes shown |
|---|---|---|
| VOCAL + BEAT + BUS | pass 1 (vocal + beat), then pass 2 (bus) | full result |
| VOCAL + BEAT | pass 1 only | "Add BUS on master for loudness and peak safety" |
| VOCAL + BUS (beat not linked) | VOCAL tone/dynamics/space; **vocal level kept** (LEVEL = −INPUT); pass 2 | "Add BEAT on the instrumental to balance vocal vs beat" |
| BEAT + BUS | beat tidy + static pocket; pass 2 | — |
| VOCAL alone (or link off) | standalone solve on its own capture, level kept | "LINK OFF" plus reason if detected |
| BEAT alone (or link off) | tidy plus static pocket. If the sidechain carried signal during learn, the sidechain is used as the vocal for masking statistics and the dynamic unmask runs from it | sidechain guide |
| BUS alone | pass 2 only: a mastering assistant | — |
| no MIX ever pressed | STYLE dropdown + `LOAD DEFAULTS` gives a usable starting point per role | — |

A role never changes its sound because a link appears or disappears, with one exception. The BEAT's dynamic unmask runs only while a key exists. If there is no vocal, there is nothing to unmask, which is also correct for instrumental exports.

### 1.5 Sidechain key setup (FL; fallback when bridged, and for other DAWs)

1. On the vocal track, right-click the send knob/arrow toward the beat track and choose **Sidechain to this track**.
2. In MAKE MIX (BEAT) open Wrapper ▸ Processing ▸ Input. Map input 2 ("Vox Key") to the vocal track. Set **KEY** to `SIDECHAIN` (or leave it on `AUTO` when no link exists).

The UI shows these two steps when KEY = SIDECHAIN and no signal has been seen. Whether FL delay-compensates the sidechain path is **UNVERIFIED** (A.4). The solver detects the alignment when both link and sidechain exist (§5.6, step P1.9).

---

## 2. Architecture

### 2.1 Principles

- **Chains are plain C++ classes** (`VocalChain`, `BeatChain`, `BusChain`). Each has `prepare/reset/setParams/process`. The plugin uses them on the audio thread, and the solver uses them for offline simulation. There is one implementation, so a simulation equals reality.
- **The solver is offline and deterministic.** It takes captured buffers and a style, and produces parameter sets. It uses no wall clock, randomness or thread timing.
- **The hub is infrastructure only.** It handles discovery, the live key, the capture handshake, the learn session and result delivery. It holds no DSP.
- **Audio-thread code** is allocation-free, lock-free and log-free. All of it lives in `Dsp.h`, `Chains.*` and the audio-side paths of `PluginProcessor.cpp` and `Hub.*`.

### 2.2 File layout (12 modules, 21 source files)

| File | WP | Responsibility | Depends on |
|---|---|---|---|
| `Source/Shared.h` | 0 | Constants, enums, `InstanceId`, `ParamSet`, `CompProfile`, `LearnedProfile`, `CaptureData`, solve request/result structs, `SeqBox<T>`, dB helpers. **No JUCE.** | std only |
| `Source/Params.h`, `Params.cpp` | 0 | Parameter table `kParams[]` (§6.1), `PIdx` enum, `VocalParams`/`BeatParams`/`BusParams` POD structs, `ParamSet` ↔ struct conversion. **No JUCE.** | Shared.h |
| `Source/Styles.h` | 0→3 | `StyleDef` + `kStyles[12]` constexpr table (§7). Header-only, **no JUCE.** | Shared.h |
| `Source/Dsp.h` | 1 | DSP primitives: v0 Biquad/Compressor/DeEsser/Saturator/DelayLine/Echo/Space (revised), `Svf` (TPT), `KWeighting`, `KeyBank`, `ContentClock`, `LoudnessMeter`, `TruePeak4x`, `TpLimiter`, `SoftClip`, `OnePoleDb`, `MsMatrix` | JUCE juce_dsp |
| `Source/Chains.h`, `Chains.cpp` | 1 | `VocalChain`, `BeatChain`, `BusChain`, taps, key interfaces, loudness-compensation prediction | Dsp.h, Params.h |
| `Source/Analysis.h`, `Analysis.cpp` | 3 | Offline features from captures: STFT bands, LTAS, BS.1770 series, L_w, VAD, YIN f0, crest, width, joint alignment, masking metrics | Shared.h, Dsp.h (K-weighting), juce_dsp FFT |
| `Source/AutoMix.h`, `AutoMix.cpp` | 3 | **Pure** solve maths: EQ fit, compressor amount, de-ess, sends, VBR level, beat unmask, bus EQ/glue/width/clip; `styleDefaults()`; explanation strings. **No JUCE.** | Shared.h, Params.h, Styles.h |
| `Source/Solver.h`, `Solver.cpp` | 3 | `runSolve(const SolveRequest&)`: analysis → AutoMix → chain simulations → verify loops → `SolveResult` | Analysis, AutoMix, Chains |
| `Source/Hub.h`, `Hub.cpp` | 2 | Registry, IDs, groups/style broadcast, key rings, capture handshake, learn session state machine, solve worker thread, apply/undo, link summary, bridged detection | Shared.h, Solver.h, juce_events (Timer, Thread) |
| `Source/PluginProcessor.h`, `PluginProcessor.cpp` | 2 | APVTS layout built from `kParams`, `ParamReader`, role dispatch, chunking, capture writer, hub key source/sink, state save/restore, programs = styles, meters | everything above |
| `Source/PluginEditor.h`, `PluginEditor.cpp` | 4 | Editor: header, role pages, detail view, bottom panel, timer | Processor, Hub (read-only views), Look |
| `Source/Look.h`, `Look.cpp` | 4 | `MixLook` (v0 `VoxLook` evolved), colours, `MeterBar`, `BandView` (masking/cut view), `MixButton`, `SegmentedRole` | JUCE gui |

Tools and tests:

| File | WP | Purpose |
|---|---|---|
| `tools/render.cpp` → target `mm_render` | 1 | Single-instance offline render. `mm_render in.wav out.wav --role vocal --style trap --bpm 94 [--mix] [id=value…]` prints CPU, LUFS in/out, peaks |
| `tools/mixrender.cpp` → `mm_mixrender` | 5 | Multi-instance harness (§10.2) |
| `tools/degrade.py` | 5 | Degradation protocol for stems (§10.3); writes WAVs + a JSON of the applied degradations |
| `tools/check_ffmpeg.py` | 5 | Cross-checks harness LUFS/TP against `ffmpeg ebur128=peak=true` |
| `tests/test_main.cpp` | 0 | `juce::UnitTestRunner` main (target `mm_tests`) |
| `tests/dsp_tests.cpp` | 1 | DSP and chain unit tests |
| `tests/hub_tests.cpp` | 2 | Hub concurrency/lifecycle tests (TSan target) |
| `tests/automix_tests.cpp`, `tests/analysis_tests.cpp` | 3 | Pure-engine and analysis tests |
| `.github/workflows/make-mix.yml` (repo root `/home/user/damn`) | 5 | Linux and Windows CI |

`Presets.h` is deleted; it is replaced by `Styles.h`.

### 2.3 Dependency graph (compile-time)

```
Shared.h ◄── Params.h ◄── Styles.h
   ▲            ▲  ▲          ▲
   │            │  └── AutoMix.*(pure) ◄──┐
   │       Dsp.h ◄── Chains.* ◄───────────┤
   │         ▲                            │
   └──── Analysis.* ◄──────────────── Solver.* ◄── Hub.* ◄── PluginProcessor.* ◄── PluginEditor.* / Look.*
```

### 2.4 Threads and data flow

```
 FL worker thread A            FL worker thread B             FL thread (after A,B)
 ┌───────────────┐             ┌───────────────┐              ┌──────────────┐
 │ VOCAL proc    │ key frames  │ BEAT proc     │              │ BUS proc     │
 │ VocalChain    ├────►hub ring├─►BeatChain    │  audio sums  │ BusChain     │
 │ capture write │  (lag ≥ L)  │ capture write │  in FL mixer │ capture write│
 └──────┬────────┘             └──────┬────────┘              └─────┬────────┘
        │ CaptureData (append-only, shared_ptr)                     │
        ▼                             ▼                             ▼
 ┌───────────────────────────────────────────────────────────────────────────┐
 │ Hub (message thread: registry mutex, Timer 10 Hz → tick())                │
 │  learn session per group ─► SolveRequest ─► worker thread: runSolve()      │
 │  ◄─ SolveResult ─ tick() applies to live instances (gestures)              │
 └───────────────────────────────────────────────────────────────────────────┘
```

---

## 3. The Hub

### 3.1 Creation and lifetime

- Each `MakeMixProcessor` holds `std::optional<juce::SharedResourcePointer<mm::Hub>>`, constructed in the processor **constructor** (message thread). Tests can instead construct a private `std::unique_ptr<Hub>` to simulate a bridged instance in another process.
- `Hub*` is cached. The hub is destroyed when the last instance is destroyed (message thread). `~Hub` stops the timer, sets the worker's cancel flag and joins the worker with `stopThread(10000)`.
- Slots are a `std::array<SlotShared, 32>` member of `Hub`. They are never freed while the hub exists. That makes reads from an audio thread safe even while the slot owner is being destroyed.
- `Hub::abiVersion = 1` is printed in the UI tooltip. Two different DLL copies produce two independent hubs. They cannot see each other, and the UI then simply shows fewer links. Cross-module detection is v2 (§12).

### 3.2 `Source/Shared.h` (WP0, verbatim interface)

```cpp
#pragma once
// 公共类型：不依赖 JUCE，音频线程 / 消息线程 / 解算线程都会用到
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
} // namespace mm
```

### 3.3 `Source/Hub.h` (WP2, public interface)

```cpp
#pragma once
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
};
} // namespace mm
```

### 3.4 Registry and instance IDs

**`attach`** (processor constructor, message thread, under `registryMutex`):
1. If `id` is null, generate it: `InstanceId::random()` from `juce::Uuid`.
2. If any live slot holds the same `id`, regenerate.
3. Find the lowest slot with even `generation`. If there is none, return −1. The instance then works standalone and the UI shows `LINK FULL (32)`.
4. Initialise the slot:
   - `owners[s] = &client`, `ids[s] = id`;
   - role/sub/group/style atomics (relaxed);
   - `lastHop = -1`, `syncMiss = 0`, `capState = Idle`, `capture = nullptr`;
   - every `ring[i].tag.store(0, relaxed)`.
5. Then `generation.fetch_add(1, release)` (now odd), then `live.fetch_or(1ull << s, release)`.

**`detach`** (processor destructor, message thread, under the lock):
1. `live.fetch_and(~bit, release)`, then `generation.fetch_add(1, release)` (now even), then `owners[s] = nullptr`.
2. If the slot participates in a session, it is marked gone. Its `shared_ptr<CaptureData>` stays alive in the session until the solve finishes.

**`restoreId(slot, current, saved)`** is called from `setStateInformation` (message thread):

| Case | Detection | Action | Typical FL trigger |
|---|---|---|---|
| Same | `saved == current` | nothing | undo, spare-state flip, reloading own preset |
| Clone | another live slot has `ids == saved` | keep `current`; store `cloneOf = saved` (state only, informational); copy role/sub/group/style/params from the state | Clone mixer track, "Save mixer track state" dragged, preset dragged onto another instance, paste |
| Adopted | otherwise | `ids[slot] = current = saved` | project load, undo after deleting the instance |

If two instances in an old project carry the same ID, the instance restored first adopts it. The second becomes a Clone and keeps its fresh ID. This is deterministic for a given load order.

**What the ID is used for.** Nothing in another instance's *sound* depends on an ID. IDs are used for:
- routing solve outputs to the right live instances: a slot can be reused during an async solve, but an ID cannot;
- UNDO MIX snapshots;
- stable ordering of the link list;
- `LearnedProfile.sessionId`, which marks instances that were mixed together.

### 3.5 Groups and style

- `group` (A–D, default A) partitions the hub. All lookups (key sum, sessions, link list) are group-local.
- STYLE is per instance (state). Changing it in any editor calls `hub.broadcastStyle`, which calls `hubSetStyle` on every peer in the group.
- On project load, peers may disagree (for example when an instance was saved from another project). The link line then shows `STYLE MIXED`, and MIX uses the initiator's style and rebroadcasts it.

### 3.6 Live key publication and reading

**What is published.** The VOCAL computes a `KeyBank` (§4.1) on its **dry chain output after LEVEL, before the pad delay**. It publishes one frame per key hop `Hk = keyHop(fs)` (128·r samples, 2.67 ms at 48 k) on the **content timeline**:
- `hop = floor(contentPos / Hk)`, where `contentPos = timeInSamples + i − coreLatency`. Hop boundaries are therefore timeline-locked and independent of the host block size.
- The frame holds 10 band mean-square levels in dB and `vad` (1 if the 10-band power sum > −50 dBFS).
- Nothing is published while the transport is stopped or the host provides no `timeInSamples`.
- The beat treats missing frames as silence.

**Ring write** (owner's audio thread only, slot `s`, entry `e = ring[hop & 511]`):
```
e.tag.store(0, relaxed); atomic_thread_fence(release);
e.bandDb[k].store(v[k], relaxed) …; e.vad.store(vad, relaxed);
e.tag.store(hop + 1, release); lastHop.store(hop, release); displayDb[k] (relaxed, smoothed)
```

**Ring read** (BEAT audio thread). `readVocalKey(group, self, hop, out)`:
```
mask = live.load(acquire); sum[10] = 0; any = late = false
for each bit s in mask, s != self:
  g1 = gen[s].load(acquire); if (g1 & 1) == 0 → continue
  if group[s] != group or role[s] != Vocal → continue
  e = ring[s][hop & 511]; t1 = e.tag.load(acquire)
  if t1 == hop + 1:
      read bands relaxed into tmp; atomic_thread_fence(acquire)
      if e.tag.load(relaxed) == t1 && gen[s].load(relaxed) == g1:
          sum[k] += kVocalWeight[sub[s]] · 10^(tmp[k]/10); any = true; continue
  lh = lastHop[s].load(acquire)
  if lh >= 0 && lh < hop && lh > hop − 64 → late = true     // 正在发布但还没写到这一帧
out[k] = powToDb(sum[k]); return any ? Ok : (late ? Late : Silent)
```
On `Late`, the BEAT uses the frame as silence and increments `syncMiss`. Late frames never block or spin.

**Sync condition (why it is order-independent).** The BEAT reports latency L and outputs content `c` at host input time `c + L`. To duck content `c` it needs the vocal frame for `c + look` (`look = keyLookahead(fs)` = 128·r). The VOCAL wrote that frame during the host cycle that processed input `c + look + coreLatency_v + Hk`. That cycle is strictly earlier than the BEAT's cycle whenever:

```
hostBlock  ≤  L − look − Hk − coreLatency_v     (≈ L − 272·r)
```

So the limit is ≤ 752 samples at 44.1/48 k when L = 1024, and ≤ 1776 samples when L = 2048. `latencyFor()` picks L = 2048·r automatically when the prepared `maxBlock` exceeds 736·r. Within that bound, the frames the BEAT reads were written in a previous cycle, whichever order or thread FL uses. That makes the result identical in realtime and export. Beyond the bound (host blocks larger than prepared, or FL export blocks bigger than the prepared maximum, **UNVERIFIED**, A.2), reads may be `Late`. The UI then shows `SYNC LATE n`, and the sidechain key is the deterministic alternative.

**Smart disable / bypass / removal.** If no frames arrive, the key reads `Silent`, so there is no duck. A removed instance's slot generation changes, so its stale frames are rejected. A bypassed VOCAL keeps publishing from its input (`processBlockBypassed`, §6.0), because the raw vocal is what is then heard.

### 3.7 Capture protocol (learn data)

States live in `SlotShared::capState`: `Idle → Armed → Running → StopReq → Stopped → Idle`.

| Step | Thread | Operation |
|---|---|---|
| arm | message | `cap = client.hubMakeCapture(pass, seconds)` allocates `CaptureData` (try/catch `bad_alloc`; on failure retry at 15 s, else the session fails with `OUT OF MEMORY`). The processor keeps the `shared_ptr` and does `shared.capture.store(raw, release)`. Then `capState.store(Armed, release)`. |
| start | audio | At block start, `s = capState.load(acquire)`. If `Armed` && playing && timeline known: `skip` handling (BUS pass 2 skips 0.5 s), then `CAS(Armed → Running, acq_rel)` |
| write | audio | If `Running`: detect discontinuity (`timeline != expectTimeline`). If so, append a segment (`pass++` if the timeline went backwards), `numSeg.store(+1, release)`. Then copy `min(n, capacity − written)` frames of the **chain input** (and the sidechain mono sum for BEAT), update `activeSamples` (50 ms RMS of mono > −45 dBFS), `written.store(new, release)`. Full → stays `Running` but writes nothing. |
| stop | message | `CAS(Running → StopReq)`, or `CAS(Armed → Stopped)` if it never started. From now on the solver may read `[0, written)`. Prefix reads are race-free because the audio thread only ever writes beyond `written`. |
| ack | audio | `s == StopReq` → `capState.store(Stopped, release)`. The buffer is not touched again. |
| reclaim | message | Only when `Stopped` (or in the processor destructor): `shared.capture.store(nullptr)`, `client.hubReleaseCapture()` drops the processor's `shared_ptr`, `capState = Idle`. The session's own `shared_ptr` keeps the memory until the solve ends, or until the captures are released (§3.9). |

Capture lengths:
- Pass 1: 30 s capacity per VOCAL/BEAT.
- Pass 2: 20 s for the BUS.

Memory is `fs × channels × 4 B × seconds`: 11.5 MB at 48 k stereo for 30 s, and 46 MB at 192 k. Retained captures across all groups are capped at 256 MB; the oldest session's captures are released first.

### 3.8 Learn session (per group, message thread only)

```
Idle ──beginMix──► Listen1 ──done──► Solve1 ──result──► [apply] ──BUS==1──► Settle2 ─► Listen2 ─► Solve2 ─► [apply] ─► Done
  ▲                  │ (no VOCAL/BEAT → Settle2)          └──BUS!=1──────────────────────────────────────────► Done
  └── dismiss ◄── Failed / Cancelled ◄── cancel / error at any phase
```

- **`beginMix(initiator, style)`:**
  1. Refuse if the group's session is active.
  2. Broadcast the style.
  3. Snapshot every live member (params + profile) for UNDO.
  4. Choose the participants: pass 1 is every VOCAL and BEAT in the group; pass 2 is the single BUS.
  5. Arm the pass-1 captures. If the group has no VOCAL/BEAT, arm the BUS directly (Settle2).
  6. `startTimerHz(10)`.
- **`tick()` in Listen1:**
  - `vocalSec_i = activeSamples_i / fs`, `beatSec_j = written_j / fs`.
  - `progress = min(1, min_i(vocalSec_i)/15, min_j(beatSec_j)/15)`, with an empty term counted as 1.
  - Status:
    - `PRESS PLAY` if no participant's `written` advanced in the last 0.5 s;
    - `NEED VOCAL` if beats advance but `max vocalSec < 1` after 10 s;
    - `LISTENING 1/2 · nn%` otherwise.
  - Stop when any of these holds:
    1. `progress ≥ 1`;
    2. any capture is full (30 s);
    3. the user pressed MIX again (STOP) and every vocal has ≥ 6 s active, or every beat ≥ 6 s when there is no vocal.
  - If a capture fills before the 6 s minimum: `Failed("NOT ENOUGH VOCAL")`. No parameters change.
- **Solve1.** Request every participant to stop (§3.7). Build the `SolveRequest` from live participants (id, role, sub, name, capture `shared_ptr`, `hubSnapshotParams`, comp, sidechain flag), with `linkKeyAvailable = (VOCAL count ≥ 1)`. Post it to the worker. The status shows `MIXING…`. Reclaim the captures once they are acknowledged as stopped.
- **Apply** (`tick()` sees the result):
  - For each `SolveOutput`, find the live slot with that id, under the lock, and call `owner->hubApply(params, profile)`. A missing instance is skipped.
  - The session summary comes from `result.summary`.
  - `!ok` → `Failed(error)`.
- **Settle2 / Listen2.** Arm the BUS capture with `skip = 0.5 s × fs`. This lets parameter smoothing settle on the new values: gains have 30 ms ramps and the unmask hops are converging. Then `progress = written/(12 s)`. Stop at ≥ 12 s, or on STOP with ≥ 6 s.
- **Solve2.** Request with `pass = 2`, the BUS source, and `busTargetLufs`/`busCeilingDb` taken from the BUS's current `m_target`/`m_ceiling`. Then apply.
- **Done.** The status reads `DONE`. `canUndo` is true. `canRemix` is true while the pass-1 captures are retained. The timer stops after 3 s.
- **Transport stopped mid-listen:** the session waits indefinitely and shows `PRESS PLAY`. **Seeking or looping** creates segments, which the solver aligns (§4.2.6).
- **Role or group changes** are disabled in the UI while the instance participates in an active session.
- **Instance destroyed mid-session:** marked gone. Its capture remains usable for the solve (the vocal still informs the beat), and its output is skipped at apply.

Timing guarantees:
- Nothing that affects audio depends on timer timing. The timer only decides *when* to stop listening; it is a user-driven action, equivalent to a human pressing stop.
- Captures and the solver are deterministic given the captured data.

### 3.9 Worker, apply, UNDO, re-mix

- **Worker.** `SolveWorker : juce::Thread` with a single job slot (`std::mutex`, message ↔ worker only; never touched by the audio thread). It runs `mm::runSolve(req)` (Solver.h) and stores `std::unique_ptr<SolveResult>`. `tick()` picks it up.
  - `setSynchronousSolve(true)` makes `tick()` call `runSolve` inline. The harness and tests use this.
  - Cancel sets `std::atomic<bool> cancel`. The solver checks it after every analysis or simulation step, at least every 1 s of simulated audio.
- **`hubApply`** (processor, message thread):
  ```
  for (id, v) in params: p = apvts.getParameter(id); nv = p->convertTo0to1(v)
     if |nv − p->getValue()| > 1e-6: p->beginChangeGesture(); p->setValueNotifyingHost(nv); p->endChangeGesture();
  learned = profile; compBox.write(profile.comp)        // SeqBox → 音频线程
  ```
  - Every change is a normal host-visible edit: knobs move, FL records undo, and automation clips (if any) win on playback. When a parameter has an automation clip, MIX's value is overridden; the explanation cannot detect this (§12).
- **UNDO MIX.** Applies the session snapshot to every live participant through the same path, then clears `canUndo`.
- **Re-mix on STYLE change.** If the group has a `Done` session with retained pass-1 captures, `remix(style)` reruns pass 1 from the captures immediately, applies it, then re-arms pass 2, since the bus must hear the new mix. If none are retained, the UI offers `LOAD DEFAULTS` (§7.3). Captures are released when a new `beginMix` starts, when an instance is destroyed, or when the 256 MB cap evicts them.

### 3.10 Threading rules and memory ordering

| Data | Writer | Readers | Mechanism |
|---|---|---|---|
| registry (owners, ids, names, sessions) | message thread | message thread | `std::recursive_mutex registryMutex`. It is recursive because `hubApply` → `setValueNotifyingHost` may re-enter the plugin synchronously through host callbacks. |
| `generation`, `live`, role/sub/group/style atomics | message (locked) | any | release store / acquire load |
| key ring entry | owner audio thread | other audio threads, editors | per-entry seqlock (tag + fences), §3.6 |
| `lastHop`, `displayDb`, `cutDb`, meters | owner audio thread | any | relaxed atomics (display), acquire for `lastHop` |
| `CaptureData` samples/segments | owner audio thread | worker, message | append-only; `written`/`numSeg` release/acquire |
| `capState` | both | both | CAS state machine, §3.7 |
| `CompProfile` per instance | message thread (`hubApply`, `setStateInformation`) | own audio thread | `SeqBox<CompProfile>` |
| APVTS parameters | host/message/automation | own audio thread | JUCE atomics |
| role/sub/group of own instance | message thread | own audio thread | `std::atomic<uint8_t>` in processor |
| solve job/result | message ↔ worker | — | mutex (never audio) |

Rules:
- **R1:** No `thread_local` and no thread-ID logic. FL may run an instance on a different worker thread each cycle.
- **R2:** The audio thread never allocates, locks, logs, calls `MessageManager`, or waits.
- **R3:** Each slot's real-time fields have exactly one writer: its owner.
- **R4:** Readers never spin. A seqlock retry fails over to `Silent`/old value.
- **R5:** No audio decision uses `isNonRealtime()`, wall-clock time or timers.

Allocation-free paths: `processBlock`/`processBlockBypassed` (all roles), `publishKey`, `readVocalKey`, capture write, `SeqBox::read`, every chain `process` and `setParams` (fixed arrays only).

Allocation happens in:
- constructors, `prepareToPlay` (delay lines, oversampling, reverb buffers sized for `maxBlock`), `CaptureData` construction, and `attach` (message thread);
- the solver (worker thread).

### 3.11 Link status and bridged/sandboxed detection

- `bridgedHost = File::getSpecialLocation(hostApplicationPath).getFileName().containsIgnoreCase("ilbridge")`.
  - The text is `LINK OFF — BRIDGED: Wrapper ▸ Processing ▸ untick "Make bridged"`.
- `sandboxHint` if the executable name contains `PluginHost` or `reaper_host`, **and** no peers are seen after 3 s of playing.
  - The text is `LINK OFF — SANDBOXED: Bitwig hosting = Together / Reaper: not dedicated process`.
- `alone` if this instance has played more than 3 s and its group holds only itself.
  - The text is `ALONE · works standalone`.
- The full link text is `LINK A · VOCAL ×2 · BEAT ×1 · BUS`. The tooltip lists each peer as `VOCAL LEAD · "Vox" · active`.
  - `active` means `lastMs` advanced within 1 s.

---

## 4. Analysis module

### 4.1 Realtime analysis (in chains; `Dsp.h`, WP1)

| Primitive | Definition | Where |
|---|---|---|
| `KeyBank` | 10 constant-0 dB-peak band-pass filters (TPT SVF band-pass output × k, Q = 2.0) at `kKeyCentresHz`, applied to the mono mix `(L+R)/2`. Squares are accumulated over a timeline-locked hop `Hk`, giving per hop `bandDb[k] = 10·log10(mean x_k² + 1e-12)`. Centres are capped at 0.45 fs (a band above the cap emits −120 dB). | VOCAL (dry output), BEAT (delayed beat mid, sidechain mono) |
| `ContentClock` | Ring of `(inputSampleIndex, timeline)` change points. It maps an output sample, delayed by D, back to the timeline of its content, so the BEAT knows the content hop across jumps. | BEAT |
| `KWeighting` | BS.1770 pre-filter by bilinear transform at any fs: shelf f0 = 1681.974 Hz, G = +3.99984 dB, Q = 0.70718; high-pass f0 = 38.1355 Hz, Q = 0.50033 (libebur128 constants) | meters, `L_w` |
| `LoudnessMeter` | K-weighted mean squares per channel in 100 ms sub-blocks counted on its own counter. Momentary = last 4, short-term = last 30. Integrated: gated histogram (0.1 dB bins over −70…+5, 750 bins, preallocated) with absolute −70 and relative −10 gates. `reset()` at transport start. | BUS output meter (all roles show momentary) |
| `TruePeak4x` | Polyphase 4× windowed-sinc (Kaiser β = 8, 16 taps per phase, latency 8 samples at base rate). Used at 2× for fs ≤ 100 k and 1× (sample peak) above. | BUS limiter detector + meter |

The cost is about 0.1 % of a core per KeyBank at 48 k (10 SVFs on one channel plus 10 adds per hop).

### 4.2 Offline analysis (solver; `Analysis.*`, WP3)

The solver runs on captured audio. Resolution and cost are not real-time constrained.

#### 4.2.1 Framings

| Name | Window | Hop | Use |
|---|---|---|---|
| short | Hann, N = 2048·r | 512·r (10.7 ms at 48 k) | per-frame 32-band levels (masking, VAD support) |
| long | Hann, N = 8192·r | 2048·r | LTAS (32 bands) and octave bands (good below 100 Hz) |
| loud | 400 ms blocks, 100 ms step | — | BS.1770 momentary/integrated/LRA; `L_w` (same with an extra 150 Hz 2nd-order Butterworth HPF after K-weighting) |
| sample | — | — | 10 ms peaks, 30 ms RMS, 50 ms crest, 200 ms C² windows |

Frames are placed on the **timeline grid** inside each capture segment. The first frame centre is the first multiple of the hop at or after `segment.timeline`. Frame key = `(pass, floor(timelineCentre / hop))`.

#### 4.2.2 Bands

**32 ERB-spaced bands, 40 Hz–16 kHz** (ERB-number scale, Glasberg & Moore). Bands above 0.45 fs are dropped (weight 0).
- Edges (Hz): 40, 77, 119, 166, 220, 281, 351, 430, 521, 623, 739, 872, 1022, 1193, 1388, 1609, 1860, 2145, 2470, 2839, 3258, 3735, 4277, 4893, 5593, 6389, 7294, 8323, 9492, 10821, 12331, 14048, 16000.
- Centres (ERB-midpoint, Hz): 58, 97, 142, 192, 250, 315, 389, 474, 570, 679, 803, 945, 1105, 1287, 1495, 1730, 1998, 2303, 2649, 3042, 3489, 3997, 4575, 5232, 5979, 6827, 7792, 8888, 10135, 11552, 13162, 14993.
- Band power `E_k = Σ_bins w(bin)·|X|²` uses rectangular bin membership; bins straddling an edge are split linearly.

**Octave bands.** Long-STFT bins are grouped into octaves centred at `kOctCentresHz` (edges at centre/√2 and centre·√2; the 16 k band is capped at 0.45 fs).

**Pink-referenced levels (for every tonal comparison).**

```
pinkDb_k = 10·log10( E_k / (f_hi,k − f_lo,k) ) + 10·log10( 0.2316 · f_c,k )
```

That is, mean PSD scaled to a third-octave bandwidth at the centre frequency, so pink noise reads flat and tilts are in "third-octave band dB per octave", matching the research numbers. Absolute band powers (`ltasDb`) are kept for loudness predictions.

**Importance weights** I(f) for vocal bands: log-f linear interpolation through
(100, 0), (150, 0.2), (500, 0.6), (1000, 0.9), (2000, 1.0), (3000, 1.0), (4000, 0.9), (6000, 0.5), (8000, 0.25), (10000, 0), normalised to sum 1 over the bands. For the 10 key bands: 0.25, 0.40, 0.55, 0.70, 0.85, 0.95, 1.0, 1.0, 0.85, 0.45.

#### 4.2.3 Features per role (from the chain **input** capture unless noted)

| Feature | Definition | Roles |
|---|---|---|
| `vad[t]` | 400 ms K-weighted momentary loudness (vocal input) > max(P10_all + 15, I_gated − 20) LUFS, 150 ms hangover | VOCAL (also used for joint frames) |
| `activeSec` | count(vad) × step | VOCAL |
| `iActive` | power-mean K-weighted loudness over vad frames (LUFS) | VOCAL |
| `momLufs[]` | 400 ms momentary values (100 ms step) on vad frames | VOCAL |
| `f0P5`, `f0P50`, `voicedSec` | YIN on 4× (≤50 k; 8× at ≤100 k, 16× above) decimated mono, 40 ms window, τ for 60–700 Hz, CMND threshold 0.13, parabolic refinement, vad frames only, voiced if aperiodicity < 0.2; percentiles from a 2 %-bin histogram | VOCAL |
| `ltasPink[32]`, `ltasDb[32]` | power-mean of long-STFT band power over vad frames (VOCAL) or all frames with momentary > −50 LUFS (BEAT/BUS) | all |
| `levelW[80]` | 30 ms RMS (mono) histogram, 1 dB bins from −80 dBFS, over active frames, normalised → `CompProfile` | all |
| `crestDb` | median over active 50 ms windows of (10 ms peak dB − 30 ms RMS dB) | VOCAL, BUS |
| `crestC2` | median over active 200 ms windows of peak²/mean-square (linear) | VOCAL |
| `hfTilt` | least-squares slope of `ltasPink` vs log2 f over 4–12 kHz | VOCAL, BEAT |
| `octMid[10]` | octave levels of the mid (L+R)/2, normalised to the mean of the 500/1k/2k octaves | BUS |
| `widthLu` | K-weighted integrated loudness of S minus that of M (gated on M) | BUS, BEAT |
| `iIn`, `tpIn`, `plr` | integrated LUFS, 4× true peak, TP − I | all |
| `rmsGlue[]` | 10 ms RMS (dB) of the 120 Hz-high-passed stereo-linked bus input, active frames | BUS |
| `keyActive` | sidechain capture > −50 dBFS for ≥ 2 s | BEAT |

From **simulated chain outputs** (taps, §6.6):
- sibilance detector `d[n]`, 5 ms frames;
- dry/echo/space loudness for WDR;
- compressor and limiter GR;
- `L_w` series.

#### 4.2.4 Masking metrics (joint; VOCAL-sum vs BEAT-sum on joined short frames)

Inputs per joined frame:
- `V_k = powToDb(Σ_v w_v·E_v,k)`, the vocal outputs (weights `kVocalWeight`);
- `B_k = powToDb(Σ_b E_b,k)`, the beat outputs.

Only frames with any vocal `vad` count, and only bands with `V_k > max_j V_j − 35` and centre in 150 Hz–8 kHz.

| Metric | Formula |
|---|---|
| masked-cell fraction | share of counted cells with `B_k > V_k` |
| SII proxy | mean over frames of `Σ_k I_k·clamp((V_k − B_k + 15)/30, 0, 1)` |
| `medBeatMinusVoc[k]` | median of `B_k − V_k` over counted frames (0.5 dB histogram) |
| VBR (`L_w` ratio) | `10·log10(mean_{vad} P_w,vocal) − 10·log10(mean_{same frames} P_w,beat)` with `L_w` 400 ms blocks. For a LEAD, frames = that lead's vad frames. |
| LAR_mid (report only) | same over a 300 Hz–5 kHz 4th-order band-pass |

#### 4.2.5 Memory bounds and CPU

- Captures: §3.7.
- Analysis working memory per capture: frame arrays of 30 s × 94 frames/s × (32 + 8) floats ≈ 0.5 MB. Temporary sample buffers equal the capture size (one simulation output at a time).
- Solver CPU on one background core (48 k, 2 vocals + 1 beat, 20 s): analysis ≈ 0.3 s, chain simulations ≈ 6 × 0.4 s, total **≤ 4 s**. Pass 2 (12 s bus, ≤ 10 bus simulations at about 1.5 % RT) **≤ 2.5 s**.
- Harness budget: §10.2.

#### 4.2.6 Joining captures

- Each capture's segments give `(pass, timeline)` for every frame. Frames from different instances are joined on equal `(pass, frameIndex)`.
- `pass` counts backwards jumps since that capture started, so loop playback joins correctly.
- Frames missing in one instance (late start, smart disable) are excluded.
- If fewer than 3 s of joined vocal-active frames exist, the joint features are marked invalid. The solver then uses style defaults for unmask depth and skips the masking EQ terms.

### 4.3 `Source/Analysis.h` (WP3, public interface)

```cpp
#pragma once
#include "Shared.h"

namespace mm::analysis
{
struct FrameSeries                                   // 短帧（时间线对齐）
{
    int hop = 0; double fs = 0;
    std::vector<int32_t> pass; std::vector<int64_t> index;          // 时间线帧号
    std::vector<std::array<float, kAnaBands>> bandDb;               // 绝对功率 dB
};
struct LoudSeries { std::vector<float> momLufs, wLufs; std::vector<int32_t> pass; std::vector<int64_t> index; };  // 100 ms 步长
struct Features
{
    float activeSec = 0, voicedSec = 0, iIn = -100, iActive = -100, tpIn = -100, f0P5 = 0, f0P50 = 0;
    float crestDb = 12, crestC2 = 10, hfTilt = 0, widthLu = -20;
    std::array<float, kAnaBands> ltasPink {}, ltasDb {};
    std::array<float, kOctBands> octMid {};
    std::array<float, 80> levelW {};
    std::vector<float> momActive;                     // 有声的 400 ms 响度
    std::vector<uint8_t> vad;                         // 对应 LoudSeries
    FrameSeries frames; LoudSeries loud;
    bool keyActive = false;
};
// 时间线信息来自 CaptureData::seg；n = written（只读前缀）
Features analyse (const CaptureData&, Role, int64_t n);
Features analyseBuffer (const float* const* ch, int numCh, int64_t n, double fs,
                        const std::vector<CaptureData::Segment>&, Role);       // 给仿真输出用
float integratedLufs (const float* const* ch, int numCh, int64_t n, double fs);
float truePeakDb (const float* const* ch, int numCh, int64_t n, double fs);
struct Joint { bool valid = false; float maskedFraction = 0, sii = 0, vbr = 0, larMid = 0; std::array<float, kAnaBands> medBeatMinusVoc {}; int frames = 0; };
// vocals / beats：每个实例的特征（要求 frames/loud 已算好）；leadIndex = 只统计该 LEAD 的 vad
Joint joint (const std::vector<const Features*>& vocals, const std::vector<float>& vocalWeights,
             const std::vector<const Features*>& beats, int leadIndex = -1);
std::vector<float> sibilancePercentiles (const float* sibDb, int64_t n, const std::vector<uint8_t>& vadPerSample);   // P50, P90, P99
} // namespace mm::analysis
```

---

## 5. Auto-mix engine

### 5.1 `Source/AutoMix.h` (WP3, pure: std only)

```cpp
#pragma once
#include "Params.h"
#include "Styles.h"

namespace mm::automix
{
// —— 输入：都由 Solver 从 analysis::Features 填 ——
struct VocalIn  { float iActive, activeSec, voicedSec, f0P5, crestC2, crestDb, hfTilt; std::array<float, kAnaBands> ltasPink; std::vector<float> momActive; };
struct BeatIn   { float hfTilt; bool keyAvailable; };
struct JointIn  { bool valid; std::array<float, kAnaBands> medBeatMinusVoc; float maskedFraction; };
struct BusIn    { float iIn; std::array<float, kOctBands> octMidNorm; float widthLu; std::vector<float> rmsGlue; };

struct EqFit { float lowF, lowG, midF, midG, presF, presG, airF, airG; float residualDb; };

VocalParams vocalStatic (const StyleDef&, const VocalIn&, const JointIn*, SubRole, bool beatLinked, bool unmaskActive,
                         const BeatIn* beatSum, double fs);         // 5.3 步骤 V1–V7
EqFit fitVocalEq (const std::array<float, kAnaBands>& targetMinusMeasured, const std::array<float, kAnaBands>& weight,
                  float hpfHz, int hpfSlope, double fs);
float compAmountFor (const std::vector<float>& momInternal, float crestDb, float targetSpread);   // 二分
void  ballisticsFromCrest (float crestC2, float& attackMs, float& releaseMs);
float deessAmountFor (float p90, float margin);                    // 返回 0..1
float sendAmountCorrect (float amount, float wdrTarget, float wdrMeasured);
float levelForVbr (float vbrTarget, float vbrMeasured);            // 限幅 [-24, 24]
BeatParams beatStatic (const StyleDef&, bool keyAvailable);
float adaptUnmask (float amount, float maskedAfter);
BusParams busStatic (const StyleDef&, const BusIn&, float targetLufs, float ceilingDb);   // EQ、glue 比例/时间、宽度
float glueThresholdFor (const std::vector<float>& rmsDb, float ratio, float kneeDb, float targetMeanGr);
float clipFor (const StyleDef&, float overshootP97Db);
ParamSet styleDefaults (StyleId, Role);                            // 7.3
// 静态曲线（和 Dsp.h 的 Compressor 完全一样的软拐点）
float staticGrDb (float levelDb, float thrDb, float ratio, float kneeDb);
} // namespace mm::automix
```

### 5.2 Common mathematics

- **Soft-knee static curve** (identical in the DSP, the prediction and the solver). W is the knee, R the ratio and o = L − T:
  `GR(L) = 0` if `2o ≤ −W`; `(1/R − 1)·(o + W/2)²/(2W)` if `|2o| < W`; `(1/R − 1)·o` otherwise.
- **Gaussian log-f smoothing** of a band vector: weights `exp(−0.5·(log2(f_j/f_k)/σ)²)` with σ = 1/3 octave, renormalised.
- **Confidence shrink.** `c = n/(n + n0)` with n = active seconds and n0 = 10 s. `θ = θ_default + c·(θ_measured − θ_default)` for the EQ deviation and the HPF.
- **Dead band.** `db(x, t) = sign(x)·max(0, |x| − t)`.
- **Clamps and rounding.** Every output is clamped to the parameter range (§6.1). Gains are rounded to 0.1 dB, frequencies to 3 significant digits, amounts to 0.01.

### 5.3 VOCAL solve (per VOCAL instance)

- **V1 Input normalisation.** `v_input = clamp(kInternalRefLufs − iActive, −24, +24)`. The −20 LUFS internal level gives every threshold mapping (§6.2) the same meaning for every recording, and fixes the saturator's −12 dBFS reference.
- **V2 Low cut.**
  - `f = clamp(S.vHpfK·f0P5, S.vHpfMin, S.vHpfMax)` if `voicedSec ≥ 2`; otherwise `(S.vHpfMin + S.vHpfMax)/2`. Shrink with c toward the midpoint.
  - DOUBLE/ADLIB: `f ×= 1.2`, clamped to `1.3·S.vHpfMax`.
  - `v_hpfSlope = S.vHpfSlope`.
  - `v_lpf = S.vLpf` (22000 = off).
- **V3 Tonal target and EQ.**
  1. `M_k` = `ltasPink` + the predicted HPF/LPF response at f_k. Normalise M and T by their importance-weighted mean over 200 Hz–5 kHz.
  2. `T_k = S.vTilt·log2(f_k/1000) + S.vMudG·G(f_k; S.vMudF) + S.vPresG·G(f_k; S.vPresF) + S.vAirG·Sh(f_k; S.vAirF)`, where `G(f; f0) = exp(−0.5·(log2(f/f0)/0.5)²)` and `Sh(f; f0) = 1/(1 + exp(−(log2(f/f0))/0.25))`.
  3. `D_k = S.vMatch·(T_k − M_k)`.
  4. Masking terms, only if the joint features are valid:
     - for f_k < 400 Hz, `D_k −= min(3, 0.15·max(0, medBeatMinusVoc_k − 10))`;
     - for 1–5 kHz, `D_k += min(2.5, β·max(0, medBeatMinusVoc_k − 3))·I_k/I_max`, with β = 0.25 if the beat unmask is active, else 0.5.
  5. Smooth (σ = 1/3 oct), shrink with c, apply the dead band at 1 dB, clamp to [−6, +4].
  6. Fit: `fitVocalEq(D, w, …)`, with weights `w_k = I_k·(1 if M_k > max M − 40 else 0.2)` for 100 Hz–12 kHz, else 0. Filters, with `|H|` evaluated exactly from the SVF bell/shelf responses (same formulas as `Dsp.h`):

     | Filter | Frequency range | Q | Gain range |
     |---|---|---|---|
     | LOW bell | 120–800 | 1.2 | −12…+6 |
     | MID bell | 500–3000 | 1.0 | −8…+6 |
     | PRES bell | 1500–8000 | 0.9 | −6…+8 |
     | AIR shelf | 6–16 k | 0.7 | −6…+10 |

     Algorithm:
     - initialise every gain to 0 and every frequency to the style centre;
     - 3 sweeps of coordinate descent over the 4 filters. For each filter, try 12 log-spaced frequencies in its range. For each frequency the gain is `g = Σ w_k r_k h_k / (Σ w_k h_k² + λ)` (λ = 0.05), where h_k is the dB response per +1 dB (unit-gain response at +3 dB divided by 3) and r_k is the residual excluding this filter. Keep the (f, g) with the lowest weighted squared error. Then recompute residuals with exact responses at the chosen gains.
     - The AIR gain additionally gets `clamp(0.3·(beatHfTilt − vocalHfTilt), −1, +1)` (tone glue toward the beat) when a beat is linked.
- **V4 Dynamics.**
  - `momInternal = momActive + v_input`.
  - `v_comp = compAmountFor(momInternal, crestDb, S.vSpread)`: bisection (24 iterations) on a ∈ [0, 1]. The predicted spread `P90 − P10` of `L' = L + GR_A(L; a) + GR_B(L + crestDb + GR_A(L; a); a)` uses the §6.2 mappings. If spread(0) ≤ target, a = 0.15; if spread(1) > target, a = 1.
  - `ballisticsFromCrest`: `τ_A = 2·80/C²` ms, `τ_R = 2·1000/C² − τ_A` ms (Giannoulis), clamped to attack [3, 40] and release [60, 400].
- **V5 De-ess.** Takes the sibilance P90/P99 of `d[n]` from the first simulation's `sibDb` tap on vad samples.
  - `θ* = P90 + S.vDeessMargin`; `v_deess = clamp((−1 − θ*)/11, 0, 1)`.
  - `v_deessF = clamp(0.75·centroid(frames with d > P90), 4000, 8000)`. Fallback: 5000 if `f0P50 < 180` else 6500.
- **V6 Warmth and space.**
  - `v_warmth = S.vWarmth`, +0.10 for ADLIB.
  - `v_spaceDecay = S.vRevDecay`, `v_preDelay = S.vPreDelayMs`, `v_echoTime = S.vEchoTime`, `v_echoFb = S.vEchoFb`, `v_wetDuck = S.vWetDuck`.
  - Initial `v_space`/`v_echo` from the calibration (§7.3).
  - DOUBLE/ADLIB: sends ×0.7.
- **V7 Level** (after simulation, §5.6):
  - LEAD with beat: `v_level = levelForVbr(S.vbr, VBR_meas)`, where VBR_meas uses the simulated output at `v_level = 0`.
  - DOUBLE/ADLIB: `v_level = (Lw_lead_final − Lw_this_at0) + S.dblOffset` (or `adlibOffset`). Each `L_w` is a power mean over that instance's own vad frames.
  - No beat linked: `v_level = −v_input` (keeps the original loudness).

### 5.4 BEAT solve

- `beatStatic(S, keyAvailable)` sets:
  - `b_hpf = S.bHpf`, `b_monoBelow = S.bMono`;
  - `b_pocketF = S.bPocketF`, `b_pocketQ = 1.0`;
  - `b_pocketG = keyAvailable ? S.bPocketLinked : S.bPocketStatic`;
  - `b_unmask = S.bDmax/9`, `b_sens = S.bSens`, `b_focusLo/Hi = S.bLo/S.bHi`, `b_release = S.bRelMs`, `b_side = S.bSide`, `b_ride = S.bRide`, `b_level = 0`.
  - `b_key` is left unchanged if the user forced it; otherwise `AUTO`.
- `keyAvailable = linkKeyAvailable || keyActive(sidechain during learn)`.
- **Depth adaptation**, after the beat simulation with the vocal key: `adaptUnmask(a, maskedAfter)`:
  - if maskedAfter > 0.42, `a·1.25`;
  - if < 0.25, `a·0.8`;
  - clamped to [0.2, 1]. One re-simulation for the report.
- **Loudness.** The chain auto-compensates static EQ (§6.3.5), so `b_level` stays 0. The unmask is not compensated (about −0.2 dB by design).
- **Sidechain alignment** (only when link and sidechain both exist, step P1.9): `scAlignRaw` is written into the BEAT's CompProfile.

### 5.5 BUS solve (pass 2)

- **B1 Tonal EQ.**
  - `D_j = S.busCurve_j − octMidNorm_j`, then dead band `tol_j/2`. The tolerances are `{5, 5, 3, 3, 2, 2, 2, 3, 3.5, 5}` dB for 31.5…16 k.
  - Weights `{0.3, 0.6, 1, 1, 1, 1, 1, 1, 1, 0.5}`.
  - WLS fit of the 4 fixed filters (low shelf 90 Hz Q 0.7, bell 300 Hz Q 0.9, bell 3 kHz Q 0.8, high shelf 10 kHz Q 0.7). Closed-form 4×4 normal equations on unit responses at the octave centres, λ = 0.1. Gains clamped to ±2 dB, the solver limit; the parameter range is ±4. Then `m_lpf = S.busLpf`.
- **B2 Glue.**
  - `m_glueRatio/Atk/Rel/ScHpf` from the style.
  - `m_glue = glueThresholdFor(rmsGlue, ratio, 6, S.glueGr)`: bisection on T so that the mean static GR over active frames equals the target.
  - Verified in simulation: `T += (GR_meas − GR_target)/(1 − 1/R)` (one correction).
- **B3 Width.** `Δ = S.width − widthLu`. If `|Δ| > 1.5`: `m_width = clamp(10^(clamp(Δ, −4, +3)/20), 0.6, 1.4)`, else 1.0. `m_monoBelow = S.busMono`.
- **B4 Drive and clip** (simulation, §5.6 P2):
  - target = `busTargetLufs` (the BUS's `m_target`), ceiling = `busCeilingDb` (`m_ceiling`).
  - Clip: `m_clip = S.clipDb > 0 ? clamp(6 − (O97 − S.clipDb), 0, 6) : 0`, where O97 is the P97 of the pre-limiter true-peak overshoot above the ceiling, measured in the first drive simulation. With the clip threshold defined as `ceiling + (6 − m_clip)` dB, the clipper takes about `S.clipDb` off the P97 peaks.
  - Drive: bisection on `m_drive ∈ [0, 24]`, at most 8 simulations, stopping at `|I_out − target| ≤ 0.1 LU`. Start at `target − I_out(drive 0) + 1`.
  - Limit: if the limiter GR at P95 exceeds `S.maxLimGr` at the target, stop at the drive where P95 GR equals `maxLimGr` and report `TARGET NOT REACHED CLEANLY`.
- **B5 Profile.** The BUS `CompProfile` (level histogram of the bus input, ltas) feeds the glue auto-makeup (§6.4).
- **B6 Unlinked-track hint** (informational). Compare the pass-2 mean `L_w` of the bus with the pass-1 mean `L_w` of (simulated vocal out + beat out) at their solved levels. If the bus is more than 4 dB louder: "UNLINKED TRACKS ON MASTER ≈ n dB — vocal may sit loud".

### 5.6 Solver orchestration (`Source/Solver.h`, WP3)

```cpp
#pragma once
#include "Shared.h"
namespace mm
{
// 阻塞调用，在 hub 的解算线程里跑；确定性（同样的输入 → 同样的输出）
SolveResult runSolve (const SolveRequest&);
}
```

**Pass 1** (`req.pass == 1`):
1. **P1.1** Analyse every capture with `analysis::analyse`. Missing or empty captures are skipped. If no LEAD/DOUBLE/ADLIB has ≥ 6 s active and no BEAT has ≥ 6 s, fail with `NOT ENOUGH AUDIO`.
2. **P1.2** Static solves: `vocalStatic` per vocal; `beatStatic` per beat.
3. **P1.3** Simulate each `VocalChain` on its capture:
   - `ChainSpec{fs, 4096, numCh, latency = 0}`, `v_level = 0`, the new CompProfile (level histogram and ltas from features, `satGainDb` from P1.4).
   - Taps: dry, echo, space, sibDb, compGr.
   - Process in 4096-sample blocks per segment with `ProcessContext{timeline, bpm}`. Collect key frames through a `KeySink` into a map `(pass, hop) → frame`.
   - Delay-compensate by `coreLatency()`.
4. **P1.4** Saturator table: run `dsp::Saturator` alone on the `preSat` tap for warmth 0, 1/8, …, 1, giving `satGainDb[i] = I(out) − I(in)`. Before P1.3, the table is filled once with a quick pass on the normalised capture through HPF+EQ only.
5. **P1.5** Verify and correct (at most 2 simulations per vocal):
   - spread error → `v_comp += 0.06·(spread − target)`;
   - sibilance `P99 − P90 > S.vSibTarget` → `v_deess += 0.05·excess`;
   - sends: `amount ← sendAmountCorrect(amount, target, measured)` for echo and space;
   - re-simulate if anything changed by more than 0.02 / 0.5 dB.
6. **P1.6** Simulate each `BeatChain` (latency = look + Hk) with the static params and `KeySource = none`. Its outputs are the "before" beat.
7. **P1.7** VBR and level: `vbr_i = joint(lead i).vbr`, then V7. Key frames are offset by `v_level` dB, exact because LEVEL precedes the KeyBank.
8. **P1.8** Simulate each `BeatChain` with a `KeySource` that serves the offset vocal key frames, per `(pass, hop)`, using the same power-sum weights as the hub. Then compute the masking metrics before/after → `adaptUnmask` → re-simulate once → final metrics. Then the VBR fixed point: `v_level += S.vbr − vbr_after` (one iteration, clamped to ±1.5 dB).
9. **P1.9** Sidechain alignment (BEAT with `key` capture and ≥ 1 VOCAL):
   - Cross-correlate the broadband envelope (per Hk) of the sidechain capture with the VOCAL-sum *input* envelope at lags 0 and L ± 4 hops.
   - Lag ≈ 0 → `scAlignRaw = 0` (host compensated). Lag ≈ L → `scAlignRaw = 1`.
   - Leave the default (0) if the peak correlation is below 0.5.
10. **P1.10** Outputs: `SolveOutput` per instance (ParamSet of that role's parameters only, CompProfile, notes). Summary and metrics: `vbr_before/after`, `masked_before/after`, `sii_before/after`, `dI_vocal_chain`, `dI_beat`, `spread`, `sib_p99_p90`, `ltas_dist_before/after`.

**Pass 2:**
1. **P2.1** Analyse the bus capture.
2. **P2.2** `busStatic` (B1–B3).
3. **P2.3** Simulate `BusChain` with drive 0, clip 0 → glue GR and O97 → B2 correction and clip.
4. **P2.4** Drive bisection (B4) → final simulation → metrics: `bus_I`, `bus_TP`, `glue_GR`, `lim_GR_p95`, `width_after`.
5. **P2.5** B6 hint and outputs.

All simulations are single-threaded, use fixed block sizes and fresh `prepare()`, and are therefore deterministic. **The solver never touches live processors.**

### 5.7 Explanation strings (UI, English, ≤ 7 lines per instance)

```
VOCAL LEAD
INPUT +6.2 dB  (recorded −26.2 LUFS)
LOW CUT 104 Hz  (lowest note 122 Hz)
EQ  mud −2.5 @ 310 · presence +2.0 @ 3.6k · air +3.5 dB
COMPRESS 46%  (phrase spread 9.1 → 5.6 LU, target 5.5)
DE-ESS 5.8 kHz  (S peaks 6.1 → 2.7 dB)
SPACE 1.2 s · ECHO 1/8D  (wet −20 / −18 LU)
LEVEL +5.4 dB  (vocal vs beat +1.0 dB · TRAP)

BEAT
UNMASK 6.0 dB · 250 Hz–5 kHz  (masked 44% → 33%)
KEY LINK · VOCAL ×2        |  KEY SIDECHAIN  |  NO KEY → POCKET −2.0 dB @ 3.0k
MONO < 120 Hz · LOW CUT 20 Hz

BUS
TONE low −0.8 · low-mid +0.4 · presence 0 · air +1.0 dB
GLUE 2.5:1 · −1.5 dB avg
WIDTH 106%  (side −14.2 → −13.0 LU)
LOUDNESS −9.0 LUFS · TP −1.0 dBTP  (drive +7.4, clip 2.0, limiter 2.1 dB)
```

Warnings are lines starting with `!`, drawn in red: `! TARGET NOT REACHED CLEANLY −9.8 LUFS`, `! UNLINKED TRACKS ON MASTER ≈ 4 dB`, `! FADERS? vocal track not at 0 dB` (v2), `! NOT ENOUGH VOCAL`.

### 5.8 Engine constants (single source: `AutoMix.cpp` top, `namespace k`)

| Constant | Value | Constant | Value |
|---|---|---|---|
| internal ref | −20 LUFS | confidence n0 | 10 s |
| learn target / min / cap | 15 s / 6 s / 30 s | bus listen / min / cap | 12 s / 6 s / 20 s |
| settle before pass 2 | 0.5 s | EQ match λ (`S.vMatch`) | 0.5–0.75 |
| EQ clamp / dead band | [−6, +4] / 1 dB | EQ smoothing σ | 1/3 oct |
| EQ fit λ / sweeps / grid | 0.05 / 3 / 12 | presence masking β | 0.5 (0.25 with unmask) |
| Giannoulis τA,max / τR,max | 80 ms / 1000 ms | comp bisection iterations | 24 |
| de-ess map | θ = −1 − 11a | VBR clamp / fixed-point step clamp | ±24 / ±1.5 dB |
| unmask α / per-hop slew | 0.7 / 1.5 dB | unmask mean cap (1–5 kHz) | min(4, 0.66·Dmax) dB |
| unmask adapt hi/lo | 0.42 / 0.25 | bus EQ solver clamp | ±2 dB |
| bus drive iterations / tolerance | 8 / 0.1 LU | width dead band / clamp | 1.5 LU / 0.6–1.4 |
| limiter margin | 0.15 dB | double/adlib HPF factor | 1.2 |

---

## 6. DSP per role

### 6.0 Common rules

- **Block handling.** `processBlock` returns early on 0 samples, after reading parameters into the chain. It processes in chunks of `≤ maxBlock` (as in v0), so blocks larger than prepared work. `prepared == false` → pass-through: FL Patcher may call `process()` before `prepareToPlay`. Only the first two main channels are used. Mono and stereo are both supported (`in == out`), with an aux input "Vox Key" (mono/stereo/disabled, enabled by default).
- **Control grid.** Filter coefficients and smoothed parameters update on a 32-sample grid aligned to the timeline (`(timeline + i) mod 32 == 0`) while playing, otherwise to the instance's sample counter. Within a grid step, coefficients are linearly interpolated. **All EQs are TPT SVFs** (modulation-safe), replacing v0's per-block RBJ updates. Gains use `SmoothedValue` with 30 ms ramps. Static automation-free renders are therefore bit-identical for any block slicing.
- **Latency.** `L = latencyFor(fs, maxBlock)` is set in `prepareToPlay` for every role, and the chain pads to L. `processBlockBypassed` delays the main input by L (separate delay line) so bypass keeps PDC alignment. A bypassed VOCAL keeps publishing key frames from its input.
- **Tail.** `getTailLengthSeconds()` = 6.0 for all roles.
- **Reset.** `reset()` (JUCE `setProcessing(false)`) clears every chain state and delay line, and sets `expectTimeline = −1`. It does not deallocate.
- **Safety.**
  - Non-finite detection per block: if any output sample is not finite, the chain resets and the block is zeroed.
  - VOCAL/BEAT have a soft safety above 0 dBFS only: `y = x` for `|x| ≤ 1`, else `sign(x)·(1 + 0.26·tanh((|x| − 1)/0.26))`. It is never active below 0 dBFS. FL's mixer is floating point, so ceilings are the BUS's job.
  - Peak meters show `OVER` above 0 dBFS.
- **Denormals.** `ScopedNoDenormals`. **Determinism:** no randomness, and no `isNonRealtime()` branches.

### 6.1 Parameter list (one APVTS layout for all roles; inactive roles hidden in UI)

All parameters use `ParameterID{id, 1}`. **IDs and ranges are frozen once released.** Only appending is allowed. "c=" is `NormalisableRange::setSkewForCentre`; blank means linear. Amount parameters (0–1) display as %. Choice parameters store the index. `auto` = automatable. Defaults equal `styleDefaults(TRAP)`, and a unit test enforces this.

**VOCAL (`v_`, 26)**

| id | name | range | default | unit | skew | auto | notes |
|---|---|---|---|---|---|---|---|
| v_input | Vox Input | −24…24 (0.1) | 0 | dB | | ✓ | solver: normalise to −20 LUFS |
| v_hpf | Vox Low Cut | 20…400 | 105 | Hz | c=100 | ✓ | 12 or 24 dB/oct Butterworth |
| v_hpfSlope | Vox Low Cut Slope | {12 dB, 24 dB} | 24 dB (1) | | | ✗ | |
| v_lowF | Vox Mud Freq | 120…800 | 320 | Hz | c=320 | ✓ | bell Q 1.2 |
| v_lowG | Vox Mud | −12…6 (0.1) | −3.0 | dB | | ✓ | |
| v_midF | Vox Mid Freq | 500…3000 | 1200 | Hz | c=1200 | ✓ | bell Q 1.0 |
| v_midG | Vox Mid | −8…6 | 0 | dB | | ✓ | |
| v_presF | Vox Presence Freq | 1500…8000 | 4000 | Hz | c=3500 | ✓ | bell Q 0.9 |
| v_presG | Vox Presence | −6…8 | 2.5 | dB | | ✓ | |
| v_airF | Vox Air Freq | 6000…16000 | 11000 | Hz | c=10000 | ✓ | high shelf Q 0.7, capped 0.42 fs |
| v_airG | Vox Air | −6…10 | 4.0 | dB | | ✓ | |
| v_lpf | Vox High Cut | 3000…22000 | 22000 | Hz | c=9000 | ✓ | ≥ 21000 = off; 2nd order |
| v_comp | Vox Compress | 0…1 | 0.50 | % | | ✓ | §6.2 mapping |
| v_compAtk | Vox Comp Attack | 1…80 | 15 | ms | c=15 | ✓ | leveler |
| v_compRel | Vox Comp Release | 30…800 | 180 | ms | c=180 | ✓ | leveler |
| v_deess | Vox De-ess | 0…1 | 0.40 | % | | ✓ | θ = −1 − 11a |
| v_deessF | Vox De-ess Freq | 3000…10000 | 6000 | Hz | c=6000 | ✓ | LR4 split |
| v_warmth | Vox Warmth | 0…1 | 0.25 | % | | ✓ | |
| v_echo | Vox Echo | 0…1 | 0.20 | % | | ✓ | send gain = a² |
| v_echoTime | Vox Echo Time | {1/16, 1/8, 1/8D, 1/4, 1/4D, 1/2} | 1/8D (2) | | | ✓ | beats 0.25, 0.5, 0.75, 1, 1.5, 2 |
| v_echoFb | Vox Echo Feedback | 0…0.8 | 0.25 | % | | ✓ | |
| v_space | Vox Space | 0…1 | 0.30 | % | | ✓ | send gain = a² |
| v_spaceDecay | Vox Space Decay | 0.3…4 | 1.2 | s | c=1.5 | ✓ | → roomSize |
| v_preDelay | Vox Pre-delay | 0…150 | 35 | ms | | ✓ | |
| v_wetDuck | Vox Wet Duck | 0…12 | 6 | dB | | ✓ | |
| v_level | Vox Level | −24…24 (0.1) | 0 | dB | | ✓ | solver: VBR |

**BEAT (`b_`, 14)**

| id | name | range | default | unit | skew | auto | notes |
|---|---|---|---|---|---|---|---|
| b_hpf | Beat Low Cut | 10…120 | 20 | Hz | c=30 | ✓ | ≤ 12 = off; 2nd order |
| b_monoBelow | Beat Mono Below | 0…300 (1) | 120 | Hz | | ✓ | < 20 = off; LR4 on S |
| b_pocketF | Beat Pocket Freq | 500…8000 | 3000 | Hz | c=2500 | ✓ | static bell on M |
| b_pocketG | Beat Pocket | −9…0 | 0 | dB | | ✓ | |
| b_pocketQ | Beat Pocket Q | 0.4…3 | 1.0 | | c=1 | ✓ | |
| b_unmask | Beat Unmask | 0…1 | 0.67 | % | | ✓ | Dmax = 9·a dB |
| b_sens | Beat Unmask Sens | 0…12 | 6 | dB | | ✓ | θ |
| b_focusLo | Beat Focus Low | 120…2000 | 250 | Hz | c=400 | ✓ | |
| b_focusHi | Beat Focus High | 1000…9000 | 5000 | Hz | c=3500 | ✓ | |
| b_release | Beat Unmask Release | 40…600 | 160 | ms | c=160 | ✓ | |
| b_side | Beat Unmask Side | 0…1 | 0.30 | % | | ✓ | σ |
| b_ride | Beat Ride | 0…4 | 0.5 | dB | | ✓ | broadband duck |
| b_key | Beat Key Source | {AUTO, LINK, SIDECHAIN, OFF} | AUTO | | | ✗ | |
| b_level | Beat Level | −24…12 | 0 | dB | | ✓ | |

**BUS (`m_`, 16)**

| id | name | range | default | unit | skew | auto | notes |
|---|---|---|---|---|---|---|---|
| m_lowG | Bus Low | −4…4 | 0 | dB | | ✓ | low shelf 90 Hz Q 0.7 |
| m_lowMidG | Bus Low-Mid | −4…4 | 0 | dB | | ✓ | bell 300 Hz Q 0.9 |
| m_presG | Bus Presence | −4…4 | 0 | dB | | ✓ | bell 3 kHz Q 0.8 |
| m_airG | Bus Air | −4…4 | 0 | dB | | ✓ | high shelf 10 kHz Q 0.7 |
| m_lpf | Bus High Cut | 6000…22000 | 22000 | Hz | c=12000 | ✓ | ≥ 21000 off |
| m_glue | Bus Glue Threshold | −36…0 | −14 | dB | | ✓ | RMS, stereo-linked |
| m_glueRatio | Bus Glue Ratio | 1…6 | 2.5 | :1 | c=2 | ✓ | knee 6 dB |
| m_glueAtk | Bus Glue Attack | 1…60 | 20 | ms | c=15 | ✓ | |
| m_glueRel | Bus Glue Release | 50…600 | 120 | ms | c=150 | ✓ | |
| m_glueScHpf | Bus Glue SC HPF | 20…300 | 120 | Hz | c=100 | ✓ | |
| m_width | Bus Width | 0…2 | 1.0 | % | | ✓ | side gain above mono-below |
| m_monoBelow | Bus Mono Below | 0…300 | 120 | Hz | | ✓ | < 20 off |
| m_drive | Bus Drive | 0…24 (0.1) | 0 | dB | | ✓ | solver |
| m_clip | Bus Clip | 0…6 (0.1) | 0 | dB | | ✓ | clip threshold = ceiling + (6 − clip) |
| m_ceiling | Bus Ceiling | −3…0 (0.1) | −1.0 | dBTP | | ✓ | |
| m_target | Bus Target | −18…−6 (0.1) | −9.0 | LUFS | | ✗ | solver input + meter line |

Total: 56 parameters. `Params.cpp` provides the table, the `PIdx` enum in the same order, and the conversions:

```cpp
namespace mm
{
enum class PKind : uint8_t { Float, Choice };
struct ParamInfo
{
    const char* id; const char* name; Role role; PKind kind;
    float min, max, def, centre, step;      // centre <= 0 → 线性；step 0 = 连续
    const char* unit; bool automatable;
    const char* const* choices; int numChoices;
};
extern const ParamInfo kParams[]; extern const int kNumParams;   // 56
enum PIdx : int { v_input, v_hpf, v_hpfSlope, /* … 与表格顺序一致 … */ m_target, kPIdxCount };
struct VocalParams { float input, hpf; int hpfSlope; float lowF, lowG, midF, midG, presF, presG, airF, airG, lpf,
                     comp, compAtk, compRel, deess, deessF, warmth, echo; int echoTime; float echoFb, space, spaceDecay,
                     preDelay, wetDuck, level; bool operator== (const VocalParams&) const = default; };
struct BeatParams  { float hpf, monoBelow, pocketF, pocketG, pocketQ, unmask, sens, focusLo, focusHi, release, side, ride;
                     int key; float level; bool operator== (const BeatParams&) const = default; };
struct BusParams   { float lowG, lowMidG, presG, airG, lpf, glue, glueRatio, glueAtk, glueRel, glueScHpf, width, monoBelow,
                     drive, clip, ceiling, target; bool operator== (const BusParams&) const = default; };
VocalParams vocalFrom (const float* raw);   BeatParams beatFrom (const float* raw);   BusParams busFrom (const float* raw);  // raw[kPIdxCount]
ParamSet toSet (const VocalParams&); ParamSet toSet (const BeatParams&); ParamSet toSet (const BusParams&);
VocalParams vocalFrom (const ParamSet&);  BeatParams beatFrom (const ParamSet&);  BusParams busFrom (const ParamSet&);   // 缺的用默认值
std::string formatValue (int pidx, float v);   // "+2.0 dB" "120 Hz" "3.8 kHz" "45%" "1/8D"
}
```

Defaulted `operator==` needs C++20. `CMakeLists.txt` switches to `CMAKE_CXX_STANDARD 20` (JUCE 8 supports it; MSVC `/std:c++20`).

### 6.2 VOCAL chain

**Signal flow** (stereo-linked dynamics; mono input runs with `numCh = 1`):

```
in ─► ×INPUT ─► HPF(12/24) ─► LOW bell ─► MID bell ─► PRES bell ─► AIR shelf ─► LPF
   ─► LEVELER ─► PEAK ─► DE-ESS ─► WARMTH(2× OS) ─► ×(autoComp · LEVEL) ─┬─► KeyBank → key frames (hub)
                                                                         ├─► echo send (ducked) ─┐
                                                                         ├─► space send (ducked) ─┤
                                                                         └─► dry ────────────────(+)─► pad to L ─► safety ─► out
```

This changes v0 in three ways:
- The de-esser now sits after the EQ boosts.
- Makeup is replaced by `autoComp`.
- The echo duck is relative to the internal level.

**Mappings** (a = amount 0–1; levels are internal dBFS after INPUT):
- **Leveler.**
  - Detector: RMS, one-pole on the stereo-max of x² with τ = 10 ms.
  - `thrA = −14 − 14a` dB, `ratioA = 1.5 + 3a`, knee 8 dB.
  - Ballistics: attack `v_compAtk`, release `v_compRel`, in dB.
  - Bypassed if a < 0.01.
- **Peak stage.**
  - Detector: |x| stereo-max.
  - `thrB = −6 − 8a` dBFS, `ratioB = 4 + 4a`, knee 4 dB, attack 1 ms, release 60 ms.
  - Active only if a ≥ 0.05.
- **De-esser** (relative, level-independent). v0's DeEsser is revised as follows:
  - LR4 split at `v_deessF`: lo/hi.
  - `d = powToDb(ms5(hi)) − powToDb(ms5(lo + hi))`, where `ms5` is a 5 ms one-pole on squares, so d ≤ 0.
  - `θ = −1 − 11a`, `range = 3 + 7a` dB, ratio 4.
  - `GR = clamp((d − θ)·(1 − 1/4), 0, range)`, attack 0.5 ms, release 45 ms, `out = lo + hi·10^(−GR/20)`.
  - Off when a < 0.01. The `sibDb` tap exposes d.
- **Warmth.** The v0 `Saturator` is kept: `drive = 1 + 7w`, `bias = 0.08w`, `mix = min(1, 2.2w)`, normalised at the −12 dBFS reference (`ref = 0.25`), 2× IIR oversampling, DC blocker. Because INPUT normalises the vocal to −20 LUFS, the reference has a fixed meaning. Its loudness effect is removed by `autoComp`, so WARMTH changes harmonics and density, not loudness.
- **Loudness compensation `autoComp`** is recomputed in `setParams` only when parameters or the profile change. It runs on the audio thread, allocation-free, at about 3k flops:
  ```
  ΔEQ   = powToDb( Σ_k WK(f_k)·P_k·|H(f_k)|² / Σ_k WK(f_k)·P_k )          // P_k = 10^(ltasDb_k/10)，H = HPF·LOW·MID·PRES·AIR·LPF，WK = K 计权功率
  hist  = levelW shifted by (INPUT + ΔEQ) dB                             // 30 ms RMS 分布
  ΔCOMP = powToDb( Σ_i w_i·10^((L_i + GR_A(L_i) + GR_B(L_i + crestDb + GR_A(L_i)))/10) / Σ_i w_i·10^(L_i/10) )
  ΔSAT  = interp(satGainDb, 8·warmth)
  autoComp = −(ΔEQ + ΔCOMP + ΔSAT)          // dB，限制 [−12, +18]
  ```
  `autoComp` and `LEVEL` are applied together through one 30 ms smoothed gain.
- **Echo.** v0 `Echo` (tempo-synced ping-pong, HP 320/LP 4800 in feedback). `send = a²`, `feedback = v_echoFb`, time = `beats[v_echoTime]·60/bpm` (fallback bpm 120).
- **Duck** (shared by echo and space): `env` = one-pole of |dry_internal| (attack 10 ms, release 200 ms); `amt = clamp((gainToDb(env) + 40)/15, 0, 1)`; `wetGain = 10^(−v_wetDuck·amt/20)`.
- **Space.** v0 `Space`: pre-delay `v_preDelay`, wet HP 250 Hz, wet LP `min(8000, v_lpf)`, Freeverb.
  - `roomSize = clamp((10^(−0.09/T) − 0.7)/0.28, 0, 1)` with T = `v_spaceDecay`; damping 0.45, width 1.
  - `send = a²`. The wet return is summed in stereo (mono: average).
- **Key.** `KeyBank` on the mono sum of the dry output after `autoComp·LEVEL`, published per §3.6. `coreLatency()` = the rounded oversampling latency.
- **Meters:** in peak, out peak, leveler+peak GR, de-ess GR, out momentary LUFS.

### 6.3 BEAT chain

#### 6.3.1 Signal flow

```
in ─► delay D (= L in plugin) ─► HPF ─► M/S ─► [M: POCKET bell ─► 10 unmask bells(g)] [S: LR4 HPF @ monoBelow ─► 10 bells(σ·g)]
    ─► L/R ─► ×(autoComp · LEVEL · ride) ─► safety ─► out
       ▲ KeyBank(M, at output content)        ▲ need/cut computer per key hop
 aux ─► mono ─► KeyBank(SC, at input time) ─► SC ring (own counter)
 hub ─► readVocalKey(content hop + look)
```

All filters have zero latency. The core latency is 0 and the chain delays its input by `spec.latency`. With every gain at 0 dB, HPF off and mono-below off, the chain is an exact delay (unit test, ≤ −120 dBFS).

#### 6.3.2 Unmask bells

10 TPT SVF bells at `kKeyCentresHz`, Q = 2.0, capped at 0.45 fs.

Gains `g` (dB ≤ 0) come from the per-band cut through a fixed interaction model:
- `Mx_ij` = the dB response of bell j at f_i for −3 dB, divided by −3. It is precomputed in `prepare`.
- Per hop, solve `min ||Mx·g + cut||² + 0.05||g||²` subject to `g ≤ 0` with 3 projected Gauss–Seidel sweeps, warm-started from the previous hop (about 300 MAC).
- Coefficients interpolate over the 4 grid steps of a hop. The per-hop change is limited to 1.5 dB per band.

#### 6.3.3 Need/cut computer (per key hop of output content `c`)

```
hop = floor((c + look) / Hk)
key: mode = b_key==AUTO ? (linkAvailable ? LINK : (aux != null ? SC : OFF)) : b_key
     LINK → r = link->vocalKey(hop, V)          // Ok / Silent / Late
     SC   → V = scRing[ hop_sc ]   (hop_sc = scAlignRaw ? latest frame : floor((o + look)/Hk), o = 输出样本的本实例计数位置)
V̂_k = ballistic(V_k; att 5 ms, rel 60 ms)        // dB 域单极点，a = exp(−Hk/(τ·fs))
B̂_k = ballistic(B_k(c); att 30 ms, rel 300 ms)    // 本实例 M 声道在输出内容时刻的 KeyBank
vad = r==Ok && max V̂ > −60 && max V̂ > max B̂ − 30
need_k = clamp(0.7·(B̂_k − V̂_k + b_sens), 0, Dmax)·min(1, 1.5·I_k/I_max)·vad   (Dmax = 9·b_unmask)
need_k = 0 if V̂_k < max_j V̂_j − 30 or f_k ∉ [b_focusLo, b_focusHi]
need = [0.25 0.5 0.25] ⊛ need (edges renormalised)
if mean(need over 927–4779 Hz bands) > min(4, 0.66·Dmax): scale need down to that mean
cut_k = ballistic(need_k; att 10 ms, rel b_release)
ride: rv = ballistic(vad; att 20 ms, rel b_release); rideGain = −b_ride·rv dB
display: shared.cutDb = −cut, shared.displayDb = B̂, keyState
```

`linkAvailable` is a processor flag: "a VOCAL peer is registered in my group" (registry state, stable during playback).

The mono-below LR4 high-pass on S is applied before the side bells. The side bells use gains `b_side·g`. For mono input, only M is processed.

#### 6.3.4 Sidechain alignment

The VOCAL's latency L sits upstream of the sidechain, so there are two cases:
- `scAlignRaw = 0` means FL delay-compensated the sidechain to the beat's main input (assumed default; **UNVERIFIED**, A.4). The SC frame indexed by own input time maps to content time directly, and lookahead `look` is available.
- `scAlignRaw = 1` means the sidechain arrives L late. The latest SC frame is used (zero lookahead).

The solver detects which case applies (P1.9).

#### 6.3.5 Compensation and meters

- Static compensation: `ΔEQ` from the BEAT `ltasDb` for HPF + pocket (mono-below is ignored), then `autoComp = −ΔEQ`. The dynamic cut is not compensated.
- Meters: in/out peak, max cut, ride, key state, `syncMiss`.

### 6.4 BUS chain

```
in ─► LOW shelf ─► LOW-MID bell ─► PRES bell ─► AIR shelf ─► LPF ─► GLUE (SC HPF, stereo-linked RMS 5 ms, knee 6) ─► ×glueMakeup
   ─► M/S: S ─► LR4 HPF @ monoBelow ─► ×width ─► L/R ─► ×DRIVE ─► SOFT CLIP (4× / 2× / 1× IIR OS) ─► TP LIMITER ─► pad to L ─► out
```

- **Glue makeup.** Same static prediction as the VOCAL `ΔCOMP`, from the BUS `levelW` and glue parameters (RMS levels, no peak stage), limited to [0, +6] dB. The default profile gives about +1.5 dB at the trap defaults.
- **Soft clip.** Threshold `Tc = ceiling_lin·10^((6 − m_clip)/20)`, applied as `y = Tc·s(x/Tc)` with `s(u) = u` for |u| ≤ 0.7, else `sign(u)·(0.7 + 0.3·tanh((|u| − 0.7)/0.3))`. When `m_clip < 0.05` it is bypassed, but the signal still passes the oversampler so latency stays constant. Oversampling is `juce::dsp::Oversampling` with polyphase IIR half-bands, `useIntegerLatency = true`, 2 stages at ≤ 50 k, 1 at ≤ 100 k, none above.
- **True-peak limiter** (lookahead W = round(2 ms·fs), stereo-linked):
  1. `g_req(n) = min(1, c/tp(n))`, where `c = ceiling_lin·10^(−0.15/20)` and `tp` is the max over channels of `TruePeak4x` (4 phases).
  2. `h(n)` = sliding minimum of `g_req` over the last W samples (monotonic deque, preallocated).
  3. `r(n) = min(h(n), rel(r(n−1)))`. `rel` rises toward 1 in dB with two one-poles (30 ms and 300 ms); the lower gain is kept.
  4. `s(n)` = box average of `r` over W (double running sum, re-summed every 4096 samples).
  5. `y(m) = x(m)·s(m + W − 1)`, i.e. the audio is delayed by W − 1 plus the TP filter delay. Since every r in the window is ≤ `g_req(m)`, the average is ≤ `g_req(m)`. That guarantees output TP ≤ ceiling − 0.15 dB, up to the interpolator's error, which is tested to be < 0.1 dB.
- **Core latency** = OS latency + TP filter delay (8) + W − 1 (about 110 samples at 48 k). Padded to L.
- **Meters:** input/output momentary, short-term, integrated LUFS (`LoudnessMeter`), output TP max (4×), glue GR, limiter GR, clip activity, plus the target line.

### 6.5 Latency and PDC summary

| Role | Core latency (48 k) | Pad | Reported |
|---|---|---|---|
| VOCAL | IIR 2× OS (≈ 2–4 samples, rounded) | L − core | L |
| BEAT | 0 (input delayed by L for key lookahead/sync) | — | L |
| BUS | OS + 8 + W − 1 ≈ 110 | L − core | L |

`L = 1024·r`, or `2048·r` when `maxBlock > 736·r`. That is 21.3 ms or 42.7 ms at 48 k.
- It is set once in `prepareToPlay` via `setLatencySamples(L)` and never changed elsewhere.
- Recording through MAKE MIX adds L of monitoring delay. The manual says: bypass while tracking.

### 6.6 `Source/Chains.h` (WP1, public interface)

```cpp
#pragma once
#include "Params.h"
#include <juce_dsp/juce_dsp.h>

namespace mm
{
struct ChainSpec { double fs = 48000; int maxBlock = 512; int numCh = 2; int latency = 0; };   // latency 0 = 不补齐（解算仿真用）
struct ProcessContext { int64_t timeline = -1; double bpm = 120; };   // timeline = 本块第一个输入样本的时间线位置；-1 = 未播放
struct KeyFrame { int64_t hop = 0; std::array<float, kKeyBands> bandDb {}; float vad = 0; };
class KeySink   { public: virtual ~KeySink() = default;   virtual void keyFrame (const KeyFrame&) noexcept = 0; };
class KeySource { public: virtual ~KeySource() = default; virtual KeyRead vocalKey (int64_t hop, float* bandDb) noexcept = 0; };

struct VocalTaps { float* dry = nullptr; float* echo = nullptr; float* space = nullptr; float* preSat = nullptr;
                   float* sibDb = nullptr; float* compGrDb = nullptr; };          // 每个 n 个样本（两声道平均），可为空
struct BeatTaps  { float* cutMeanDb = nullptr; };
struct BusTaps   { float* glueGrDb = nullptr; float* limGrDb = nullptr; float* preLimTpDb = nullptr; };
struct ChainMeters { float inPeak = 0, outPeak = 0, gr1 = 0, gr2 = 0, gr3 = 0, loudM = -100, loudS = -100, loudI = -100, truePeak = -100; };

class VocalChain
{
public:
    void prepare (const ChainSpec&);                       // 分配（非实时）
    void reset() noexcept;
    void setParams (const VocalParams&, const CompProfile&) noexcept;   // 每块调用；变了才重算
    void process (float* const* ch, int numCh, int n, const ProcessContext&, KeySink*, VocalTaps* = nullptr) noexcept;
    void processBypassed (float* const* ch, int numCh, int n, const ProcessContext&, KeySink*) noexcept;
    int coreLatency() const noexcept;
    float autoCompDb() const noexcept;
    const ChainMeters& meters() const noexcept;            // 本块的值；处理器拷到原子量
};
enum class KeyMode : uint8_t { Off = 0, Link = 1, Sidechain = 2 };
class BeatChain
{
public:
    void prepare (const ChainSpec&);                       // spec.latency ≥ keyLookahead + keyHop（插件里 = L）
    void reset() noexcept;
    void setParams (const BeatParams&, const CompProfile&) noexcept;
    void process (float* const* ch, int numCh, const float* const* aux, int auxCh, int n,
                  const ProcessContext&, KeySource* link, BeatTaps* = nullptr) noexcept;   // link = nullptr：没有 VOCAL 同伴
    void processBypassed (float* const* ch, int numCh, int n) noexcept;
    KeyMode keyMode() const noexcept;   uint32_t lateCount() const noexcept;
    const std::array<float, kKeyBands>& cutDb() const noexcept;
    const std::array<float, kKeyBands>& beatDb() const noexcept;
    const ChainMeters& meters() const noexcept;
};
class BusChain
{
public:
    void prepare (const ChainSpec&);
    void reset() noexcept;
    void setParams (const BusParams&, const CompProfile&) noexcept;
    void process (float* const* ch, int numCh, int n, const ProcessContext&, BusTaps* = nullptr) noexcept;
    void processBypassed (float* const* ch, int numCh, int n) noexcept;
    int coreLatency() const noexcept;
    void resetIntegrated() noexcept;                        // 播放开始时清积分响度
    const ChainMeters& meters() const noexcept;
};
} // namespace mm
```

**Processor `processBlock` (WP2, sketch):**

```
ScopedNoDenormals; n = buffer.getNumSamples(); clear extra channels
if !prepared → return                                  // 直通
pos = playhead → playing, timeInSamples, bpm (fallbackBpm)
role = roleAtomic.load(relaxed); if role != activeRole → chains[role].reset(); activeRole = role
raw[] ← paramReader (atomic loads); compBox.read(comp, seq)
captureStep(buffer main + aux, n, playing, timeInSamples)   // 3.7
if n == 0 → return
chain.setParams(structFrom(raw), comp)
for start in 0..n step maxBlock: chain.process(…, ctx{ playing ? timeInSamples + start : −1, bpm }, sink/source)
meters → atomics; shared.lastMs / loudM; keyState / syncMiss
```

`HubKeySink::keyFrame` calls `hub.publishKey(slot, f.hop, f.bandDb.data(), f.vad)`. `HubKeySource::vocalKey` calls `hub.readVocalKey(group, slot, hop, out)` and counts `Late` into `shared.syncMiss`.

---

## 7. STYLE system

### 7.1 `Source/Styles.h` (WP0 creates from the tables below; WP3 owns and tunes)

```cpp
#pragma once
#include "Shared.h"
namespace mm
{
struct StyleDef
{
    const char* key; const char* name;                  // "trap", "TRAP" / "MANDOPOP 华语流行"
    // 人声音色（表 A）
    float vTilt, vMudF, vMudG, vPresF, vPresG, vAirF, vAirG, vHpfK, vHpfMin, vHpfMax; int vHpfSlope; float vLpf, vMatch;
    // 人声动态 / 空间 / 电平（表 B）
    float vSpread, vDeessMargin, vSibTarget, vWarmth, vRevDecay, vPreDelayMs, vRevWdr; int vEchoTime;
    float vEchoFb, vEchoWdr, vWetDuck, vbr, dblOffset, adlibOffset;
    // 伴奏（表 C）
    float bDmax, bSens, bLo, bHi, bRelMs, bSide, bRide, bPocketF, bPocketStatic, bPocketLinked, bHpf, bMono;
    // 总线（表 D、E）
    float lufsStyle, lufsLoud, ceiling, glueRatio, glueAtk, glueRel, glueGr, glueScHpf, clipDb, width, busMono, busLpf, maxLimGr;
    std::array<float, kOctBands> busCurve;              // 相对 500/1k/2k 平均，dB
};
extern const std::array<StyleDef, kNumStyles> kStyles;  // 定义在本头文件（inline constexpr）
inline const StyleDef& style (StyleId s) { return kStyles[(size_t) std::clamp (s, 0, kNumStyles - 1)]; }
}
```

The order of styles is fixed and saved by index: 0 TRAP, 1 DRILL, 2 MELODIC RAP, 3 BOOM BAP, 4 R&B, 5 MANDOPOP 华语流行, 6 BALLAD 华语抒情, 7 LO-FI, 8 POP, 9 ROCK, 10 EDM, 11 AFROBEATS. New styles may only be appended.

Confidence comes from the genre research:
- H/M: trap and the bus loudness/width columns of pop, EDM and rock.
- L: Mandopop, ballad, drill and Afrobeats. These must be calibrated with 10–20 references each (§12).

### 7.2 Style tables

**A. Vocal tone.**
- Tilt is in pink-referenced dB/oct.
- Mud, presence and air are target-shape terms in dB at a frequency in Hz.
- The HPF is `K × f0_P5` within [min, max].
- `vMatch` is the fraction of the target deviation corrected.

| Style | tilt | mud F/G | pres F/G | air F/G | HPF K | min–max | slope | LPF | match |
|---|---|---|---|---|---|---|---|---|---|
| TRAP | −1.0 | 320/−3.0 | 4000/+2.5 | 11000/+4.0 | 0.85 | 90–120 | 24 | off | 0.70 |
| DRILL | −1.0 | 320/−2.5 | 3200/+3.0 | 10000/+2.0 | 0.85 | 100–130 | 24 | off | 0.70 |
| MELODIC RAP | −1.0 | 400/−2.5 | 4000/+2.0 | 11000/+4.0 | 0.75 | 80–110 | 24 | off | 0.70 |
| BOOM BAP | −1.5 | 400/−1.5 | 2200/+2.5 | 10000/+1.0 | 0.75 | 70–95 | 12 | off | 0.60 |
| R&B | −1.5 | 280/−2.0 | 3500/+1.5 | 12000/+4.5 | 0.60 | 70–95 | 12 | off | 0.70 |
| MANDOPOP | −1.0 | 320/−2.5 | 3200/+2.0 | 11000/+3.0 | 0.70 | 70–95 | 24 | off | 0.70 |
| BALLAD | −1.5 | 350/−1.0 | 3000/+1.5 | 10000/+3.0 | 0.60 | 60–85 | 12 | off | 0.60 |
| LO-FI | −3.0 | 500/−1.0 | 1500/+2.0 | 10000/0 | 1.20 | 150–300 | 12 | 7500 | 0.50 |
| POP | −0.5 | 300/−2.5 | 4000/+3.0 | 11000/+5.0 | 0.80 | 90–120 | 24 | off | 0.75 |
| ROCK | −1.0 | 400/−2.0 | 2200/+3.0 | 10000/+2.0 | 0.85 | 100–130 | 24 | off | 0.70 |
| EDM | −0.5 | 300/−3.0 | 4500/+3.0 | 11000/+5.0 | 1.00 | 120–180 | 24 | off | 0.75 |
| AFROBEATS | −1.0 | 320/−2.0 | 4000/+2.5 | 11000/+3.0 | 0.80 | 90–120 | 24 | off | 0.70 |

**B. Vocal dynamics, space and level.**
- Spread is the P10–P90 of 400 ms momentary loudness on active frames.
- WDR is the wet return loudness minus the dry, in LU.
- VBR is the `L_w` vocal-to-beat ratio in dB.
- The double/ad-lib offsets are levels below the LEAD.

| Style | spread | de-ess margin | sib P99−P90 | warmth | rev s | pre ms | rev WDR | echo | fb | echo WDR | wet duck | VBR | dbl | adlib |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| TRAP | 5.5 | 1.0 | 2.5 | 0.25 | 1.2 | 35 | −20 | 1/8D | 0.25 | −18 | 6 | +1.0 | −8 | −7 |
| DRILL | 5.0 | 1.5 | 3.0 | 0.45 | 0.5 | 15 | −27 | 1/8 | 0.15 | −24 | 8 | +1.5 | −8 | −6 |
| MELODIC RAP | 5.5 | 1.0 | 2.5 | 0.30 | 2.2 | 50 | −15 | 1/4 | 0.38 | −15 | 6 | −0.5 | −7 | −7 |
| BOOM BAP | 5.5 | 1.5 | 3.0 | 0.30 | 0.7 | 20 | −23 | 1/16 | 0.10 | −22 | 4 | +2.5 | −9 | −8 |
| R&B | 6.0 | 0.0 | 2.5 | 0.12 | 2.0 | 35 | −17 | 1/4 | 0.28 | −19 | 4 | +1.0 | −9 | −9 |
| MANDOPOP | 5.5 | 0.0 | 2.5 | 0.08 | 2.3 | 60 | −15 | 1/4 | 0.28 | −19 | 3 | +2.5 | −10 | −10 |
| BALLAD | 7.0 | 0.5 | 3.0 | 0.05 | 2.8 | 75 | −13 | 1/4 | 0.32 | −18 | 2.5 | +3.5 | −11 | −11 |
| LO-FI | 6.5 | 3.0 | 4.0 | 0.60 | 1.1 | 20 | −17 | 1/8D | 0.35 | −16 | 1 | −2.0 | −8 | −8 |
| POP | 5.0 | 0.0 | 2.5 | 0.20 | 1.6 | 30 | −19 | 1/8 | 0.20 | −21 | 6 | +1.5 | −9 | −8 |
| ROCK | 5.5 | 1.5 | 3.0 | 0.40 | 1.4 | 60 | −21 | 1/16 | 0.05 | −15 | 4 | −1.0 | −6 | −8 |
| EDM | 5.0 | 0.0 | 2.5 | 0.25 | 3.0 | 30 | −15 | 1/4 | 0.40 | −16 | 9 | +1.5 | −8 | −8 |
| AFROBEATS | 5.5 | 1.0 | 2.5 | 0.15 | 1.6 | 30 | −19 | 1/8D | 0.28 | −19 | 4 | +1.0 | −8 | −7 |

The VBR column is on the `L_w` scale, which is about LAR_mid − 0.5 dB, per the algorithm research's measurements. Two anchors were measured on the user's songs: 谎话 +1.2 (trap) and Isolated −0.9 (melodic).

**C. Beat (unmask = dynamic, keyed by the vocal; pocket = static mid bell).**

| Style | Dmax dB | sens θ | focus Hz | release ms | side σ | ride dB | pocket F | pocket static / linked dB | low cut | mono below |
|---|---|---|---|---|---|---|---|---|---|---|
| TRAP | 6 | 6 | 250–5000 | 160 | 0.3 | 0.5 | 3000 | −2.0 / 0 | 20 | 120 |
| DRILL | 6 | 6 | 200–4000 | 110 | 0.3 | 0 | 2500 | −1.5 / 0 | 20 | 100 |
| MELODIC RAP | 6 | 6 | 300–5000 | 200 | 0.3 | 0.75 | 2500 | −2.0 / 0 | 20 | 120 |
| BOOM BAP | 7 | 6 | 400–4000 | 120 | 0.3 | 1.5 | 2000 | −2.5 / 0 | 30 | 100 |
| R&B | 4 | 4 | 200–5000 | 220 | 0.3 | 0.5 | 3000 | −1.5 / 0 | 20 | 120 |
| MANDOPOP | 5 | 5 | 250–5000 | 200 | 0.3 | 0.75 | 2800 | −2.0 / 0 | 20 | 120 |
| BALLAD | 5 | 5 | 250–4000 | 250 | 0.3 | 1.5 | 2200 | −2.0 / −0.5 | 20 | 100 |
| LO-FI | 3 | 3 | 1500–3000 | 250 | 0.3 | 0 | 2200 | −1.0 / 0 | 25 | 150 |
| POP | 5 | 6 | 200–5000 | 180 | 0.3 | 0.75 | 3000 | −2.0 / 0 | 20 | 120 |
| ROCK | 6 | 6 | 400–4000 | 150 | 1.0 | 0.5 | 2200 | −2.0 / 0 | 25 | 100 |
| EDM | 7 | 6 | 300–6000 | 180 | 0.3 | 0 | 3000 | −2.5 / 0 | 20 | 120 |
| AFROBEATS | 4 | 5 | 300–5000 | 200 | 0.3 | 0 | 3000 | −1.5 / 0 | 20 | 120 |

**D. Bus loudness, glue and width.**
- STYLE is the default target, the "streaming-safe" column of the genre research.
- LOUD is the release-loud option.
- Width is the K-weighted side − mid in LU.

| Style | STYLE LUFS | LOUD LUFS | ceiling dBTP | glue ratio | atk ms | rel ms | glue GR dB | SC HPF | clip dB | width LU | mono below | bus LPF | max lim GR |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| TRAP | −9.0 | −8.0 | −1.0 | 2.5 | 20 | 120 | 1.5 | 120 | 2.0 | −13.0 | 120 | off | 6 |
| DRILL | −8.5 | −7.5 | −1.0 | 3.0 | 15 | 100 | 2.0 | 120 | 3.0 | −12.0 | 100 | off | 6 |
| MELODIC RAP | −9.5 | −8.5 | −1.0 | 2.0 | 20 | 150 | 1.5 | 120 | 1.0 | −11.0 | 120 | off | 5 |
| BOOM BAP | −10.0 | −9.0 | −1.0 | 4.0 | 20 | 200 | 2.5 | 100 | 1.0 | −14.0 | 100 | off | 5 |
| R&B | −10.0 | −9.5 | −1.0 | 1.8 | 30 | 200 | 1.0 | 120 | 0 | −9.5 | 120 | off | 4 |
| MANDOPOP | −9.5 | −9.0 | −1.0 | 1.8 | 30 | 200 | 1.5 | 120 | 0.5 | −9.5 | 120 | off | 4 |
| BALLAD | −11.0 | −10.0 | −1.0 | 1.5 | 30 | 250 | 1.0 | 100 | 0 | −9.5 | 100 | off | 3 |
| LO-FI | −13.0 | −12.0 | −1.0 | 3.0 | 20 | 200 | 2.5 | 150 | 0 | −10.5 | 150 | 12000 | 4 |
| POP | −9.0 | −8.0 | −1.0 | 2.0 | 20 | 150 | 2.0 | 120 | 1.0 | −7.5 | 120 | off | 5 |
| ROCK | −8.5 | −8.0 | −1.0 | 4.0 | 20 | 120 | 3.0 | 100 | 1.5 | −8.5 | 100 | off | 5 |
| EDM | −8.0 | −7.0 | −1.0 | 3.0 | 15 | 120 | 2.0 | 120 | 2.0 | −8.5 | 120 | off | 6 |
| AFROBEATS | −10.0 | −9.0 | −1.0 | 2.0 | 30 | 200 | 1.5 | 120 | 0.5 | −8.5 | 120 | off | 4 |

The TARGET selector (§8) also offers `−14` (platform-normalised: −14 LUFS, −1 dBTP). The ceiling defaults to −1.0 dBTP throughout. Spotify suggests −2 dBTP for masters louder than −14 LUFS; this is offered as a manual setting and noted in the tooltip.

**E. Bus tonal target.** Octave-band power relative to the mean of the 500 Hz/1 kHz/2 kHz bands, in dB. The genre research's values are relative to 1 k; they are used as-is and `fitBusEq` renormalises.

| Style | 31.5 | 63 | 125 | 250 | 500 | 1k | 2k | 4k | 8k | 16k |
|---|---|---|---|---|---|---|---|---|---|---|
| TRAP | +8 | +12 | +7 | +3 | +2 | 0 | −2 | −3 | −3 | −7 |
| DRILL | +6 | +11 | +7 | +3 | +2 | 0 | −1.5 | −2.5 | −2.5 | −6 |
| MELODIC RAP | +5 | +10 | +6 | +3.5 | +2.5 | 0 | −2 | −3.5 | −4.5 | −9 |
| BOOM BAP | 0 | +7 | +7 | +4.5 | +2.5 | 0 | −2.5 | −5 | −7 | −13 |
| R&B | +4 | +9 | +6.5 | +4 | +2.5 | 0 | −2.5 | −4 | −5 | −9 |
| MANDOPOP | +1 | +6 | +5.5 | +3.5 | +2 | 0 | −2 | −3.5 | −5 | −10 |
| BALLAD | −2 | +4 | +5 | +4 | +2.5 | 0 | −2.5 | −4.5 | −6.5 | −12 |
| LO-FI | +2 | +7 | +7 | +5 | +3 | 0 | −3.5 | −7 | −11 | −20 |
| POP | +2 | +8 | +6 | +3 | +1.5 | 0 | −1.5 | −2.5 | −3.5 | −7 |
| ROCK | −2 | +5 | +5 | +3.5 | +2.5 | 0 | −1 | −2 | −3 | −8 |
| EDM | +5 | +10 | +6 | +2.5 | +1 | 0 | −1 | −1.5 | −2 | −5 |
| AFROBEATS | +2 | +8 | +7 | +3.5 | +1.5 | 0 | −1.5 | −2.5 | −3 | −7 |

### 7.3 Standalone defaults (`automix::styleDefaults(style, role)`) and calibration

`LOAD DEFAULTS` and host programs keep the level-owning parameters: `v_input`, `v_level`, `b_level`, `m_drive`, `m_glue`, `m_target`.

- **VOCAL:**
  - `v_hpf = (vHpfMin + vHpfMax)/2`, `v_hpfSlope`, `v_lpf = vLpf or 22000`;
  - `v_lowF/G = vMudF/G`, `v_midF = 1200`, `v_midG = 0`, `v_presF/G = vPresF/G`, `v_airF/G = vAirF/G`;
  - `v_comp = clamp(0.5 + 0.1·(5.5 − vSpread), 0.2, 0.8)`, attack 15, release 180;
  - `v_deess = clamp(0.45 − 0.05·vDeessMargin, 0.1, 0.8)`, `v_deessF = 6000`;
  - `v_warmth`, echo time/fb, decay, pre-delay and wet duck from the style;
  - `v_space = sqrt(10^((vRevWdr − kRevWdrUnity(vRevDecay))/20))` and `v_echo = sqrt(10^((vEchoWdr − kEchoWdrUnity(vEchoFb))/20))`, both clamped to [0, 1].
- **BEAT:** `beatStatic(S, keyAvailable = true)`, i.e. linked pocket values.
- **BUS:** EQ 0, `m_lpf = busLpf or 22000`, glue ratio/atk/rel/SC from the style, `m_width = 1`, `m_monoBelow = busMono`, `m_clip = 0`, `m_ceiling = ceiling`.

**Calibration constants (WP1 measures and embeds; the placeholders are used until then):**
- `kRevWdrUnity(T)`: WDR at `v_space = 1` for decay T ∈ {0.3, 0.6, 1.2, 2, 3, 4} s, measured on a speech-modulated pink-noise vocal proxy at −20 LUFS through the VocalChain. Linear interpolation in T. Placeholder: −8 dB at all T.
- `kEchoWdrUnity(fb)`: same for echo, at fb ∈ {0, 0.2, 0.4, 0.6, 0.8}. Placeholder: `−4 + 10·log10(1/(1 − fb²))`.
- Default `satGainDb` table (no learn): measured on the same proxy. Placeholder `{0, 0.1, 0.25, 0.45, 0.7, 0.95, 1.2, 1.45, 1.7}` dB (WARMTH raises density).
- Default `levelW`: Gaussian, μ = −22 dBFS, σ = 6 dB. Default `ltasDb`:
  - VOCAL: pink-referenced −1.5 dB/oct from 150 Hz, −12 dB/oct below 120 Hz;
  - BEAT/BUS: the TRAP bus curve.

The solver measures each of these quantities directly, so the placeholders only affect standalone defaults before the first MIX.

---

## 8. UI specification

**Size.** The logical canvas is **980 × 560**. It is resizable with a fixed aspect ratio, from 735 × 420 to 1960 × 1120. All children live in one `Content` component scaled with `setTransform`, and VST3 content scaling is honoured, so nobody needs to bridge for HiDPI.

**Colours** (from v0): bg `0xff0a0b10`, panel `0xff12141b`, line `0xff23262f`, red `0xffe0112b`, silver `0xffc9ced6`, dim `0xff6b7080`. Knob body gradient `0xff2a2d36→0xff111318`, rim `0xff3a3e49`.
- Red marks active values, warnings and the MIX button. Silver is for values and labels, dim for secondary text.
- There are no other hues.
- Fonts: JUCE default, bold for labels.
- The style names include CJK. Verify the JUCE 8 font fallback on Windows; if glyphs are missing, use the "Microsoft YaHei UI" typeface for the style combo and explanation text.

**Header** (y 0–88):

| Element | Bounds (x, y, w, h) | Behaviour |
|---|---|---|
| Title `MAKE MIX` | 28, 14, 220, 34 | 30 pt bold silver |
| Subtitle | 30, 48, 220, 16 | `MAKE STUDIO · VOCAL STRIP` / `BEAT` / `BUS`, 12 pt dim |
| Red bar | 30, 70, 46, 3 | |
| Role segmented control | 250, 20, 264, 32 | `VOCAL │ BEAT │ BUS`. The selected segment is filled red. Clicking calls `proc.setRole(r, byUser = true)`. It pulses (alpha 0.5↔1, 1 Hz) until the role is confirmed. Disabled during this instance's session. |
| Sub selector | 524, 20, 116, 32 | VOCAL: `LEAD/DOUBLE/ADLIB` (state). BEAT: `KEY AUTO/LINK/SIDECHAIN/OFF` (attached to `b_key`). BUS: `TARGET STYLE/LOUD/−14`, which writes `m_target` to the style column, the LOUD column or −14. |
| Style combo | 650, 20, 180, 32 | 12 names as in §7.1. On change: broadcast, then re-mix or show the `LOAD DEFAULTS` chip (§3.9). |
| MIX button | 842, 14, 118, 44 | Red pill. The label follows the session: `MIX` → `LISTENING` with a left-to-right progress fill → `MIXING…` (animated dots) → `DONE` for 3 s. A click while listening means STOP (solve now). A small `×` at its top-right cancels. |
| Status under MIX | 842, 62, 118, 14 | `1/2 · 37%`, `PRESS PLAY`, `NEED VOCAL`, error text (red) |
| Link line | 250, 60, 520, 16 | `LINK A · VOCAL ×2 · BEAT ×1 · BUS` (counts silver, separators dim), or the warning in red. Tooltip: peer list. |
| Group chip | 776, 58, 54, 20 | `GRP A`; a click cycles A–D. Disabled during a session. |

**Knob area** (y 96–332): labels at y 100, knobs at y 120 (150 px tall including the value box), hint text below (as in v0). A `DETAIL` text toggle at (900, 96, 60, 18) swaps the big row for a grid of small knobs (64 × 82, two rows at y 112 and y 214).

| Role | Big knobs (left → right) | Detail grid |
|---|---|---|
| VOCAL (9 knobs, 96 px wide, gap 9.5) | CLEAN `v_hpf` (hint "low cut"), MUD `v_lowG`, PRESENCE `v_presG`, AIR `v_airG`, COMPRESS `v_comp`, DE-ESS `v_deess`, WARMTH `v_warmth`, ECHO `v_echo`, SPACE `v_space` | SLOPE, MUD FREQ, MID, MID FREQ, PRES FREQ, AIR FREQ, HIGH CUT, ATTACK, RELEASE, DE-ESS FREQ, ECHO TIME (combo), FEEDBACK, DECAY, PRE-DELAY, WET DUCK |
| BEAT (8 knobs, 104 px) | UNMASK, SENS, FOCUS LOW, FOCUS HIGH, RELEASE, SIDE, RIDE, POCKET | POCKET FREQ, POCKET Q, LOW CUT, MONO BELOW |
| BUS (8 knobs, 104 px) | LOW, LOW-MID, PRESENCE, AIR, GLUE, WIDTH, DRIVE, CLIP | RATIO, ATTACK, RELEASE, SC HPF, MONO BELOW, HIGH CUT, CEILING, TARGET |

Knobs whose value MIX changed in the last session get a 1-px red dot above the label until they are touched. The editor compares the current value with the session snapshot.

**Bottom panel** (20, 344, 940, 196; rounded 8; panel fill, line outline):

| Region | Bounds | VOCAL | BEAT | BUS |
|---|---|---|---|---|
| Meters | 36, 356, 290, 100 | IN, COMPRESS (GR), DE-ESS (GR), OUT. These are v0-style bars, x0 = 120, w = 160, rows at y 362/386/410/434; values right. | IN, UNMASK (max cut), RIDE, OUT | GLUE GR, LIMIT GR, TRUE PEAK, OUT |
| Small knobs | 36, 462, 64, 70 and 110, 462, 64, 70 | INPUT, LEVEL | LOW CUT, LEVEL | CEILING, TARGET |
| Role view | 340, 356, 300, 172 | **KEY**: 10 vertical bars of the published key (silver) | **UNMASK**: 10 bars of `B̂` (dim) with the current cut drawn downward in red from the top; key state text `KEY LINK ×2` / `KEY SIDECHAIN` / `NO KEY` / `SYNC LATE 12` (red) | **MIX VIEW**: per band, vocal-sum (silver) vs beat-sum (dim) levels from the hub, overlap shaded red, cut overlay as a red line. Below: big `−9.1 LUFS-S`, `I −9.0`, `TP −1.0`, with the target tick on a horizontal loudness bar (−24…−4 LUFS). |
| Explanation | 656, 356, 290, 140 | own notes (§5.7) | own notes | session summary (all instances; scrollable) |
| Actions | 656, 504, 90, 24 and 756, 504, 90, 24 | `UNDO MIX`, `LOAD DEFAULTS` / `RE-MIX` when applicable | same | same |

**Timer.** The editor timer runs at 30 Hz. It reads the processor's meter atomics, `hub.linkSummary(slot)` and `hub.session(slot)` (cheap, locked copies), and the hub `displayDb`/`cutDb` of peers. It repaints only the regions that changed.

---

## 9. State, persistence, automation, presets

### 9.1 What is saved per instance (`getStateInformation`)

The state is an XML ValueTree `MAKEMIX`, saved with `copyXmlToBinary` as in v0:

```
<MAKEMIX stateVersion="1" id="<32 hex>" cloneOf="" role="0" subRole="0" group="0" style="0" roleUserSet="1" trackName="Vox">
  <PARAMS …apvts.copyState()… />
  <LEARNED sessionId="17" style="0" learnedAt="2026-10-14T12:00:00" levelLoDb="-80" crestDb="11.2" scAlignRaw="0" valid="1"
           levelW="80 floats, space-separated" ltasDb="32 floats" satGainDb="9 floats">
     <NOTE text="INPUT +6.2 dB (recorded −26.2 LUFS)"/> …
  </LEARNED>
</MAKEMIX>
```

The size is about 4 KB. Captures are never saved.

`setStateInformation` (message thread):
1. Parse. If the root tag is not `MAKEMIX`, ignore the state, so v0 states are not supported.
2. If `stateVersion > 1`, load the known fields and keep the unknown ones as a child for round-trip.
3. Call `hub.restoreId(...)` (§3.4).
4. Set role, sub, group and style (atomics plus `hub.setInfo`), then `apvts.replaceState(PARAMS)`.
5. Parse `LEARNED` into `LearnedProfile`, then `compBox.write(profile.comp)`.

**Versioning:**
- `stateVersion` increments only for breaking changes, and a migration function `migrate(ValueTree&, from)` handles old ones.
- Parameter IDs and ranges never change. New parameters are appended, and old states then pick up their defaults through APVTS.

### 9.2 Automation

- All DSP parameters are automatable except `v_hpfSlope`, `b_key` and `m_target`. Parameter changes take effect smoothly (§6.0 grid).
- ROLE, SUB-ROLE, GROUP and STYLE are not parameters, so automation, FL Randomize and Hybridize cannot touch them.
- MIX writes are ordinary gestures. If an automation clip drives a parameter, the clip wins on playback. Users who automate LEVEL should press MIX first and automate relative moves after.

### 9.3 Presets

- **Host programs = the 12 styles.** `setCurrentProgram(i)` sets the style and applies `styleDefaults(i, role)` to the current role's parameters (keeping the level-owning ones), using gestures.
- Program 0 (TRAP) is the parameter default, so a fresh instance equals TRAP defaults.
- **User presets** are the host's own (FL "Save preset as"). They contain the full state, including role and ID, so the clone rule (§3.4) applies when a preset is dropped onto a running instance.
- In v1, presets are not stored by MAKE MIX itself.

### 9.4 Track name and role suggestion

- `updateTrackProperties` (JUCE calls it on the message thread) stores the name, sends it to the hub with `setName`, and shows it in the tooltip.
- If `roleUserSet == 0` and the instance has never received a state with `roleUserSet = 1`, the name suggests a role. Lower-case match:
  - `vox|vocal|voc|人声|lead|主唱|rap` → VOCAL/LEAD;
  - `adlib|ad-lib|ad lib|adl` → VOCAL/ADLIB;
  - `dbl|double|back|bv|harm|choir|和声|伴唱` → VOCAL/DOUBLE;
  - `beat|inst|instrumental|伴奏|music|808|drum|bass|melody|loop` → BEAT;
  - `master|主` → BUS.
- The suggestion is shown as `ROLE: VOCAL (from "Vox")` and the role control keeps pulsing until a click confirms it.

---

## 10. Testing plan

### 10.1 Unit tests (`mm_tests`, JUCE `UnitTest`; `ctest` runs it)

**DSP (`dsp_tests.cpp`, WP1):**
1. K-weighting coefficients at 48 k match BS.1770 within 1e-6, and at 44.1/96/192 k the response matches the analog prototype within 0.01 dB from 20 Hz to 20 kHz.
2. LoudnessMeter on EBU Tech 3341 cases 1–2: a 1 kHz stereo sine at −23/−33 dBFS reads −23.0/−33.0 ± 0.1 LUFS. A 20 s gated case with silence reads the integrated value ± 0.1.
3. TruePeak4x on an fs/4 sine at 45° phase reads +3.01 dB over the sample peak, ± 0.1.
4. The TP limiter on 30 s of pink noise plus impulses at +12 dB drive, at 44.1/48/96 k, gives output TP (independent 8× reference) ≤ ceiling + 0.1 dB and sample peak ≤ ceiling.
5. Every chain is block-size invariant: blocks {1, 7, 64, 512, 4096 (via chunking), random} against 512 give a max difference ≤ −120 dBFS. This is checked for static parameters and for parameter changes at fixed timeline positions.
6. A neutral BEAT is an exact delay (≤ −120 dBFS).
7. SVF bells under a 10 dB/10 ms gain sweep show no sample-step greater than 3× the static-response bound (no clicks).
8. Compensation: on the vocal proxy at −20 LUFS, predicted vs measured ΔI for `v_comp` ∈ {0, 0.25, 0.5, 0.75, 1} × `v_warmth` ∈ {0, 0.5, 1} × EQ presets is within ±0.7 LU.
9. Mono, stereo and 44.1–192 k: no NaN/Inf, latency equals `latencyFor`.
10. The NaN guard resets.

**Hub (`hub_tests.cpp`, WP2; also built with `-fsanitize=thread`, target `mm_tests_tsan`, and ASan):**
1. **Key ring:** 6 VOCAL writer threads publish 1e6 hops each, encoding `(slot, hop)` in the band values, at jittered rates. 4 BEAT reader threads read at lag 0…40 hops. Every `Ok` read must decode to exactly the requested hop and a correct weighted sum. There must be no torn reads and no TSan reports.
2. **Churn:** the main thread attaches and detaches slots every 1 ms while readers run. Reads from a detached slot are never `Ok` after detach has been observed. Generations stay monotonic.
3. **IDs:** the Same/Adopted/Clone matrix; duplicate IDs at attach; 33 instances (FULL); clone, then save, then reload preserves distinct IDs.
4. **Capture handshake:** the audio thread writes random block sizes while the message thread stops and reads the prefix; cancel at Armed; destruction while Running; StopReq is never acknowledged (the instance is not processed), then the processor is destroyed. No races and no leaks (ASan).
5. **Session machine** with fake `HubClient`s and a synchronous fake solver: Listen1 → Solve1 → apply → Settle2 → Listen2 → Done. Also: STOP early, not enough audio, transport stopped, a participant destroyed mid-solve, UNDO, remix, two BUSes (pass 2 skipped).
6. **Gesture apply:** values arrive as real units; begin/end gestures are paired.

**AutoMix and analysis (`automix_tests.cpp`, `analysis_tests.cpp`, WP3; no audio dependencies for AutoMix):**
1. The EQ fit recovers a synthetic 3-filter curve (±0.5 dB, ±10 % frequency).
2. `compAmountFor` is monotonic and hits the target spread on synthetic Gaussian/bimodal histograms within ±0.2 LU.
3. Ballistics from C² match the Giannoulis examples (C² = 10 gives 16 ms / 184 ms).
4. `deessAmountFor`, `levelForVbr` and `glueThresholdFor` behave correctly at their bounds.
5. The style table is sane: every field lies within the parameter ranges, every bus curve has a finite mean, and `styleDefaults(TRAP, role)` equals the parameter defaults.
6. Analysis: YIN gives ±2 % on synthetic harmonic tones from 80 to 600 Hz; VAD on bursts; joining with loops and gaps; the masking metrics on synthetic tone-in-noise.

### 10.2 Offline multi-instance harness (`tools/mixrender.cpp`, WP5)

```
mm_mixrender --vocal v.wav [--vocal-sub lead|double|adlib] [--vocal v2.wav …] --beat b.wav [--beat b2.wav …]
             [--no-bus] [--style trap] [--bpm 94] [--fs-check] [--blocks 512|random|1,32,441,4096] [--threads 2]
             [--learn-start 0] [--sidechain] [--bridged] [--clone-at 5.0] [--remove-at 8.0] [--seed 1]
             --out DIR [--json DIR/report.json] [--check]
```

Procedure:
1. `ScopedJuceInitialiser_GUI`. Create one `MakeMixProcessor` per input plus one BUS, all sharing the in-process hub. `--bridged` gives the BEAT a private hub. Set roles, sub-roles and style. Call `hub.setSynchronousSolve(true)`. Install a `HarnessPlayHead` (timeInSamples, isPlaying, bpm), then `prepareToPlay(fs, maxBlock)`.
2. **Cycle loop** (simulated FL):
   - The block size n comes from `--blocks`. `random` draws from 1…maxBlock, 2 % zero-length blocks and 2 % oversized blocks (1.45× maxBlock, which stays within the §3.6 sync bound and exercises chunking).
   - VOCAL and BEAT instances run in random order. With `--threads 2` they run concurrently on two `std::thread`s separated by a per-cycle barrier.
   - `busIn = Σ vocal out + Σ beat out` (and `--other` tracks). Then the BUS processes. If `--sidechain` is set, the vocal output is fed into the BEAT's aux delayed by L (raw-mode simulation) or not (compensated), switched by `--sc-raw`.
   - `hub.tick()` runs every 100 ms of audio.
3. **LEARN.** At `--learn-start`, call `processor.requestMix()` on the BUS. Keep cycling, looping the song if needed, until the session reaches `Done`/`Failed` (timeout: 3 song lengths).
4. **RENDER.** Reset positions, call `reset()` on all instances, render the whole song from 0 with tails (+3 s), and compensate the latency L (and 2L for the bus path).
   - Outputs: `vocal_i_out.wav`, `beat_j_out.wav`, `bus_out.wav`, `raw_sum.wav` (inputs summed at unity), `params.json` (every instance's ParamSet before and after).
5. **Determinism pass.** Render again with `--blocks 512 --threads 1` and with `--blocks random --threads 2`, then compare the bit difference of every output.
6. **Lifecycle events:** `--clone-at` creates a new instance by copying a VOCAL's state mid-play; `--remove-at` destroys a BEAT mid-play.
7. **Report JSON.** Metrics are computed with `analysis::*` on the WAV outputs, plus the solver metrics, CPU per instance (`steady_clock` around `processBlock`), solver time, `syncMiss` per BEAT, IDs and link counts. `tools/check_ffmpeg.py` cross-checks I/TP with `ffmpeg -af ebur128=peak=true`, requiring agreement within 0.2 LU and 0.2 dB.

### 10.3 Test material and degradation

The stems were separated with demucs from **mastered** songs, so they are already loud, compressed and de-essed:
- `…/scratchpad/vox/sep/htdemucs/huanghua_mix/{vocals,no_vocals}.wav` (谎话, TRAP or MELODIC RAP, 94 BPM);
- `…/iso_mix/{vocals,no_vocals}.wav` (Isolated, MELODIC RAP).

Tests therefore use three variants:
- **as-is**, a sanity check: mastered material must not be damaged;
- **degraded** with `tools/degrade.py --seed s`. It simulates raw recordings:
  - vocal gain −14…−6 dB;
  - tilt ±2 dB/oct;
  - +4 dB box bell at 200–300 Hz;
  - upward expansion 1:1.5 around the median (+50 % spread);
  - a short room IR at a WDR of −18 LU;
  - noise at −65 dBFS;
  - sibilance boost: a +4 dB shelf above 6 kHz on frames with d > P90;
  - beat gain ±6 dB, tilt ±1 dB/oct.
  Ten seeds per song.
- **flattened**: the vocal through a 1:2 upward expander plus a −3 dB/oct tilt, to undo mastering density.

The mastered original sum serves as the reference "pro" balance.

### 10.4 Pass/fail thresholds (`--check`; CI uses synthetic material, the developer machine uses the stems)

| # | Metric | Case | Pass |
|---|---|---|---|
| T1 | Preset loudness invariance: standalone VOCAL, `LOAD DEFAULTS` for each of the 12 styles, INPUT = LEVEL = 0, \|I_out − I_in\| | as-is + degraded seed 1, both songs | ≤ 1.0 LU (v0 failed by +5…+13) |
| T2 | VOCAL chain tone-only: \|I_out − (I_in + v_input + v_level)\| after MIX | all | ≤ 0.5 LU |
| T3 | VBR error \|VBR_out − style VBR\| | as-is | ≤ 1.0 dB |
| T3b | same | degraded (20 runs) | ≤ 1.5 dB in ≥ 90 % |
| T4 | SII proxy gain (after − before, both at the solved vocal level) | all | ≥ +0.04 |
| T4b | masked-cell fraction | all | decrease ≥ 0.03; final within 0.25–0.45 |
| T5 | beat loudness change \|ΔI_beat\| (unmask on, static EQ compensated) | all | ≤ 0.6 LU |
| T5b | beat transparency with vocal muted and neutral settings | synthetic + stems | output = input delayed by L, ≤ −120 dBFS |
| T6 | BUS \|I − target\| over the learned excerpt / over the whole file | all | ≤ 0.5 LU / report (≤ 1.5 LU expected) |
| T6b | BUS true peak (harness 8× reference and ffmpeg), sample peak | all | TP ≤ ceiling + 0.1 dB; sample peak ≤ ceiling |
| T6c | glue mean GR vs style target | all | ±0.75 dB |
| T7 | vocal LTAS distance to the style target (1/3-oct smoothed, 150 Hz–12 kHz, importance-weighted RMS) | degraded | ≥ 40 % reduction; final ≤ 2.5 dB |
| T8 | vocal momentary spread P10–P90 | degraded | target ± 1.5 LU |
| T8b | sibilance P99 − P90 after; change of P50 | degraded | ≤ style target + 1 dB; \|ΔP50\| ≤ 0.5 dB (no lisp) |
| T9 | determinism, realtime vs offline: random blocks 1…512 + 0-length + oversized (≤ 742) + random order + 2 threads vs fixed 512 single thread (maxBlock 512, L = 1024) | all | max diff ≤ −120 dBFS on every output; `syncMiss == 0` |
| T9b | blocks up to 4096 with `maxBlock = 4096` (so L = 2048) | synthetic | same as T9 |
| T9c | solver determinism: two MIX runs on identical captures | all | identical ParamSets |
| T10 | robustness: NaN/Inf, DC, mono vocal, silence input, noise at full scale, sweeps | synthetic | none; DC < −60 dBFS; silence → `NOT ENOUGH VOCAL`, no parameter changes |
| T10b | sample rates 44.1/48/96 (stems resampled by ffmpeg) | as-is HH | bus I target error ≤ 0.5 LU each; VBR spread across fs ≤ 0.3 dB |
| T11 | CPU at 48 k/512, Release, this container | — | VOCAL ≤ 3 %, BEAT ≤ 1.5 %, BUS ≤ 2 % of one core; 192 k ≤ 4× those |
| T11b | solver time (2 vocals + 1 beat 20 s; bus 12 s) | — | ≤ 5 s / ≤ 3 s |
| T12 | lifecycle: clone and remove mid-play; bridged BEAT | — | no crash; distinct IDs; clone carries params; bridged BEAT gives `LINK OFF` with an unchanged static sound |

The thresholds are starting values. Changing one needs a listening justification recorded in the PR.

### 10.5 pluginval

Run `pluginval --strictness-level 10 --validate-in-process --timeout-ms 600000 --output-dir build/pv "MAKE MIX.vst3"`. On Linux CI add `--skip-gui-tests`; on Windows run the GUI tests too.

pluginval's state save/restore across instances exercises the Clone path. Random parameter changes and block sizes exercise §6.0.

### 10.6 Build and CI notes

- **Linux dev:**
  ```
  cmake -S make-mix -B build -G Ninja -DJUCE_DIR=/home/user/JUCE -DCMAKE_BUILD_TYPE=Release && ninja -C build
  ```
  The targets are `MakeMix_VST3`, `mm_render`, `mm_mixrender` and `mm_tests`.
- **TSan:** a separate build directory with `-DMAKEMIX_TSAN=ON`, which adds `-fsanitize=thread -g -O1` to `mm_tests`/`mm_mixrender` only.
- **ASan:** `-DMAKEMIX_ASAN=ON`.
- **GitHub Actions** (`.github/workflows/make-mix.yml`, `working-directory: make-mix`):
  - **linux**: Ubuntu, JUCE dependencies (libasound2-dev, libx11-dev, libxrandr-dev, libxinerama-dev, libxcursor-dev, libfreetype-dev, libwebkit2gtk not needed). Build, `ctest`, TSan job, `mm_mixrender --check` on synthetic material, pluginval Linux.
  - **windows** (`windows-latest`):
    ```
    cmake -S make-mix -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
    cmake --build build --config Release --target MakeMix_VST3 mm_tests
    ```
    JUCE comes through FetchContent 8.0.10, cached with `actions/cache` on `build/_deps`. Then run `mm_tests`, run pluginval Windows (downloaded release zip) on `build/MakeMix_artefacts/Release/VST3/MAKE MIX.vst3`, and upload that folder as an artifact.
  - Use the static runtime, so there is no VC++ redistributable dependency.
  - `/utf-8` is required, because the sources contain Chinese comments and CJK UI strings: add `$<$<CXX_COMPILER_ID:MSVC>:/utf-8>` to the compile options.
  - Keep `JUCE_VST3_CAN_REPLACE_VST2=0`, `JUCE_WEB_BROWSER=0` and `JUCE_USE_CURL=0`.

---

## 11. Implementation work breakdown

File ownership is exclusive. A package that needs a change in another package's header files it as a request to the owner, with no direct edits.

| WP | Owner files | Delivers | Interfaces consumed | Est. LOC |
|---|---|---|---|---|
| **WP0 Foundation** (first, ≤ 1 day; then hands over) | `Shared.h`, `Params.h`, `Params.cpp`, `Styles.h` (data from §7, then owned by WP3), `CMakeLists.txt` (then owned by WP5), `tests/test_main.cpp`, **initial stubs** of every other file | Compiles everything with stubs; parameter table exactly as §6.1; `defaultCompProfile`; `formatValue`; C++20 switch | — | 900 |
| **WP1 DSP & chains** | `Dsp.h`, `Chains.h`, `Chains.cpp`, `tests/dsp_tests.cpp`, `tools/render.cpp` | §4.1 primitives, §6.2–6.4 chains, compensation, taps, key frames, calibration constants (§7.3) | Shared.h, Params.h | 2200 |
| **WP2 Hub & processor** | `Hub.h`, `Hub.cpp`, `PluginProcessor.h`, `PluginProcessor.cpp`, `tests/hub_tests.cpp` | §3 in full; APVTS layout from `kParams`; `ParamReader`; capture writer; key sink/source; state (§9); programs; meters; bypass; bridged detection; `requestMix/Stop/Undo` API for the editor and harness | Shared, Params, Styles, Chains.h, Solver.h, AutoMix.h (`styleDefaults`) | 1800 |
| **WP3 Analysis & auto-mix** | `Styles.h` (after WP0), `Analysis.h/.cpp`, `AutoMix.h/.cpp`, `Solver.h/.cpp`, `tests/automix_tests.cpp`, `tests/analysis_tests.cpp` | §4.2, §5, style tuning | Shared, Params, Chains.h (simulation), Dsp.h (K-weighting, Saturator) | 2400 |
| **WP4 UI** | `PluginEditor.h/.cpp`, `Look.h/.cpp` | §8 | `MakeMixProcessor` public API, `Hub::linkSummary/session/shared`, `formatValue` | 1500 |
| **WP5 Harness, CI & validation** | `CMakeLists.txt` (after WP0), `tools/mixrender.cpp`, `tools/degrade.py`, `tools/check_ffmpeg.py`, `.github/workflows/make-mix.yml` | §10.2–10.6; threshold report | processor public API, `Hub::tick/setSynchronousSolve`, `analysis::*` | 1100 |

**Processor public API** (WP2, consumed by WP4 and WP5; frozen in WP0's stub `PluginProcessor.h`):

```cpp
class MakeMixProcessor : public juce::AudioProcessor, public mm::HubClient
{
public:
    struct Options { bool privateHub = false; };          // 测试：模拟桥接（独立 hub）
    explicit MakeMixProcessor (Options = {});
    // 角色等（消息线程）
    mm::Role role() const noexcept;       void setRole (mm::Role, bool byUser);
    mm::SubRole subRole() const noexcept; void setSubRole (mm::SubRole);
    int group() const noexcept;           void setGroup (int);
    mm::StyleId style() const noexcept;   void setStyle (mm::StyleId, bool broadcast);
    void requestMix(); void requestStop (bool cancel); void requestUndo(); void requestRemix(); void loadStyleDefaults();
    mm::Hub* hub() const noexcept;        int slot() const noexcept;   const mm::InstanceId& instanceId() const noexcept;
    const mm::LearnedProfile& learned() const noexcept;               // 消息线程
    bool isBridgedHost() const noexcept;  std::string trackName() const;
    // 电平表（音频线程写，界面读）
    struct MeterAtomics { std::atomic<float> inPeak { 0 }, outPeak { 0 }, gr1 { 0 }, gr2 { 0 }, gr3 { 0 },
                          loudM { -100 }, loudS { -100 }, loudI { -100 }, truePeak { -100 }, autoComp { 0 }; };
    MeterAtomics meters;
    juce::AudioProcessorValueTreeState apvts;
    double fallbackBpm = 120.0;                                        // 没有宿主速度时
    // AudioProcessor / HubClient 覆盖略
};
```

**Stubs created by WP0** (each compiles and links, and is behaviourally neutral):

| Module | Stub behaviour |
|---|---|
| Chains.cpp | `prepare` allocates a delay of `spec.latency`. `process` = delay only. `coreLatency()` = 0. No key frames. `keyMode()` = Off. Meters zero. |
| Hub.cpp | `attach` returns −1. `publishKey`/`readVocalKey` → no-op/`Silent`. `beginMix` → false. `session()` → Idle. `linkSummary` → "ALONE". `InstanceId::random()` real (juce::Uuid). |
| Analysis.cpp | Every function returns default-initialised structs; `integratedLufs` → −100. |
| AutoMix.cpp | `styleDefaults` returns `kParams` defaults for the role; others return their inputs/defaults. |
| Solver.cpp | `runSolve` → `{ ok = false, error = "SOLVER STUB" }`. |
| PluginProcessor.cpp | Full APVTS layout, pass-through with `setLatencySamples(latencyFor(...))`, state save/load of the root attributes, `createEditor` → `GenericAudioProcessorEditor`. |
| PluginEditor.cpp / Look.cpp | Empty classes, not referenced by the stub processor. |
| tools/mixrender.cpp | Prints usage. |

**Integration order and milestones:**
1. **M0 (after WP0):** everything compiles with stubs. pluginval passes on the pass-through plugin with constant latency.
2. **M1 (WP1 + WP2):** real chains in the processor. Standalone styles work (T1). The hub key links VOCAL→BEAT (`mm_mixrender` without MIX; T5b, T9 for unmask keyed by a fixed vocal). IDs and state work (hub tests, T12). pluginval passes at strictness 10.
3. **M2 (+ WP3):** MIX end-to-end in the harness. T2–T8 are tuned. Style data are reviewed by listening on the stems (§10.3 cases).
4. **M3 (+ WP4):** UI is complete. Manual test inside a host (Linux: JUCE AudioPluginHost; Windows: FL).
5. **M4 (WP5 green):** Windows CI artifact. The Appendix A FL checks are performed and their results fed back (A.2/A.4 may change `latencyFor` or the `scAlignRaw` default). This is the release candidate.

WP4 and WP5 can start at M0, against stubs and the frozen processor API.

---

## 12. Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| FL export uses host blocks larger than `prepareToPlay` maxBlock, or sizes not covered by L (A.2, **UNVERIFIED**) | Hub-keyed unmask goes `Late`, so export ≠ realtime | `latencyFor` scales L with maxBlock; `SYNC LATE` indicator; sidechain key is deterministic; manual check A.2 before release; if needed raise the cap to 4096·r (85 ms) as a setting |
| FL sidechain path delay compensation unknown (A.4) | sidechain key misaligned by L | auto-detect `scAlignRaw` when link + sidechain both exist; default "compensated"; manual check |
| Timeline semantics under PDC (does `timeInSamples` refer to input or output time? A.3) | key frame misalignment between tracks | equal latency on every role makes both tracks see the same convention; the harness covers both; manual impulse test A.3 |
| Style numbers are low-confidence (L) for Mandopop, ballad, drill, Afro | results feel generic or wrong | data-driven `Styles.h`; v1.1 adds measurement with `mm_mixrender --analyse-ref` on 10–20 demucs-separated references per style (Mandopop and ballad first); "learn target from reference" planned for v2 |
| Test stems are mastered, so they do not represent raw input | false confidence | degradation protocol (§10.3); listening checklist (algorithm research §6.4) on degraded cases |
| Capture memory at high fs or with many instances | allocation failure | message-thread allocation with fallback to 15 s; 256 MB retention cap; release on new MIX |
| Solver slow on weak CPUs | long `MIXING…` | background thread; progress; cancel; 4096-sample simulation blocks; T11b budget |
| Users move faders, or have unlinked tracks or other FX on master | VBR or loudness off | first-run hint "keep faders at 0 dB, MAKE MIX last"; B6 unlinked-track warning; v2 FADER CHECK (NNLS gain estimate from host research §2) |
| Automation clips override MIX values | MIX looks ineffective | documented; v2 detection (parameter moved by host shortly after apply → note) |
| Bridged, sandboxed or duplicate DLL copies | no link | detection and explicit hint; every role works standalone; sidechain key; v2 named shared-memory presence registry to diagnose duplicates and bridging precisely |
| FL undo history flooded (≈ 50 gestures per MIX) | annoyance | values written only when they changed; `UNDO MIX` is one click |
| Time-varying EQ artifacts on beat (zipper, pumping hats) | audible | SVF + coefficient interpolation + 1.5 dB per-hop slew + no cuts above `b_focusHi` (≤ 9 kHz) + mid-only by default; listening checklist item 3 |
| Learn excerpt not representative (verse vs hook) | bus LUFS off over the whole song | UI advises looping the hook; whole-file I reported in the harness; `RE-MIX`; v2 multi-section learn |
| Freeverb CPU and colour | CPU about 1.5 %, dated sound | acceptable for v1; v2 FDN reverb behind the same `Space` interface |
| Role mis-set (VOCAL on both a track and its sub-bus) | double processing | track-name suggestion; link-list tooltip; heuristic warning (§1.3) |
| CJK glyphs not rendered on Windows | unreadable style names | explicit "Microsoft YaHei UI" fallback typeface |
| Patcher, smart disable, 0/1-sample blocks | crashes or stale data | prepared flag; constructor registration; staleness → silence; 0-length early return; tests T9/T10 and pluginval |

---

## Appendix A. One-time manual FL Studio checks (debug build `-DMAKEMIX_DIAG=ON`)

The diagnostic build logs into a lock-free ring, flushed by a message-thread timer to `%TEMP%/makemix_diag.txt`.

| # | Check | Expected / decision |
|---|---|---|
| A.1 | Every `setChannelContextInfos` key and value (name, colour, index, UID; master's name) | name feeds the role suggestion; if index/UID exist, add them to the tooltip |
| A.2 | `prepareToPlay` maxBlock and the actual block sizes during playback and during export (all 4 export modes, split mixer tracks) | confirm `latencyFor`; if export blocks exceed the prepared maximum, raise the cap or force SC for determinism |
| A.3 | Impulse test: clicks at known song positions on the vocal and beat tracks, with MAKE MIX on both; logged `timeInSamples` vs the content positions of the clicks | confirms the content-timeline stamping (`timeInSamples` = input time) |
| A.4 | Sidechain from the vocal track (with MAKE MIX VOCAL, latency L) to the beat; offset between aux and main | sets the `scAlignRaw` default |
| A.5 | VST3 calls on transport start/stop, song-position jumps, smart disable, Reset Plugins on Transport | `reset()` behaviour and staleness handling |
| A.6 | Whether `activateBus` is called for the aux input, and whether "Auto map inputs" works | the sidechain guide text |
| A.7 | Bridged mode: `ilbridge.exe` process count and parent | detection text; the v2 shared-memory design |
| A.8 | Thread IDs and call order per cycle across the three tracks | confirms the "sources before master" assumption |

## Appendix B. v0 reuse map

| v0 element | v1 fate |
|---|---|
| `vox::Biquad` (RBJ, TDF-II) | kept in `Dsp.h` for static filters (K-weighting, de-ess split helpers). EQs that can be automated move to the `Svf` (TPT) primitive. |
| `vox::Compressor` (soft knee, dB ballistics) | kept as the gain computer and ballistics; detector made explicit (RMS one-pole or peak); `makeupDb` removed and replaced by `autoComp` |
| `vox::DeEsser` (LR4 split) | kept split; detector changed to the relative HF/full ratio; placed after the EQ |
| `vox::Saturator` (tanh, −12 dBFS reference, DC blocker, 2× OS) | kept as is; loudness compensated through `satGainDb`; its meaning is fixed by the input normalisation |
| `vox::DelayLine`, `vox::Echo` | kept; duck changed from `env*5` (absolute) to the relative mapping in §6.2; time choices get 1/16 |
| `vox::Space` | kept; `roomSize` from decay seconds; wet LP follows `v_lpf` |
| chunked `processBlock`, latency from oversampling | kept and padded to the constant L |
| `updateDsp` per block with RBJ recompute and block-start ramps | replaced by the 32-sample timeline grid and interpolated SVFs |
| `makeupDb = −thr·(1 − 1/R)·0.45` | removed (root cause of +5…+13 LU) |
| `VoxLook`, knob drawing, meters | evolve into `MixLook` (`Look.*`) |
| `Presets.h` | replaced by `Styles.h` and `styleDefaults` |
| `tools/render.cpp` | becomes `mm_render`, with `--role/--style/--mix` |
