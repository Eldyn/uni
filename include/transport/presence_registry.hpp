#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <transport/ipresence_store.hpp>
#include <websocket_context.hpp>

/**
 * @file presence_registry.hpp
 * @brief Definition of the connection registry tracking which users are online
 * and, per perf audit M-1, which lobby each online user currently belongs to.
 */

/**
 * @class PresenceRegistry
 * @brief Tracks live username -> socket mappings across the whole server.
 * Registered via `WebServer::OnConnectionOpen`/`OnConnectionClose`, same shape
 * as `LobbyController`'s existing hooks (additive, does not replace them).
 * @tag PRESENCE-REG-000
 */
class PresenceRegistry : public IPresenceStore {
public:
    /**
     * @brief Handler called when a client establishes a new WebSocket connection.
     * @param ws Pointer to the WebSocket socket.
     * @param sd Data associated with the socket (contains username, set post-upgrade).
     * @tag PRESENCE-REG-001
     */
    void OnOpen(AppWebSocket* ws, PerSocketData* sd);

    /**
     * @brief Handler called when a client closes the connection.
     * @param ws Pointer to the disconnected WebSocket socket.
     * @param sd Data associated with the socket.
     * @tag PRESENCE-REG-002
     */
    void OnClose(AppWebSocket* ws, PerSocketData* sd);

    bool IsOnline(const std::string& username) const override;
    AppWebSocket* GetSocket(const std::string& username) const override;
    std::vector<std::string> OnlineUsernames() const override;

    /**
     * @brief Records which lobby a user currently belongs to (perf audit M-1).
     * Replaces `LobbyController::FindLobbyForUser`'s O(N) linear scan with an
     * O(1) lookup, kept up to date by `LobbyController` on join/leave/kick.
     * @param username The username to index.
     * @param lobby_id The lobby's internal numeric ID.
     * @tag PRESENCE-REG-003
     */
    void SetUserLobby(const std::string& username, uint32_t lobby_id);

    /**
     * @brief Clears the lobby index entry for a user (on leave/kick/eviction).
     * @param username The username to clear.
     * @tag PRESENCE-REG-004
     */
    void ClearUserLobby(const std::string& username);

    /**
     * @brief Looks up which lobby a user currently belongs to.
     * @param username The username to look up.
     * @return uint32_t The lobby's internal numeric ID, or 0 if not in any lobby.
     * @tag PRESENCE-REG-005
     */
    uint32_t GetUserLobbyId(const std::string& username) const;

private:
    /**< Primary storage: username -> live socket pointer. */
    std::unordered_map<std::string, AppWebSocket*> sockets_;
    /**< Secondary index: username -> current lobby ID (perf audit M-1). */
    std::unordered_map<std::string, uint32_t> user_to_lobby_id_;
};
