#include <controllers/stats_controller.hpp>
#include <services/auth_service.hpp>
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

    auto payload = AuthService::VerifyToken(*token);
    if (!payload) {
        res->writeStatus("401 Unauthorized")->end();
        return;
    }

    auto stats_result = stats_service_.GetUserStats(payload->username);
    if (!stats_result) {
        Logger::Error("[Stats] DB query failed: " + stats_result.error().message);
        res->writeStatus("500 Internal Server Error")->end();
        return;
    }

    const UserStats& stats = stats_result.value();
    json response_data;
    if (stats.rank.has_value()) {
        response_data = {
            {"username", stats.username},
            {"total_wins", stats.total_wins},
            {"total_losses", stats.total_losses},
            {"rank", *stats.rank},

            {"cards_played_red", stats.cards_played_red},
            {"cards_played_blue", stats.cards_played_blue},
            {"cards_played_green", stats.cards_played_green},
            {"cards_played_yellow", stats.cards_played_yellow},
            {"cards_played_0", stats.cards_played_0},
            {"cards_played_1", stats.cards_played_1},
            {"cards_played_2", stats.cards_played_2},
            {"cards_played_3", stats.cards_played_3},
            {"cards_played_4", stats.cards_played_4},
            {"cards_played_5", stats.cards_played_5},
            {"cards_played_6", stats.cards_played_6},
            {"cards_played_7", stats.cards_played_7},
            {"cards_played_8", stats.cards_played_8},
            {"cards_played_9", stats.cards_played_9},
            {"cards_played_skip", stats.cards_played_skip},
            {"cards_played_reverse", stats.cards_played_reverse},
            {"cards_played_draw2", stats.cards_played_draw2},
            {"cards_played_draw4", stats.cards_played_draw4},
            {"cards_played_jolly", stats.cards_played_jolly}
        };
    } else {
        response_data = {
            {"username", stats.username},
            {"total_wins", 0},
            {"total_losses", 0},
            {"rank", nullptr}
        };
    }

    res->writeHeader("Content-Type", "application/json")
       ->end(response_data.dump());
}

void StatsController::HandleGetLeaderboard(AppResponse* res, AppRequest* req) {
    auto entries_result = stats_service_.GetLeaderboard();
    if (!entries_result) {
        Logger::Error("[Stats] DB query failed: " + entries_result.error().message);
        res->writeStatus("500 Internal Server Error")->end();
        return;
    }

    json leaderboard = json::array();
    for (const auto& entry : entries_result.value()) {
        leaderboard.push_back({
            {"username", entry.username},
            {"total_wins", entry.total_wins},
            {"total_losses", entry.total_losses},
            {"rank", entry.rank}
        });
    }

    json response_data = { {"leaderboard", leaderboard} };

    res->writeHeader("Content-Type", "application/json")
       ->end(response_data.dump());
}
