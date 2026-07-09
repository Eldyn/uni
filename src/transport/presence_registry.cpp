#include <transport/presence_registry.hpp>

void PresenceRegistry::OnOpen(AppWebSocket* ws, PerSocketData* sd) {
    sockets_[sd->username] = ws;
}

void PresenceRegistry::OnClose(AppWebSocket* ws, PerSocketData* sd) {
    auto it = sockets_.find(sd->username);
    if (it != sockets_.end() && it->second == ws) {
        sockets_.erase(it);
    }
}

bool PresenceRegistry::IsOnline(const std::string& username) const {
    return sockets_.contains(username);
}

AppWebSocket* PresenceRegistry::GetSocket(const std::string& username) const {
    auto it = sockets_.find(username);
    return it != sockets_.end() ? it->second : nullptr;
}

std::vector<std::string> PresenceRegistry::OnlineUsernames() const {
    std::vector<std::string> usernames;
    usernames.reserve(sockets_.size());
    for (const auto& [username, socket] : sockets_) {
        usernames.push_back(username);
    }
    return usernames;
}

void PresenceRegistry::SetUserLobby(const std::string& username, uint32_t lobby_id) {
    user_to_lobby_id_[username] = lobby_id;
}

void PresenceRegistry::ClearUserLobby(const std::string& username) {
    user_to_lobby_id_.erase(username);
}

uint32_t PresenceRegistry::GetUserLobbyId(const std::string& username) const {
    auto it = user_to_lobby_id_.find(username);
    return it != user_to_lobby_id_.end() ? it->second : 0;
}
