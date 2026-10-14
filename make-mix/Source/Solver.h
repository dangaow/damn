#pragma once
// 解算编排（5.6）：分析 → AutoMix → 链仿真 → 校验 → SolveResult。
#include "Shared.h"
namespace mm
{
// 阻塞调用，在 hub 的解算线程里跑；确定性（同样的输入 → 同样的输出）
SolveResult runSolve (const SolveRequest&);
}
