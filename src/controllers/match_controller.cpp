/**
 * @file match_controller.cpp
 * @brief Implementation of the MatchController routing active transaction frames to running match engines.
 */

#include "controllers/match_controller.hpp"
#include "match/server/bot_policy.hpp"
#include "match/server/match_session.hpp"
#include "match/view/view_util.hpp"
#include "common/lobby.hpp"
#include "common/ws.hpp"
#include "common/payloads.hpp"
#include "common/env.hpp"
#include "logger.hpp"
#include <algorithm>
#include <optional>
#include <random>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

using match::server::MatchSession;

/**
 * INFO: a held window lasts 800 ms, so bots must decide inside it: they react
 *       on a short jitter that ends well before the hold does.
 */
constexpr int kBotHoldReactionMinMs = 150;
constexpr int kBotHoldReactionMaxMs = 600;

/**
 * @brief Resolve the players that must act right now, in priority order.
 *
 * A parked op-input prompt comes first (only its target may answer), then any
 * still-unanswered responder of an open response window, else the current-turn
 * player. Empty when nobody can act (match over / no current player).
 * @param session Live match session.
 * @return Usernames that must act, highest priority first.
 */
std::vector<std::string> PendingActors(const MatchSession& session) {
    std::vector<std::string> actors;
    const std::optional<json> pending = session.Engine().PendingInput();
    if (pending.has_value() && pending->is_object()) {
        const std::string target = match::view::ResolvePlayer(
            session.Engine(), pending->value("target", json()));
        if (!target.empty()) actors.push_back(target);
        return actors;
    }

    if (session.Engine().WindowOpen()) {
        const json window = session.Engine().ExportWindow();
        for (const json& responder :
             window.value("responders", json::array())) {
            if (!responder.is_string()) continue;
            const std::string name = responder.get<std::string>();
            bool responded = false;
            for (const json& response :
                 window.value("responses", json::array())) {
                if (response.value("player", std::string()) == name) {
                    responded = true;
                    break;
                }
            }
            if (!responded) actors.push_back(name);
        }
        return actors;
    }

    const std::string current = session.Engine().GetCurrentPlayerUsername();
    if (!current.empty()) actors.push_back(current);
    return actors;
}

/**
 * @brief True when `username` is bot-controlled or currently disconnected.
 *
 * Both cases are driven by the heuristic policy (no mid-game
 * engine removal, a disconnected seat is played by the bot).
 * @param lobby Target lobby holding the member bookkeeping.
 * @param username Member to classify.
 */
bool IsAutoActor(const Lobby& lobby, const std::string& username) {
    const LobbyMember* member = lobby.FindMember(username);
    if (member == nullptr) return false;
    return member->is_bot || !member->is_connected;
}

/**
 * @brief Milliseconds left on the clock the pending human actor races.
 *
 * A pending prompt uses its own deadline and a plain turn uses the turn
 * deadline; a window (or an unarmed clock) falls back to the full limit.
 * @param session Live match session.
 * @param time_limit_ms Lobby turn time limit.
 */
int64_t ActorTimeLeftMs(const MatchSession& session, int64_t time_limit_ms) {
    const match::engine::MatchInstance& engine = session.Engine();
    int64_t deadline_ms = 0;
    if (const std::optional<json> pending = engine.PendingInput()) {
        deadline_ms = pending->value("deadline_ms", int64_t{0});
    } else if (!engine.WindowOpen()) {
        deadline_ms = engine.CurrentTurnDeadlineMs();
    }
    if (deadline_ms == 0) return time_limit_ms;
    const int64_t now_ms = engine.Timers().Turn().Now();
    return std::clamp<int64_t>(deadline_ms - now_ms, 0, time_limit_ms);
}

/**
 * @brief Stable per-lobby, per-player bot seed (replay-deterministic).
 * @param lobby_id Numeric lobby id.
 * @param username Seat the policy decides for.
 */
uint64_t BotSeed(uint32_t lobby_id, const std::string& username) {
    uint64_t hash = 1469598103934665603ull;
    for (char c : username) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ull;
    }
    hash ^= static_cast<uint64_t>(lobby_id);
    return hash;
}

}  // namespace

/**
 * @brief Constructs the MatchController and binds the action router triggers to type-safe map strings.
 * @param router Reference to the hosting asynchronous web server infrastructure.
 * @param lobby_controller Reference to the central room lifecycle management engine.
 */
MatchController::MatchController(IActionRouter& router, IBroadcaster& broadcast,
                                 ITimerService& timers, ILobbyStore& lobby_store)
    : action_router_(router), broadcaster_(broadcast),
      timer_service_(timers), lobby_store_(lobby_store) {
    bot_instant_delay_ms_  = std::max(0, Env::GetInt("BOT_TURN_DELAY_MS", 1000));
    bot_wait_min_ms_       = std::max(0, Env::GetInt("BOT_WAIT_MIN_MS", 500));
    bot_wait_max_ms_       = std::max(bot_wait_min_ms_ + 1, Env::GetInt("BOT_WAIT_MAX_MS", 3500));
    Logger::Info("[Match] Bot instant delay: ", bot_instant_delay_ms_, "ms, wait spread: ",
                 bot_wait_min_ms_, "-", bot_wait_max_ms_, "ms");

    action_router_.On(ws::ClientAction::kMatchPlayCard,
                      [this](WsContext context, const json& message) {
        HandlePlayCard(context, message);
        return true;
    });

    action_router_.On(ws::ClientAction::kMatchDrawCard,
                      [this](WsContext context, const json& message) {
        HandleDrawCard(context, message);
        return true;
    });

    action_router_.On(ws::ClientAction::kMatchKeepDrawn,
                      [this](WsContext context, const json& message) {
        HandleKeepDrawn(context, message);
        return true;
    });

    action_router_.On(ws::ClientAction::kMatchSubmitInput,
                      [this](WsContext context, const json& message) {
        HandleProvideInput(context, message);
        return true;
    });

    // INFO: the prompt-response channel carries the same
    //       `{prompt_id, value}` pair as `match_submit_input`; both route
    //       through HandleProvideInput so the session sees one prompt path.
    action_router_.On(ws::ClientAction::kMatchPromptResponse,
                      [this](WsContext context, const json& message) {
        HandleProvideInput(context, message);
        return true;
    });

    // INFO: The Pass button's wire route. A window response is
    //       either a pass (`pass: true`) or a card (`card_id`), both resolved
    //       by the session's window methods.
    action_router_.On(ws::ClientAction::kMatchWindowResponse,
                      [this](WsContext context, const json& message) {
        HandleWindowResponse(context, message);
        return true;
    });

    // INFO: spectator_view carries which player a spectator is watching; the
    //       lobby-side member records it so the next snapshot's per-player
    //       spectator counts are accurate.
    action_router_.On(ws::ClientAction::kSpectatorView,
                      [this](WsContext context, const json& message) {
        HandleSpectatorView(context, message);
        return true;
    });

    action_router_.On(ws::ClientAction::kMatchClientReady,
                      [this](WsContext context, const json& message) {
        HandleClientReady(context, message);
        return true;
    });

    lobby_store.OnGameStarted([this](Lobby* active_lobby) {
        if (active_lobby == nullptr || !active_lobby->session) return;
        // INFO: `defs` + `match_start` go out once, before any
        //       engine event; the turn driver then waits for every seated
        //       client to report loaded.
        active_lobby->session->EmitMatchStart(broadcaster_);
        active_lobby->session->BeginReadyBarrier(broadcaster_,
                                                    kReadyBarrierTimeoutMs);
        const uint32_t lobby_id = active_lobby->id;
        timer_service_.Schedule("ready_" + std::to_string(lobby_id),
                                kReadyBarrierTimeoutMs, false, [this, lobby_id]() {
            Lobby* current = lobby_store_.GetLobbyById(lobby_id);
            if (current != nullptr) OpenReadyBarrier(current);
        });
        TryOpenReadyBarrier(active_lobby);
    });

    lobby_store.OnMatchSeatDisconnected([this](Lobby* active_lobby) {
        TryOpenReadyBarrier(active_lobby);
    });

    lobby_store.OnPlayerReplaced([this](Lobby* active_lobby) {
        OnTurnStarted(active_lobby);
        BroadcastMatchState(active_lobby);
    });

    lobby_store.OnMatchAborted([this](Lobby* lobby, const std::string& winner) {
        if (lobby->members.empty()) return;
        const auto& member = lobby->members.front();
        if (!member.is_connected || !member.socket) return;
        json payload = ws::MakeResponse(ws::ServerAction::kMatchOver);
        payload["winner"] = winner;
        broadcaster_.Send(member.socket, payload.dump(), uWS::OpCode::TEXT);
    });

    Logger::Info("[Match] MatchController registered");
}

/**
 * @brief Intercepts explicit match transaction inputs, routing candidate card identifiers to the engine.
 * @param context Signaling packet metadata tracking incoming user sockets.
 * @param message JSON input structure carrying data properties.
 */
void MatchController::HandlePlayCard(WsContext context, const json& message) {
    Lobby* active_lobby = lobby_store_.GetLobbyById(context.socket_data->lobby_id);
    if (!active_lobby) {
        return;
    }

    std::string request_identifier = ws::GetOr<std::string>(message, "request_id", "");

    LobbyMember* member = active_lobby->FindMember(context.socket_data->username);
    if (member && member->is_spectator) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kSpectatorCannotAct, request_identifier);
        return;
    }

    // INFO: new-engine path. `card_id` carries the wire
    //       `CompactCardV2.bits`; the session resolves it to an entity.
    if (!active_lobby->session) {
        return;
    }

    if (!active_lobby->session->ReadyBarrierOpen()) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kCannotDraw,
                               request_identifier);
        return;
    }

    auto payload_res = ws::ParsePayload<ws::GamePlayCardPayload>(message);
    if (!payload_res) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kInvalidPayload,
                               request_identifier,
                               payload_res.error().message);
        return;
    }
    uint32_t card_bits = static_cast<uint32_t>(payload_res->card_id);

    if (!active_lobby->session->PlayCard(context.socket_data->username,
                                         card_bits)) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kInvalidMove,
                               request_identifier);
        return;
    }

    active_lobby->session->EmitEvents(broadcaster_);
    // INFO: Route the post-input broadcast through
    //       BroadcastMatchState so a human action that ends the match also
    //       notifies the lobby store (teardown + rematch allowed).
    // INFO: arm the incoming turn before the snapshot so `turn_deadline_ms`
    //       carries the fresh value (the advance emits it unarmed as 0).
    ClearTurnTimer(active_lobby->id);
    OnTurnStarted(active_lobby);
    BroadcastMatchState(active_lobby);
    ScheduleWindowTick(active_lobby);
}

/**
 * @brief Signals the running match instance engine to allocate a fresh card to the requesting user.
 * @param context Signaling packet metadata tracking incoming user sockets.
 * @param message JSON input structure containing callback identifiers.
 */
void MatchController::HandleDrawCard(WsContext context, const json& message) {
    Lobby* active_lobby = lobby_store_.GetLobbyById(context.socket_data->lobby_id);
    if (!active_lobby) {
        return;
    }

    std::string request_identifier = ws::GetOr<std::string>(message, "request_id", "");

    LobbyMember* member = active_lobby->FindMember(context.socket_data->username);
    if (member && member->is_spectator) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kSpectatorCannotAct, request_identifier);
        return;
    }

    // INFO: new-engine path.
    if (!active_lobby->session) {
        return;
    }

    if (!active_lobby->session->ReadyBarrierOpen()) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kCannotDraw,
                               request_identifier);
        return;
    }

    if (!active_lobby->session->DrawCard(context.socket_data->username)) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kCannotDraw,
                               request_identifier);
        return;
    }

    active_lobby->session->EmitEvents(broadcaster_);
    // INFO: arm the incoming turn before the snapshot so `turn_deadline_ms`
    //       carries the fresh value (the advance emits it unarmed as 0).
    ClearTurnTimer(active_lobby->id);
    OnTurnStarted(active_lobby);
    BroadcastMatchState(active_lobby);
    ScheduleWindowTick(active_lobby);
}

/**
 * @brief Resolves the owner's decision to keep a playable voluntary draw.
 *
 * Mirrors `HandleDrawCard`'s spectator and ready-barrier guards, then forwards
 * to the session so the parked play/keep choice clears and the turn advances.
 * @param context Signaling packet metadata tracking incoming user sockets.
 * @param message JSON input structure containing callback identifiers.
 */
void MatchController::HandleKeepDrawn(WsContext context, const json& message) {
    Lobby* active_lobby = lobby_store_.GetLobbyById(context.socket_data->lobby_id);
    if (!active_lobby) {
        return;
    }

    std::string request_identifier = ws::GetOr<std::string>(message, "request_id", "");

    LobbyMember* member = active_lobby->FindMember(context.socket_data->username);
    if (member && member->is_spectator) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kSpectatorCannotAct, request_identifier);
        return;
    }

    // INFO: new-engine path.
    if (!active_lobby->session) {
        return;
    }

    if (!active_lobby->session->ReadyBarrierOpen()) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kCannotDraw,
                               request_identifier);
        return;
    }

    if (!active_lobby->session->KeepDrawn(context.socket_data->username)) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kInvalidMove,
                               request_identifier);
        return;
    }

    active_lobby->session->EmitEvents(broadcaster_);
    // INFO: arm the incoming turn before the snapshot so `turn_deadline_ms`
    //       carries the fresh value (the advance emits it unarmed as 0).
    ClearTurnTimer(active_lobby->id);
    OnTurnStarted(active_lobby);
    BroadcastMatchState(active_lobby);
    ScheduleWindowTick(active_lobby);
}

/**
 * @brief Forwards localized resolution responses (like color selections) down into pending effect queues.
 * @param context Signaling packet metadata tracking incoming user sockets.
 * @param message Input document carrying state selections.
 */
void MatchController::HandleProvideInput(WsContext context, const json& message) {
    Lobby* active_lobby = lobby_store_.GetLobbyById(context.socket_data->lobby_id);
    if (!active_lobby) return;

    std::string request_identifier = ws::GetOr<std::string>(message, "request_id", "");

    LobbyMember* member = active_lobby->FindMember(context.socket_data->username);
    if (member && member->is_spectator) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kSpectatorCannotAct, request_identifier);
        return;
    }

    // INFO: new-engine path. The prompt channel carries
    //       `{prompt_id, value}` and the value is read RAW: the generated
    //       MatchPromptResponsePayload::Value drops the JSON body.
    if (!active_lobby->session) return;

    if (!active_lobby->session->ReadyBarrierOpen()) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kCannotDraw,
                               request_identifier);
        return;
    }

    auto prompt_id = ws::Get<std::string>(message, "prompt_id");
    const auto value_it = message.find("value");
    if (!prompt_id || value_it == message.end()) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kInvalidPayload,
                               request_identifier);
        return;
    }

    if (!active_lobby->session->SubmitInput(context.socket_data->username,
                                            *prompt_id, *value_it)) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kInvalidMove,
                               request_identifier);
        return;
    }

    active_lobby->session->EmitEvents(broadcaster_);
    // INFO: arm the incoming turn before the snapshot so `turn_deadline_ms`
    //       carries the fresh value (the advance emits it unarmed as 0).
    ClearTurnTimer(active_lobby->id);
    OnTurnStarted(active_lobby);
    BroadcastMatchState(active_lobby);
    ScheduleWindowTick(active_lobby);
}

/**
 * @brief Routes an open response-window reply to the session.
 *
 * `pass: true` declines the window; otherwise `card_id` (CompactCardV2.bits)
 * plays a response card. A message carrying neither is an invalid payload.
 * @param context Signaling packet metadata tracking incoming user sockets.
 * @param message Input document carrying `pass` and/or `card_id`.
 */
void MatchController::HandleWindowResponse(WsContext context, const json& message) {
    Lobby* active_lobby = lobby_store_.GetLobbyById(context.socket_data->lobby_id);
    if (!active_lobby) return;

    std::string request_identifier = ws::GetOr<std::string>(message, "request_id", "");

    LobbyMember* member = active_lobby->FindMember(context.socket_data->username);
    if (member && member->is_spectator) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kSpectatorCannotAct, request_identifier);
        return;
    }

    if (!active_lobby->session) return;

    if (!active_lobby->session->ReadyBarrierOpen()) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kCannotDraw,
                               request_identifier);
        return;
    }

    auto payload_res = ws::ParsePayload<ws::MatchWindowResponsePayload>(message);
    if (!payload_res) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kInvalidPayload,
                               request_identifier,
                               payload_res.error().message);
        return;
    }

    const std::string& username = context.socket_data->username;
    bool accepted = false;
    if (payload_res->pass.value_or(false)) {
        accepted = active_lobby->session->PassWindow(username);
    } else if (payload_res->card_id.has_value()) {
        accepted = active_lobby->session->RespondWindow(
            username, static_cast<uint32_t>(*payload_res->card_id));
    } else {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kInvalidPayload,
                               request_identifier);
        return;
    }

    if (!accepted) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kInvalidMove,
                               request_identifier);
        return;
    }

    active_lobby->session->EmitEvents(broadcaster_);
    // INFO: arm the incoming turn before the snapshot so `turn_deadline_ms`
    //       carries the fresh value (the advance emits it unarmed as 0).
    ClearTurnTimer(active_lobby->id);
    OnTurnStarted(active_lobby);
    BroadcastMatchState(active_lobby);
    ScheduleWindowTick(active_lobby);
}

/**
 * @brief Records which player a spectator is watching, for the per-player
 * spectator counts in the match state.
 * @param context Caller's socket/session context.
 * @param message Incoming spectator_view payload.
 */
void MatchController::HandleSpectatorView(WsContext context, const json& message) {
    Lobby* active_lobby = lobby_store_.GetLobbyById(context.socket_data->lobby_id);
    if (!active_lobby || !active_lobby->session) return;

    LobbyMember* member = active_lobby->FindMember(context.socket_data->username);
    // Only spectators have a viewed player; a seated player's own POV is
    // implicit and must not be overwritten by a stray message.
    if (!member || !member->is_spectator) return;

    auto payload_res = ws::ParsePayload<ws::SpectatorViewPayload>(message);
    if (!payload_res) return;

    std::string viewed = payload_res->viewed_username.value_or("");
    // Ignore a target that isn't actually in this match, so a stale or
    // malicious username can't create a phantom count or hide the spectator
    // from every real seat.
    if (!viewed.empty() && !active_lobby->session->Engine().FindPlayer(viewed)) {
        viewed.clear();
    }

    member->viewed_username = viewed;
    BroadcastMatchState(active_lobby);
}

/**
 * @brief Broadcasts each connected recipient their filtered match snapshot.
 * @param current_lobby Target room pointer whose context needs to be synchronized.
 */
void MatchController::BroadcastMatchState(Lobby* current_lobby) {
    if (!current_lobby || !current_lobby->session) return;

    // Per-player spectator counts: each viewer sees how many people are
    // watching a given player's POV, not the whole lobby's spectator total. A
    // spectator with no explicit POV choice is attributed to whoever's turn it
    // is, matching the client's default "follow the current turn" view.
    match::view::SnapshotOptions options;
    const std::string current_turn =
        current_lobby->session->Engine().GetCurrentPlayerUsername();
    for (const auto& member : current_lobby->members) {
        if (!member.is_spectator || !member.is_connected) continue;
        options.spectator_count++;
        const std::string target =
            member.viewed_username.empty() ? current_turn : member.viewed_username;
        options.spectator_counts[target]++;
    }

    current_lobby->session->BroadcastSnapshot(broadcaster_, options);
    if (current_lobby->session->Engine().IsMatchOver()) {
        lobby_store_.NotifyMatchOver(current_lobby->id);
    }
}

/**
 * @brief Sets turn constraints, routing automatic simulation threads on bot turns or player timeouts.
 * @param active_lobby Target active match room layout evaluated.
 */
void MatchController::OnTurnStarted(Lobby* active_lobby) {
    if (!active_lobby || !active_lobby->session) return;
    OnTurnStartedSession(active_lobby);
}

/**
 * @brief Handles a seat's `match_client_ready` report and advances the barrier.
 * @param context Signaling packet metadata tracking incoming user sockets.
 * @param message JSON input structure (unused).
 */
void MatchController::HandleClientReady(WsContext context, const json&) {
    Lobby* active_lobby = lobby_store_.GetLobbyById(context.socket_data->lobby_id);
    if (active_lobby == nullptr || !active_lobby->session) return;
    active_lobby->session->MarkSeatReady(context.socket_data->username, broadcaster_);
    TryOpenReadyBarrier(active_lobby);
}

/**
 * @brief Opens the ready barrier when every pending seat has reported loaded.
 * @param lobby Target lobby whose session owns the barrier.
 */
void MatchController::TryOpenReadyBarrier(Lobby* lobby) {
    if (lobby == nullptr || !lobby->session) return;
    if (!lobby->session->ReadyBarrierComplete()) return;
    OpenReadyBarrier(lobby);
}

/**
 * @brief Opens the ready barrier unconditionally and arms the first turn.
 * @param lobby Target lobby whose session owns the barrier.
 */
void MatchController::OpenReadyBarrier(Lobby* lobby) {
    if (lobby == nullptr || !lobby->session) return;
    timer_service_.Cancel("ready_" + std::to_string(lobby->id));
    if (!lobby->session->OpenReadyBarrier(broadcaster_)) return;
    OnTurnStarted(lobby);
    BroadcastMatchState(lobby);
}

bool MatchController::ResolveAfkHeldDraw(
    match::server::MatchSession& session, const std::string& username) {
    const std::optional<match::engine::PendingPlayDrawn>& hold =
        session.Engine().PendingPlayDrawnState();
    if (!hold.has_value()) return false;
    const auto player = session.Engine().FindPlayer(username);
    if (!player.has_value() || !(hold->player == *player)) return false;
    return session.KeepDrawn(username);
}

/**
 * @brief New-engine turn driver.
 *
 * Replaces the legacy timeout policy with the seam: any bot or
 * disconnected seat that must act is driven one `BotStep` at a time behind a
 * short bot-thinking timer, while a connected human keeps the full
 * turn-time-limit timer for AFK takeover and prompt timeouts. One action per
 * timer fire keeps every step individually observable (broadcast between
 * steps) and bounded (each accepted step makes progress).
 * @param active_lobby Target active match room whose session must advance.
 */
void MatchController::OnTurnStartedSession(Lobby* active_lobby) {
    match::server::MatchSession& session = *active_lobby->session;
    // INFO: A finished engine must still notify the lobby store
    //       so `lobby.session` / `match_id` are torn down (else a rematch is
    //       rejected with `kMatchAlreadyStarted`); BroadcastMatchState is
    //       idempotent and owns the terminal `match_over` send.
    if (session.Engine().IsMatchOver()) {
        BroadcastMatchState(active_lobby);
        return;
    }

    // INFO: the loading barrier gates the first turn clock; opening it calls
    //       back into this driver.
    if (!session.ReadyBarrierOpen()) return;

    // INFO: The turn clock is armed once per turn and paused while a
    //       prompt or window is pending; each prompt runs its own clock. The
    //       AFK/bot policy stays the controller's single-shot `turn_` timer;
    //       the engine deadlines are display/reconnect truth.
    const int64_t time_limit_ms = active_lobby->settings.turn_time_limit_ms;
    session.Engine().SyncClocks(time_limit_ms);

    const std::vector<std::string> actors = PendingActors(session);
    std::vector<std::string> auto_actors;
    std::string human_actor;
    for (const std::string& actor : actors) {
        if (IsAutoActor(*active_lobby, actor)) {
            auto_actors.push_back(actor);
            continue;
        }
        if (human_actor.empty()) human_actor = actor;
    }
    const std::string auto_actor =
        auto_actors.empty() ? std::string() : auto_actors.front();

    const uint32_t lobby_id = active_lobby->id;

    if (!auto_actor.empty()) {
        const LobbyMember* actor_member = active_lobby->FindMember(auto_actor);
        const bool disconnected_human =
            actor_member != nullptr && !actor_member->is_bot;
        int delay_ms;
        if (session.Engine().WindowHoldMs() > 0) {
            delay_ms = std::uniform_int_distribution<int>(
                kBotHoldReactionMinMs, kBotHoldReactionMaxMs - 1)(rng_);
        } else if (disconnected_human ||
                   active_lobby->settings.bot_mode ==
                       BotTakeoverMode::kPlayInstantly) {
            delay_ms = bot_instant_delay_ms_;
        } else {
            delay_ms = std::uniform_int_distribution<int>(
                bot_wait_min_ms_, bot_wait_max_ms_ - 1)(rng_);
        }

        SetTurnTimer(lobby_id, delay_ms, [this, lobby_id, auto_actors]() {
            Lobby* verified_lobby = lobby_store_.GetLobbyById(lobby_id);
            if (verified_lobby == nullptr || !verified_lobby->session) return;
            if (verified_lobby->session->Engine().IsMatchOver()) return;
            // INFO: a window responder that cannot act yet (a non-victim, or
            //       the victim inside the hold) must not starve the others.
            for (const std::string& actor : auto_actors) {
                match::server::HeuristicBotPolicy policy(
                    BotSeed(lobby_id, actor));
                if (match::server::BotStep(*verified_lobby->session, policy,
                                           actor)) {
                    break;
                }
            }
            verified_lobby->session->Tick();
            // INFO: Flush the bot step's engine events before
            //       the snapshot; otherwise an all-bot match emits no
            //       `match_event` until the next human input.
            verified_lobby->session->EmitEvents(broadcaster_);
            OnTurnStarted(verified_lobby);
            BroadcastMatchState(verified_lobby);
        });
        // INFO: bot wakes are sparse, so the window also gets its own tick to
        //       close the group when its line runs out.
        ScheduleWindowTick(active_lobby);
        return;
    }

    if (human_actor.empty()) {
        return;
    }

    // INFO: a connected human gets whatever is left on the clock they are
    //       acting against; if it expires a bot answers for them (AFK
    //       takeover). A window responder is covered by the window tick.
    const int afk_delay_ms =
        static_cast<int>(ActorTimeLeftMs(session, time_limit_ms));
    SetTurnTimer(lobby_id, afk_delay_ms,
                 [this, lobby_id, human_actor]() {
        Lobby* verified_lobby = lobby_store_.GetLobbyById(lobby_id);
        if (verified_lobby == nullptr || !verified_lobby->session) return;
        if (verified_lobby->session->Engine().IsMatchOver()) return;
        // INFO: a held playable draw resolves as the engine's auto-keep on
        //       timeout; the takeover bot must not play it (that would invert
        //       the play/keep promise and risk an unintended win).
        if (!ResolveAfkHeldDraw(*verified_lobby->session, human_actor)) {
            Logger::Info("[MATCH] Bot playing for AFK player: ", human_actor);
            match::server::HeuristicBotPolicy policy(
                BotSeed(lobby_id, human_actor));
            match::server::BotStep(*verified_lobby->session, policy,
                                   human_actor);
        }
        verified_lobby->session->Tick();
        // INFO: Flush the takeover step's engine events before
        //       the snapshot so AFK plays reach the wire as `match_event`.
        verified_lobby->session->EmitEvents(broadcaster_);
        OnTurnStarted(verified_lobby);
        BroadcastMatchState(verified_lobby);
    });
    ScheduleWindowTick(active_lobby);
}

/**
 * @brief Schedules a single-shot turn timer via the ITimerService.
 * @param lobby_id Numeric lobby ID, used as the timer key.
 * @param timeout_ms Milliseconds before the timer fires.
 * @param callback Invoked when the timer expires.
 */
void MatchController::SetTurnTimer(uint32_t lobby_id, int timeout_ms,
                                   std::function<void()> callback) {
    const std::string key = "turn_" + std::to_string(lobby_id);
    timer_service_.Schedule(key, timeout_ms, false, std::move(callback));
}

/**
 * @brief Cancels the turn timer for a given lobby.
 * @param lobby_id ID of the lobby whose timer to cancel.
 */
void MatchController::ClearTurnTimer(uint32_t lobby_id) {
    timer_service_.Cancel("turn_" + std::to_string(lobby_id));
}

/**
 * @brief Arms (or cancels) the response-window timeout tick.
 * @param lobby Target lobby whose open window should be watched.
 */
void MatchController::ScheduleWindowTick(Lobby* lobby) {
    if (lobby == nullptr) return;

    const std::string key = "window_" + std::to_string(lobby->id);
    if (!lobby->session || !lobby->session->Engine().WindowOpen()) {
        timer_service_.Cancel(key);
        return;
    }

    // INFO: the engine window deadline is absolute epoch ms on the engine's own
    //       clock (seam); derive the remaining lifetime from that same clock
    //       so an injected test clock stays consistent.
    match::server::MatchSession& session = *lobby->session;

    const int64_t upper     = std::max<int64_t>(
        100, static_cast<int64_t>(lobby->settings.turn_time_limit_ms));
    const int64_t now       = session.Engine().Timers().Turn().Now();
    const json window = session.Engine().ExportWindow();
    int64_t deadline = window.value("deadline_ms", int64_t{0});
    // INFO: a recorded winner closes a held group at the hold end, so the
    //       tick targets that instant instead of the full duration.
    const int64_t hold_ms = window.value("hold_ms", int64_t{0});
    const bool has_winner = std::any_of(
        window["responses"].begin(), window["responses"].end(),
        [](const json& response) { return !response.value("pass", false); });
    if (hold_ms > 0 && has_winner && deadline > 0) {
        const int64_t hold_end =
            deadline - window.value("duration_ms", int64_t{0}) + hold_ms;
        if (hold_end > now) deadline = hold_end;
        else deadline = now;
    }
    const int64_t remaining =
        std::clamp<int64_t>(deadline > 0 ? deadline - now : 0, 100, upper);

    const uint32_t id = lobby->id;
    timer_service_.Schedule(key, static_cast<int>(remaining), false,
                            [this, id]() {
        Lobby* current = lobby_store_.GetLobbyById(id);
        if (current == nullptr || !current->session) return;
        if (current->session->Engine().IsMatchOver()) return;
        current->session->Tick();
        current->session->EmitEvents(broadcaster_);
        if (current->session->Engine().WindowOpen()) {
            BroadcastMatchState(current);
            ScheduleWindowTick(current);
            return;
        }
        // INFO: the close resolved the window route and may have advanced the
        //       turn; re-arm the turn driver for the new actor so the timer
        //       armed for a window responder cannot fire as a stale AFK
        //       takeover.
        ClearTurnTimer(id);
        OnTurnStarted(current);
        BroadcastMatchState(current);
        ScheduleWindowTick(current);
    });
}
