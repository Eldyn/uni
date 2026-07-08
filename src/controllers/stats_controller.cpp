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

Result<UserStats> StatsController::GetUserStats(const std::string& username) {
    auto row_result = Database::Get().QueryOne(R"(
        WITH RankedPlayers AS (
            SELECT *,
                   RANK() OVER (ORDER BY total_wins DESC, total_losses ASC) as rank
            FROM player_stats
        )
        SELECT * FROM RankedPlayers WHERE username = ?;
    )", {username});

    if (!row_result) {
        return std::unexpected(row_result.error());
    }

    if (!row_result->has_value()) {
        UserStats stats;
        stats.username = username;
        return stats;
    }

    const DbRow& row = row_result->value();
    UserStats stats;
    stats.username     = row.Get<std::string>("username");
    stats.total_wins    = row.Get<int>("total_wins");
    stats.total_losses  = row.Get<int>("total_losses");
    stats.rank          = row.GetOr<int>("rank", 0);

    stats.cards_played_red     = row.GetOr<int>("cards_played_red", 0);
    stats.cards_played_blue    = row.GetOr<int>("cards_played_blue", 0);
    stats.cards_played_green   = row.GetOr<int>("cards_played_green", 0);
    stats.cards_played_yellow  = row.GetOr<int>("cards_played_yellow", 0);
    stats.cards_played_0       = row.GetOr<int>("cards_played_0", 0);
    stats.cards_played_1       = row.GetOr<int>("cards_played_1", 0);
    stats.cards_played_2       = row.GetOr<int>("cards_played_2", 0);
    stats.cards_played_3       = row.GetOr<int>("cards_played_3", 0);
    stats.cards_played_4       = row.GetOr<int>("cards_played_4", 0);
    stats.cards_played_5       = row.GetOr<int>("cards_played_5", 0);
    stats.cards_played_6       = row.GetOr<int>("cards_played_6", 0);
    stats.cards_played_7       = row.GetOr<int>("cards_played_7", 0);
    stats.cards_played_8       = row.GetOr<int>("cards_played_8", 0);
    stats.cards_played_9       = row.GetOr<int>("cards_played_9", 0);
    stats.cards_played_skip    = row.GetOr<int>("cards_played_skip", 0);
    stats.cards_played_reverse = row.GetOr<int>("cards_played_reverse", 0);
    stats.cards_played_draw2   = row.GetOr<int>("cards_played_draw2", 0);
    stats.cards_played_draw4   = row.GetOr<int>("cards_played_draw4", 0);
    stats.cards_played_jolly   = row.GetOr<int>("cards_played_jolly", 0);

    return stats;
}

Result<std::vector<LeaderboardEntry>> StatsController::GetLeaderboard() {
    auto rows_result = Database::Get().Query(R"(
        SELECT username, total_wins, total_losses,
               DENSE_RANK() OVER (ORDER BY total_wins DESC, total_losses ASC) as rank
        FROM player_stats
        ORDER BY rank ASC
        LIMIT 50;
    )");

    if (!rows_result) {
        return std::unexpected(rows_result.error());
    }

    std::vector<LeaderboardEntry> entries;
    entries.reserve(rows_result->size());
    for (const auto& row : rows_result.value()) {
        entries.push_back({
            row.Get<std::string>("username"),
            row.Get<int>("total_wins"),
            row.Get<int>("total_losses"),
            row.Get<int>("rank"),
        });
    }

    return entries;
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

    auto stats_result = GetUserStats(payload->username);
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
    auto entries_result = GetLeaderboard();
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
