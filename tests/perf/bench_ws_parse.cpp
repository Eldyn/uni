// tests/perf/bench_ws_parse.cpp
// Baseline for the WS inbound parse path: mirrors WebServer::OnSocketMessage
// (src/webserver.cpp) — parse frame with exceptions off, reject non-objects,
// require string "action" — then extract action/request_id.
#include <chrono>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>
#include "bench.hpp"

namespace {
using nlohmann::json;

constexpr std::string_view kFrame =
    R"({"action":"chat_send","request_id":"r1","body":"hi"})";

Sample BenchWsParse() {
    std::vector<double> ns;
    ns.reserve(5000);
    std::size_t ok = 0;
    for (int i = 0; i < 5000; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        json msg = json::parse(kFrame, nullptr, /*allow_exceptions=*/false);
        bool valid = !msg.is_discarded() && msg.is_object() && msg.contains("action") &&
                     msg["action"].is_string();
        std::string action = valid ? msg["action"].get<std::string>() : "";
        std::string req = (valid && msg.contains("request_id") && msg["request_id"].is_string())
                              ? msg["request_id"].get<std::string>()
                              : "";
        auto t1 = std::chrono::steady_clock::now();
        if (valid && action == "chat_send" && req == "r1") ++ok;
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
