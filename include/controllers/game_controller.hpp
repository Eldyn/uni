#pragma once

#include <webserver.hpp>
#include <controllers/lobby_controller.hpp>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <functional>
#include <cstdint>

/**
 * @file game_controller.hpp
 * @brief Controller for handling in-game events and the flow of turns.
 * * Dispatches the WebSocket messages concerning game actions (playing cards,
 * drawing, declaring UNO) to the respective MatchInstance instances.
 */

/**
 * @class GameController
 * @brief Receives and processes player input during an active match.
 * * Works closely with the `LobbyController` to identify the match
 * associated with the user. This class is also responsible for the lifecycle of the
 * turn timers (libuv) to handle AFK players or bot takeover.
 * @tag CTRL-GAME-001
 */
class GameController {
public:
    /**
     * @brief Constructor of the game controller.
     * Registers the `game_*` WebSocket action handlers on the ActionRouter.
     * @param server The main WebServer instance.
     * @param lobby_controller Reference to the lobby controller to retrieve the state.
     * @tag CTRL-GAME-MTH-001
     */
    GameController(WebServer& server, LobbyController& lobby_controller);

private:
    ActionRouter& action_router_;       /**< Reference to the WebSocket router. */
    LobbyController& lobby_controller_; /**< Reference to access the in-memory lobbies. */

    /** * @brief Map associating a Lobby ID with the pointer of the current libuv timer.
     * Used to activate the AFK protection (e.g. the bot takes over or the move is skipped).
     */
    std::unordered_map<uint32_t, struct us_timer_t*> active_turn_timers_;

    // --- WebSocket Event Handlers ---

    /**
     * @brief Handles the request to play a card.
     * @param context Context of the calling socket.
     * @param message JSON payload containing the `card_id` field.
     * @tag CTRL-GAME-ACT-001
     */
    void HandlePlayCard(WsContext context, const nlohmann::json& message);

    /**
     * @brief Handles the request to draw a card from the deck.
     * @param context Context of the calling socket.
     * @param message JSON payload of the request.
     * @tag CTRL-GAME-ACT-002
     */
    void HandleDrawCard(WsContext context, const nlohmann::json& message);

    /**
     * @brief Handles the submission of input from the client for a pending effect (e.g. colour choice).
     * @param context Context of the calling socket.
     * @param message JSON payload containing the chosen data.
     * @tag CTRL-GAME-ACT-003
     */
    void HandleProvideInput(WsContext context, const nlohmann::json& message);

    /**
     * @brief Handles the "UNO" declaration by the player.
     * @param context Context of the calling socket.
     * @param message JSON payload of the request.
     * @tag CTRL-GAME-ACT-004
     */
    void HandleCallUno(WsContext context, const nlohmann::json& message);

    // --- Core Game Flow ---

    /**
     * @brief Callback/Hook triggered at the start of each new player turn.
     * Starts/resets the AFK timer based on the lobby settings.
     * @param active_lobby Pointer to the lobby whose turn has started.
     * @tag CTRL-GAME-FLW-001
     */
    void OnTurnStarted(Lobby* active_lobby);

    /**
     * @brief Sends the censored game state to all connected members.
     * Iterating over each user, it uses `MatchInstance::SerializePlayerState` to
     * hide opponents' hands and forwards the WS message.
     * @param current_lobby Pointer to the lobby to update.
     * @tag CTRL-GAME-FLW-002
     */
    void BroadcastGameState(Lobby* current_lobby);

    // --- Timer Management Helpers ---

    /**
     * @brief Creates or updates the turn timer for AFK protection.
     * @param lobby_id ID of the ongoing lobby.
     * @param timeout_ms Milliseconds before the timeout fires (e.g. 15000ms).
     * @param callback Function to execute when the timer expires (e.g. auto-play or bot).
     * @tag CTRL-GAME-TMR-001
     */
    void SetTurnTimer(uint32_t lobby_id, int timeout_ms, std::function<void()> callback);

    /**
     * @brief Stops and destroys the active timer for a given lobby.
     * Used when a player performs a valid action in time or the match ends.
     * @param lobby_id ID of the lobby whose timer to cancel.
     * @tag CTRL-GAME-TMR-002
     */
    void ClearTurnTimer(uint32_t lobby_id);
};
