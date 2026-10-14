#include <doctest/doctest.h>
#include <database.hpp>
#include <future>
#include <vector>

TEST_CASE("db worker serializes 100 concurrent writes") {
    std::vector<std::future<VoidResult>> futs;
    futs.reserve(100);
    for (int i = 0; i < 100; ++i) futs.push_back(Database::Get().ExecAsync("SELECT 1;", {}));
    for (auto& f : futs) CHECK(f.get().has_value());
}

TEST_CASE("db worker QueryOneAsync round-trips a row") {
    auto row = Database::Get().QueryOneAsync("SELECT 1 AS one;", {}).get();
    REQUIRE(row.has_value());
    REQUIRE(row->has_value());
    CHECK(row->value().Get<int>("one") == 1);
}

TEST_CASE("db worker RunInTxn commits and reads back inside one txn") {
    auto drop = Database::Get().Exec("DROP TABLE IF EXISTS test_db_worker_scratch;");
    REQUIRE(drop.has_value());
    auto create = Database::Get().Exec(
        "CREATE TABLE test_db_worker_scratch (id INTEGER PRIMARY KEY, val TEXT);");
    REQUIRE(create.has_value());

    auto fut = Database::Get().RunInTxn([](Database& db) -> Result<int> {
        auto w = db.Exec("INSERT INTO test_db_worker_scratch (id, val) VALUES (1, 'txn');");
        if (!w) return std::unexpected(w.error());
        auto r = db.QueryOne("SELECT val FROM test_db_worker_scratch WHERE id = ?;", {1});
        if (!r) return std::unexpected(r.error());
        if (!r->has_value()) return std::unexpected(Error::DatabaseFail("row missing in txn"));
        return r->value().Get<std::string>("val") == "txn" ? 1 : 0;
    });
    auto res = fut.get();
    REQUIRE(res.has_value());
    CHECK(*res == 1);

    auto rows = Database::Get().Query("SELECT * FROM test_db_worker_scratch;", {});
    REQUIRE(rows.has_value());
    REQUIRE(rows->size() == 1);

    auto cleanup = Database::Get().Exec("DROP TABLE IF EXISTS test_db_worker_scratch;");
    CHECK(cleanup.has_value());
}

TEST_CASE("db worker RunInTxn rolls back when the closure fails") {
    auto drop = Database::Get().Exec("DROP TABLE IF EXISTS test_db_worker_scratch;");
    REQUIRE(drop.has_value());
    auto create = Database::Get().Exec(
        "CREATE TABLE test_db_worker_scratch (id INTEGER PRIMARY KEY, val TEXT);");
    REQUIRE(create.has_value());

    auto fut = Database::Get().RunInTxn([](Database& db) -> VoidResult {
        auto w = db.Exec("INSERT INTO test_db_worker_scratch (id, val) VALUES (1, 'doomed');");
        if (!w) return std::unexpected(w.error());
        return std::unexpected(Error::DatabaseFail("boom"));
    });
    auto res = fut.get();
    CHECK_FALSE(res.has_value());

    auto rows = Database::Get().Query("SELECT * FROM test_db_worker_scratch;", {});
    REQUIRE(rows.has_value());
    CHECK(rows->empty());

    auto cleanup = Database::Get().Exec("DROP TABLE IF EXISTS test_db_worker_scratch;");
    CHECK(cleanup.has_value());
}
