#pragma once
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
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
 */
class StatsService {
public:
    /**
     * @param db Database to read player statistics from.
     */
    explicit StatsService(Database& db = Database::Get());

    /**
     * @brief Runs the ranking query and returns the top 50 players.
     * @return Result<std::vector<LeaderboardEntry>> The ranked rows, or an
     * Error on DB failure.
     */
    Result<std::vector<LeaderboardEntry>> GetLeaderboard();

    /**
     * @brief Looks up a single player's stats and leaderboard rank.
     * If the player has no `player_stats` row yet, returns a zeroed
     * `UserStats` with `rank == std::nullopt`.
     * @param username The player to look up.
     * @return Result<UserStats> The player's stats, or an Error on DB failure.
     */
    Result<UserStats> GetUserStats(const std::string& username);

    /**
     * @brief Drop the cached leaderboard rows, forcing the next
     * `GetLeaderboard` to query.
     *
     * The cache is per-instance with a 15 s TTL (backend-perf Task 9: the
     * k6-polled endpoint must not hit SQLite on every poll). Writes to
     * `player_stats` go through `LobbyController` directly, so a fresh
     * result can lag the table by up to the TTL; tests use this hook for
     * determinism.
     */
    void InvalidateLeaderboardCache();

private:
    Database& db_;
    /** Leaderboard TTL: poll bursts share one query result per window. */
    static constexpr std::chrono::seconds kLeaderboardTtl{15};
    std::mutex lb_mu_;
    std::vector<LeaderboardEntry> lb_cache_;
    std::chrono::steady_clock::time_point lb_expires_{};
    bool lb_have_ = false;
};
