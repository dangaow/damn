// 【WP0 桩】不解算，直接失败。WP3 换成 5.6 的实现。
#include "Solver.h"
#include "Analysis.h"
#include "AutoMix.h"
#include "Chains.h"

namespace mm
{
SolveResult runSolve (const SolveRequest&)
{
    SolveResult r;
    r.ok = false;
    r.error = "SOLVER STUB";
    return r;
}
} // namespace mm
