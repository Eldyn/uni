#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <transport/ipresence_store.hpp>
#include <unordered_map>
#include <vector>
#include <websocket_context.hpp>

/**
 * @file presence_registry.hpp
 * @brief Definition of the connection registry tracking which users are online
 * and which lobby each online user currently belongs to.
 */

/**
 * @class PresenceRegistry
 * @brief Tracks live username -> socket mappings across the whole server.
 * Registered via `WebServer::OnConnectionOpen`/`OnConnectionClose`, same shape
 * as `LobbyController`'s existing hooks (additive, does not replace them).
 */
class PresenceRegistry : public IPresenceStore {
public:
    /**
     * @brief Handler called when a client establishes a new WebSocket connection.
     * @param ws Pointer to the WebSocket socket.
     * @param sd Data associated with the socket (contains username, set post-upgrade).
     */
    void OnOpen(AppWebSocket* ws, PerSocketData* sd);

    /**
     * @brief Handler called when a client closes the connection.
     * @param ws Pointer to the disconnected WebSocket socket.
     * @param sd Data associated with the socket.
     */
    void OnClose(AppWebSocket* ws, PerSocketData* sd);

    bool                     IsOnline(const std::string& username) const override;
    AppWebSocket*            GetSocket(const std::string& username) const override;
    std::vector<std::string> OnlineUsernames() const override;

    /**
     * @brief Number of currently connected players (unique usernames).
     * Cheap `sockets_.size()`; avoids materialising OnlineUsernames() for the
     * public `/stats/online` count.
     * @return std::size_t The live connection count.
     */
    std::size_t OnlineCount() const { return sockets_.size(); }

    /**
     * @brief Records which lobby a user currently belongs to.
     * Replaces `LobbyController::FindLobbyForUser`'s O(N) linear scan with an
     * O(1) lookup, kept up to date by `LobbyController` on join/leave/kick.
     * @param username The username to index.
     * @param lobby_id The lobby's internal numeric ID.
     */
    void SetUserLobby(const std::string& username, uint32_t lobby_id);

    /**
     * @brief Clears the lobby index entry for a user (on leave/kick/eviction).
     * @param username The username to clear.
     */
    void ClearUserLobby(const std::string& username);

    /**
     * @brief Looks up which lobby a user currently belongs to.
     * @param username The username to look up.
     * @return uint32_t The lobby's internal numeric ID, or 0 if not in any lobby.
     */
    uint32_t GetUserLobbyId(const std::string& username) const;

private:
    /**< Primary storage: username -> live socket pointer. */
    std::unordered_map<std::string, AppWebSocket*> sockets_;
    /**< Secondary index: username -> current lobby ID. */
    std::unordered_map<std::string, uint32_t> user_to_lobby_id_;
};
