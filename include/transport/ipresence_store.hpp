#pragma once
#include <string>
#include <vector>
#include <websocket_context.hpp>

/**
 * @class IPresenceStore
 * @brief Interface for connection presence lookups (who's online and on which socket).
 * Decouples controllers from the concrete PresenceRegistry to enable isolated unit tests.
 * @tag PRESENCE-IFACE-000
 */
class IPresenceStore {
public:
    virtual ~IPresenceStore() = default;

    /**
     * @brief Checks whether a user currently has an open WebSocket connection.
     * @param username The username to look up.
     * @return true if the user is online.
     * @tag PRESENCE-IFACE-001
     */
    virtual bool IsOnline(const std::string& username) const = 0;

    /**
     * @brief Retrieves the live socket for a connected user.
     * @param username The username to look up.
     * @return AppWebSocket* Pointer to the socket, or nullptr if offline.
     * @tag PRESENCE-IFACE-002
     */
    virtual AppWebSocket* GetSocket(const std::string& username) const = 0;

    /**
     * @brief Lists the usernames of every currently connected user.
     * @return std::vector<std::string> Snapshot of online usernames.
     * @tag PRESENCE-IFACE-003
     */
    virtual std::vector<std::string> OnlineUsernames() const = 0;
};
