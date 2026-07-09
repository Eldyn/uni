#include <services/stats_service.hpp>

StatsService::StatsService(Database& db) : db_(db) {}

Result<UserStats> StatsService::GetUserStats(const std::string& username) {
    auto row_result = db_.QueryOne(R"(
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

Result<std::vector<LeaderboardEntry>> StatsService::GetLeaderboard() {
    auto rows_result = db_.Query(R"(
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
