#include <doctest/doctest.h>
#include <services/stats_service.hpp>
#include <database.hpp>

namespace {

// stats_test_* usernames avoid colliding with rows other test files insert.
void CleanupTestRows() {
    auto result = Database::Get().Exec(
        "DELETE FROM player_stats WHERE username LIKE 'stats_test_%';");
    REQUIRE(result.has_value());
}

void InsertStatsRow(const std::string& username, int wins, int losses) {
    auto result = Database::Get().Exec(
        "INSERT INTO player_stats (username, total_wins, total_losses) VALUES (?, ?, ?);",
        {username, wins, losses});
    REQUIRE(result.has_value());
}

struct StatsFixture {
    StatsFixture() {
        REQUIRE(Database::Get().RunMigrations().has_value());
        CleanupTestRows();
    }
    ~StatsFixture() { CleanupTestRows(); }
};

}  // namespace

TEST_SUITE("StatsController") {

TEST_CASE("GetLeaderboard: orders by wins desc, losses asc") {
    StatsFixture f;
    InsertStatsRow("stats_test_alice", 10, 2);
    InsertStatsRow("stats_test_bob", 20, 1);
    InsertStatsRow("stats_test_carol", 5, 0);

    auto result = StatsService().GetLeaderboard();
    REQUIRE(result.has_value());

    auto find = [&](const std::string& username) -> const LeaderboardEntry* {
        for (const auto& entry : *result) {
            if (entry.username == username) return &entry;
        }
        return nullptr;
    };

    const auto* bob = find("stats_test_bob");
    const auto* alice = find("stats_test_alice");
    const auto* carol = find("stats_test_carol");
    REQUIRE(bob != nullptr);
    REQUIRE(alice != nullptr);
    REQUIRE(carol != nullptr);

    CHECK(bob->rank < alice->rank);
    CHECK(alice->rank < carol->rank);
    CHECK(bob->total_wins == 20);
    CHECK(alice->total_losses == 2);
}

TEST_CASE("GetLeaderboard: DENSE_RANK does not skip on ties") {
    StatsFixture f;
    InsertStatsRow("stats_test_dan", 10, 0);
    InsertStatsRow("stats_test_erin", 10, 0);
    InsertStatsRow("stats_test_frank", 5, 0);

    auto result = StatsService().GetLeaderboard();
    REQUIRE(result.has_value());

    auto find = [&](const std::string& username) -> const LeaderboardEntry* {
        for (const auto& entry : *result) {
            if (entry.username == username) return &entry;
        }
        return nullptr;
    };

    const auto* dan = find("stats_test_dan");
    const auto* erin = find("stats_test_erin");
    const auto* frank = find("stats_test_frank");
    REQUIRE(dan != nullptr);
    REQUIRE(erin != nullptr);
    REQUIRE(frank != nullptr);

    CHECK(dan->rank == erin->rank);
    // DENSE_RANK does not skip: the tied pair takes rank N, the next distinct
    // value takes rank N + 1 (as opposed to RANK(), which would skip to N + 2).
    CHECK(frank->rank == dan->rank + 1);
}

TEST_CASE("GetUserStats: no row returns zeroed defaults with no rank") {
    StatsFixture f;

    auto result = StatsService().GetUserStats("stats_test_ghost");
    REQUIRE(result.has_value());
    CHECK(result->username == "stats_test_ghost");
    CHECK(result->total_wins == 0);
    CHECK(result->total_losses == 0);
    CHECK_FALSE(result->rank.has_value());
}

TEST_CASE("GetUserStats: existing row matches inserted values") {
    StatsFixture f;
    InsertStatsRow("stats_test_henry", 7, 3);

    auto result = StatsService().GetUserStats("stats_test_henry");
    REQUIRE(result.has_value());
    CHECK(result->username == "stats_test_henry");
    CHECK(result->total_wins == 7);
    CHECK(result->total_losses == 3);
    REQUIRE(result->rank.has_value());
    CHECK(*result->rank == 1);
}

}  // TEST_SUITE
