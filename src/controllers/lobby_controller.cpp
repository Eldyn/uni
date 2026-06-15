/**
 * @file lobby_controller.cpp
 * @brief Implementation of the LobbyController class managing websocket signaling and session lifecycles for game rooms.
 */

#include "common/lobby.hpp"
#include "webserver.hpp"
#include "websocket_context.hpp"
#include <WebSocketProtocol.h>
#include <controllers/lobby_controller.hpp>
#include <common/bot_names.hpp>
#include <common/ws.hpp>
#include <common/payloads.hpp>
#include <logger.hpp>
#include <openssl/rand.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <random>

using namespace std::chrono;
using std::string, std::vector, std::runtime_error, std::memory_order_relaxed, std::transform, std::to_string; 

static constexpr char kCodeAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static constexpr int  kCodeLen        = 6;
static constexpr int  kAlphabetLen    = 36;   

/**
 * @brief Selects a random, unique bot name from a predefined thematic list.
 * * Iterates through the current lobby members to ensure that the selected 
 * bot name is not already taken by an existing human or bot. If all predefined 
 * names are in use (unlikely but possible), it safely falls back to a 
 * sequentially numbered "Bot_N" format.
 * * @param lobby The target lobby context used to verify name availability.
 * @return std::string A unique bot name safe for insertion into the lobby.
 */
std::string GetRandomBotName(const Lobby& lobby) {
    std::vector<std::string> available;
    
    // Use the globally shared bot roster instead of a local variable
    for (const auto& name : game::kReservedBotNames) {
        bool taken = std::ranges::any_of(lobby.members, [&](const LobbyMember& m) {
            return m.username == name;
        });
        if (!taken) available.push_back(name);
    }
    
    // Failsafe in case all preset names are miraculously taken
    if (available.empty()) {
        static int fallback_id = 1;
        return "Bot_" + std::to_string(fallback_id++);
    }
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, available.size() - 1);
    
    return available[dist(gen)];
}


/**
 * @brief Constructs the LobbyController instance and establishes central inbound routing maps.
 * @param server Reference to the hosting asynchronous web server infrastructure.
 */
LobbyController::LobbyController(WebServer& server) : action_router_(server.GetActionRouter()), app_(server.GetApp()) {
    
    action_router_.On(ws::ClientAction::kLobbyCreate, [this](WsContext ctx, const json& msg) {
        HandleCreate(ctx, msg);
        return true;
    });

    action_router_.On(ws::ClientAction::kLobbyJoin, [this](WsContext ctx, const json& msg) {
        HandleJoin(ctx, msg);
        return true;
    });

    action_router_.On(ws::ClientAction::kLobbyRejoin, [this](WsContext ctx, const json& msg) {
        HandleRejoin(ctx, msg);
        return true;
    });

    action_router_.On(ws::ClientAction::kLobbyLeave, [this](WsContext ctx, const json& msg) {
        HandleLeave(ctx, msg);
        return true;
    });

    action_router_.On(ws::ClientAction::kLobbyList, [this](WsContext ctx, const json& msg) {
        HandleList(ctx, msg);
        return true;
    });

    action_router_.On(ws::ClientAction::kLobbyKick, [this](WsContext ctx, const json& msg) {
        HandleKick(ctx, msg);
        return true;
    });

    action_router_.On(ws::ClientAction::kLobbyPromote, [this](WsContext ctx, const json& msg) {
        HandlePromote(ctx, msg);
        return true;
    });

    action_router_.On(ws::ClientAction::kLobbyListSavedMatches, [this](WsContext ctx, const json& msg) {
        HandleGetSavedMatchesList(ctx, msg);
        return true;
    });

    action_router_.On(ws::ClientAction::kLobbyDeleteSavedMatch, [this](WsContext ctx, const json& msg) {
        HandleDeleteSavedMatch(ctx, msg);
        return true;
    });

    action_router_.On(ws::ClientAction::kLobbyResumeSavedMatch, [this](WsContext ctx, const json& msg) {
        HandleResumeSavedMatch(ctx, msg);
        return true;
    });

    action_router_.On(ws::ClientAction::kLobbyUpdateSettings, [this](WsContext ctx, const json& msg) {
        HandleUpdateSettings(ctx, msg);
        return true;
    });

    action_router_.On(ws::ClientAction::kLobbyStartMatch, [this](WsContext context, const nlohmann::json& message) {
        HandleStartGame(context, message);
        return true;
    });

    server.OnConnectionOpen([this](AppWebSocket* ws, PerSocketData* sd) {
        this->OnOpen(ws, sd);
    });

    server.OnConnectionClose([this](AppWebSocket* ws, PerSocketData* sd) {
        this->OnClose(ws, sd);
    });

    eviction_timer_ = us_create_timer((us_loop_t*)uWS::Loop::get(), 0, sizeof(LobbyController*));
    *(LobbyController**)us_timer_ext(eviction_timer_) = this;

    us_timer_set(eviction_timer_, [](us_timer_t* t) {
        auto* self = *(LobbyController**)us_timer_ext(t);
        auto  now  = steady_clock::now();

        std::vector<std::pair<uint32_t, std::string>> to_evict;

        for (const auto& [id, lobby] : self->lobbies_) {
            for (const auto& m : lobby.members) {
                if (!m.is_connected && !m.is_bot) {
                    auto elapsed = duration_cast<milliseconds>(now - m.disconnected_at).count();
                    if (elapsed > kReconnectGraceMs) {
                        to_evict.push_back({id, m.username});
                    }
                }
            }
        }

        std::set<uint32_t> lobbies_to_update;

        for (const auto& [id, username] : to_evict) {
            Logger::Log("[Lobby] Grace expired. Evicting ", username, " from lobby ", id);
            
            bool lobby_survived = self->RemoveMember(id, username, false, "");

            if (lobby_survived) {
                Lobby& lobby = self->lobbies_.at(id);
                
                if (lobby.host == username) {
                    for (const auto& m : lobby.members) {
                        if (m.is_connected && !m.is_bot) {
                            lobby.host = m.username;
                            Logger::Log("[Lobby] Host auto-passed to ", m.username, " in lobby ", id);
                            break;
                        }
                    }
                }

                self->SyncBots(lobby);
                lobbies_to_update.insert(id);
            } else {
                lobbies_to_update.erase(id);
            }
        }

        for (uint32_t id : lobbies_to_update) {
            self->BroadcastUpdate(self->lobbies_.at(id));
        }

    }, 1000, 1000);

    Logger::Info("[Lobby] Registered — grace window: " + to_string(kReconnectGraceMs / 1000) + "s");
}

/**
 * @brief Destructor that handles explicit system loop timer releases.
 */
LobbyController::~LobbyController() {
    if (eviction_timer_) {
        us_timer_close(eviction_timer_);
        eviction_timer_ = nullptr;
    }
}

/**
 * @brief Commits the structured snapshot of an ongoing match to the SQLite storage layer.
 * @param lobby Reference to the target active lobby containing the current game match.
 */
void LobbyController::SaveMatchStateToDB(Lobby& lobby) {
    if (!lobby.match || lobby.match->IsGameOver()) return;
    if (!lobby.settings.save_state) return;

    json saved_state = lobby.match->ExportState();
    std::string json_payload = saved_state.dump();
    std::string match_id = lobby.match->GetMatchId();

    try {
        auto& db = Database::Get();
        if (!db.IsOpen()) return;

        TransactionGuard tx(db);

        auto res_upsert = db.Exec(R"(
            INSERT INTO saved_matches (id, state_json) VALUES (?, ?)
            ON CONFLICT(id) DO UPDATE SET 
                state_json = excluded.state_json, 
                saved_at = CURRENT_TIMESTAMP, 
                expires_at = datetime('now', '+1 day')
        )", {match_id, json_payload});
        
        if (!res_upsert) {
            throw std::runtime_error("Upsert saved_matches failed: " + res_upsert.error().message);
        }

        auto res_del = db.Exec("DELETE FROM saved_match_participants WHERE match_id = ?", {match_id});
        if (!res_del) {
            throw std::runtime_error("Delete participants failed: " + res_del.error().message);
        }

        for (const auto& member : lobby.members) {
            if (!member.is_bot) {
                auto res_insert = db.Exec("INSERT INTO saved_match_participants (match_id, username) VALUES (?, ?)", {match_id, member.username});
                if (!res_insert) {
                    throw std::runtime_error("Insert participant failed for " + member.username + ": " + res_insert.error().message);
                }
            }
        }

        tx.Commit();
        Logger::Info("[Lobby] Safely upserted match state to DB: ", match_id);

    } catch (const std::exception& e) {
        Logger::Error("[Lobby DB Error] Failed to save state: ", e.what());
    }
}

/**
 * @brief Validates remaining room configurations and cleans up abandoned empty environments.
 * @param lobby Reference to the checked targeted room instance.
 */
void LobbyController::CheckMatchIntegrity(Lobby& lobby) {
    if (lobby.match && lobby.members.size() < 2) {
        Logger::Info("[Lobby] Match aborted for lobby ", lobby.id, " due to disconnections.");
        
        if (lobby.members.size() == 1) {
            json game_over_payload = ws::MakeResponse(ws::ServerAction::kGameOver);
            game_over_payload["winner"] = lobby.members.front().username;
            
            if (lobby.members.front().is_connected && lobby.members.front().socket) {
                lobby.members.front().socket->send(game_over_payload.dump(), uWS::OpCode::TEXT);
            }
        }
        
        lobby.match.reset();
    }
}

/**
 * @brief Triggers immediately on connection open, restoring user binding back into their active room session context.
 * @param ws Incoming pointer to the active raw client socket instance.
 * @param sd Extracted metadata context state owned by the underlying connection.
 */
void LobbyController::OnOpen(AppWebSocket* ws, PerSocketData* sd) {
    Lobby* lobby = FindLobbyForUser(sd->username);
    if (!lobby) return;

    for (auto& member : lobby->members) {
        if (member.username == sd->username) {
            Logger::Log("[Lobby] Reconnect: ", sd->username, " back in lobby ", lobby->id);

            member.socket    = ws;
            member.is_connected = true;
            member.disconnected_at = steady_clock::time_point{};
            sd->lobby_code = lobby->invite_code;

            BroadcastUpdate(*lobby);
            return;
        }
    }
}

/**
 * @brief Intercepts socket drop frames, setting up transient disconnection grace boundaries.
 * @param ws Connection reference which dropped out of visibility frames.
 * @param sd Socket data structure tracking current connection information.
 */
void LobbyController::OnClose(AppWebSocket* ws, PerSocketData* sd) {
    if (sd->lobby_code.empty()) return;

    auto it = code_to_id_.find(sd->lobby_code);
    if (it == code_to_id_.end()) return;

    auto lobby_it = lobbies_.find(it->second);
    if (lobby_it == lobbies_.end()) return;

    Lobby& lobby = lobby_it->second;

    for (auto& member : lobby.members) {
        if (member.username == sd->username) {
            Logger::Log("[Lobby] Disconnect: ", sd->username, " in lobby ", lobby.id, " — grace window open");
            member.is_connected    = false;
            member.socket          = nullptr;
            member.disconnected_at = steady_clock::now();
            BroadcastUpdate(lobby);
            return;
        }
    }
}

/**
 * @brief Allocates structural memory maps for initializing a novel game instance channel.
 * @param ctx Payload context wrapping request sockets and raw buffers.
 * @param message Received raw JSON document mapping initialization preferences.
 */
void LobbyController::HandleCreate(WsContext ctx, const json& message) {
    const string& username = ctx.socket_data->username;
    const string request_id = ws::GetOr<string>(message, "request_id", "");
    auto payload_res = ws::ParsePayload<ws::LobbyCreatePayload>(message);
    if (!payload_res) {
        ws::SendError(ctx.socket, ctx.op_code, payload_res.error().message, request_id);
        return;
    }

    if (!ctx.socket_data->lobby_code.empty()) {
        ws::SendError(ctx.socket, ctx.op_code, "Already in a lobby", request_id);
        return;
    }

    string code;
    int attempts = 0;
    do {
        code = GenerateInviteCode();
        if (++attempts > 10)
            throw runtime_error("[Lobby] Failed to generate unique code");
    } while (code_to_id_.count(code));

    uint32_t id = next_id_.fetch_add(1, memory_order_relaxed);

    Lobby& lobby = lobbies_[id];
    lobby.id                   = id;
    lobby.settings.is_public   = payload_res->is_public.value_or(false);
    lobby.invite_code          = code;
    lobby.host                 = username;
    lobby.name                 = payload_res->name.value_or(username + "'s lobby");
    lobby.members.emplace_back(username, ctx.socket, true, false);

    code_to_id_[code] = id;
    ctx.socket_data->lobby_code = code;
    
    std::string topic = "lobby_" + code;
    ctx.socket->subscribe(topic);
    SyncBots(lobby);

    Logger::Log("[Lobby] Created lobby ", id, " code=", code, " host=", username);

    auto resp = MakeResponse(ws::ServerAction::kLobbyJoined, request_id);
    resp["lobby"] = json{
        {"invite_code", code},
        {"host", lobby.host},
        {"members", MemberListJson(lobby)},
        {"settings", lobby.settings},
        {"name", lobby.name}
    };
    ctx.socket->send(resp.dump(), ctx.op_code);
}

/**
 * @brief Attaches incoming network contexts to matching pre-configured room environments.
 * @param ctx Payload context wrapping request sockets and raw buffers.
 * @param message Received payload string mapping specific target identification tokens.
 */
void LobbyController::HandleJoin(WsContext ctx, const json& message) {
    const string request_id = ws::GetOr<string>(message, "request_id", "");
    auto payload_res = ws::ParsePayload<ws::LobbyJoinPayload>(message);

    if (!payload_res) {
        ws::SendError(ctx.socket, ctx.op_code, "Malformed or missing join code: " + payload_res.error().message, request_id);
        return;
    }

    if (!ctx.socket_data->lobby_code.empty()) {
        ws::SendError(ctx.socket, ctx.op_code, "Already in a lobby", request_id);
        return;
    }

    string code = payload_res->code;
    transform(code.begin(), code.end(), code.begin(), ::toupper);

    auto it = code_to_id_.find(code);
    if (it == code_to_id_.end()) {
        ws::SendError(ctx.socket, ctx.op_code, "Lobby not found", request_id);
        return;
    }

    auto lobby_it = lobbies_.find(it->second);
    if (lobby_it == lobbies_.end()) {
        code_to_id_.erase(it); 
        ws::SendError(ctx.socket, ctx.op_code, "Lobby not found", request_id);
        return;
    }

    Lobby& lobby = lobby_it->second;
    const string& username = ctx.socket_data->username;

    if (std::ranges::contains(lobby.members, username, &LobbyMember::username)) {
        ws::SendError(ctx.socket, ctx.op_code, "Already a member", request_id);
        return;
    }

    if (lobby.match && !lobby.settings.allow_bot_takeover) {
        ws::SendError(ctx.socket, ctx.op_code, "The host disabled joining during a match.", request_id);
        return;
    }

    bool joined = false;

    if (lobby.settings.allow_bot_takeover) {
        for (auto& member : lobby.members) {
            if (member.is_bot) {
                std::string old_bot_name = member.username;
                
                member.username = username;
                member.socket = ctx.socket;
                member.is_connected = true;
                member.is_bot = false;
                
                if (lobby.match) {
                    game::Player* engine_player = lobby.match->GetPlayer(old_bot_name);
                    if (engine_player) {
                        engine_player->username = username;
                        engine_player->is_bot = false;
                    }
                }

                joined = true;
                Logger::Info("[Game] '", username, "' hijacked ", old_bot_name);
                break;
            }
        }
    }

    if (!joined && lobby.members.size() < kMaxMembers) {
        lobby.members.emplace_back(username, ctx.socket, true, false);
        
        if (lobby.match) {
            lobby.match->AddPlayerMidGame(username, false);
        }

        joined = true;
        Logger::Info("[Lobby] '", username, "' joined an empty slot.");
    }

    if (!joined) {
        ws::SendError(ctx.socket, ctx.op_code, "Lobby full", request_id);
        return;
    }

    ctx.socket_data->lobby_code = code;
    
    std::string topic = "lobby_" + code;
    ctx.socket->subscribe(topic);

    auto resp = MakeResponse(ws::ServerAction::kLobbyJoined, request_id);
    resp["lobby"] = json({
        {"invite_code", code},
        {"name",        lobby.name},
        {"host",        lobby.host},
        {"members",     MemberListJson(lobby)},
        {"settings",    lobby.settings}
    });
    ctx.socket->send(resp.dump(), ctx.op_code);

    BroadcastUpdate(lobby);

    if (lobby.match) {
        auto game_resp = MakeResponse(ws::ServerAction::kGameStateUpdated);
        game_resp["game_state"] = lobby.match->SerializePlayerState(username);
        ctx.socket->send(game_resp.dump(), ctx.op_code);
    }
}

/**
 * @brief Resolves tracking references for re-hooking dropped sessions immediately without data loss.
 * @param ctx Payload context wrapping request sockets and raw buffers.
 * @param message Raw input structure indicating desired identity verification keys.
 */
void LobbyController::HandleRejoin(WsContext ctx, const json& message) {
    const std::string request_id = ws::GetOr<string>(message, "request_id", "");
    auto payload_res = ws::ParsePayload<ws::LobbyRejoinPayload>(message);

    if (!payload_res) {
        ws::SendError(ctx.socket, ctx.op_code, "Missing lobby code: " + payload_res.error().message, request_id);
        return;
    }

    string code = payload_res->code;
    transform(code.begin(), code.end(), code.begin(), ::toupper);

    auto it = code_to_id_.find(code);
    if (it == code_to_id_.end()) {
        auto resp = MakeResponse(ws::ServerAction::kLobbyEvicted, request_id);
        resp["reason"] = "Lobby expired";
        ctx.socket->send(resp.dump(), ctx.op_code);
        return;
    }

    auto lobby_it = lobbies_.find(it->second);
    if (lobby_it == lobbies_.end()) {
        code_to_id_.erase(it); 
        auto resp = MakeResponse(ws::ServerAction::kLobbyEvicted, request_id);
        resp["reason"] = "Lobby expired or invalid";
        ctx.socket->send(resp.dump(), ctx.op_code);
        return;
    }

    Lobby& lobby = lobby_it->second;
    const string& username = ctx.socket_data->username;

    if (std::ranges::contains(lobby.members, username, &LobbyMember::username)) {
        string topic = "lobby_" + lobby.invite_code;
        ctx.socket->subscribe(topic);

        auto resp = MakeResponse(ws::ServerAction::kLobbyJoined, request_id);
        resp["lobby"] = json({
            {"invite_code", code},
            {"host",        lobby.host},
            {"members",     MemberListJson(lobby)},
            {"settings",    lobby.settings},
            {"name",        lobby.name}
        });

        ctx.socket->send(resp.dump(), ctx.op_code);

        if (lobby.match) {
            auto game_resp = MakeResponse(ws::ServerAction::kGameStateUpdated);
            game_resp["game_state"] = lobby.match->SerializePlayerState(username);
            ctx.socket->send(game_resp.dump(), ctx.op_code);
        }

        return;
    }

    ws::SendError(ctx.socket, ctx.op_code, "No longer a member of this lobby", request_id);
}

/**
 * @brief Cleanly unsubscribes channels when an explicit leave event is sent by a player.
 * @param ctx Payload context wrapping request sockets and raw buffers.
 * @param message JSON representation of the leave request payload.
 */
void LobbyController::HandleLeave(WsContext ctx, const json& message) {
    const string& username   = ctx.socket_data->username;
    const string& code       = ctx.socket_data->lobby_code;
    const string  request_id = ws::GetOr<string>(message, "request_id", "");

    if (code.empty()) {
        ws::SendError(ctx.socket, ctx.op_code, "Not in a lobby", request_id);
        return;
    }

    auto it = code_to_id_.find(code);
    if (it == code_to_id_.end()) {
        ws::SendError(ctx.socket, ctx.op_code, "Lobby not found", request_id);
        return;
    }

    Lobby& lobby = lobbies_.at(it->second);
    uint32_t id  = lobby.id;
    bool was_host = (lobby.host == username);

    bool lobby_still_exists = RemoveMember(id, username, true, request_id);

    if (lobby_still_exists) {
        SyncBots(lobbies_.at(id));
        if (was_host) {
            for (const auto& m : lobbies_.at(id).members) {
                if (m.is_connected && !m.is_bot) {
                    lobbies_.at(id).host = m.username;
                    Logger::Log("[Lobby] Host auto-passed to ", m.username, " in lobby ", id);
                    break;
                }
            }
        }
        BroadcastUpdate(lobbies_.at(id));
    }
}

/**
 * @brief Returns a public directory of all active joinable lobbies.
 * @param ctx Payload context wrapping request sockets and raw buffers.
 * @param message JSON message block from the client requesting room states.
 */
void LobbyController::HandleList(WsContext ctx, const json& message) {
    json list = json::array();
    string request_id = ws::GetOr<string>(message, "request_id", "");

    for (const auto& [id, lobby] : lobbies_) {
        if (!lobby.settings.is_public) continue;
        bool any_connected = std::ranges::any_of(lobby.members, &LobbyMember::is_connected);

        int humans = std::ranges::count_if(lobby.members, [](const auto& member) {
            return !member.is_bot;
        });

        if (!any_connected) continue;
        if (humans >= kMaxMembers) continue;
       
        list.push_back({
            {"name", lobby.name},
            {"member_count", humans},
            {"bot_count", lobby.members.size()}, 
            {"invite_code", lobby.invite_code}
        });
    }

    auto resp = MakeResponse(ws::ServerAction::kLobbyList, request_id);
    resp["lobbies"] = list;
    ctx.socket->send(resp.dump(), ctx.op_code);
}

/**
 * @brief Transfers room ownership permissions to another human user in the lobby.
 * @param ctx Payload context wrapping request sockets and raw buffers.
 * @param message Payload specifying targeted user label parameters.
 */
void LobbyController::HandlePromote(WsContext ctx, const json& message) {
    const string& code       = ctx.socket_data->lobby_code;
    const string& request_id = ws::GetOr<string>(message, "request_id", "");
    auto payload_res = ws::ParsePayload<ws::LobbyPromotePayload>(message);

    if (!payload_res) {
        ws::SendError(ctx.socket, ctx.op_code, "Username not specified", request_id);
        return; 
    }
    
    const string& username = payload_res->username;

    auto it = code_to_id_.find(code);
    if (it == code_to_id_.end()) {
        ws::SendError(ctx.socket, ctx.op_code, "Lobby not found", request_id);
        return;
    }

    auto lobby_it = lobbies_.find(it->second);
    if (lobby_it == lobbies_.end()) {
        ws::SendError(ctx.socket, ctx.op_code, "Lobby data integrity error", request_id);
        return;
    }
    Lobby& lobby = lobby_it->second;

    if (ctx.socket_data->username != lobby.host) {
        ws::SendError(ctx.socket, ctx.op_code, "You must be the host of this lobby!", request_id);
        return;
    }

    auto target_it = std::ranges::find(lobby.members, username, &LobbyMember::username);
    
    if (target_it == lobby.members.end()) {
        ws::SendError(ctx.socket, ctx.op_code, "No such user is in the lobby", request_id);
        return;
    }

    if (target_it->is_bot) {
        ws::SendError(ctx.socket, ctx.op_code, "You cannot promote a bot to host", request_id);
        return;
    }

    lobby.host = username;
    BroadcastUpdate(lobby);
}

/**
 * @brief Evicts a target entity out of the active channel scope maps.
 * @param ctx Payload context wrapping request sockets and raw buffers.
 * @param message Input mapping parameters declaring execution constraints.
 */
void LobbyController::HandleKick(WsContext ctx, const json& message) {
    const string& code       = ctx.socket_data->lobby_code;
    const string& request_id = ws::GetOr<string>(message, "request_id", "");
    auto payload_res = ws::ParsePayload<ws::LobbyKickPayload>(message);

    if (!payload_res) {
        ws::SendError(ctx.socket, ctx.op_code, "Username not specified", request_id);
        return; 
    }
    
    const string& username = payload_res->username;

    auto it = code_to_id_.find(code);
    if (it == code_to_id_.end()) {
        ws::SendError(ctx.socket, ctx.op_code, "Lobby not found", request_id);
        return;
    }

    auto lobby_it = lobbies_.find(it->second);
    if (lobby_it == lobbies_.end()) {
        ws::SendError(ctx.socket, ctx.op_code, "Lobby data integrity error", request_id);
        return;
    }
    Lobby& lobby = lobby_it->second;

    if (ctx.socket_data->username != lobby.host) {
        ws::SendError(ctx.socket, ctx.op_code, "You must be the host of this lobby!", request_id);
        return;
    }

    if (username == lobby.host) {
        ws::SendError(ctx.socket, ctx.op_code, "You cannot kick yourself", request_id);
        return;
    }

    auto target_it = std::ranges::find(lobby.members, username, &LobbyMember::username);
    
    if (target_it == lobby.members.end()) {
        ws::SendError(ctx.socket, ctx.op_code, "No such user is in the lobby", request_id);
        return;
    }

    if (target_it->is_bot && lobby.settings.bot_count > 0) {
        lobby.settings.bot_count--;
    }

    uint32_t lobby_id = lobby.id;
    bool lobby_still_exists = RemoveMember(lobby_id, username, false, request_id);

    if (lobby_still_exists) {
        SyncBots(lobbies_.at(lobby_id));
        BroadcastUpdate(lobbies_.at(lobby_id));
    }
}

/**
 * @brief Parses room modification payloads and adjusts internal structures on mutations.
 * @param ctx Payload context wrapping request sockets and raw buffers.
 * @param message Document declaration holding updated setting keys.
 */
void LobbyController::HandleUpdateSettings(WsContext ctx, const json& message) {
    const string& code       = ctx.socket_data->lobby_code;
    const string& request_id = ws::GetOr<string>(message, "request_id", "");

    auto it = code_to_id_.find(code);
    if (it == code_to_id_.end()) {
        ws::SendError(ctx.socket, ctx.op_code, "Lobby not found", request_id);
        return;
    }

    Lobby& lobby = lobbies_.at(it->second);

    if (ctx.socket_data->username != lobby.host) {
        ws::SendError(ctx.socket, ctx.op_code, "Only the host can modify settings", request_id);
        return;
    }

    auto payload_res = ws::ParsePayload<ws::LobbyUpdateSettingsPayload>(message);
    if (!payload_res) {
        ws::SendError(ctx.socket, ctx.op_code, "Invalid settings payload", request_id);
        return;
    }
    const auto& p = *payload_res;

    bool changed        = false;
    int  old_bot_count  = lobby.settings.bot_count;

    if (p.is_public.has_value())             { lobby.settings.is_public            = *p.is_public;                              changed = true; }
    if (p.name.has_value())                  { lobby.name                           = *p.name;                                   changed = true; }
    if (p.bot_count.has_value())             { lobby.settings.bot_count             = *p.bot_count;                              changed = true; }
    if (p.turn_time_limit_ms.has_value())    { lobby.settings.turn_time_limit_ms    = *p.turn_time_limit_ms;                     changed = true; }
    if (p.allow_bot_takeover.has_value())    { lobby.settings.allow_bot_takeover    = *p.allow_bot_takeover;                     changed = true; }
    if (p.allow_bot_replacement.has_value()) { lobby.settings.allow_bot_replacement = *p.allow_bot_replacement;                  changed = true; }
    if (p.save_state.has_value())            { lobby.settings.save_state            = *p.save_state;                             changed = true; }
    if (p.quit_deletes_match.has_value())    { lobby.settings.quit_deletes_match    = *p.quit_deletes_match;                     changed = true; }
    if (p.bot_mode.has_value())              { lobby.settings.bot_mode              = static_cast<BotTakeoverMode>(*p.bot_mode); changed = true; }
    if (p.starting_cards.has_value())        { lobby.settings.starting_cards        = *p.starting_cards;                         changed = true; }
    // active_mods is a plain vector (not optional) — use raw JSON check to distinguish "not sent" from "sent as []"
    if (message.contains("active_mods"))     { lobby.settings.active_mods           = p.active_mods;                             changed = true; }

    if (old_bot_count != lobby.settings.bot_count) {
        SyncBots(lobby);
    }

    if (changed) {
        auto response = ws::MakeResponse(ws::ServerAction::kSuccess, request_id);
        ctx.socket->send(response.dump(), uWS::OpCode::TEXT);
        BroadcastUpdate(lobby);
    }
}

/**
 * @brief Queries database layers compiling historical session records for connected users.
 * @param ctx Payload context wrapping request sockets and raw buffers.
 * @param message Inbound payload message.
 */
void LobbyController::HandleGetSavedMatchesList(WsContext ctx, const json& message) {
    const string request_id = ws::GetOr<string>(message, "request_id", "");
    const string& username = ctx.socket_data->username;
    const string& code = ctx.socket_data->lobby_code;

    if (code.empty()) {
        ws::SendError(ctx.socket, ctx.op_code, "Not in a lobby", request_id);
        return;
    }

    auto it = code_to_id_.find(code);
    if (it == code_to_id_.end()) {
        ws::SendError(ctx.socket, ctx.op_code, "Lobby not found", request_id);
        return;
    }

    auto lobby_it = lobbies_.find(it->second);
    if (lobby_it == lobbies_.end()) {
        ws::SendError(ctx.socket, ctx.op_code, "Lobby not found", request_id);
        return;
    }

    Lobby& lobby = lobbies_.at(it->second);

    std::vector<std::string> current_humans;
    for (const auto& m : lobby.members) {
        if (!m.is_bot) {
            current_humans.push_back(m.username);
        }
    }

    std::sort(current_humans.begin(), current_humans.end());

    auto& db = Database::Get();
    auto _ = db.Exec("DELETE FROM saved_matches WHERE expires_at <= CURRENT_TIMESTAMP", {});

    auto rows_result = db.Query(R"(
        SELECT m.id, m.saved_at 
        FROM saved_matches m 
        JOIN saved_match_participants p ON m.id = p.match_id 
        WHERE p.username = ?
        ORDER BY m.saved_at DESC
    )", {username});

    json list = json::array();
    if (rows_result) {
        for (const auto& row : rows_result.value()) {
            std::string match_id = row.Get<std::string>("id");
            std::string saved_at = row.Get<std::string>("saved_at");
            
            auto players_res = db.Query("SELECT username FROM saved_match_participants WHERE match_id = ?", {match_id});
            
            std::vector<std::string> match_humans;
            if (players_res) {
                for(const auto& p_row : players_res.value()) {
                    match_humans.push_back(p_row.Get<std::string>("username"));
                }
            }
            
            std::sort(match_humans.begin(), match_humans.end());

            if (current_humans == match_humans) {
                list.push_back({
                    {"match_id", match_id},
                    {"saved_at", saved_at},
                    {"players", match_humans}
                });
            }
        }
    }

    auto resp = MakeResponse(ws::ServerAction::kSuccess, request_id);
    resp["saved_matches"] = list; 
    ctx.socket->send(resp.dump(), ctx.op_code);
}

/**
 * @brief Removes a saved historical game session completely from persistence tracking.
 * @param context Payload context wrapping request sockets and raw buffers.
 * @param message Structure holding the specific `match_id` string to purge.
 */
void LobbyController::HandleDeleteSavedMatch(WsContext context, const json& message) {
    const string& code = context.socket_data->lobby_code;
    const string request_id = ws::GetOr<string>(message, "request_id", "");
    auto payload_res = ws::ParsePayload<ws::LobbyDeleteSavedMatchPayload>(message);

    if (!payload_res) {
        ws::SendError(context.socket, context.op_code, payload_res.error().message, request_id);
        return;
    }
    std::string match_id = payload_res->match_id;

    auto it = code_to_id_.find(code);
    if (it == code_to_id_.end()) return;

    Lobby& lobby = lobbies_.at(it->second);

    if (lobby.host != context.socket_data->username) {
        ws::SendError(context.socket, context.op_code, "Only host can delete saved matches.", request_id);
        return;
    }

    auto& db = Database::Get();
    auto row = db.QueryOne("SELECT state_json FROM saved_matches WHERE id = ?", {match_id});
    
    if (!row || !row->has_value()) {
        ws::SendError(context.socket, context.op_code, "Match expired or not found.", request_id);
        return;
    }

    auto delete_status = db.Exec("DELETE FROM saved_matches WHERE id = ?", {match_id});
    if (!delete_status) {
        ws::SendError(context.socket, context.op_code, "Error while deleting.", request_id);
    }
    
    BroadcastUpdate(lobby);
    json response = ws::MakeResponse(ws::ServerAction::kSuccess, request_id);
    context.socket->send(response.dump(), context.op_code);
}

/**
 * @brief Restores a previously saved match state from the DB and broadcasts it to participants.
 * @param context Payload context wrapping request sockets and raw buffers.
 * @param message Structure containing the `match_id` targeting the saved state.
 */
void LobbyController::HandleResumeSavedMatch(WsContext context, const json& message) {
    const string& code = context.socket_data->lobby_code;
    const string request_id = ws::GetOr<string>(message, "request_id", "");
    auto payload_res = ws::ParsePayload<ws::LobbyResumeSavedMatchPayload>(message);

    if (!payload_res) {
        ws::SendError(context.socket, context.op_code, payload_res.error().message, request_id);
        return;
    }
    std::string match_id = payload_res->match_id;

    auto it = code_to_id_.find(code);
    if (it == code_to_id_.end()) return;

    Lobby& lobby = lobbies_.at(it->second);

    if (lobby.host != context.socket_data->username) {
        ws::SendError(context.socket, context.op_code, "Only host can resume games.", request_id);
        return;
    }

    auto& db = Database::Get();
    auto row = db.QueryOne("SELECT state_json FROM saved_matches WHERE id = ?", {match_id});
    
    if (!row || !row->has_value()) {
        ws::SendError(context.socket, context.op_code, "Match expired or not found.", request_id);
        return;
    }

    std::string state_json_str = row->value().Get<std::string>("state_json");
    lobby.match = std::make_unique<game::MatchInstance>(json::parse(state_json_str), lobby.settings);
    lobby.match->SetMatchId(match_id); 

    if (game_started_callback_) {
        game_started_callback_(&lobby);
    }

    Logger::Info("[Lobby] Match successfully resumed by host in lobby ", lobby.id);

    for (const auto& lobby_member : lobby.members) {
        if (!lobby_member.is_connected || !lobby_member.socket) continue;
        
        json response_payload = ws::MakeResponse(ws::ServerAction::kGameStateUpdated);
        response_payload["game_state"] = lobby.match->SerializePlayerState(lobby_member.username);
        lobby_member.socket->send(response_payload.dump(), uWS::OpCode::TEXT);
    }
}

/**
 * @brief Commits initialization arrays instantiating a fresh active MatchInstance environment.
 * @param context Payload context wrapping request sockets and raw buffers.
 * @param message Structured message parameter data from client frames.
 */
void LobbyController::HandleStartGame(WsContext context, const nlohmann::json& message) {
    const string& code       = context.socket_data->lobby_code;
    const string& request_id = ws::GetOr<string>(message, "request_id", "");

    auto it = code_to_id_.find(code);
    if (it == code_to_id_.end()) {
        ws::SendError(context.socket, context.op_code, "Lobby not found", request_id);
        return;
    }

    Lobby& lobby = lobbies_.at(it->second);

    if (lobby.host != context.socket_data->username) {
        ws::SendError(context.socket, context.op_code, "Only the host can start the game.", request_id);
        return;
    }

    if (lobby.match != nullptr) {
        ws::SendError(context.socket, context.op_code, "Match already started!", request_id);
        return;
    }

    if (lobby.members.size() < 2) {
        ws::SendError(context.socket, context.op_code, "Not enough players to start.", request_id);
        return;
    }

    std::vector<std::pair<std::string, bool>> players_info;
    for (const auto& lobby_member : lobby.members) {
        players_info.push_back({lobby_member.username, lobby_member.is_bot});
    }

    lobby.match = std::make_unique<game::MatchInstance>(players_info, lobby.settings);
    lobby.match->SetMatchId("match_" + GenerateInviteCode() + GenerateInviteCode()); 
    lobby.match->Start();

    if (game_started_callback_) {
        game_started_callback_(&lobby);
    }

    Logger::Info("[Lobby] Match started by host '", lobby.host, "' in lobby ", lobby.id);

    for (const auto& lobby_member : lobby.members) {
        if (!lobby_member.is_connected || !lobby_member.socket) {
            continue;
        }

        nlohmann::json response_payload = ws::MakeResponse(ws::ServerAction::kGameStateUpdated);
        response_payload["game_state"] = lobby.match->SerializePlayerState(lobby_member.username);
        lobby_member.socket->send(response_payload.dump(), uWS::OpCode::TEXT);
    }
}

/**
 * @brief Helper parsing cryptographic entropy arrays to output a random unique string identifier.
 * @return string Cryptographically randomized alphanumeric joining token.
 */
string LobbyController::GenerateInviteCode() {
    uint8_t raw[kCodeLen];
    if (RAND_bytes(raw, kCodeLen) != 1)
        throw runtime_error("[Lobby] RAND_bytes failed generating invite code");

    string code(kCodeLen, ' ');
    for (int i = 0; i < kCodeLen; ++i)
        code[i] = kCodeAlphabet[raw[i] % kAlphabetLen];
    return code;
}

/**
 * @brief Encodes the current room roster status flags into a compact JSON array template.
 * @param lobby The targeted room reference evaluated.
 * @return json The serialized list array.
 */
json LobbyController::MemberListJson(const Lobby& lobby) {
    json arr = json::array();
    for (const auto& m : lobby.members) {
        arr.push_back({
            {"username",  m.username},
            {"is_connected", m.is_connected},
            {"is_host",   m.username == lobby.host},
            {"is_bot", m.is_bot}
        });
    }
    return arr;
}

/**
 * @brief Blasts generalized configuration status changes down to all users currently subscribed to the room thread.
 * @param lobby Targeted state structure tracking information.
 */
void LobbyController::BroadcastUpdate(const Lobby& lobby) const {
    auto notification = MakeResponse(ws::ServerAction::kLobbyUpdated);
    notification["lobby"] = json{
        {"name", lobby.name},
        {"host", lobby.host},
        {"invite_code", lobby.invite_code},
        {"members", MemberListJson(lobby)},
        {"settings", lobby.settings}
    };

    app_.publish("lobby_" + lobby.invite_code, notification.dump(), uWS::OpCode::TEXT);
}

/**
 * @brief Removes an active member and handles hot-swapping logic or graceful database saving.
 * @param lobby_id Unique internal numerical tracker identifying the instance folder.
 * @param username String literal targeting unique tracking tag of the client entity.
 * @param explicit_leave True if the action represents an intentional exit command.
 * @param request_id Contextual callback tracker for matching active frontend promises.
 * @return true If the lobby survived the removal mutation.
 */
bool LobbyController::RemoveMember(uint32_t lobby_id, const string& username, bool explicit_leave, const string& request_id) {
    auto it = lobbies_.find(lobby_id);
    if (it == lobbies_.end()) return false;

    Lobby& lobby = it->second;

    auto member_it = std::ranges::find(lobby.members, username, &LobbyMember::username);
    if (member_it != lobby.members.end()) {
        if (member_it->is_connected && member_it->socket) {
            PerSocketData* sd = member_it->socket->getUserData();
            if (sd) sd->lobby_code.clear();

            string topic = "lobby_" + lobby.invite_code;
            member_it->socket->unsubscribe(topic);

            json response;
            if (explicit_leave) response = MakeResponse(ws::ServerAction::kLobbyLeft, request_id);
            else {
                response = MakeResponse(ws::ServerAction::kLobbyEvicted);
                response["reason"] = "Kicked by host";
            }
            member_it->socket->send(response.dump(), uWS::OpCode::TEXT);
        }

        if (lobby.match) {
            std::string old_name = member_it->username;
            bool was_their_turn = (lobby.match->GetCurrentPlayerUsername() == old_name);

            if (lobby.settings.quit_deletes_match) {
                Logger::Info("[Match] Human '", old_name, "' quit. Aborting and saving game.");
                SaveMatchStateToDB(lobby);
                
                json game_over_payload = ws::MakeResponse(ws::ServerAction::kGameOver);
                game_over_payload["winner"] = ""; 
                game_over_payload["reason"] = "A player left. The game state has been safely saved.";
                
                for (const auto& m : lobby.members) {
                    if (m.is_connected && m.socket && m.username != old_name) {
                        m.socket->send(game_over_payload.dump(), uWS::OpCode::TEXT);
                    }
                }
                
                lobby.match.reset();
                lobby.members.erase(member_it); 
            } else if (lobby.settings.allow_bot_replacement) {
                std::string new_bot_name = GetRandomBotName(lobby);

                member_it->username = new_bot_name;
                member_it->is_bot = true;
                member_it->is_connected = true;

                game::Player* engine_player = lobby.match->GetPlayer(old_name);
                if (engine_player) {
                    engine_player->username = new_bot_name;
                    engine_player->is_bot = true;
                }

                Logger::Info("[Match] Human '", old_name, "' evicted mid-game. Replaced by ", new_bot_name);

                if (was_their_turn && player_replaced_callback_) {
                    player_replaced_callback_(&lobby);
                }
            } else {
                lobby.match->RemovePlayerMidGame(old_name);
                lobby.members.erase(member_it);
                Logger::Info("[Match] Human '", old_name, "' evicted mid-game. Dropped from engine.");

                if (was_their_turn && player_replaced_callback_) {
                    player_replaced_callback_(&lobby);
                }
            }
        } else {
            lobby.members.erase(member_it);
        }

        CheckMatchIntegrity(lobby);
    }

    SaveMatchStateToDB(lobby);

    bool has_humans = std::ranges::any_of(lobby.members, [](const LobbyMember& m) {
        return !m.is_bot;
    });

    if (!has_humans) {
        code_to_id_.erase(lobby.invite_code);
        lobbies_.erase(it);
        Logger::Log("[Lobby] Destroyed lobby ", lobby_id, " (no humans left)");
        return false;
    }
    return true;
}

/**
 * @brief Scans active registries matching connected users back into their hosted environment pointer.
 * @param username Unique string tracking key mapping current entity identity.
 * @return Lobby* Pointer to target lobby framework, or nullptr on map miss.
 */
Lobby* LobbyController::FindLobbyForUser(const string& username) {
    for (auto& [id, lobby] : lobbies_) {
        if (std::ranges::contains(lobby.members, username, &LobbyMember::username)) {
            return &lobby;
        }
    }
    return nullptr;
}

/**
 * @brief Re-evaluates configuration limits, adding or purging bot instances until matching thresholds are synchronized.
 * @param lobby Active room reference requiring bot balancing.
 */
void LobbyController::SyncBots(Lobby& lobby) {
    if (lobby.match) return;
    int human_count = 0;
    int bot_count = 0;

    for (const auto& member : lobby.members) {
        if (member.is_bot) bot_count++;
        else human_count++;
    }

    int desired_bots = lobby.settings.bot_count;
    if (human_count + desired_bots > 4) {
        desired_bots = 4 - human_count;
    }

    while (bot_count < desired_bots) {
    static std::vector<size_t> available_indices;
    static size_t fallback_index = 0;
    static bool is_initialized = false;

    std::string bot_name = GetRandomBotName(lobby);

    LobbyMember bot{bot_name, nullptr, true, true};

    lobby.members.push_back(bot);
    bot_count++;
}

    while (bot_count > desired_bots) {
        for (auto it = lobby.members.rbegin(); it != lobby.members.rend(); ++it) {
            if (it->is_bot) {
                lobby.members.erase(std::next(it).base());
                bot_count--;
                break;
            }
        }
    }
}
