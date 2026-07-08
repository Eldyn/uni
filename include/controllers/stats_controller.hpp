#pragma once
#include <http_router.hpp>
#include <optional>
#include <string>
#include <vector>
#include <result.hpp>

/**
 * @file stats_controller.hpp
 * @brief HTTP controller for managing user statistics and leaderboards.
 * * Handles RESTful (GET) requests, interacting with the Database to compute
 * the global ranking (win rate, total matches, etc.).
 */

/**
 * @struct LeaderboardEntry
 * @brief A single ranked row of the `/stats/leaderboard` result set.
 * @tag CTRL-STATS-TYP-001
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
 * * `rank` is `std::nullopt` when the player has no `player_stats` row yet
 * (mirrors the pre-extraction "no-stats-row default" branch).
 * @tag CTRL-STATS-TYP-002
 */
struct UserStats {
    std::string       username;
    int                total_wins   = 0;
    int                total_losses = 0;
    std::optional<int> rank;

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
 * @class StatsController
 * @brief Exposes the REST endpoints needed by the frontend for the Profile and Leaderboard panel.
 * @tag CTRL-STATS-001
 */
class StatsController {
public:
    /**
     * @brief Constructor of the StatsController.
     * Registers the HTTP routes (GET methods) on the provided HttpRouter.
     * @param router The central HTTP router of the application.
     * @tag CTRL-STATS-MTH-001
     */
    explicit StatsController(HttpRouter& router);

    /**
     * @brief Runs the ranking query and returns the top 50 players.
     * Pure DB logic, no HTTP concerns — extracted for direct unit testing.
     * @return Result<std::vector<LeaderboardEntry>> The ranked rows, or an Error on DB failure.
     * @tag CTRL-STATS-MTH-002
     */
    static Result<std::vector<LeaderboardEntry>> GetLeaderboard();

    /**
     * @brief Looks up a single player's stats and leaderboard rank.
     * If the player has no `player_stats` row yet, returns a zeroed `UserStats`
     * with `rank == std::nullopt` (matches the pre-extraction default branch).
     * @param username The player to look up.
     * @return Result<UserStats> The player's stats, or an Error on DB failure.
     * @tag CTRL-STATS-MTH-003
     */
    static Result<UserStats> GetUserStats(const std::string& username);

private:
    /**
     * @brief Handler for the GET route `/stats/me`.
     * * Extracts the personal statistics and the leaderboard position (rank)
     * of the currently authenticated player (by evaluating the JWT in the header).
     * @param res Pointer to the HTTP response (uWS).
     * @param req Pointer to the HTTP request (uWS).
     * @tag CTRL-STATS-ACT-001
     */
    void HandleGetMe(AppResponse* res, AppRequest* req);

    /**
     * @brief Handler for the GET route `/stats/leaderboard`.
     * * Runs a query on the database to return the top 50 players
     * ordered by descending number of wins.
     * @param res Pointer to the HTTP response.
     * @param req Pointer to the HTTP request.
     * @tag CTRL-STATS-ACT-002
     */
    void HandleGetLeaderboard(AppResponse* res, AppRequest* req);
};
