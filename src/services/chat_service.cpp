#include <services/chat_service.hpp>
#include <common/crypto.hpp>
#include <common/env.hpp>
#include <nlohmann/json.hpp>
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

// INFO: Binds a DM's ciphertext to its sender/recipient pair. The 0x1f unit
// separator is not valid in a username, so the concatenation is unambiguous.
std::string DmAad(const std::string& sender, const std::string& recipient) {
    return sender + "\x1f" + recipient;
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
    const auto now = RateLimiter::Clock::now();
    if (now - last_flood_evict_ >= std::chrono::seconds(60)) {
        last_flood_evict_ = now;
        flood_limiter_.Evict();
    }
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

bool ChatService::UserExists(const std::string& username) {
    auto rows = db_.Query("SELECT 1 FROM users WHERE username = ?;", {username});
    return rows && !rows->empty();
}

VoidResult ChatService::SendDirectMessage(const std::string& sender,
                                          const std::string& recipient,
                                          const std::string& plaintext) {
    const std::string aad = DmAad(sender, recipient);
    auto blob = Crypto::Encrypt(plaintext, dm_key_, aad);
    if (!blob) {
        return std::unexpected(blob.error());
    }

    // INFO: Insert + prune run as one closure wholly on the DB worker thread
    // (single queue hop, single BEGIN…COMMIT): no other task can interleave
    // between them, and the loop thread never touches sqlite3_*.
    auto fut = db_.RunInTxn(
        [sender, recipient, nonce = blob->nonce_b64, ciphertext = blob->ciphertext_b64,
         limit = static_cast<int>(kDmHistoryLimit)](Database& db) -> VoidResult {
            auto insert = db.Exec(
                "INSERT INTO chat_dms (sender, recipient, nonce, ciphertext, aad_version) "
                "VALUES (?, ?, ?, ?, 1);",
                {sender, recipient, nonce, ciphertext});
            if (!insert) {
                return std::unexpected(insert.error());
            }

            auto prune = db.Exec(
                "DELETE FROM chat_dms WHERE "
                "((sender = ? AND recipient = ?) OR (sender = ? AND recipient = ?)) AND id NOT IN ("
                "  SELECT id FROM chat_dms WHERE "
                "  ((sender = ? AND recipient = ?) OR (sender = ? AND recipient = ?)) "
                "  ORDER BY id DESC LIMIT ?"
                ");",
                {sender, recipient, recipient, sender,
                 sender, recipient, recipient, sender,
                 limit});
            if (!prune) {
                return std::unexpected(prune.error());
            }
            return {};
        });
    return fut.get();
}

Result<std::vector<ChatMessageEntry>> ChatService::GetDirectHistory(const std::string& user_a,
                                                                    const std::string& user_b) {
    auto rows = db_.Query(
        "SELECT id, sender, recipient, nonce, ciphertext, aad_version FROM chat_dms WHERE "
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

        const std::string sender_col = row.Get<std::string>("sender");
        const std::string recipient_col = row.Get<std::string>("recipient");
        const int aad_version = row.Get<int>("aad_version");

        std::string aad;
        if (aad_version >= 1) aad = DmAad(sender_col, recipient_col);

        // INFO: Version 0 rows predate AAD and were encrypted with no associated
        // data, so `aad` stays empty for them and decryption succeeds. Version 1
        // rows are bound to the *stored* sender/recipient: a relocated row fails
        // the tag check and is rejected.
        auto plaintext = Crypto::Decrypt(blob, dm_key_, aad);
        if (!plaintext) {
            return std::unexpected(plaintext.error());
        }
        history.push_back({row.Get<int>("id"), sender_col, *plaintext});
    }
    return history;
}

Result<ChatHistoryPage> ChatService::GetDirectHistoryPage(const std::string& user_a,
                                                           const std::string& user_b,
                                                           std::optional<int> before_id,
                                                           int raw_limit) {
    int limit = NormalizeShardLimit(raw_limit);
    // Client-facing entry point, never allow the unbounded (-1) mode here.
    if (limit < 0) limit = default_shard_size_;

    // INFO: The page is built inside SQLite as one JSON array (single TEXT
    // cell, no per-cell DbRow map). Decryption stays on the calling (loop)
    // thread after the worker returns — one alloc for the whole page.
    std::string sql =
        "SELECT COALESCE(json_group_array(json_object('id',id,'sender',sender,'recipient',recipient,"
        "'nonce',nonce,'ciphertext',ciphertext,'aad_version',aad_version)), '[]') AS j "
        "FROM (SELECT id,sender,recipient,nonce,ciphertext,aad_version FROM chat_dms WHERE "
        "((sender = ? AND recipient = ?) OR (sender = ? AND recipient = ?))";
    std::vector<DbValue> params = {user_a, user_b, user_b, user_a};
    if (before_id.has_value()) {
        sql += " AND id < ?";
        params.push_back(*before_id);
    }
    sql += " ORDER BY id DESC LIMIT ?);";
    params.push_back(limit + 1);

    auto row = db_.QueryOne(sql.c_str(), params);
    if (!row) {
        return std::unexpected(row.error());
    }
    // An aggregate without GROUP BY always yields exactly one row; the
    // COALESCE above guarantees valid JSON even for an empty page.
    std::string payload = "[]";
    if (row->has_value()) {
        payload = row->value().GetOr<std::string>("j", "[]");
    }
    nlohmann::json arr;
    try {
        arr = nlohmann::json::parse(payload);
    } catch (const std::exception& e) {
        return std::unexpected(Error::DatabaseFail(e.what()));
    }

    // json_group_array follows the subquery scan order in practice, but the
    // planner is free to flatten the ordered subquery — sort by id (unique)
    // on the loop so the window is always the newest `limit` rows.
    std::sort(arr.begin(), arr.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
        return a.at("id").get<int>() > b.at("id").get<int>();
    });

    bool has_more = arr.size() > static_cast<std::size_t>(limit);
    std::size_t take = has_more ? static_cast<std::size_t>(limit) : arr.size();

    std::vector<ChatMessageEntry> page;
    page.reserve(take);
    for (std::size_t i = 0; i < take; ++i) {
        const auto& e = arr[i];
        Crypto::EncryptedBlob blob{e.at("nonce").get<std::string>(),
                                   e.at("ciphertext").get<std::string>()};

        const std::string sender_col = e.at("sender").get<std::string>();
        const std::string recipient_col = e.at("recipient").get<std::string>();
        const int aad_version = e.at("aad_version").get<int>();

        std::string aad;
        if (aad_version >= 1) aad = DmAad(sender_col, recipient_col);

        auto plaintext = Crypto::Decrypt(blob, dm_key_, aad);
        if (!plaintext) {
            return std::unexpected(plaintext.error());
        }
        page.push_back({e.at("id").get<int>(), sender_col, *plaintext});
    }
    std::reverse(page.begin(), page.end());
    return ChatHistoryPage{page, has_more};
}
