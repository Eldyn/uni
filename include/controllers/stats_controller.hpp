#pragma once
#include <http_router.hpp>
#include <services/stats_service.hpp>

/**
 * @file stats_controller.hpp
 * @brief HTTP controller for managing user statistics and leaderboards.
 * * Translates between the wire protocol (HTTP requests/JSON responses)
 * and StatsService, which owns the ranking query logic.
 */

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

    StatsService stats_service_;
};
