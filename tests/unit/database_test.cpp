#include <doctest/doctest.h>
#include <database.hpp>

TEST_SUITE("Database") {

TEST_CASE("migrations: user_version advances to latest") {
    auto applied = Database::Get().RunMigrations();
    REQUIRE(applied.has_value());

    auto ver = Database::Get().QueryOne("PRAGMA user_version;", {});
    REQUIRE(ver.has_value());
    REQUIRE(ver->has_value());
    CHECK(ver.value()->Get<int>("user_version") == 2);
}

TEST_CASE("migrations: expected tables exist") {
    auto applied = Database::Get().RunMigrations();
    REQUIRE(applied.has_value());

    auto rows = Database::Get().Query(
        "SELECT name FROM sqlite_master WHERE type = 'table' AND name = ?;",
        {std::string("users")});
    REQUIRE(rows.has_value());
    CHECK(rows->size() == 1);

    rows = Database::Get().Query(
        "SELECT name FROM sqlite_master WHERE type = 'table' AND name = ?;",
        {std::string("player_stats")});
    REQUIRE(rows.has_value());
    CHECK(rows->size() == 1);
}

TEST_CASE("migrations: v2 renamed cards_played_colorswitch to cards_played_jolly") {
    auto applied = Database::Get().RunMigrations();
    REQUIRE(applied.has_value());

    auto rows = Database::Get().Query("PRAGMA table_info(player_stats);", {});
    REQUIRE(rows.has_value());

    bool has_jolly = false;
    bool has_colorswitch = false;
    for (const auto& row : *rows) {
        std::string name = row.Get<std::string>("name");
        if (name == "cards_played_jolly") has_jolly = true;
        if (name == "cards_played_colorswitch") has_colorswitch = true;
    }
    CHECK(has_jolly);
    CHECK_FALSE(has_colorswitch);
}

TEST_CASE("migrations: re-running RunMigrations is a no-op") {
    auto before = Database::Get().QueryOne("PRAGMA user_version;", {});
    REQUIRE(before.has_value());
    REQUIRE(before->has_value());
    int before_version = before.value()->Get<int>("user_version");

    auto result = Database::Get().RunMigrations();
    CHECK(result.has_value());

    auto after = Database::Get().QueryOne("PRAGMA user_version;", {});
    REQUIRE(after.has_value());
    REQUIRE(after->has_value());
    CHECK(after.value()->Get<int>("user_version") == before_version);
}

TEST_CASE("TransactionGuard: begins successfully") {
    TransactionGuard tx(Database::Get());
    CHECK(tx.Ok());
    auto commit = tx.Commit();
    CHECK(commit.has_value());
}

TEST_CASE("TransactionGuard: rollback on scope exit without Commit") {
    auto drop = Database::Get().Exec("DROP TABLE IF EXISTS test_db_scratch;");
    REQUIRE(drop.has_value());
    auto create = Database::Get().Exec(
        "CREATE TABLE test_db_scratch (id INTEGER PRIMARY KEY, val TEXT);");
    REQUIRE(create.has_value());

    {
        TransactionGuard tx(Database::Get());
        REQUIRE(tx.Ok());
        auto write = Database::Get().Exec(
            "INSERT INTO test_db_scratch (id, val) VALUES (1, 'rolled_back');");
        REQUIRE(write.has_value());
    }

    auto rows = Database::Get().Query("SELECT * FROM test_db_scratch;", {});
    REQUIRE(rows.has_value());
    CHECK(rows->empty());

    auto cleanup = Database::Get().Exec("DROP TABLE IF EXISTS test_db_scratch;");
    CHECK(cleanup.has_value());
}

TEST_CASE("TransactionGuard: commit persists the write") {
    auto drop = Database::Get().Exec("DROP TABLE IF EXISTS test_db_scratch;");
    REQUIRE(drop.has_value());
    auto create = Database::Get().Exec(
        "CREATE TABLE test_db_scratch (id INTEGER PRIMARY KEY, val TEXT);");
    REQUIRE(create.has_value());

    {
        TransactionGuard tx(Database::Get());
        REQUIRE(tx.Ok());
        auto write = Database::Get().Exec(
            "INSERT INTO test_db_scratch (id, val) VALUES (1, 'committed');");
        REQUIRE(write.has_value());
        auto commit = tx.Commit();
        CHECK(commit.has_value());
    }

    auto rows = Database::Get().Query("SELECT * FROM test_db_scratch;", {});
    REQUIRE(rows.has_value());
    REQUIRE(rows->size() == 1);
    CHECK(rows->at(0).Get<std::string>("val") == "committed");

    auto cleanup = Database::Get().Exec("DROP TABLE IF EXISTS test_db_scratch;");
    CHECK(cleanup.has_value());
}

TEST_CASE("TransactionGuard: nested BEGIN fails and Ok() reflects it") {
    TransactionGuard outer(Database::Get());
    REQUIRE(outer.Ok());

    TransactionGuard inner(Database::Get());
    CHECK_FALSE(inner.Ok());
    CHECK(inner.GetError().code == Error::Code::kDatabaseFailure);

    auto commit = inner.Commit();
    CHECK_FALSE(commit.has_value());

    auto outer_commit = outer.Commit();
    CHECK(outer_commit.has_value());
}

TEST_CASE("DbRow: Get/GetOr/Has round-trip through a real query") {
    auto drop = Database::Get().Exec("DROP TABLE IF EXISTS test_db_scratch;");
    REQUIRE(drop.has_value());
    auto create = Database::Get().Exec(
        "CREATE TABLE test_db_scratch (id INTEGER PRIMARY KEY, val TEXT, note TEXT);");
    REQUIRE(create.has_value());
    auto insert = Database::Get().Exec(
        "INSERT INTO test_db_scratch (id, val, note) VALUES (1, 'hello', NULL);");
    REQUIRE(insert.has_value());

    auto row = Database::Get().QueryOne(
        "SELECT * FROM test_db_scratch WHERE id = ?;", {1});
    REQUIRE(row.has_value());
    REQUIRE(row->has_value());

    const DbRow& r = **row;
    CHECK(r.Has("id"));
    CHECK(r.Has("val"));
    CHECK(r.Has("note"));
    CHECK_FALSE(r.Has("nonexistent"));

    CHECK(r.Get<int>("id") == 1);
    CHECK(r.Get<std::string>("val") == "hello");

    CHECK(r.GetOr<std::string>("note", "fallback") == "fallback");
    CHECK(r.GetOr<std::string>("val", "fallback") == "hello");
    CHECK(r.GetOr<std::string>("missing", "fallback") == "fallback");

    auto cleanup = Database::Get().Exec("DROP TABLE IF EXISTS test_db_scratch;");
    CHECK(cleanup.has_value());
}

}  // TEST_SUITE
