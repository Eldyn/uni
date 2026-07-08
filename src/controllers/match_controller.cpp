/**
 * @file match_controller.cpp
 * @brief Implementation of the MatchController routing active transaction frames to running match engines.
 */

#include "controllers/match_controller.hpp"
#include "match/match_instance.hpp"
#include "common/lobby.hpp"
#include "common/ws.hpp"
#include "common/payloads.hpp"
#include "common/env.hpp"
#include "logger.hpp"
#include <algorithm>
#include <random>

using json = nlohmann::json;

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

    action_router_.On(ws::ClientAction::kMatchCallUno,
                      [this](WsContext context, const json& message) {
        HandleCallUno(context, message);
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
    if (!active_lobby || !active_lobby->match) {
        return;
    }

    std::string request_identifier = ws::GetOr<std::string>(message, "request_id", "");
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
    if (!active_lobby || !active_lobby->match) {
        return;
    }

    std::string request_identifier = ws::GetOr<std::string>(message, "request_id", "");

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
    if (!active_lobby || !active_lobby->match) return;

    std::string request_identifier = ws::GetOr<std::string>(message, "request_id", "");
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
    if (!current_lobby || !current_lobby->match) return;

    bool is_match_over = current_lobby->match->IsMatchOver();
    bool is_waiting_for_input = current_lobby->match->IsWaitingForInput();
    std::string pending_player_username = current_lobby->match->GetPendingPlayer();
    match::Action required_action = current_lobby->match->GetPendingAction();

    json match_over_payload;
    if (is_match_over) {
        match_over_payload = ws::MakeResponse(ws::ServerAction::kMatchOver);
        match_over_payload["winner"] = current_lobby->match->GetWinner();
    }

    json base_state = current_lobby->match->SerializeBaseState();

    for (const auto& lobby_member : current_lobby->members) {
        if (!lobby_member.is_connected || !lobby_member.socket) continue;

        json response_payload = ws::MakeResponse(ws::ServerAction::kMatchStateUpdated);
        json match_state = base_state;
        for (auto& p_json : match_state["players"]) {
            if (p_json["username"] == lobby_member.username) {
                p_json["hand"] = current_lobby->match->SerializeHandFor(lobby_member.username);
                break;
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
 * @brief Flags a player's safety verification state within the match rule evaluation layers.
 * @param context Signaling packet metadata tracking incoming user sockets.
 * @param message Received payload data map document.
 */
void MatchController::HandleCallUno(WsContext context, const json& message) {
    Lobby* active_lobby = lobby_store_.GetLobbyById(context.socket_data->lobby_id);
    if (!active_lobby || !active_lobby->match) {
        return;
    }

    active_lobby->match->CallUno(context.socket_data->username);
    BroadcastMatchState(active_lobby);
}

/**
 * @brief Sets turn constraints, routing automatic simulation threads on bot turns or player timeouts.
 * @param active_lobby Target active match room layout evaluated.
 */
void MatchController::OnTurnStarted(Lobby* active_lobby) {
    if (!active_lobby || !active_lobby->match || active_lobby->match->IsMatchOver()) {
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
                Logger::Error("[MATCH] kPlayInstantly stall detected — aborting bot loop");
            }

            OnTurnStarted(active_lobby);
            return;
        }

        case match::TurnTimeoutPolicy::kInputWaitTimeout: {
            // INFO: The engine is waiting for action input (e.g. colour pick
            //       after a Jolly). A timer is always armed here regardless of
            //       bot mode — the pending player must respond within the turn
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
