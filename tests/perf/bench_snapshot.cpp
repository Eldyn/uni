// tests/perf/bench_snapshot.cpp
// Baseline for the match snapshot/event emit path: mirrors
// MatchSession::EmitEvents (src/match/server/match_session.cpp) — wrap one
// engine event into a per-viewer packet and serialize it (today: one
// nlohmann::json dump per recipient).
#include <chrono>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "bench.hpp"

namespace {
using nlohmann::json;

Sample BenchSnapshotEmit() {
    std::vector<double> ns;
    ns.reserve(2000);
    std::size_t ok = 0;
    for (int i = 0; i < 2000; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        json event = {{"type", "turn_changed"},
                      {"turn_no", i},
                      {"player", "alice"},
                      {"cards", {"red_5", "blue_skip"}}};
        json packet = event;
        packet["action"] = "match_event";
        std::string wire = packet.dump();
        auto t1 = std::chrono::steady_clock::now();
        if (wire.find("match_event") != std::string::npos) ++ok;
        ns.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    if (ok != 2000) return {"snapshot_emit", -1.0, -1.0, -1};
    return MakeSample("snapshot_emit", ns, 0);
}

struct Reg {
    Reg() { RegisterBench(BenchSnapshotEmit); }
};
Reg kReg;
}  // namespace
