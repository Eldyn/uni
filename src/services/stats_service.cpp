#include <services/stats_service.hpp>

// Process-wide user-stats cache (shared across all instances).
std::mutex StatsService::us_mu_;
std::unordered_map<std::string, StatsService::UserStatsCacheEntry> StatsService::us_cache_;
// Process-wide leaderboard cache (see header): one shared entry + SWR flag.
std::mutex StatsService::lb_mu_;
std::vector<LeaderboardEntry> StatsService::lb_cache_;
std::chrono::steady_clock::time_point StatsService::lb_expires_{};
bool StatsService::lb_have_ = false;
std::atomic<bool> StatsService::lb_refreshing_{false};
#include <nlohmann/json.hpp>
#include <algorithm>
#include <thread>

StatsService::StatsService(Database& db) : db_(db) {}

Result<UserStats> StatsService::GetUserStats(const std::string& username) {
    {
        std::lock_guard<std::mutex> lock(us_mu_);
        auto it = us_cache_.find(username);
        if (it != us_cache_.end() && std::chrono::steady_clock::now() < it->second.expires) {
            return it->second.stats;
        }
        if (it != us_cache_.end()) us_cache_.erase(it);
    }

    auto row_result = db_.QueryOne(R"(
        WITH RankedPlayers AS (
            SELECT ps.*,
                   RANK() OVER (
                       ORDER BY ps.total_wins DESC, ps.total_losses ASC) as rank
            FROM player_stats ps
            JOIN users u ON u.username = ps.username
        )
        SELECT * FROM RankedPlayers WHERE username = ?;
    )", {username});

    if (!row_result) {
        return std::unexpected(row_result.error());
    }

    if (!row_result->has_value()) {
        UserStats stats;
        stats.username = username;
        std::lock_guard<std::mutex> lock(us_mu_);
        us_cache_[username] = UserStatsCacheEntry{stats, std::chrono::steady_clock::now() + kUserStatsTtl};
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

    std::lock_guard<std::mutex> lock(us_mu_);
    us_cache_[username] = UserStatsCacheEntry{stats, std::chrono::steady_clock::now() + kUserStatsTtl};

    return stats;
}

Result<std::vector<LeaderboardEntry>> StatsService::GetLeaderboard() {
    // INFO: TTL cache + stale-while-revalidate. Warm hits return without
    // touching SQLite. After expiry the first caller serves the stale rows
    // and kicks one detached background refresh; latecomers keep getting
    // stale rows until it lands. The loop never blocks on the DENSE_RANK
    // window query under match-write contention. Cold start (no rows yet)
    // blocks exactly once.
    {
        std::lock_guard<std::mutex> lock(lb_mu_);
        const auto now = std::chrono::steady_clock::now();
        if (lb_have_ && now < lb_expires_) {
            return lb_cache_;
        }
        if (lb_have_ && !lb_refreshing_.exchange(true)) {
            std::vector<LeaderboardEntry> stale = lb_cache_;
            std::thread([] {
                Database& db = Database::Get();
                auto fresh = StatsService::FetchLeaderboardRows(db);
                {
                    std::lock_guard<std::mutex> lock(lb_mu_);
                    if (fresh) {
                        lb_cache_ = std::move(*fresh);
                        lb_expires_ = std::chrono::steady_clock::now() + kLeaderboardTtl;
                    }
                    lb_refreshing_ = false;
                }
            }).detach();
            return stale;
        }
        if (lb_have_) {
            return lb_cache_;
        }
    }

    auto fresh = FetchLeaderboardRows(db_);
    if (!fresh) return std::unexpected(fresh.error());

    {
        std::lock_guard<std::mutex> lock(lb_mu_);
        lb_cache_ = *fresh;
        lb_expires_ = std::chrono::steady_clock::now() + kLeaderboardTtl;
        lb_have_ = true;
    }

    return *fresh;
}

Result<std::vector<LeaderboardEntry>> StatsService::FetchLeaderboardRows(Database& db) {
    // INFO: One TEXT row: SQLite builds the whole top-50 JSON array, so the
    // loop parses a single cell instead of one DbRow map per entry.
    auto row_result = db.QueryOne(R"(
        SELECT COALESCE(json_group_array(json_object('username',username,'total_wins',total_wins,'total_losses',total_losses,'rank',rank)), '[]') AS j FROM (
        SELECT ps.username, ps.total_wins, ps.total_losses,
               DENSE_RANK() OVER (
                   ORDER BY ps.total_wins DESC, ps.total_losses ASC) as rank
        FROM player_stats ps
        JOIN users u ON u.username = ps.username
        ORDER BY rank ASC
        LIMIT 50);
    )");

    if (!row_result) {
        return std::unexpected(row_result.error());
    }
    if (!row_result->has_value()) {
        return std::vector<LeaderboardEntry>{};
    }

    nlohmann::json arr;
    try {
        arr = nlohmann::json::parse(row_result->value().GetOr<std::string>("j", "[]"));
    } catch (const std::exception& e) {
        return std::unexpected(Error::DatabaseFail(e.what()));
    }

    // The planner may flatten the ordered subquery, so re-establish rank
    // order on the loop (stable: ties keep SQLite's return order, matching
    // the old ORDER BY, which left ties unordered too).
    std::stable_sort(arr.begin(), arr.end(),
                     [](const nlohmann::json& a, const nlohmann::json& b) {
                         return a.at("rank").get<int>() < b.at("rank").get<int>();
                     });

    std::vector<LeaderboardEntry> entries;
    entries.reserve(arr.size());
    for (const auto& e : arr) {
        entries.push_back({
            e.at("username").get<std::string>(),
            e.at("total_wins").get<int>(),
            e.at("total_losses").get<int>(),
            e.at("rank").get<int>(),
        });
    }

    return entries;
}

void StatsService::InvalidateLeaderboardCache() {
    std::lock_guard<std::mutex> lock(lb_mu_);
    lb_have_ = false;
}

void StatsService::InvalidateUserStatsCache(const std::string& username) {
    std::lock_guard<std::mutex> lock(us_mu_);
    us_cache_.erase(username);
}

void StatsService::InvalidateUserStatsCache() {
    std::lock_guard<std::mutex> lock(us_mu_);
    us_cache_.clear();
}
