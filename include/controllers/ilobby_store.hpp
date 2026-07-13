#pragma once
#include <common/lobby.hpp>
#include <cstdint>
#include <functional>
#include <string>

/**
 * @typedef MatchStartedCallback
 * @brief Callback invoked when a match starts.
 * @tag LOBBY-TYP-001
 */
using MatchStartedCallback = std::function<void(Lobby*)>;

/**
 * @typedef PlayerReplacedCallback
 * @brief Callback invoked when a player is replaced (e.g. by a bot).
 * @tag LOBBY-TYP-002
 */
using PlayerReplacedCallback = std::function<void(Lobby*)>;

/**
 * @typedef MatchAbortedCallback
 * @brief Callback invoked when a match is aborted due to disconnections (< 2 members remain).
 * @tag LOBBY-TYP-003
 */
using MatchAbortedCallback = std::function<void(Lobby*, const std::string& winner_username)>;

/**
 * @typedef LobbyDestroyedCallback
 * @brief Callback invoked once a lobby has no humans left and is torn down.
 * @tag LOBBY-TYP-004
 */
using LobbyDestroyedCallback = std::function<void(const std::string& lobby_code)>;

/**
 * @class ILobbyStore
 * @brief Interface for lobby lookup and match lifecycle hooks consumed by MatchController.
 * Decouples MatchController from the concrete LobbyController to enable isolated unit tests.
 * @tag LOBBY-IFACE-000
 */
class ILobbyStore {
public:
    virtual ~ILobbyStore() = default;

    /**
     * @brief Retrieves a pointer to a lobby by its internal numeric ID.
     * @param id Unique lobby ID.
     * @return Lobby* Pointer to the lobby, or nullptr if not found.
     * @tag LOBBY-IFACE-001
     */
    virtual Lobby* GetLobbyById(uint32_t id) = 0;

    /**
     * @brief Registers a callback to execute when a match starts.
     * @param cb The function to invoke.
     * @tag LOBBY-IFACE-002
     */
    virtual void OnGameStarted(MatchStartedCallback cb) = 0;

    /**
     * @brief Registers a callback to execute when a player is replaced.
     * @param cb The function to invoke.
     * @tag LOBBY-IFACE-003
     */
    virtual void OnPlayerReplaced(PlayerReplacedCallback cb) = 0;

    /**
     * @brief Registers a callback invoked when a match is aborted (< 2 members remain).
     * The callback is responsible for sending the kMatchOver wire frame to the surviving player.
     * @param cb The function to invoke.
     * @tag LOBBY-IFACE-004
     */
    virtual void OnMatchAborted(MatchAbortedCallback cb) = 0;

    /**
     * @brief Registers a callback invoked once a lobby has no humans left
     * and is about to be destroyed.
     * @param cb The function to invoke.
     * @tag LOBBY-IFACE-006
     */
    virtual void OnLobbyDestroyed(LobbyDestroyedCallback cb) = 0;

    /**
     * @brief Notifies the lobby layer that a match has ended normally.
     * Transfers teardown ownership to LobbyController (resets lobby.match).
     * @param lobby_id ID of the lobby whose match ended.
     * @tag LOBBY-IFACE-005
     */
    virtual void NotifyMatchOver(uint32_t lobby_id) = 0;
};
