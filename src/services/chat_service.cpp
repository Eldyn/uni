#include <services/chat_service.hpp>
#include <common/crypto.hpp>
#include <common/env.hpp>
#include <algorithm>

namespace {
double ResolveFloodCapacity(double flood_capacity) {
    return flood_capacity >= 0 ? flood_capacity : std::stod(Env::Get("RATE_CHAT_BURST", "5"));
}

double ResolveFloodRps(double flood_rps) {
    return flood_rps >= 0 ? flood_rps : std::stod(Env::Get("RATE_CHAT_RPS", "1"));
}

int ResolveDefaultShardSize(int default_shard_size) {
    return default_shard_size >= 0
               ? default_shard_size
               : Env::GetInt("CHAT_HISTORY_SHARD_SIZE", ChatService::kDefaultShardSizeFallback);
}
}  // namespace

int ChatService::NormalizeShardLimit(int limit) const {
    if (limit < 0) return limit;
    if (limit == 0) return default_shard_size_;
    return std::min(limit, kMaxShardSize);
}

ChatService::ChatService(Database& db, double flood_capacity, double flood_rps,
                         int default_shard_size)
    : db_(db), dm_key_(Crypto::LoadKey()),
      default_shard_size_(ResolveDefaultShardSize(default_shard_size)),
      flood_limiter_(ResolveFloodCapacity(flood_capacity), ResolveFloodRps(flood_rps)) {}

void ChatService::PostGlobalMessage(const std::string& username, const std::string& message) {
    global_history_.push_back({next_global_id_++, username, message});
    if (global_history_.size() > kGlobalHistoryLimit) {
        global_history_.pop_front();
    }
}

const std::deque<ChatMessageEntry>& ChatService::GetGlobalHistory() const {
    return global_history_;
}

ChatHistoryPage ChatService::GetGlobalHistoryPage(std::optional<int> before_id, int limit) const {
    limit = NormalizeShardLimit(limit);

    std::vector<ChatMessageEntry> window;
    for (auto it = global_history_.rbegin(); it != global_history_.rend(); ++it) {
        if (before_id.has_value() && it->id >= *before_id) continue;
        window.push_back(*it);
        if (limit >= 0 && window.size() > static_cast<std::size_t>(limit)) break;
    }

    bool has_more = limit >= 0 && window.size() > static_cast<std::size_t>(limit);
    if (has_more) window.pop_back();
    std::reverse(window.begin(), window.end());
    return {window, has_more};
}

bool ChatService::AllowSend(const std::string& username) {
    return flood_limiter_.Allow(username);
}

void ChatService::PostLobbyMessage(const std::string& lobby_code, const std::string& username,
                                   const std::string& message) {
    auto& history = lobby_history_[lobby_code];
    auto [it, _] = next_lobby_id_.try_emplace(lobby_code, 1);
    history.push_back({it->second++, username, message});
    if (history.size() > kLobbyHistoryLimit) {
        history.pop_front();
    }
}

ChatHistoryPage ChatService::GetLobbyHistoryPage(const std::string& lobby_code,
                                                 std::optional<int> before_id, int limit) const {
    limit = NormalizeShardLimit(limit);

    std::vector<ChatMessageEntry> window;
    auto it = lobby_history_.find(lobby_code);
    if (it != lobby_history_.end()) {
        for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
            if (before_id.has_value() && rit->id >= *before_id) continue;
            window.push_back(*rit);
            if (limit >= 0 && window.size() > static_cast<std::size_t>(limit)) break;
        }
    }

    bool has_more = limit >= 0 && window.size() > static_cast<std::size_t>(limit);
    if (has_more) window.pop_back();
    std::reverse(window.begin(), window.end());
    return {window, has_more};
}

void ChatService::ClearLobbyHistory(const std::string& lobby_code) {
    lobby_history_.erase(lobby_code);
    next_lobby_id_.erase(lobby_code);
}

VoidResult ChatService::SendDirectMessage(const std::string& sender,
                                          const std::string& recipient,
                                          const std::string& plaintext) {
    auto blob = Crypto::Encrypt(plaintext, dm_key_);
    if (!blob) {
        return std::unexpected(blob.error());
    }

    auto insert = db_.Exec(
        "INSERT INTO chat_dms (sender, recipient, nonce, ciphertext) VALUES (?, ?, ?, ?);",
        {sender, recipient, blob->nonce_b64, blob->ciphertext_b64});
    if (!insert) {
        return std::unexpected(insert.error());
    }

    auto prune = db_.Exec(
        "DELETE FROM chat_dms WHERE "
        "((sender = ? AND recipient = ?) OR (sender = ? AND recipient = ?)) AND id NOT IN ("
        "  SELECT id FROM chat_dms WHERE "
        "  ((sender = ? AND recipient = ?) OR (sender = ? AND recipient = ?)) "
        "  ORDER BY id DESC LIMIT ?"
        ");",
        {sender, recipient, recipient, sender,
         sender, recipient, recipient, sender,
         static_cast<int>(kDmHistoryLimit)});
    if (!prune) {
        return std::unexpected(prune.error());
    }
    return {};
}

Result<std::vector<ChatMessageEntry>> ChatService::GetDirectHistory(const std::string& user_a,
                                                                    const std::string& user_b) {
    auto rows = db_.Query(
        "SELECT id, sender, nonce, ciphertext FROM chat_dms WHERE "
        "(sender = ? AND recipient = ?) OR (sender = ? AND recipient = ?) "
        "ORDER BY id ASC;",
        {user_a, user_b, user_b, user_a});
    if (!rows) {
        return std::unexpected(rows.error());
    }

    std::vector<ChatMessageEntry> history;
    history.reserve(rows->size());
    for (const auto& row : rows.value()) {
        Crypto::EncryptedBlob blob{row.Get<std::string>("nonce"),
                                  row.Get<std::string>("ciphertext")};
        auto plaintext = Crypto::Decrypt(blob, dm_key_);
        if (!plaintext) {
            return std::unexpected(plaintext.error());
        }
        history.push_back({row.Get<int>("id"), row.Get<std::string>("sender"), *plaintext});
    }
    return history;
}

Result<ChatHistoryPage> ChatService::GetDirectHistoryPage(const std::string& user_a,
                                                           const std::string& user_b,
                                                           std::optional<int> before_id,
                                                           int raw_limit) {
    int limit = NormalizeShardLimit(raw_limit);
    // Client-facing entry point — never allow the unbounded (-1) mode here.
    if (limit < 0) limit = default_shard_size_;

    std::string sql =
        "SELECT id, sender, nonce, ciphertext FROM chat_dms WHERE "
        "((sender = ? AND recipient = ?) OR (sender = ? AND recipient = ?))";
    std::vector<DbValue> params = {user_a, user_b, user_b, user_a};
    if (before_id.has_value()) {
        sql += " AND id < ?";
        params.push_back(*before_id);
    }
    sql += " ORDER BY id DESC LIMIT ?;";
    params.push_back(limit + 1);

    auto rows = db_.Query(sql.c_str(), params);
    if (!rows) {
        return std::unexpected(rows.error());
    }

    bool has_more = rows->size() > static_cast<std::size_t>(limit);
    std::size_t take = has_more ? static_cast<std::size_t>(limit) : rows->size();

    std::vector<ChatMessageEntry> page;
    page.reserve(take);
    for (std::size_t i = 0; i < take; ++i) {
        const auto& row = rows->at(i);
        Crypto::EncryptedBlob blob{row.Get<std::string>("nonce"),
                                  row.Get<std::string>("ciphertext")};
        auto plaintext = Crypto::Decrypt(blob, dm_key_);
        if (!plaintext) {
            return std::unexpected(plaintext.error());
        }
        page.push_back({row.Get<int>("id"), row.Get<std::string>("sender"), *plaintext});
    }
    std::reverse(page.begin(), page.end());
    return ChatHistoryPage{page, has_more};
}
