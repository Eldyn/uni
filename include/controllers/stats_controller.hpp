#pragma once
#include <http_router.hpp>
#include <services/stats_service.hpp>
#include <cstddef>
#include <functional>

/**
 * @file stats_controller.hpp
 * @brief HTTP controller for managing user statistics and leaderboards.
 * * Translates between the wire protocol (HTTP requests/JSON responses)
 * and StatsService, which owns the ranking query logic.
 */

/**
 * @class StatsController
 * @brief Exposes the REST endpoints needed by the frontend for the Profile and Leaderboard panel.
 */
class StatsController {
public:
    /**
     * @brief Constructor of the StatsController.
     * Registers the HTTP routes (GET methods) on the provided HttpRouter.
     * @param router The central HTTP router of the application.
     */
    explicit StatsController(HttpRouter& router);

    /**
     * @brief Supplies the live online-player count for the public `/stats/online`
     * route. Wired by main() to PresenceRegistry::OnlineCount; when unset the
     * route reports 0 rather than erroring.
     * @param provider Callable returning the number of connected players.
     */
    void SetOnlineCountProvider(std::function<std::size_t()> provider) {
        online_count_provider_ = std::move(provider);
    }

private:
    /**
     * @brief Handler for the GET route `/stats/me`.
     * * Extracts the personal statistics and the leaderboard position (rank)
     * of the currently authenticated player (by evaluating the JWT in the header).
     * @param res Pointer to the HTTP response (uWS).
     * @param req Pointer to the HTTP request (uWS).
     */
    void HandleGetMe(AppResponse* res, AppRequest* req);

    /**
     * @brief Handler for the GET route `/stats/leaderboard`.
     * * Runs a query on the database to return the top 50 players
     * ordered by descending number of wins.
     * @param res Pointer to the HTTP response.
     * @param req Pointer to the HTTP request.
     */
    void HandleGetLeaderboard(AppResponse* res, AppRequest* req);

    /**
     * @brief Handler for the public GET route `/stats/online`.
     * Returns `{"online": <n>}`, an aggregate connected-player count only (no
     * usernames). Read by the blog's player-count pill.
     * @param res Pointer to the HTTP response.
     * @param req Pointer to the HTTP request (unused).
     */
    void HandleGetOnline(AppResponse* res, AppRequest* req);

    StatsService stats_service_;
    std::function<std::size_t()> online_count_provider_;
};
