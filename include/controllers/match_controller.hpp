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

    /**< Mersenne Twister RNG for bot delay jitter. */
    std::mt19937 rng_{std::random_device {}()};

    // --- WebSocket Event Handlers ---

    /**
     * @brief Handles the request to play a card.
     * @param context Context of the calling socket.
     * @param message JSON payload containing the `card_id` field.
     */
    void HandlePlayCard(WsContext context, const nlohmann::json& message);

    /**
     * @brief Handles the request to draw a card from the deck.
     * @param context Context of the calling socket.
     * @param message JSON payload of the request.
     */
    void HandleDrawCard(WsContext context, const nlohmann::json& message);

    /**
     * @brief Handles the owner's decision to keep a playable voluntary draw.
     * Clears the parked play/keep choice and advances the turn.
     * @param context Context of the calling socket.
     * @param message JSON payload of the request (unused).
     */
    void HandleKeepDrawn(WsContext context, const nlohmann::json& message);

    /**
     * @brief Handles the submission of input from the client for a pending effect (e.g. colour choice).
     * @param context Context of the calling socket.
     * @param message JSON payload containing the chosen data.
     */
    void HandleProvideInput(WsContext context, const nlohmann::json& message);

    /**
     * @brief Handles a reply to an open response window (pass or respond with a card).
     * @param context Context of the calling socket.
     * @param message JSON payload carrying `pass` and/or `card_id`.
     */
    void HandleWindowResponse(WsContext context, const nlohmann::json& message);

    /**
     * @brief Records which player a spectator is currently watching, so the
     * per-player spectator counts in the next match state are accurate.
     * No-op for anyone who isn't a spectator.
     * @param context Context of the calling socket.
     * @param message JSON payload containing the optional `viewed_username`.
     */
    void HandleSpectatorView(WsContext context, const nlohmann::json& message);

    /**
     * @brief Handles a seat's `match_client_ready` report (the client finished
     * loading), advancing the ready barrier.
     * @param context Context of the calling socket.
     * @param message JSON payload (unused).
     */
    void HandleClientReady(WsContext context, const nlohmann::json& message);

    // --- Core Match Flow ---

    /**
     * @brief Callback/Hook triggered at the start of each new player turn.
     * Drives the new-engine session turn (bot steps / AFK timers).
     * @param active_lobby Pointer to the lobby whose turn has started.
     */
    void OnTurnStarted(Lobby* active_lobby);

    /**
     * @brief New-engine turn driver.
     * Drives bots/disconnected seats through `BotStep` and arms the
     * AFK/prompt timer for connected humans.
     * @param active_lobby Pointer to the lobby whose session must advance.
     */
    void OnTurnStartedSession(Lobby* active_lobby);

    /**
     * @brief Sends the filtered match snapshot to all connected members and
     * forwards the terminal teardown once the engine is finished.
     * @param current_lobby Pointer to the lobby to update.
     */
    void BroadcastMatchState(Lobby* current_lobby);

    /**
     * @brief Opens the ready barrier when every pending seat has reported
     * loaded. No-op while any seated client is still loading.
     * @param lobby Target lobby whose session owns the barrier.
     */
    void TryOpenReadyBarrier(Lobby* lobby);

    /**
     * @brief Opens the ready barrier unconditionally (timeout / disconnect of
     * the last pending seat), arming the first turn.
     * @param lobby Target lobby whose session owns the barrier.
     */
    void OpenReadyBarrier(Lobby* lobby);

    /**< Maximum ms a loading match waits before opening the barrier. */
    static constexpr int kReadyBarrierTimeoutMs = 15000;

    // --- Timer Management Helpers ---

    /**
     * @brief Creates or updates the turn timer for AFK protection.
     * @param lobby_id ID of the ongoing lobby.
     * @param timeout_ms Milliseconds before the timeout fires.
     * @param callback Function to execute when the timer expires.
     */
    void SetTurnTimer(uint32_t lobby_id, int timeout_ms, std::function<void()> callback);

    /**
     * @brief Stops the active timer for a given lobby.
     * @param lobby_id ID of the lobby whose timer to cancel.
     */
    void ClearTurnTimer(uint32_t lobby_id);

    /**
     * @brief (Re)arm the window-timeout tick for `lobby`, or cancel it.
     *
     * When a response window is open, schedules a one-shot timer at its
     * remaining lifetime (clamped to `[100, settings.turn_time_limit_ms]`) that
     * calls `Tick` and re-evaluates; otherwise cancels any pending tick. This
     * is what closes an open window at its own deadline while no player input
     * arrives.
     * @param lobby Target lobby whose session window should be watched.
     */
    void ScheduleWindowTick(Lobby* lobby);

    /**
     * @brief Auto-keeps a held draw when its AFK human's turn times out.
     *
     * A timed match resolves a playable voluntary draw's play/keep hold
     * through this AFK takeover, not the engine's `Tick` expiry, so the
     * heuristic bot would otherwise play the held card. When the hold is
     * owned by `username` it is kept (the card stays in hand) and the turn
     * advances; any other turn is left to `BotStep`.
     * @param session Live match session.
     * @param username The AFK human whose turn clock elapsed.
     * @return true when a held draw owned by `username` was kept.
     */
    bool ResolveAfkHeldDraw(match::server::MatchSession& session,
                            const std::string& username);
};
