#include "controllers/game_controller.hpp"
#include "common/ws.hpp"
#include "logger.hpp"
#include <App.h>

using json = nlohmann::json;

GameController::GameController(WebServer& server, LobbyController& lobby_controller)
    : action_router_(server.GetActionRouter()), lobby_controller_(lobby_controller) {

    action_router_.On("game_play_card", [this](WsContext context, const json& message) {
        HandlePlayCard(context, message);
        return true;
    });

    action_router_.On("game_draw_card", [this](WsContext context, const json& message) {
        HandleDrawCard(context, message);
        return true;
    });

    action_router_.On("game_submit_input", [this](WsContext context, const json& message) {
        HandleProvideInput(context, message);
        return true;
    });

    action_router_.On("game_call_uno", [this](WsContext context, const json& message) {
        HandleCallUno(context, message);
        return true;
    });

    Logger::Info("[Game] GameController registered");
}

void GameController::HandlePlayCard(WsContext context, const json& message) {
    Lobby* active_lobby = lobby_controller_.GetLobbyByCode(context.socket_data->lobby_code);
    if (!active_lobby || !active_lobby->match) {
        return;
    }

    std::string request_identifier = ws::GetOr<std::string>(message, "request_id", "");
    uint16_t card_identifier = ws::GetOr<uint16_t>(message, "card_id", 0);

    bool was_play_successful = active_lobby->match->PlayCard(context.socket_data->username, card_identifier);
    
    if (!was_play_successful) {
        ws::SendError(context.socket, context.op_code, "Invalid move", request_identifier);
        return;
    }

    // Resolve any synchronous effects resulting from the card
    active_lobby->match->Tick();
    
    // The human player successfully acted, so we cancel their AFK timer
    ClearTurnTimer(active_lobby->id); 
    
    // Broadcast the new board state
    BroadcastGameState(active_lobby);
    
    // Trigger evaluation for the next player (in case it is a bot's turn)
    OnTurnStarted(active_lobby);
}

void GameController::HandleDrawCard(WsContext context, const json& message) {
    Lobby* active_lobby = lobby_controller_.GetLobbyByCode(context.socket_data->lobby_code);
    if (!active_lobby || !active_lobby->match) {
        return;
    }

    std::string request_identifier = ws::GetOr<std::string>(message, "request_id", "");

    bool was_draw_successful = active_lobby->match->DrawCard(context.socket_data->username);
    
    if (!was_draw_successful) {
        ws::SendError(context.socket, context.op_code, "Cannot draw card", request_identifier);
        return;
    }

    active_lobby->match->Tick();

    ClearTurnTimer(active_lobby->id);
    BroadcastGameState(active_lobby);
    OnTurnStarted(active_lobby);
}

void GameController::HandleProvideInput(WsContext context, const json& message) {
    Lobby* active_lobby = lobby_controller_.GetLobbyByCode(context.socket_data->lobby_code);
    if (!active_lobby || !active_lobby->match) return;

    std::string input_value = ws::GetOr<std::string>(message, "value", "");
    active_lobby->match->ProvideInput(context.socket_data->username, input_value);
    active_lobby->match->Tick();
    
    ClearTurnTimer(active_lobby->id);
    BroadcastGameState(active_lobby);
    OnTurnStarted(active_lobby);
}

void GameController::BroadcastGameState(Lobby* current_lobby) {
    if (current_lobby->match->IsGameOver()) {
        json game_over_payload = ws::MakeResponse(ws::ServerAction::kGameOver);

        game_over_payload["winner"] = current_lobby->match->GetCurrentPlayerUsername(); 
            
        std::string serialized = game_over_payload.dump();
            
        for (const auto& lobby_member : current_lobby->members) {
            if (lobby_member.is_connected && lobby_member.socket) {
                lobby_member.socket->send(serialized, uWS::OpCode::TEXT);
            }
        }

        return;
    }

    bool is_waiting_for_input = current_lobby->match->IsWaitingForInput();
    std::string pending_player_username = current_lobby->match->GetPendingPlayer();
    std::string required_input_type = current_lobby->match->GetPendingInputType();

    for (const auto& lobby_member : current_lobby->members) {
        if (!lobby_member.is_connected || !lobby_member.socket) continue;

        json response_payload = ws::MakeResponse(ws::ServerAction::kGameStateUpdated);
        response_payload["game_state"] = current_lobby->match->SerializePlayerState(lobby_member.username);

        if (is_waiting_for_input && lobby_member.username == pending_player_username) {
            response_payload["action_required"] = required_input_type;
            
            std::string context_string = current_lobby->match->GetPendingInputContext();
            if (!context_string.empty()) {
                response_payload["action_context"] = json::parse(context_string);
            }
        }

        lobby_member.socket->send(response_payload.dump(), uWS::OpCode::TEXT);
    }
}

void GameController::HandleCallUno(WsContext context, const json& message) {
    Lobby* active_lobby = lobby_controller_.GetLobbyByCode(context.socket_data->lobby_code);
    if (!active_lobby || !active_lobby->match) {
        return;
    }

    active_lobby->match->CallUno(context.socket_data->username);
    BroadcastGameState(active_lobby);
}

void GameController::OnTurnStarted(Lobby* active_lobby) {
    if (!active_lobby || !active_lobby->match || active_lobby->match->IsGameOver()) {
        return;
    }

    std::string current_player_username = active_lobby->match->GetCurrentPlayerUsername();
    
    bool is_player_connected = false;
    for (const auto& lobby_member : active_lobby->members) {
        if (lobby_member.username == current_player_username) {
            is_player_connected = lobby_member.is_connected;
            break;
        }
    }

    if (active_lobby->settings.bot_mode == BotTakeoverMode::kPlayInstantly && !is_player_connected) {
        Logger::Info("[Bot] Taking instant turn for disconnected player: ", current_player_username);
        
        active_lobby->match->TakeBotTurn();
        BroadcastGameState(active_lobby);
        
        // Recursively evaluate the next turn (in case multiple players are disconnected)
        OnTurnStarted(active_lobby); 
        return;
    }

    if (active_lobby->settings.bot_mode == BotTakeoverMode::kWaitUntilTurnEnd) {
        auto end_time = std::chrono::steady_clock::now() + 
                        std::chrono::milliseconds(active_lobby->settings.turn_time_limit_ms);
        active_lobby->match->SetTurnEndTime(end_time);

        uint32_t current_lobby_id = active_lobby->id;
        SetTurnTimer(current_lobby_id, active_lobby->settings.turn_time_limit_ms, [this, current_lobby_id, current_player_username]() {
            // NOTE: This lambda executes asynchronously in the future.
            //       We must re-fetch the lobby to ensure it was not 
            //       deleted while we waited.
            Lobby* verified_lobby = lobby_controller_.GetLobbyById(current_lobby_id);
            
            if (verified_lobby && verified_lobby->match) {
                // Ensure it is still the exact same player's turn (they didn't play at the last millisecond)
                if (verified_lobby->match->GetCurrentPlayerUsername() == current_player_username) {
                    Logger::Info("[Bot] Time expired. Bot playing for AFK player: ", current_player_username);
                    
                    verified_lobby->match->TakeBotTurn();
                    BroadcastGameState(verified_lobby);
                    
                    // Recursively call for the next player's turn
                    OnTurnStarted(verified_lobby);
                }
            }
        });
    }
}

struct TurnTimerData {
    std::function<void()> callback;
    uint32_t lobby_id;
    GameController* controller;
};

void GameController::SetTurnTimer(uint32_t lobby_id, int timeout_ms, std::function<void()> callback) {
    ClearTurnTimer(lobby_id);
    struct us_loop_t* loop = (struct us_loop_t*) uWS::Loop::get();
    
    // INFO: Allocate space for a POINTER, not the struct itself
    struct us_timer_t* timer = us_create_timer(loop, 0, sizeof(TurnTimerData*));

    auto* timer_data = new TurnTimerData{std::move(callback), lobby_id, this};
    *(TurnTimerData**)us_timer_ext(timer) = timer_data;

    us_timer_set(timer, [](struct us_timer_t* fired_timer) {
        TurnTimerData* data = *(TurnTimerData**)us_timer_ext(fired_timer);
        if (data->callback) data->callback();
        
        // This will call delete data and us_timer_close!
        data->controller->ClearTurnTimer(data->lobby_id); 
    }, timeout_ms, 0);

    active_turn_timers_[lobby_id] = timer;
}

void GameController::ClearTurnTimer(uint32_t lobby_id) {
    auto timer_iterator = active_turn_timers_.find(lobby_id);
    if (timer_iterator != active_turn_timers_.end()) {
        struct us_timer_t* timer = timer_iterator->second;
        TurnTimerData* data = *(TurnTimerData**)us_timer_ext(timer);
        
        delete data;
        us_timer_close(timer);
        
        active_turn_timers_.erase(timer_iterator);
    }
}
