#pragma once
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>
#include <result.hpp>
#include <database.hpp>
#include <common/rate_limiter.hpp>

/**
 * @file chat_service.hpp
 * @brief Domain layer for chat messages, independent of the WebSocket wire layer.
 */

/**
 * @struct ChatMessageEntry
 * @brief A single stored chat message.
 * @tag SVC-CHAT-STR-001
 */
struct ChatMessageEntry {
    int         id;
    std::string username;
    std::string message;
};

/**
 * @struct ChatHistoryPage
 * @brief One shard of chat history, oldest message first, plus whether an
 * older shard still exists beyond it.
 * @tag SVC-CHAT-STR-002
 */
struct ChatHistoryPage {
    std::vector<ChatMessageEntry> messages;
    bool                          has_more;
};

/**
 * @class ChatService
 * @brief Owns the global chat ring buffer and encrypted DM history.
 * @tag SVC-CHAT-CLS-001
 */
class ChatService {
public:
    static constexpr std::size_t kGlobalHistoryLimit = 200;
    static constexpr std::size_t kDmHistoryLimit      = 200;
    /** Shard size used when a `chat_history_request` omits `limit`. */
    static constexpr int kDefaultShardSize = 20;
    /** Hard cap on `limit`, regardless of what a client requests. */
    static constexpr int kMaxShardSize = 100;

    /**
     * @param db             Database to read/write DM rows from.
     * @param flood_capacity Burst size for the per-user chat send bucket.
     * @param flood_rps      Sustained tokens/sec refill for the chat send bucket.
     * @tag SVC-CHAT-MTH-000
     */
    explicit ChatService(Database& db = Database::Get(), double flood_capacity = -1,
                        double flood_rps = -1);

    /**
     * @brief Appends a message to global chat, dropping the oldest entry once
     * the history exceeds `kGlobalHistoryLimit`.
     * @tag SVC-CHAT-MTH-001
     */
    void PostGlobalMessage(const std::string& username, const std::string& message);

    /**
     * @brief Returns the current global chat history, oldest message first.
     * @tag SVC-CHAT-MTH-002
     */
    const std::deque<ChatMessageEntry>& GetGlobalHistory() const;

    /**
     * @brief Returns one shard of global history, oldest-first within the
     * shard, newest shard first overall.
     * @param before_id Cursor — only messages older than this id are
     *   considered. `std::nullopt` starts from the most recent message.
     * @param limit Shard size. Values > 0 are clamped to `kMaxShardSize`;
     *   a negative value returns every matching message unclamped (trusted
     *   server-side callers only — never pass client input here directly).
     * @tag SVC-CHAT-MTH-002B
     */
    ChatHistoryPage GetGlobalHistoryPage(std::optional<int> before_id, int limit) const;

    /**
     * @brief Encrypts and persists a DM from `sender` to `recipient`, then
     * prunes the pair's history down to the most recent `kDmHistoryLimit`
     * messages.
     * @tag SVC-CHAT-MTH-003
     */
    VoidResult SendDirectMessage(const std::string& sender, const std::string& recipient,
                                 const std::string& plaintext);

    /**
     * @brief Returns the decrypted DM history between two users, oldest
     * message first.
     * @tag SVC-CHAT-MTH-004
     */
    Result<std::vector<ChatMessageEntry>> GetDirectHistory(const std::string& user_a,
                                                           const std::string& user_b);

    /**
     * @brief Returns one shard of the DM history between two users,
     * oldest-first within the shard, newest shard first overall.
     * @param before_id Cursor — only messages older than this id are
     *   considered. `std::nullopt` starts from the most recent message.
     * @param limit Requested shard size — always clamped to
     *   [1, kMaxShardSize] server-side, regardless of client input.
     * @tag SVC-CHAT-MTH-004B
     */
    Result<ChatHistoryPage> GetDirectHistoryPage(const std::string& user_a,
                                                 const std::string& user_b,
                                                 std::optional<int> before_id, int limit);

    /**
     * @brief Consumes one flood-control token for `username`.
     * @return false once the user's send bucket is empty, until it refills.
     * @tag SVC-CHAT-MTH-005
     */
    bool AllowSend(const std::string& username);

private:
    Database&            db_;
    std::vector<uint8_t> dm_key_;
    std::deque<ChatMessageEntry> global_history_;
    int                   next_global_id_ = 1;
    RateLimiter           flood_limiter_;
};
