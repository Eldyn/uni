#include <controllers/stats_controller.hpp>
#include <controllers/auth_controller.hpp>
#include <database.hpp>
#include <common/http.hpp>
#include <nlohmann/json.hpp>
#include <logger.hpp>

using json = nlohmann::json;

StatsController::StatsController(HttpRouter& router) {
    router.Get("/stats/me", [this](AppResponse* res, AppRequest* req) {
        HandleGetMe(res, req);
    });

    router.Get("/stats/leaderboard", [this](AppResponse* res, AppRequest* req) {
        HandleGetLeaderboard(res, req);
    });
}

void StatsController::HandleGetMe(AppResponse* res, AppRequest* req) {
    std::string_view cookies = req->getHeader("cookie");
    // INFO: ws_token (SameSite=None) accepted for cross-origin embeds (e.g. itch.io)
    auto token = http::GetCookieValue(cookies, "ws_token");
    if (!token || token->empty()) token = http::GetCookieValue(cookies, "auth_token");

    if (!token) {
        res->writeStatus("401 Unauthorized")->end();
        return;
    }

    auto payload = AuthController::VerifyToken(*token);
    if (!payload) {
        res->writeStatus("401 Unauthorized")->end();
        return;
    }

    std::string username = payload->username;

    auto row_result = Database::Get().QueryOne(R"(
        WITH RankedPlayers AS (
            SELECT *,
                   RANK() OVER (ORDER BY total_wins DESC, total_losses ASC) as rank 
            FROM player_stats
        )
        SELECT * FROM RankedPlayers WHERE username = ?;
    )", {username});

    if (!row_result) {
        Logger::Error("[Stats] DB query failed: " + row_result.error().message);
        res->writeStatus("500 Internal Server Error")->end();
        return;
    }

    json response_data;
    if (row_result->has_value()) {
        const DbRow& row = row_result->value();
        response_data = {
            {"username", row.Get<std::string>("username")},
            {"total_wins", row.Get<int>("total_wins")},
            {"total_losses", row.Get<int>("total_losses")},
            {"rank", row.GetOr<int>("rank", 0)},
            
            {"cards_played_red", row.GetOr<int>("cards_played_red", 0)},
            {"cards_played_blue", row.GetOr<int>("cards_played_blue", 0)},
            {"cards_played_green", row.GetOr<int>("cards_played_green", 0)},
            {"cards_played_yellow", row.GetOr<int>("cards_played_yellow", 0)},
            {"cards_played_0", row.GetOr<int>("cards_played_0", 0)},
            {"cards_played_1", row.GetOr<int>("cards_played_1", 0)},
            {"cards_played_2", row.GetOr<int>("cards_played_2", 0)},
            {"cards_played_3", row.GetOr<int>("cards_played_3", 0)},
            {"cards_played_4", row.GetOr<int>("cards_played_4", 0)},
            {"cards_played_5", row.GetOr<int>("cards_played_5", 0)},
            {"cards_played_6", row.GetOr<int>("cards_played_6", 0)},
            {"cards_played_7", row.GetOr<int>("cards_played_7", 0)},
            {"cards_played_8", row.GetOr<int>("cards_played_8", 0)},
            {"cards_played_9", row.GetOr<int>("cards_played_9", 0)},
            {"cards_played_skip", row.GetOr<int>("cards_played_skip", 0)},
            {"cards_played_reverse", row.GetOr<int>("cards_played_reverse", 0)},
            {"cards_played_draw2", row.GetOr<int>("cards_played_draw2", 0)},
            {"cards_played_draw4", row.GetOr<int>("cards_played_draw4", 0)},
            {"cards_played_jolly", row.GetOr<int>("cards_played_jolly", 0)}
        };
    } else {
        response_data = {
            {"username", username},
            {"total_wins", 0},
            {"total_losses", 0},
            {"rank", nullptr}
        };
    }

    res->writeHeader("Content-Type", "application/json")
       ->end(response_data.dump());
}

void StatsController::HandleGetLeaderboard(AppResponse* res, AppRequest* req) {
    auto rows_result = Database::Get().Query(R"(
        SELECT username, total_wins, total_losses, 
               DENSE_RANK() OVER (ORDER BY total_wins DESC, total_losses ASC) as rank 
        FROM player_stats
        ORDER BY rank ASC
        LIMIT 50;
    )");

    if (!rows_result) {
        Logger::Error("[Stats] DB query failed: " + rows_result.error().message);
        res->writeStatus("500 Internal Server Error")->end();
        return;
    }

    json leaderboard = json::array();
    for (const auto& row : rows_result.value()) {
        leaderboard.push_back({
            {"username", row.Get<std::string>("username")},
            {"total_wins", row.Get<int>("total_wins")},
            {"total_losses", row.Get<int>("total_losses")},
            {"rank", row.Get<int>("rank")}
        });
    }

    json response_data = { {"leaderboard", leaderboard} };

    res->writeHeader("Content-Type", "application/json")
       ->end(response_data.dump());
}
