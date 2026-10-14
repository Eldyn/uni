// tests/perf/bench_main.cpp
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include "bench.hpp"

static std::vector<Sample (*)()> g_benches;
void RegisterBench(Sample (*fn)()) { g_benches.push_back(fn); }
int main(int argc, char** argv) {
    std::string filter = argc > 1 ? argv[1] : "";
    // Plan interface is `uni_bench --bench=<name>`; also accept a bare name.
    const std::string kPrefix = "--bench=";
    if (filter.rfind(kPrefix, 0) == 0) filter = filter.substr(kPrefix.size());
    for (auto fn : g_benches) {
        Sample s = fn();
        if (!filter.empty() && filter != s.name) continue;
        std::printf("%s median=%.0fns p95=%.0fns allocs=%ld\n", s.name, s.median_ns, s.p95_ns, s.allocs);
    }
    return 0;
}
