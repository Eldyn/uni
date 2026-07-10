#include <services/chat_service.hpp>

void ChatService::PostGlobalMessage(const std::string& username, const std::string& message) {
    global_history_.push_back({username, message});
    if (global_history_.size() > kGlobalHistoryLimit) {
        global_history_.pop_front();
    }
}

const std::deque<ChatMessageEntry>& ChatService::GetGlobalHistory() const {
    return global_history_;
}
