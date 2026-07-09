#pragma once
#include <optional>
#include <string>
#include <vector>
#include <result.hpp>
#include <database.hpp>

/**
 * @file stats_service.hpp
 * @brief Domain layer for player statistics and leaderboard ranking queries.
 */

/**
 * @struct LeaderboardEntry
 * @brief A single ranked row of the `/stats/leaderboard` result set.
 * @tag SVC-STATS-STR-001
 */
struct LeaderboardEntry {
    std::string username;
    int         total_wins   = 0;
    int         total_losses = 0;
    int         rank         = 0;
};

/**
 * @struct UserStats
 * @brief The per-player statistics returned by `/stats/me`.
 * `rank` is `std::nullopt` when the player has no `player_stats` row yet
 * (mirrors the pre-extraction "no-stats-row default" branch).
 * @tag SVC-STATS-STR-002
 */
struct UserStats {
    std::string        username;
    int                 total_wins   = 0;
    int                 total_losses = 0;
    std::optional<int>  rank;

    int cards_played_red     = 0;
    int cards_played_blue    = 0;
    int cards_played_green   = 0;
    int cards_played_yellow  = 0;
    int cards_played_0       = 0;
    int cards_played_1       = 0;
    int cards_played_2       = 0;
    int cards_played_3       = 0;
    int cards_played_4       = 0;
    int cards_played_5       = 0;
    int cards_played_6       = 0;
    int cards_played_7       = 0;
    int cards_played_8       = 0;
    int cards_played_9       = 0;
    int cards_played_skip    = 0;
    int cards_played_reverse = 0;
    int cards_played_draw2   = 0;
    int cards_played_draw4   = 0;
    int cards_played_jolly   = 0;
};

/**
 * @class StatsService
 * @brief Owns the ranking queries (DENSE_RANK/RANK window functions) and the
 * no-stats-row default, independent of the HTTP/wire layer.
 * @tag SVC-STATS-CLS-001
 */
class StatsService {
public:
    /**
     * @param db Database to read player statistics from.
     * @tag SVC-STATS-MTH-001
     */
    explicit StatsService(Database& db = Database::Get());

    /**
     * @brief Runs the ranking query and returns the top 50 players.
     * @return Result<std::vector<LeaderboardEntry>> The ranked rows, or an
     * Error on DB failure.
     * @tag SVC-STATS-MTH-002
     */
    Result<std::vector<LeaderboardEntry>> GetLeaderboard();

    /**
     * @brief Looks up a single player's stats and leaderboard rank.
     * If the player has no `player_stats` row yet, returns a zeroed
     * `UserStats` with `rank == std::nullopt`.
     * @param username The player to look up.
     * @return Result<UserStats> The player's stats, or an Error on DB failure.
     * @tag SVC-STATS-MTH-003
     */
    Result<UserStats> GetUserStats(const std::string& username);

private:
    Database& db_;
};
