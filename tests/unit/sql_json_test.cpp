// Task 6: the DM page / friend lists / leaderboard build their JSON inside
// SQLite (json_object/json_group_array) and read back a single TEXT row
// instead of one DbRow map per cell.
#include <doctest/doctest.h>
#include "database.hpp"

TEST_CASE("dm page returns single json array row") {
    auto r = Database::Get().QueryOne(
      "SELECT json_group_array(json_object('id',id,'sender',sender)) AS j FROM (SELECT 1 AS id, 'a' AS sender);", {});
    REQUIRE(r.has_value());
    REQUIRE(r->has_value());
    CHECK(r->value().Get<std::string>("j").starts_with("["));
}

TEST_CASE("json_group_array over an empty set yields an empty array cell") {
    auto r = Database::Get().QueryOne(
      "SELECT COALESCE(json_group_array(x), '[]') AS j FROM (SELECT 1 AS x WHERE 0);", {});
    REQUIRE(r.has_value());
    REQUIRE(r->has_value());
    CHECK(r->value().Get<std::string>("j") == "[]");
}

TEST_CASE("json_object preserves integer types for ids and ranks") {
    auto r = Database::Get().QueryOne(
      "SELECT json_object('id', 42, 'rank', 1) AS j;", {});
    REQUIRE(r.has_value());
    REQUIRE(r->has_value());
    CHECK(r->value().Get<std::string>("j") == "{\"id\":42,\"rank\":1}");
}
