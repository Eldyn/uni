#pragma once

#include <controllers/ilobby_store.hpp>
#include <transport/iaction_router.hpp>
#include <transport/ibroadcaster.hpp>
#include <transport/itimer_service.hpp>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <random>

/**
 * @file match_controller.hpp
 * @brief Controller for handling in-match events and the flow of turns.
 * * Dispatches the WebSocket messages concerning match actions (playing cards,
 * drawing, declaring UNO) to the respective MatchInstance instances.
 */

/**
 * @class MatchController
 * @brief Receives and processes player input during an active match.
 * * Works closely with the `ILobbyStore` to identify the match
 * associated with the user. This class is also responsible for the lifecycle of the
 * turn timers (via ITimerService) to handle AFK players or bot takeover.
 * @tag CTRL-GAME-001
 */
class MatchController {
public:
    /**
     * @brief Constructor of the match controller.
     * Registers the `match_*` WebSocket action handlers on the ActionRouter.
     * @param router    WebSocket action router (DI seam).
     * @param broadcast Transport layer for sends/publishes (DI seam).
     * @param timers    Timer service for turn timers (DI seam).
     * @param lobby_store Interface to the lobby store for lobby lookup and match lifecycle hooks.
     * @tag CTRL-GAME-MTH-001
     */
    MatchController(IActionRouter& router, IBroadcaster& broadcast,
                    ITimerService& timers, ILobbyStore& lobby_store);

private:
    IActionRouter&    action_router_;    /**< Reference to the WebSocket router. */
    IBroadcaster&     broadcaster_;      /**< Transport layer for all sends. */
    ITimerService&    timer_service_;    /**< Timer service for turn/bot timers. */
    ILobbyStore&      lobby_store_;       /**< Interface to access the in-memory lobbies. */

    /**< Minimum ms between consecutive bot turns in kPlayInstantly mode. */
    int bot_instant_delay_ms_;
    /**< Lower bound of the randomised "thinking" delay in kWaitUntilTurnEnd mode. */
    int bot_wait_min_ms_;
    int bot_wait_max_ms_;        /**< Upper bound (exclusive) of the randomised "thinking" delay. */
    /**< Safety cap on consecutive bot moves in a single kPlayInstantly burst. */
    int max_instant_bot_steps_;

    /**< Mersenne Twister RNG for bot delay jitter. */
    std::mt19937 rng_{std::random_device {}()};

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

    // --- Core Match Flow ---

    /**
     * @brief Callback/Hook triggered at the start of each new player turn.
     * Starts/resets the AFK timer based on the lobby settings.
     * @param active_lobby Pointer to the lobby whose turn has started.
     * @tag CTRL-GAME-FLW-001
     */
    void OnTurnStarted(Lobby* active_lobby);

    /**
     * @brief Sends the censored match state to all connected members.
     * Iterating over each user, it uses `MatchInstance::SerializePlayerState` to
     * hide opponents' hands and forwards the WS message.
     * @param current_lobby Pointer to the lobby to update.
     * @tag CTRL-GAME-FLW-002
     */
    void BroadcastMatchState(Lobby* current_lobby);

    // --- Timer Management Helpers ---

    /**
     * @brief Creates or updates the turn timer for AFK protection.
     * @param lobby_id ID of the ongoing lobby.
     * @param timeout_ms Milliseconds before the timeout fires.
     * @param callback Function to execute when the timer expires.
     * @tag CTRL-GAME-TMR-001
     */
    void SetTurnTimer(uint32_t lobby_id, int timeout_ms, std::function<void()> callback);

    /**
     * @brief Stops the active timer for a given lobby.
     * @param lobby_id ID of the lobby whose timer to cancel.
     * @tag CTRL-GAME-TMR-002
     */
    void ClearTurnTimer(uint32_t lobby_id);
};
