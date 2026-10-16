#include <doctest/doctest.h>
#include <cstdint>
#include "database.hpp"

TEST_CASE("stmt cache reuses prepare") {
    // Ensure the table exists regardless of suite order (migrations may not
    // have run yet in this process; prepare fails on a missing table, which
    // would leave the cache empty and the CHECK below meaningless).
    auto setup = Database::Get().Exec(
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "username TEXT NOT NULL UNIQUE, "
        "token_version INTEGER NOT NULL DEFAULT 0);");
    REQUIRE(setup.has_value());
    (void)Database::Get().QueryOne("SELECT token_version FROM users WHERE username = ?;", {"x"});
    (void)Database::Get().QueryOne("SELECT token_version FROM users WHERE username = ?;", {"y"});
    CHECK(Database::Get().StmtCacheHitsForTests() >= 1);
}

TEST_CASE("stmt cache covers QueryAsync path") {
    auto setup = Database::Get().Exec(
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "username TEXT NOT NULL UNIQUE, "
        "token_version INTEGER NOT NULL DEFAULT 0);");
    REQUIRE(setup.has_value());
    std::uint64_t before = Database::Get().StmtCacheHitsForTests();
    auto f1 = Database::Get().QueryAsync(
        "SELECT token_version FROM users WHERE username = ?;", {std::string("x")});
    auto f2 = Database::Get().QueryAsync(
        "SELECT token_version FROM users WHERE username = ?;", {std::string("y")});
    CHECK(f1.get().has_value());
    CHECK(f2.get().has_value());
    CHECK(Database::Get().StmtCacheHitsForTests() > before);
}
