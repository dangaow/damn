// 多实例离线测试工具（10.2）：模拟 FL 里的 VOCAL / BEAT / BUS 一起跑，学习、解算、渲染、检查阈值。
// 【WP0 桩】只打印用法。WP5 实现。
#include <cstring>
#include <iostream>

namespace
{
const char* const kUsage =
    "usage: mm_mixrender --vocal v.wav [--vocal-sub lead|double|adlib] [--vocal v2.wav ...] --beat b.wav [--beat b2.wav ...]\n"
    "                    [--no-bus] [--style trap] [--bpm 94] [--fs-check] [--blocks 512|random|1,32,441,4096] [--threads 2]\n"
    "                    [--learn-start 0] [--sidechain] [--bridged] [--clone-at 5.0] [--remove-at 8.0] [--seed 1]\n"
    "                    --out DIR [--json DIR/report.json] [--check]\n";
}

int main (int argc, char** argv)
{
    std::cout << kUsage;
    const bool help = argc < 2 || std::strcmp (argv[1], "--help") == 0 || std::strcmp (argv[1], "-h") == 0;
    if (help) return 0;
    std::cerr << "mm_mixrender: not implemented yet (stub)\n";
    return 2;
}
