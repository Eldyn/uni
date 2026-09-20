/**
 * @file match_controller.cpp
 * @brief Implementation of the MatchController routing active transaction frames to running match engines.
 */

#include "controllers/match_controller.hpp"
#include "match/match_instance.hpp"
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
    max_instant_bot_steps_ = std::max(1, Env::GetInt("MAX_INSTANT_BOT_STEPS", 20));
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

    lobby_store.OnGameStarted([this](Lobby* active_lobby) {
        OnTurnStarted(active_lobby);
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
    if (active_lobby->session) {
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
        active_lobby->session->BroadcastSnapshot(broadcaster_);
        ClearTurnTimer(active_lobby->id);
        OnTurnStarted(active_lobby);
        return;
    }

    if (!active_lobby->match) {
        return;
    }

    auto payload_res = ws::ParsePayload<ws::GamePlayCardPayload>(message);
    if (!payload_res) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kInvalidPayload, request_identifier,
                               payload_res.error().message);
        return;
    }
    uint16_t card_identifier = payload_res->card_id;

    bool was_play_successful = active_lobby->match->PlayCard(context.socket_data->username,
                                                              card_identifier);

    if (!was_play_successful) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kInvalidMove, request_identifier);
        return;
    }

    if (active_lobby->settings.mode == "elimination") {
        LobbyMember* m = active_lobby->FindMember(context.socket_data->username);
        if (m && !active_lobby->match->GetPlayer(m->username)) {
            m->is_spectator = true;
        }
    }

    active_lobby->match->Tick();
    ClearTurnTimer(active_lobby->id);
    OnTurnStarted(active_lobby);
    BroadcastMatchState(active_lobby);
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
    if (active_lobby->session) {
        if (!active_lobby->session->DrawCard(context.socket_data->username)) {
            broadcaster_.SendError(context.socket, context.op_code,
                                   contract::ErrorCode::kCannotDraw,
                                   request_identifier);
            return;
        }

        active_lobby->session->EmitEvents(broadcaster_);
        active_lobby->session->BroadcastSnapshot(broadcaster_);
        ClearTurnTimer(active_lobby->id);
        OnTurnStarted(active_lobby);
        return;
    }

    if (!active_lobby->match) {
        return;
    }

    bool was_draw_successful = active_lobby->match->DrawCard(context.socket_data->username);

    if (!was_draw_successful) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kCannotDraw, request_identifier);
        return;
    }

    active_lobby->match->Tick();
    ClearTurnTimer(active_lobby->id);
    OnTurnStarted(active_lobby);
    BroadcastMatchState(active_lobby);
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
    if (active_lobby->session) {
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
        active_lobby->session->BroadcastSnapshot(broadcaster_);
        ClearTurnTimer(active_lobby->id);
        OnTurnStarted(active_lobby);
        return;
    }

    if (!active_lobby->match) return;

    auto payload_res = ws::ParsePayload<ws::GameSubmitInputPayload>(message);
    if (!payload_res) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kInvalidPayload, request_identifier,
                               payload_res.error().message);
        return;
    }
    std::string input_value = payload_res->value;
    active_lobby->match->ProvideInput(context.socket_data->username, input_value);
    active_lobby->match->Tick();

    ClearTurnTimer(active_lobby->id);
    OnTurnStarted(active_lobby);
    BroadcastMatchState(active_lobby);
}

/**
 * @brief Serializes complete match configurations, sending unique filtered match boards down to each user.
 * @param current_lobby Target room pointer whose context needs to be synchronized.
 */
void MatchController::BroadcastMatchState(Lobby* current_lobby) {
    if (!current_lobby) return;

    // INFO: new-engine path. BroadcastSnapshot owns the per-recipient
    //       `match_state_updated` filtering and the terminal `match_over`
    //       packet (winner + placements); the controller only forwards the
    //       lobby-store teardown notification once the engine is finished.
    if (current_lobby->session) {
        current_lobby->session->BroadcastSnapshot(broadcaster_);
        if (current_lobby->session->Engine().IsMatchOver()) {
            lobby_store_.NotifyMatchOver(current_lobby->id);
        }
        return;
    }

    if (!current_lobby->match) return;

    bool is_match_over = current_lobby->match->IsMatchOver();
    bool is_waiting_for_input = current_lobby->match->IsWaitingForInput();
    std::string pending_player_username = current_lobby->match->GetPendingPlayer();
    match::Action required_action = current_lobby->match->GetPendingAction();

    json match_over_payload;
    if (is_match_over) {
        match_over_payload = ws::MakeResponse(ws::ServerAction::kMatchOver);
        match_over_payload["winner"] = current_lobby->match->GetWinner();
        match_over_payload["mode"] = current_lobby->settings.mode;
        match_over_payload["placements"] = current_lobby->match->GetPlacements();
    }

    json base_state = current_lobby->match->SerializeBaseState();
    int spectator_count = 0;
    for (const auto& m : current_lobby->members) {
        if (m.is_spectator && m.is_connected) {
            spectator_count++;
        }
    }
    base_state["spectator_count"] = spectator_count;

    for (const auto& lobby_member : current_lobby->members) {
        if (!lobby_member.is_connected || !lobby_member.socket) continue;

        json response_payload = ws::MakeResponse(ws::ServerAction::kMatchStateUpdated);
        json match_state = base_state;

        if (lobby_member.is_spectator) {
            for (auto& p_json : match_state["players"]) {
                std::string p_name = p_json["username"];
                LobbyMember* p_member = current_lobby->FindMember(p_name);
                bool privacy = p_member ? p_member->privacy_mode : false;
                if (!privacy) {
                    p_json["hand"] = current_lobby->match->SerializeHandFor(p_name);
                }
            }
        } else {
            for (auto& p_json : match_state["players"]) {
                if (p_json["username"] == lobby_member.username) {
                    p_json["hand"] = current_lobby->match->SerializeHandFor(lobby_member.username);
                    break;
                }
            }
        }
        response_payload["match_state"] = std::move(match_state);

        if (is_waiting_for_input && lobby_member.username == pending_player_username) {
            response_payload["action_required"] = static_cast<int>(required_action);

            const nlohmann::json& pending_context = current_lobby->match->GetPendingInputContext();
            if (!pending_context.is_null()) {
                response_payload["action_context"] = pending_context;
            }
        }

        broadcaster_.Send(lobby_member.socket, response_payload.dump(), uWS::OpCode::TEXT);

        if (is_match_over) {
            broadcaster_.Send(lobby_member.socket, match_over_payload.dump(), uWS::OpCode::TEXT);
        }
    }

    if (is_match_over) {
        lobby_store_.NotifyMatchOver(current_lobby->id);
    }
}

/**
 * @brief Sets turn constraints, routing automatic simulation threads on bot turns or player timeouts.
 * @param active_lobby Target active match room layout evaluated.
 */
void MatchController::OnTurnStarted(Lobby* active_lobby) {
    if (!active_lobby) {
        return;
    }

    if (active_lobby->session) {
        OnTurnStartedSession(active_lobby);
        return;
    }

    if (!active_lobby->match || active_lobby->match->IsMatchOver()) {
        return;
    }

    std::string current_player_username = active_lobby->match->GetCurrentPlayerUsername();

    auto is_connected = [active_lobby](const std::string& username) {
        for (const auto& m : active_lobby->members) {
            if (m.username == username && m.is_connected) return true;
        }
        return false;
    };

    switch (active_lobby->match->GetTurnTimeoutPolicy(is_connected)) {
        case match::TurnTimeoutPolicy::kBotThinking: {
            int bot_thinking_ms =
                active_lobby->settings.bot_mode == BotTakeoverMode::kPlayInstantly
                    ? bot_instant_delay_ms_
                    : std::uniform_int_distribution<int>(
                          bot_wait_min_ms_, bot_wait_max_ms_ - 1)(rng_);

            // INFO: Waiting for each input is tiresome. "Pending Color" -> ~2
            //       seconds, "Draw or Play" -> ~2 seconds. This stacks up.
            //       Let's be instantaneous!
            if (active_lobby->match->IsWaitingForInput()) bot_thinking_ms = bot_instant_delay_ms_;

            auto end_time = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(active_lobby->settings.turn_time_limit_ms);
            active_lobby->match->SetTurnEndTime(end_time);

            uint32_t current_lobby_id = active_lobby->id;

            SetTurnTimer(current_lobby_id, bot_thinking_ms,
                        [this, current_lobby_id, current_player_username]() {
                Lobby* verified_lobby = lobby_store_.GetLobbyById(current_lobby_id);
                if (verified_lobby && verified_lobby->match) {
                    if (verified_lobby->match->GetCurrentPlayerUsername() ==
                        current_player_username) {
                        verified_lobby->match->TakeBotTurn();
                        OnTurnStarted(verified_lobby);
                        BroadcastMatchState(verified_lobby);
                    }
                }
            });
            return;
        }

        case match::TurnTimeoutPolicy::kInstantBotAdvance: {
            Logger::Info("[MATCH] Bot instant turn for: ", current_player_username);

            // INFO: Broadcast between each step so every action is visible to
            //       connected players.
            auto on_step = [this, active_lobby]() { BroadcastMatchState(active_lobby); };

            auto advance_result = active_lobby->match->AdvanceBotTurns(
                is_connected, on_step, max_instant_bot_steps_);
            if (advance_result.stalled) {
                Logger::Error("[MATCH] kPlayInstantly stall detected, aborting bot loop");
            }

            OnTurnStarted(active_lobby);
            return;
        }

        case match::TurnTimeoutPolicy::kInputWaitTimeout: {
            // INFO: The engine is waiting for action input (e.g. colour pick
            //       after a Jolly). A timer is always armed here regardless of
            //       bot mode, the pending player must respond within the turn
            //       time limit or a bot handles it for them.
            std::string pending = active_lobby->match->GetPendingPlayer();
            auto end_time = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(active_lobby->settings.turn_time_limit_ms);
            active_lobby->match->SetTurnEndTime(end_time);
            uint32_t current_lobby_id = active_lobby->id;
            SetTurnTimer(current_lobby_id, active_lobby->settings.turn_time_limit_ms,
                [this, current_lobby_id, pending]() {
                    Lobby* verified_lobby = lobby_store_.GetLobbyById(current_lobby_id);
                    if (!verified_lobby || !verified_lobby->match) return;
                    if (!verified_lobby->match->IsWaitingForInput()) return;
                    if (verified_lobby->match->GetPendingPlayer() != pending) return;
                    Logger::Info("[MATCH] Action timeout for player: ", pending);
                    verified_lobby->match->TakeBotTurn();
                    OnTurnStarted(verified_lobby);
                    BroadcastMatchState(verified_lobby);
                });
            return;
        }

        case match::TurnTimeoutPolicy::kHumanAfkTimeout: {
            auto end_time = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(active_lobby->settings.turn_time_limit_ms);
            active_lobby->match->SetTurnEndTime(end_time);

            uint32_t current_lobby_id = active_lobby->id;
            SetTurnTimer(current_lobby_id, active_lobby->settings.turn_time_limit_ms,
                        [this, current_lobby_id, current_player_username]() {
                Lobby* verified_lobby = lobby_store_.GetLobbyById(current_lobby_id);
                if (verified_lobby && verified_lobby->match) {
                    if (verified_lobby->match->GetCurrentPlayerUsername() ==
                        current_player_username) {
                        Logger::Info("[MATCH] Bot playing for AFK player: ",
                                     current_player_username);
                        verified_lobby->match->TakeBotTurn();
                        OnTurnStarted(verified_lobby);
                        BroadcastMatchState(verified_lobby);
                    }
                }
            });
            return;
        }

        case match::TurnTimeoutPolicy::kNone:
            return;
    }
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
    if (session.Engine().IsMatchOver()) {
        return;
    }

    const std::vector<std::string> actors = PendingActors(session);
    std::string auto_actor;
    std::string human_actor;
    for (const std::string& actor : actors) {
        if (IsAutoActor(*active_lobby, actor)) {
            auto_actor = actor;
            break;
        }
        if (human_actor.empty()) human_actor = actor;
    }

    const uint32_t lobby_id = active_lobby->id;

    if (!auto_actor.empty()) {
        const LobbyMember* actor_member = active_lobby->FindMember(auto_actor);
        const bool disconnected_human =
            actor_member != nullptr && !actor_member->is_bot;
        int delay_ms;
        if (disconnected_human ||
            active_lobby->settings.bot_mode ==
                BotTakeoverMode::kPlayInstantly) {
            delay_ms = bot_instant_delay_ms_;
        } else {
            delay_ms = std::uniform_int_distribution<int>(
                bot_wait_min_ms_, bot_wait_max_ms_ - 1)(rng_);
        }

        SetTurnTimer(lobby_id, delay_ms, [this, lobby_id, auto_actor]() {
            Lobby* verified_lobby = lobby_store_.GetLobbyById(lobby_id);
            if (verified_lobby == nullptr || !verified_lobby->session) return;
            if (verified_lobby->session->Engine().IsMatchOver()) return;
            match::server::HeuristicBotPolicy policy(
                BotSeed(lobby_id, auto_actor));
            match::server::BotStep(*verified_lobby->session, policy,
                                   auto_actor);
            verified_lobby->session->Tick();
            OnTurnStarted(verified_lobby);
            BroadcastMatchState(verified_lobby);
        });
        return;
    }

    if (human_actor.empty()) {
        return;
    }

    // INFO: a connected human keeps the full turn-time-limit; if it expires a
    //       bot answers for them (AFK takeover), mirroring the legacy policy.
    SetTurnTimer(lobby_id, active_lobby->settings.turn_time_limit_ms,
                 [this, lobby_id, human_actor]() {
        Lobby* verified_lobby = lobby_store_.GetLobbyById(lobby_id);
        if (verified_lobby == nullptr || !verified_lobby->session) return;
        if (verified_lobby->session->Engine().IsMatchOver()) return;
        Logger::Info("[MATCH] Bot playing for AFK player: ", human_actor);
        match::server::HeuristicBotPolicy policy(
            BotSeed(lobby_id, human_actor));
        match::server::BotStep(*verified_lobby->session, policy, human_actor);
        verified_lobby->session->Tick();
        OnTurnStarted(verified_lobby);
        BroadcastMatchState(verified_lobby);
    });
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
