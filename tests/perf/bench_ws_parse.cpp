// tests/perf/bench_ws_parse.cpp
// Task 7: measures the zero-alloc inbound pre-parse path
// (ParseInbound span scan + Fnv1a, src/webserver.cpp OnSocketMessage head).
// Compare against tests/perf/baselines/m1.json `ws_parse` (old json::parse
// DOM path, median=832ns). That baseline file is untouched on purpose.
#include <chrono>
#include <string>
#include <string_view>
#include <vector>
#include "bench.hpp"
#include "common/ws_parse.hpp"

namespace {

constexpr std::string_view kFrame =
    R"({"action":"chat_send","request_id":"r1","body":"hi"})";

Sample BenchWsParse() {
    std::vector<double> ns;
    ns.reserve(5000);
    std::size_t ok = 0;
    const uint32_t want = Fnv1a("chat_send");
    for (int i = 0; i < 5000; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        Inbound in = ParseInbound(kFrame);
        uint32_t h = Fnv1a(in.action);
        auto t1 = std::chrono::steady_clock::now();
        if (in.action == "chat_send" && in.request_id == "r1" && h == want) ++ok;
        ns.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    // Guard against the loop being optimized away / silently broken.
    if (ok != 5000) return {"ws_parse", -1.0, -1.0, -1};
    return MakeSample("ws_parse", ns, 0);
}

struct Reg {
    Reg() { RegisterBench(BenchWsParse); }
};
Reg kReg;
}  // namespace
