#pragma once
// 一键混音的解算：拿到各实例录下来的原始输入，按风格算出每个实例的参数。
// 不靠公式猜结果：每一步都用真实的处理链（Chains）在录音上跑一遍，量出来再调（压缩量、人声电平、总线响度都是这样定的）。
#include "Analysis.h"
#include "Chains.h"

namespace mm::automix
{
struct Capture { analysis::Stereo audio; int64_t start = 0; int client = -1; ParamSet current; };
struct In
{
    double fs = 44100, bpm = 120; StyleId style = 0;
    std::vector<Capture> vocals, beats;
    bool hasBus = false; int busClient = -1; ParamSet busCurrent;
};
struct ClientOut { int client = -1; ParamSet set; std::string note; };
struct Metrics { float vbrBefore = 0, vbrAfter = 0, maskBefore = 0, maskAfter = 0, busLufs = -100, busTp = -100, glueGr = 0, seconds = 0; };
struct Out { bool ok = false; std::string error; std::vector<ClientOut> clients; Metrics m; std::string summary; };

Out solve (const In&);

// 模拟工具（解算和测试工具共用）：返回已去掉延迟的输出
analysis::Stereo runVocal (const analysis::Stereo&, const VocalChain::P&, StyleId, double fs, double bpm, int64_t start, KeySink*);
analysis::Stereo runBeat (const analysis::Stereo&, const BeatChain::P&, StyleId, double fs, int64_t start, KeySource*);
analysis::Stereo runBus (const analysis::Stereo&, const BusChain::P&, StyleId, double fs, float* meanGlueGr = nullptr);
} // namespace mm::automix
