#pragma once
#include <deque>
#include <string>
#include <vector>

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
    std::string username;
    std::string message;
};

/**
 * @class ChatService
 * @brief Owns the global chat ring buffer, capped at the most recent
 * `kGlobalHistoryLimit` messages.
 * @tag SVC-CHAT-CLS-001
 */
class ChatService {
public:
    static constexpr std::size_t kGlobalHistoryLimit = 200;

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

private:
    std::deque<ChatMessageEntry> global_history_;
};
