#pragma once
// Shared bench harness types. (Task 1 brief inlines `struct Sample` per-TU,
// which cannot link: each TU's `Sample` would be a distinct type for
// `RegisterBench`. This header keeps one definition.)
#include <algorithm>
#include <vector>

struct Sample {
    const char* name;
    double median_ns;
    double p95_ns;
    long allocs;
};

void RegisterBench(Sample (*)());

inline Sample MakeSample(const char* name, std::vector<double>& ns, long allocs = 0) {
    std::sort(ns.begin(), ns.end());
    return {name, ns[ns.size() / 2], ns[ns.size() * 95 / 100], allocs};
}
