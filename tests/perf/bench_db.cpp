// tests/perf/bench_db.cpp
// Baseline for the DB token lookup hot path (auth token_version read).
// Opens an in-memory DB and a minimal users table so the bench is
// self-contained (uni_bench links only src/database.cpp).
#include <chrono>
#include <string>
#include <vector>
#include "bench.hpp"
#include "database.hpp"

namespace {
void EnsureBenchDb() {
    if (!Database::Get().IsOpen()) {
        (void)Database::Get().Open(":memory:");
    }
    (void)Database::Get().Exec(
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "username TEXT NOT NULL UNIQUE, "
        "token_version INTEGER NOT NULL DEFAULT 0);");
    (void)Database::Get().Exec(
        "INSERT OR IGNORE INTO users (username, token_version) VALUES ('bench', 1);");
}

Sample BenchDbTokenLookup() {
    EnsureBenchDb();
    std::vector<double> ns;
    ns.reserve(2000);
    for (int i = 0; i < 2000; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        auto r = Database::Get().QueryOne(
            "SELECT token_version FROM users WHERE username = ?;", {std::string("bench")});
        // Keep the compiler honest: the row must exist.
        if (!r || !r->has_value()) break;
        auto t1 = std::chrono::steady_clock::now();
        ns.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    if (ns.empty()) return {"db_token_lookup", -1.0, -1.0, -1};
    return MakeSample("db_token_lookup", ns, 0);
}

struct Reg {
    Reg() { RegisterBench(BenchDbTokenLookup); }
};
Reg kReg;
}  // namespace
