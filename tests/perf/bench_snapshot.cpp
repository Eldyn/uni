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
#include "match/view/wire.hpp"

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

// Shared-frame emit: mirrors MatchSession::BroadcastMatchOver — one glaze
// dump into a reused scratch frame, "sent" to N=4 recipients. Dumps per emit
// stay 1 while recipients scale (N->1); the timer covers the single dump.
Sample BenchSnapshotEmitShared() {
    constexpr int kRecipients = 4;
    constexpr int kIters = 2000;
    std::vector<double> ns;
    ns.reserve(kIters);
    std::size_t ok = 0;
    std::size_t dumps = 0;
    std::string shared;
    shared.reserve(512);
    for (int i = 0; i < kIters; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        match::view::MatchOverWire over{"match_over", "alice",
                                        {"alice", "bob"}};
        if (!match::view::WritePacket(over, shared)) {
            return {"snapshot_emit_shared", -1.0, -1.0, -1};
        }
        ++dumps;
        for (int r = 0; r < kRecipients; ++r) {
            if (shared.find("match_over") != std::string::npos) ++ok;
        }
        auto t1 = std::chrono::steady_clock::now();
        ns.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    if (ok != static_cast<std::size_t>(kIters * kRecipients)) {
        return {"snapshot_emit_shared", -1.0, -1.0, -1};
    }
    if (dumps != static_cast<std::size_t>(kIters)) {
        return {"snapshot_emit_shared", -1.0, -1.0, -1};
    }
    return MakeSample("snapshot_emit_shared", ns, 0);
}

struct Reg {
    Reg() {
        RegisterBench(BenchSnapshotEmit);
        RegisterBench(BenchSnapshotEmitShared);
    }
};
Reg kReg;
}  // namespace
