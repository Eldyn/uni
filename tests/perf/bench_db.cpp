// tests/perf/bench_db.cpp
// Baseline for the DB token lookup hot path (auth token_version read).
// Opens an in-memory DB and a minimal users table so the bench is
// self-contained (uni_bench links only src/database.cpp).
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
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
    if (ns.size() != 2000) return {"db_token_lookup", -1.0, -1.0, -1};
    // Cache hit-rate metric (Task 5 product): 2000 identical lookups should
    // reuse one cached stmt. stderr keeps stdout sample lines diffable vs m1.
    std::uint64_t hits = Database::Get().StmtCacheHitsForTests();
    std::fprintf(stderr, "stmt_cache_hits=%llu (of 2000 lookups)\n",
                 static_cast<unsigned long long>(hits));
    return MakeSample("db_token_lookup", ns, 0);
}

struct Reg {
    Reg() { RegisterBench(BenchDbTokenLookup); }
};
Reg kReg;

// Task 6 bench: the DM page query builds one JSON array inside SQLite
// (single TEXT row) and the loop parses it — mirrors
// ChatService::GetDirectHistoryPage's SQL shape and loop-side parse.
void EnsureBenchDmTable() {
    (void)Database::Get().Exec(
        "CREATE TABLE IF NOT EXISTS chat_dms ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "sender TEXT NOT NULL, recipient TEXT NOT NULL, "
        "nonce TEXT NOT NULL, ciphertext TEXT NOT NULL, "
        "aad_version INTEGER NOT NULL DEFAULT 0);");
    (void)Database::Get().Exec("DELETE FROM chat_dms;");
    for (int i = 0; i < 60; ++i) {
        (void)Database::Get().Exec(
            "INSERT INTO chat_dms (sender, recipient, nonce, ciphertext, aad_version) "
            "VALUES ('bench_alice', 'bench_bob', 'n', 'c', 1);");
    }
}

Sample BenchDbDmPage() {
    EnsureBenchDb();
    EnsureBenchDmTable();
    static const char* kPageSql =
        "SELECT COALESCE(json_group_array(json_object('id',id,'sender',sender,'recipient',recipient,"
        "'nonce',nonce,'ciphertext',ciphertext,'aad_version',aad_version)), '[]') AS j "
        "FROM (SELECT id,sender,recipient,nonce,ciphertext,aad_version FROM chat_dms WHERE "
        "((sender = ? AND recipient = ?) OR (sender = ? AND recipient = ?)) "
        "ORDER BY id DESC LIMIT ?);";
    std::vector<double> ns;
    ns.reserve(2000);
    for (int i = 0; i < 2000; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        auto r = Database::Get().QueryOne(
            kPageSql, {std::string("bench_alice"), std::string("bench_bob"),
                       std::string("bench_bob"), std::string("bench_alice"), 51});
        // Keep the compiler honest: the page must hold the 51 newest rows.
        if (!r || !r->has_value()) break;
        auto arr = nlohmann::json::parse(r->value().Get<std::string>("j"));
        if (arr.size() != 51) break;
        auto t1 = std::chrono::steady_clock::now();
        ns.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    if (ns.size() != 2000) return {"db_dm_page", -1.0, -1.0, -1};
    return MakeSample("db_dm_page", ns, 0);
}

struct RegDmPage {
    RegDmPage() { RegisterBench(BenchDbDmPage); }
};
RegDmPage kRegDmPage;
}  // namespace
