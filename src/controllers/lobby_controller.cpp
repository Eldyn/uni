/**
 * @file lobby_controller.cpp
 * @brief Implementation of the LobbyController class managing websocket signaling and session lifecycles for game rooms.
 */

#include "common/lobby.hpp"
#include "websocket_context.hpp"
#include <WebSocketProtocol.h>
#include <controllers/lobby_controller.hpp>
#include <common/env.hpp>
#include <common/ws.hpp>
#include <common/payloads.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/modload/asset_index.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/server/match_session.hpp>
#include <match/server/stats_gate.hpp>
#include <logger.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace std::chrono;

namespace {

/**
 * @brief True when a deck snapshot declares a non-empty card multiset.
 * @param deck Snapshot in the `LobbySettings.deck` shape.
 */
bool DeckSnapshotHasCards(const json& deck) {
    const auto it = deck.find("cards");
    return it != deck.end() && it->is_object() && !it->empty();
}

/**
 * @brief Converts a `LobbySettings.deck` snapshot into a `DeckDef`.
 *
 * The snapshot is written by `ApplyDeckSnapshot` and mirrors the
 * `decks/*.json` shape (`id`, `name`, `namespace`, `mods`, `cards`,
 * `settings`). Full ids are rebuilt as `namespace:id` so assembly can match
 * the kinds declared by the loaded mods.
 *
 * @param deck Snapshot in the `LobbySettings.deck` shape.
 * @return DeckDef The equivalent deck definition.
 */
match::modload::DeckDef DeckDefFromSnapshot(const json& deck) {
    match::modload::DeckDef def;
    def.raw = deck;
    def.id = deck.value("id", "");
    def.name = deck.value("name", "");
    def.namespace_id = deck.value("namespace", "");
    def.deck_id = def.namespace_id.empty()
                      ? def.id
                      : def.namespace_id + ":" + def.id;

    const auto mods = deck.find("mods");
    if (mods != deck.end() && mods->is_array()) {
        for (const auto& mod : *mods) {
            if (mod.is_string()) def.mods.push_back(mod.get<std::string>());
        }
    }

    const auto cards = deck.find("cards");
    if (cards != deck.end() && cards->is_object()) {
        for (auto card = cards->begin(); card != cards->end(); ++card) {
            if (card.value().is_number_integer()) {
                def.cards.emplace_back(card.key(),
                                       card.value().get<int>());
            }
        }
    }

    const auto settings = deck.find("settings");
    if (settings != deck.end() && settings->is_object()) {
        def.settings = *settings;
    }
    return def;
}

/**
 * @brief Synthesizes the classic-shaped `DeckDef` for a freestyle lobby.
 *
 * Freestyle lobbies carry no deck snapshot, so the pool is rebuilt from the
 * legacy scalar `count_*` fields (mirroring `MatchInstance::GenerateDeck`)
 * with the kind ids of `mods/vanilla/decks/classic.json`. Any `active_mods`
 * rule selection is appended after `vanilla` so the base cards stay present.
 *
 * @param settings Lobby settings holding the `count_*` tuning.
 * @return DeckDef The synthesized freestyle deck.
 */
match::modload::DeckDef SynthesizeFreestyleDeck(
    const LobbySettings& settings) {
    match::modload::DeckDef def;
    def.id = "classic";
    def.name = "Classic";
    def.namespace_id = "vanilla";
    def.deck_id = "vanilla:classic";
    def.mods.push_back("vanilla");
    for (const std::string& mod : settings.active_mods) {
        if (mod == "vanilla") continue;
        def.mods.push_back(mod);
    }

    for (const char* color : {"red", "blue", "green", "yellow"}) {
        const std::string prefix = std::string("vanilla:") + color + "_";
        def.cards.emplace_back(prefix + "0", settings.count_zeros);
        for (int number = 1; number <= 9; ++number) {
            def.cards.emplace_back(prefix + std::to_string(number),
                                   settings.count_numbered);
        }
        def.cards.emplace_back(prefix + "skip", settings.count_skips);
        def.cards.emplace_back(prefix + "reverse", settings.count_reverses);
        def.cards.emplace_back(prefix + "draw2", settings.count_draw_two);
    }
    def.cards.emplace_back("vanilla:wild", settings.count_wild);
    def.cards.emplace_back("vanilla:wild_draw4",
                           settings.count_wild_draw_four);
    return def;
}

/**
 * @brief Resolves the deck definition a lobby's settings describe.
 *
 * Mirrors `HandleStartGame`'s selection (deck snapshot when present, else the
 * synthesized freestyle deck) so the match-end persistence path evaluates the
 * stats gate on the same mod set / card multiset the match was
 * assembled from.
 *
 * @param settings Lobby settings to resolve.
 * @return DeckDef The resolved deck definition, always carrying a mod list.
 */
match::modload::DeckDef ResolveMatchDeck(const LobbySettings& settings) {
    match::modload::DeckDef def =
        DeckSnapshotHasCards(settings.deck)
            ? DeckDefFromSnapshot(settings.deck)
            : SynthesizeFreestyleDeck(settings);
    if (def.mods.empty()) def.mods.push_back("vanilla");
    return def;
}

}  // namespace

/**
 * @brief Constructs the LobbyController instance and establishes central inbound routing maps.
 * @param router    WebSocket action router.
 * @param broadcast Transport layer for sends/publishes.
 * @param timers    Timer service for the eviction clock.
 */
LobbyController::LobbyController(IActionRouter& router, IBroadcaster& broadcast,
                                  ITimerService& timers,
                                  PresenceRegistry& presence,
                                  HttpRouter* http_router,
                                  std::string mods_root)
    : action_router_(router), broadcaster_(broadcast), timer_service_(timers),
      presence_(presence),
      mods_root_(mods_root.empty() ? Env::Get("UNI_MODS_DIR", "mods")
                                   : std::move(mods_root)) {
    reconnect_grace_ms_ = std::max(1000, Env::GetInt("RECONNECT_GRACE_MS", 120'000));
    absolute_max_lobby_members_ = std::clamp(
        Env::GetInt("ABSOLUTE_MAX_LOBBY_MEMBERS", contract::kMaxLobbyMembers),
        2, contract::kMaxLobbyMembers);

    if (http_router != nullptr) {
        // INFO: Public catalog endpoint: deck files are content, not user
        //       data, so no auth token is required (mirrors the public
        //       leaderboard endpoint).
        http_router->Get("/api/decks", [this](AppResponse* res, AppRequest*) {
            HandleListDecks(res);
        });
        // INFO: author-facing verification log: every mod folder's
        //       report, including the ones that failed to load.
        http_router->Get("/api/mods", [this](AppResponse* res, AppRequest*) {
            HandleListMods(res);
        });
        // INFO: mod asset serving: resolved through the in-memory
        //       index by id + content hash; no path ever reaches the client.
        http_router->Get(
            "/assets/:mod/:bundle/:slot/:tier/:hash",
            [this](AppResponse* res, AppRequest* req) {
                HandleAsset(res, req);
            });
    }

    action_router_.On(ws::ClientAction::kLobbyCreate, [this](WsContext ctx, const json& msg) {
        HandleCreate(ctx, msg);
        return true;
    });

    action_router_.On(ws::ClientAction::kLobbyJoin, [this](WsContext ctx, const json& msg) {
        HandleJoin(ctx, msg);
        return true;
    });

    action_router_.On(ws::ClientAction::kLobbyQuickJoin,
                      [this](WsContext context, const nlohmann::json& message) {
        HandleQuickJoin(context, message);
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

    action_router_.On(ws::ClientAction::kMetadataRequest, [this](WsContext ctx, const json& msg) {
        HandleGetMetadata(ctx, msg);
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

    action_router_.On(ws::ClientAction::kLobbyUpdateSettings,
                      [this](WsContext ctx, const json& msg) {
        HandleUpdateSettings(ctx, msg);
        return true;
    });

    action_router_.On(ws::ClientAction::kLobbyStartMatch,
                      [this](WsContext context, const nlohmann::json& message) {
        HandleStartGame(context, message);
        return true;
    });

    action_router_.On(ws::ClientAction::kLobbyToggleReady,
                      [this](WsContext context, const nlohmann::json& message) {
        HandleToggleReady(context, message);
        return true;
    });

    action_router_.On(ws::ClientAction::kUserUpdatePrivacy,
                      [this](WsContext context, const nlohmann::json& message) {
        HandleUserUpdatePrivacy(context, message);
        return true;
    });

    timer_service_.Schedule("lobby_eviction", 1000, true, [this] {
        auto  now  = steady_clock::now();

        std::vector<std::pair<uint32_t, std::string>> to_evict;

        for (const auto& [id, lobby] : lobbies_) {
            for (const auto& username : lobby.CollectExpiredDisconnects(now, reconnect_grace_ms_))
                to_evict.push_back({id, username});
        }

        std::set<uint32_t> lobbies_to_update;

        for (const auto& [id, username] : to_evict) {
            Logger::Log("[Lobby] Grace expired. Evicting ", username, " from lobby ", id);

            bool lobby_survived = RemoveMember(id, username, false, "");

            if (lobby_survived) {
                auto it = lobbies_.find(id);
                if (it == lobbies_.end()) continue;
                Lobby& lobby = it->second;

                if (lobby.host == username) {
                    if (lobby.PromoteNextHost()) {
                        Logger::Log("[Lobby] Host auto-passed to ", lobby.host, " in lobby ", id);
                    }
                }

                lobby.SyncBots(rng_);
                lobbies_to_update.insert(id);
            } else {
                lobbies_to_update.erase(id);
            }
        }

        for (uint32_t id : lobbies_to_update) {
            auto it = lobbies_.find(id);
            if (it != lobbies_.end()) BroadcastUpdate(it->second);
        }
    });

    Logger::Info("[Lobby] Registered, grace window: " +
                 std::to_string(reconnect_grace_ms_ / 1000) + "s");
}

/**
 * @brief Destructor. The eviction timer is owned by the ITimerService and
 *        cancelled by its destructor, nothing to clean up here.
 */
LobbyController::~LobbyController() {}

std::size_t LobbyController::ActiveMatchCount() const {
    return static_cast<std::size_t>(std::ranges::count_if(lobbies_, [](const auto& entry) {
        const Lobby& lobby = entry.second;
        return lobby.session && !lobby.session->Engine().IsMatchOver();
    }));
}

/**
 * @brief Commits the structured snapshot of an ongoing match to the SQLite storage layer.
 * @param lobby Reference to the target active lobby containing the current game match.
 */
void LobbyController::SaveMatchStateToDB(Lobby& lobby) {
    if (!lobby.session || lobby.session->Engine().IsMatchOver()) return;

    json saved_state = lobby.session->Engine().ExportState();
    std::string json_payload = saved_state.dump();
    std::string match_id = lobby.match_id;
    if (match_id.empty()) return;

    try {
        auto& db = Database::Get();
        if (!db.IsOpen()) return;

        TransactionGuard tx(db);
        if (!tx.Ok()) {
            throw std::runtime_error(tx.GetError().message);
        }

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

        auto res_del = db.Exec("DELETE FROM saved_match_participants WHERE match_id = ?",
                               {match_id});
        if (!res_del) {
            throw std::runtime_error("Delete participants failed: " + res_del.error().message);
        }

        for (const auto& member : lobby.members) {
            if (!member.is_bot) {
                auto res_insert = db.Exec(
                    "INSERT INTO saved_match_participants (match_id, username) VALUES (?, ?)",
                    {match_id, member.username});
                if (!res_insert) {
                    throw std::runtime_error("Insert participant failed for " + member.username +
                                             ": " + res_insert.error().message);
                }
            }
        }

        if (auto commit_status = tx.Commit(); !commit_status) {
            throw std::runtime_error(commit_status.error().message);
        }
        Logger::Info("[Lobby] Safely upserted match state to DB: ", match_id);
    } catch (const std::exception& e) {
        Logger::Error("[Lobby DB Error] Failed to save state: ", e.what());
    }
}

/**
 * @brief Persists a completed match's rows for the new engine.
 *
 * Restores the pre-swap `RecordMatchCompleted` SQL shape: one `matches` row
 * (winner), one `match_participants` row per engine player, and per-human
 * `match_history` ledger rows carrying the finish placement. The per-player
 * `player_stats` aggregate is updated ONLY when the gate accepts the
 * lobby's mod set / deck kind multiset; the match rows are always written.
 *
 * @param lobby  Lobby whose live session just finished.
 * @param winner Winning username.
 */
void LobbyController::RecordMatchResult(Lobby& lobby,
                                        const std::string& winner) {
    if (!lobby.session) return;

    auto& db = Database::Get();
    if (!db.IsOpen()) return;

    // INFO: Gate only the player_stats aggregate; match rows and
    //       ledger placements are recorded for every match.
    const match::modload::DeckDef deck = ResolveMatchDeck(lobby.settings);
    const bool stats_allowed =
        match::server::StatsGateAllows(deck.mods, deck.cards);

    const json state = lobby.session->Engine().ExportState();
    const std::vector<std::string> placements =
        lobby.session->Engine().GetPlacements();

    try {
        TransactionGuard tx(db);
        if (!tx.Ok()) {
            Logger::Error("[Lobby DB Error] ", tx.GetError().message);
            return;
        }

        if (!lobby.match_id.empty()) {
            (void)db.Exec("DELETE FROM saved_matches WHERE id = ?",
                          {lobby.match_id});
        }

        int match_row_id = 0;
        auto match_status =
            db.Exec("INSERT INTO matches (winner_username) VALUES (?)",
                    {winner});
        if (match_status) {
            auto row = db.QueryOne("SELECT last_insert_rowid() as id", {});
            if (row && row->has_value()) {
                match_row_id = row->value().Get<int>("id");
            }
        } else {
            Logger::Warn("[Lobby DB Error] matches insert failed: ",
                         match_status.error().message);
        }

        for (const auto& player : state.value("players", json::array())) {
            const std::string username =
                player.value("username", std::string());
            if (username.empty()) continue;

            // INFO: pre-swap inserted a participant row for every engine
            //       player, bots included.
            if (match_row_id > 0) {
                (void)db.Exec(
                    "INSERT OR IGNORE INTO match_participants "
                    "(match_id, username) VALUES (?, ?)",
                    {match_row_id, username});
            }

            const bool is_bot = player.value("is_bot", false);
            if (is_bot) continue;

            if (!lobby.match_id.empty()) {
                auto placement_it =
                    std::ranges::find(placements, username);
                std::optional<int> placement;
                if (placement_it != placements.end()) {
                    placement = static_cast<int>(
                                    std::distance(placements.begin(),
                                                  placement_it)) +
                                1;
                }
                const std::string result =
                    (username == winner) ? "win" : "loss";
                (void)db.Exec(
                    "INSERT INTO match_history "
                    "(match_id, username, mode, placement, result, "
                    "ended_reason, ranked) VALUES (?, ?, ?, ?, ?, ?, ?)",
                    {lobby.match_id, username, lobby.settings.mode,
                     placement ? DbValue(*placement) : DbValue(nullptr),
                     result, "completed", stats_allowed ? 1 : 0});
            }

            if (!stats_allowed) continue;

            auto account = db.QueryOne(
                "SELECT 1 FROM users WHERE username = ?", {username});
            if (!account || !account->has_value()) continue;

            (void)db.Exec(
                "INSERT OR IGNORE INTO player_stats (username) VALUES (?)",
                {username});
            const int is_winner = (username == winner) ? 1 : 0;
            (void)db.Exec(R"(
                UPDATE player_stats SET
                    total_wins = total_wins + ?,
                    total_losses = total_losses + ?
                WHERE username = ?
            )", {is_winner, is_winner == 1 ? 0 : 1, username});
        }

        if (auto commit_status = tx.Commit(); !commit_status) {
            Logger::Error("[Lobby DB Error] ", commit_status.error().message);
        } else {
            Logger::Info("[Lobby] Persisted match result for ", lobby.match_id);
        }
    } catch (const std::exception& e) {
        Logger::Error("[Lobby DB Error] ", e.what());
    }
}

/**
 * @brief Persists an aborted match's per-human ledger rows.
 *
 * Mirrors the pre-swap `RecordMatchAborted`: no `matches` row (an abort has
 * no winner), only `match_history` rows marking each human as aborted. The
 * stats gate never applies to an abort.
 *
 * @param lobby Lobby whose live session is being torn down.
 */
void LobbyController::RecordMatchAborted(Lobby& lobby) {
    if (!lobby.session) return;

    auto& db = Database::Get();
    if (!db.IsOpen()) return;

    const json state = lobby.session->Engine().ExportState();

    try {
        TransactionGuard tx(db);
        if (!tx.Ok()) {
            Logger::Error("[Lobby DB Error] ", tx.GetError().message);
            return;
        }

        if (!lobby.match_id.empty()) {
            (void)db.Exec("DELETE FROM saved_matches WHERE id = ?",
                          {lobby.match_id});
        }

        for (const auto& player : state.value("players", json::array())) {
            const std::string username =
                player.value("username", std::string());
            if (username.empty()) continue;
            if (player.value("is_bot", false)) continue;
            if (lobby.match_id.empty()) continue;

            (void)db.Exec(
                "INSERT INTO match_history "
                "(match_id, username, mode, placement, result, "
                "ended_reason, ranked) VALUES (?, ?, ?, ?, ?, ?, ?)",
                {lobby.match_id, username, lobby.settings.mode,
                 DbValue(nullptr), "aborted", "aborted", 0});
        }

        if (auto commit_status = tx.Commit(); !commit_status) {
            Logger::Error("[Lobby DB Error] ", commit_status.error().message);
        }
    } catch (const std::exception& e) {
        Logger::Error("[Lobby DB Error] ", e.what());
    }
}

/**
 * @brief Tears down the match for the given lobby after a normal match-over.
 * @param lobby_id ID of the lobby whose match to destroy.
 */
void LobbyController::NotifyMatchOver(uint32_t lobby_id) {
    Lobby* lobby = GetLobbyById(lobby_id);
    if (!lobby) return;
    // INFO: persist before the session is released - winner / placements /
    //       player list all live on the engine.
    if (lobby->session) {
        RecordMatchResult(*lobby, lobby->session->Engine().GetWinner());
    }
    lobby->session.reset();
    lobby->match_id.clear();
    Logger::Info("[MATCH] destroyed after MatchOver in lobby ", lobby_id);
}

/**
 * @brief Validates remaining room configurations and cleans up abandoned empty environments.
 * @param lobby Reference to the checked targeted room instance.
 */
void LobbyController::CheckMatchIntegrity(Lobby& lobby) {
    if (lobby.session && lobby.members.size() < 2) {
        Logger::Info("[Lobby] Match aborted for lobby ", lobby.id, " due to disconnections.");

        // INFO: The engine never removes a mid-game seat, so an
        //       abort only tears the session down. A sole survivor is
        //       persisted as a completed match (mirroring the pre-swap
        //       `RecordMatchCompleted(winner)`); with no survivors left only
        //       the aborted ledger rows are recorded.
        if (lobby.members.size() == 1) {
            const std::string& winner = lobby.members.front().username;
            RecordMatchResult(lobby, winner);
            for (auto& cb : on_match_aborted_) cb(&lobby, winner);
        } else {
            RecordMatchAborted(lobby);
        }

        lobby.session.reset();
        lobby.match_id.clear();
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

            member.socket          = ws;
            member.is_connected    = true;
            member.disconnected_at = steady_clock::time_point{};
            sd->lobby_code         = lobby->invite_code;
            sd->lobby_id           = lobby->id;

            // INFO: a reconnect replaces the socket pointer; rebind the live
            //       session so later broadcasts do not target the dead socket.
            //       A spectator is not a session seat, so it rebinds through
            //       the viewer registry.
            if (lobby->session) {
                if (member.is_spectator) {
                    lobby->session->BindViewer(sd->username, ws);
                } else {
                    lobby->session->BindSocket(sd->username, ws);
                }
            }

            broadcaster_.Subscribe(ws, "lobby_" + lobby->invite_code);

            json resp = ws::MakeResponse(ws::ServerAction::kLobbyJoined);
            resp["lobby"] = json({
                {"invite_code", lobby->invite_code},
                {"host",        lobby->host},
                {"members",     MemberListJson(*lobby)},
                {"settings",    lobby->settings},
                {"name",        lobby->name}
            });
            broadcaster_.Send(ws, resp.dump(), uWS::OpCode::TEXT);

            SendMatchStateToSocket(*lobby, ws, sd->username, uWS::OpCode::TEXT);
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
    // INFO: Sweep every lobby for a member bound to this socket instead of
    //       trusting `sd->lobby_code`: the code can name a different lobby
    //       than the one holding the raw pointer, and a member left bound to a
    //       closed socket is sent to through freed memory. Ids are collected
    //       first because the seat-disconnect callbacks may touch `lobbies_`.
    std::vector<uint32_t> bound_lobby_ids;
    for (const auto& [lobby_id, lobby] : lobbies_) {
        for (const auto& member : lobby.members) {
            if (member.username == sd->username && member.socket == ws) {
                bound_lobby_ids.push_back(lobby_id);
                break;
            }
        }
    }

    for (const uint32_t lobby_id : bound_lobby_ids) {
        const auto lobby_it = lobbies_.find(lobby_id);
        if (lobby_it == lobbies_.end()) continue;
        Lobby& lobby = lobby_it->second;

        for (auto& member : lobby.members) {
            if (member.username != sd->username || member.socket != ws) continue;

            Logger::Log("[Lobby] Disconnect: ", sd->username, " in lobby ", lobby.id,
                        ", grace window open");
            member.is_connected    = false;
            member.socket          = nullptr;
            member.disconnected_at = steady_clock::now();
            // INFO: Nulling the member socket is not enough: the
            //       live session still holds the freed `AppWebSocket*` and
            //       would send through it (a disconnected seat is bot-driven
            //       within seconds). Unbind it from the session; a spectator
            //       drops its viewer stream instead.
            if (lobby.session) {
                if (member.is_spectator) {
                    lobby.session->UnbindViewer(sd->username);
                } else {
                    lobby.session->BindSocket(sd->username, nullptr);
                    // INFO: a departed human counts as loaded so the ready
                    //       barrier never waits on a socket that is gone
                    //.
                    if (lobby.session->MarkSeatReady(sd->username, broadcaster_)) {
                        for (auto& cb : on_match_seat_disconnected_) cb(&lobby);
                    }
                }
            }
            BroadcastUpdate(lobby);
            break;
        }
    }
}

/**
 * @brief Allocates structural memory maps for initializing a novel game instance channel.
 * @param ctx Payload context wrapping request sockets and raw buffers.
 * @param message Received raw JSON document mapping initialization preferences.
 */
void LobbyController::HandleCreate(WsContext ctx, const json& message) {
    const std::string& username = ctx.socket_data->username;
    const std::string request_id = ws::GetOr<std::string>(message, "request_id", "");
    auto payload_res = ws::ParsePayload<ws::LobbyCreatePayload>(message);
    if (!payload_res) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kInvalidPayload,
                               request_id, payload_res.error().message);
        return;
    }

    if (!ctx.socket_data->lobby_code.empty()) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kAlreadyInLobby,
                               request_id);
        return;
    }

    // INFO: The socket check above is per connection, but an account can hold
    //       several sockets. One lobby per username keeps the member/socket
    //       bindings and the username->lobby mapping consistent, and caps a
    //       single account's share of MAX_LOBBIES.
    if (UserInOtherLobby(username, 0)) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kAlreadyInLobby,
                               request_id);
        return;
    }

    const int max_lobbies = Env::GetInt("MAX_LOBBIES", 200);
    if (static_cast<int>(lobbies_.size()) >= max_lobbies) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kInternalError,
                               request_id);
        return;
    }

    uint32_t id = next_id_.fetch_add(1, std::memory_order_relaxed);

    Lobby built = Lobby::Create(id, username, ctx.socket,
        payload_res->is_public.value_or(false), payload_res->name.value_or(username + "'s lobby"),
        Env::GetInt("DEFAULT_TURN_TIME_MS", LobbySettings{}.turn_time_limit_ms),
    if (!match::modload::IsDevContentAllowed()) ApplyFirstDeck(lobby.settings);

        Env::GetInt("DEFAULT_STARTING_CARDS", LobbySettings{}.starting_cards),
        [this](const std::string& c) { return code_to_id_.count(c) > 0; },
        absolute_max_lobby_members_);

    Lobby& lobby = lobbies_.emplace(id, std::move(built)).first->second;

    code_to_id_[lobby.invite_code] = id;
    ctx.socket_data->lobby_code = lobby.invite_code;
    ctx.socket_data->lobby_id   = id;
    presence_.SetUserLobby(username, id);

    broadcaster_.Subscribe(ctx.socket, "lobby_" + lobby.invite_code);
    lobby.SyncBots(rng_);

    Logger::Log("[Lobby] Created lobby ", id, " code=", lobby.invite_code, " host=", username);

    auto resp = MakeResponse(ws::ServerAction::kLobbyJoined, request_id);
    resp["lobby"] = json{
        {"invite_code", lobby.invite_code},
        {"host", lobby.host},
        {"members", MemberListJson(lobby)},
        {"settings", lobby.settings},
        {"name", lobby.name}
    };
    broadcaster_.Send(ctx.socket, resp.dump(), ctx.op_code);
}

/**
 * @brief Attaches incoming network contexts to matching pre-configured room environments.
 * @param ctx Payload context wrapping request sockets and raw buffers.
 * @param message Received payload std::string mapping specific target identification tokens.
 */
void LobbyController::HandleJoin(WsContext ctx, const json& message) {
    const std::string request_id = ws::GetOr<std::string>(message, "request_id", "");
    auto payload_res = ws::ParsePayload<ws::LobbyJoinPayload>(message);

    if (!payload_res) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kInvalidPayload,
                               request_id, payload_res.error().message);
        return;
    }

    if (!ctx.socket_data->lobby_code.empty()) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kAlreadyInLobby,
                               request_id);
        return;
    }

    std::string code = payload_res->code;
    std::transform(code.begin(), code.end(), code.begin(), ::toupper);

    Lobby* lobby_ptr = GetLobbyByCode(code);
    if (!lobby_ptr) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kLobbyNotFound,
                               request_id);
        return;
    }
    Lobby& lobby = *lobby_ptr;
    const std::string& username = ctx.socket_data->username;

    if (std::ranges::contains(lobby.members, username, &LobbyMember::username)) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kAlreadyMember,
                               request_id);
        return;
    }

    if (UserInOtherLobby(username, lobby.id)) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kAlreadyInLobby,
                               request_id);
        return;
    }

    JoinResult result = lobby.AddOrHijack(username, ctx.socket);

    switch (result.outcome) {
        case JoinOutcome::kHijackedBot:
            Logger::Info("[Game] '", username, "' hijacked ", result.old_bot_name);
            break;
        case JoinOutcome::kJoinedEmptySlot:
            Logger::Info("[Lobby] '", username, "' joined an empty slot.");
            break;
        case JoinOutcome::kJoinedAsSpectator:
            Logger::Info("[Lobby] '", username, "' joined ongoing match as spectator.");
            break;
        case JoinOutcome::kLobbyFull:
            broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kLobbyFull,
                                   request_id);
            return;
    }

    ctx.socket_data->lobby_code = code;
    ctx.socket_data->lobby_id   = lobby.id;
    presence_.SetUserLobby(username, lobby.id);

    std::string topic = "lobby_" + code;
    broadcaster_.Subscribe(ctx.socket, topic);

    auto resp = MakeResponse(ws::ServerAction::kLobbyJoined, request_id);
    resp["lobby"] = json({
        {"invite_code", code},
        {"name",        lobby.name},
        {"host",        lobby.host},
        {"members",     MemberListJson(lobby)},
        {"settings",    lobby.settings}
    });
    broadcaster_.Send(ctx.socket, resp.dump(), ctx.op_code);

    BroadcastUpdate(lobby);

    if (lobby.session) {
        // INFO: A mid-match spectator becomes a session viewer
        //       so it keeps receiving live events after the join snapshot;
        //       seated joiners rebind through the normal socket path.
        if (result.outcome == JoinOutcome::kJoinedAsSpectator) {
            lobby.session->BindViewer(username, ctx.socket);
        }
        SendMatchStateToSocket(lobby, ctx.socket, username, ctx.op_code);
    }
}

/**
 * @brief Joins the caller to the fullest open public lobby with a free slot.
 * @param context Caller's socket/session context.
 * @param message Incoming lobby_quick_join payload.
 */
void LobbyController::HandleQuickJoin(WsContext context, const nlohmann::json& message) {
    const std::string request_id = ws::GetOr<std::string>(message, "request_id", "");

    Lobby* best = nullptr;
    for (auto& [id, lobby] : lobbies_) {
        if (!lobby.settings.is_public) continue;
        if (lobby.session != nullptr) continue;
        if (static_cast<int>(lobby.members.size()) >= lobby.settings.max_players) {
            // A lobby that's full only because bots occupy every seat is
            // still joinable when bot-takeover is enabled, the same
            // hijack condition Lobby::AddOrHijack checks.
            bool hijackable = lobby.settings.allow_bot_takeover &&
                std::ranges::any_of(lobby.members, &LobbyMember::is_bot);
            if (!hijackable) continue;
        }
        if (!best || lobby.members.size() > best->members.size()) best = &lobby;
    }

    if (!best) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kLobbyNotFound, request_id);
        return;
    }

    // Delegates to the same join path HandleJoin uses, rather than
    // duplicating it.
    HandleJoin(context, json({{"action", "lobby_join"}, {"request_id", request_id},
                              {"code", best->invite_code}}));
}

/**
 * @brief Resolves tracking references for re-hooking dropped sessions immediately without data loss.
 * @param ctx Payload context wrapping request sockets and raw buffers.
 * @param message Raw input structure indicating desired identity verification keys.
 */
void LobbyController::HandleRejoin(WsContext ctx, const json& message) {
    const std::string request_id = ws::GetOr<std::string>(message, "request_id", "");
    auto payload_res = ws::ParsePayload<ws::LobbyRejoinPayload>(message);

    if (!payload_res) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kInvalidPayload,
                               request_id, payload_res.error().message);
        return;
    }

    std::string code = payload_res->code;
    std::transform(code.begin(), code.end(), code.begin(), ::toupper);

    Lobby* lobby_ptr = GetLobbyByCode(code);
    if (!lobby_ptr) {
        auto resp = MakeResponse(ws::ServerAction::kLobbyEvicted, request_id);
        resp["reason"] = "Lobby expired";
        broadcaster_.Send(ctx.socket, resp.dump(), ctx.op_code);
        return;
    }
    Lobby& lobby = *lobby_ptr;
    const std::string& username = ctx.socket_data->username;

    // INFO: a rejoin only reattaches a seat the user already holds. Letting a
    //       socket that sits in lobby Y rebind lobby X overwrote its
    //       `lobby_code`, so OnClose never cleared Y's member and the session
    //       kept a freed `AppWebSocket*`.
    const bool socket_in_other_lobby = !ctx.socket_data->lobby_code.empty() &&
                                       ctx.socket_data->lobby_code != lobby.invite_code;
    if (socket_in_other_lobby || UserInOtherLobby(username, lobby.id)) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kAlreadyInLobby,
                               request_id);
        return;
    }

    if (std::ranges::contains(lobby.members, username, &LobbyMember::username)) {
        // INFO: a rejoin must rebind the member record and the socket's lobby
        //       metadata exactly like a fresh open
        //       (LobbyController::OnOpen). Rebinding only the live session
        //       leaves `member.socket` and `socket_data->lobby_code` stale, so
        //       OnClose (which keys off `lobby_code`) can never clear the old
        //       socket and the next match broadcast sends through a freed
        //       `AppWebSocket*`.
        for (auto& member : lobby.members) {
            if (member.username != username) continue;
            member.socket          = ctx.socket;
            member.is_connected    = true;
            member.disconnected_at = steady_clock::time_point{};
            break;
        }
        ctx.socket_data->lobby_code = lobby.invite_code;
        ctx.socket_data->lobby_id   = lobby.id;
        presence_.SetUserLobby(username, lobby.id);

        std::string topic = "lobby_" + lobby.invite_code;
        broadcaster_.Subscribe(ctx.socket, topic);

        // INFO: Re-register the reconnecting socket with the
        //       live session (spectator viewer vs seated recipient).
        if (lobby.session) {
            const LobbyMember* member = lobby.FindMember(username);
            if (member != nullptr && member->is_spectator) {
                lobby.session->BindViewer(username, ctx.socket);
            } else {
                lobby.session->BindSocket(username, ctx.socket);
            }
        }

        auto resp = MakeResponse(ws::ServerAction::kLobbyJoined, request_id);
        resp["lobby"] = json({
            {"invite_code", code},
            {"host",        lobby.host},
            {"members",     MemberListJson(lobby)},
            {"settings",    lobby.settings},
            {"name",        lobby.name}
        });

        broadcaster_.Send(ctx.socket, resp.dump(), ctx.op_code);

        SendMatchStateToSocket(lobby, ctx.socket, username, ctx.op_code);

        return;
    }

    broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kNotInLobby, request_id);
}

/**
 * @brief Cleanly unsubscribes channels when an explicit leave event is sent by a player.
 * @param ctx Payload context wrapping request sockets and raw buffers.
 * @param message JSON representation of the leave request payload.
 */
void LobbyController::HandleLeave(WsContext ctx, const json& message) {
    const std::string& username   = ctx.socket_data->username;
    const std::string& code       = ctx.socket_data->lobby_code;
    const std::string  request_id = ws::GetOr<std::string>(message, "request_id", "");

    if (code.empty()) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kNotInLobby,
                               request_id);
        return;
    }

    Lobby* lobby_ptr = GetLobbyByCode(code);
    if (!lobby_ptr) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kLobbyNotFound,
                               request_id);
        return;
    }

    uint32_t id  = lobby_ptr->id;
    bool was_host = (lobby_ptr->host == username);

    bool lobby_still_exists = RemoveMember(id, username, true, request_id, ctx.socket_data);

    if (lobby_still_exists) {
        Lobby* remaining = GetLobbyById(id);
        if (!remaining) return;
        remaining->SyncBots(rng_);
        if (was_host) {
            if (remaining->PromoteNextHost()) {
                Logger::Log("[Lobby] Host auto-passed to ", remaining->host, " in lobby ", id);
            }
        }
        BroadcastUpdate(*remaining);
    }
}

/**
 * @brief Returns a public directory of all active joinable lobbies.
 * @param ctx Payload context wrapping request sockets and raw buffers.
 * @param message JSON message block from the client requesting room states.
 */
void LobbyController::HandleList(WsContext ctx, const json& message) {
    json list = json::array();
    std::string request_id = ws::GetOr<std::string>(message, "request_id", "");

    for (const auto& [id, lobby] : lobbies_) {
        if (!lobby.settings.is_public) continue;
        bool any_connected = std::ranges::any_of(lobby.members, &LobbyMember::is_connected);

        int humans = std::ranges::count_if(lobby.members, [](const auto& member) {
            return !member.is_bot;
        });

        if (!any_connected) continue;

        std::string status = lobby.session != nullptr ? "in-game"
                       : humans >= lobby.settings.max_players ? "full"
                       : "open";

        list.push_back({
            {"name", lobby.name},
            {"member_count", humans},
            {"bot_count", lobby.members.size() - humans},
            {"max_players", lobby.settings.max_players},
            {"invite_code", lobby.invite_code},
            {"status", status},
            {"active_mods", lobby.settings.active_mods},
            {"allow_bot_takeover", lobby.settings.allow_bot_takeover}
        });
    }

    auto resp = MakeResponse(ws::ServerAction::kLobbyList, request_id);
    resp["lobbies"] = list;
    broadcaster_.Send(ctx.socket, resp.dump(), ctx.op_code);
}

/**
 * @brief Returns static server metadata (the rule catalog).
 * @param ctx Payload context wrapping request sockets and raw buffers.
 * @param message JSON message block from the client requesting metadata.
 */
void LobbyController::HandleGetMetadata(WsContext ctx, const json& message) {
    std::string request_id = ws::GetOr<std::string>(message, "request_id", "");

    // INFO: Swap: the catalog is now the mod-folder scan
    //       rather than the legacy in-process RuleRegistry. `vanilla` is the
    //       always-on base mod, so it is not offered as a toggleable rule.
    json available_rules = json::array();
    match::modload::LoadResult loaded =
        match::modload::ScanModsDirectory(mods_root_);
    if (loaded.fatal()) {
        Logger::Warn("[Lobby] Metadata: mods root unreadable for '", mods_root_,
                     "'");
    } else {
        for (const auto& mod : loaded.mods) {
            if (mod.manifest.id == "vanilla") continue;
            available_rules.push_back({
                {"id",          mod.manifest.id},
                {"label",       mod.manifest.name},
                {"description", mod.manifest.description}
            });
        }
    }

    auto resp = MakeResponse(ws::ServerAction::kMetadata, request_id);
    resp["available_rules"] = std::move(available_rules);
    broadcaster_.Send(ctx.socket, resp.dump(), ctx.op_code);
}

json LobbyController::DeckCatalogJson(
    const std::vector<match::modload::LoadedMod>& mods) {
    std::set<std::string> loaded_ids;
    for (const auto& mod : mods) loaded_ids.insert(mod.manifest.id);

    json decks = json::array();
    for (const auto& mod : mods) {
        for (const auto& deck : mod.decks) {
            // INFO: a deck is invalid when it requires a mod that is missing or
            //       failed validation; the client marks it and match
            //       start refuses it (fail closed at the match boundary).
            bool valid = true;
            for (const auto& required : deck.mods) {
                if (loaded_ids.count(required) == 0) {
                    valid = false;
                    break;
                }
            }
            decks.push_back({
                {"id",        deck.id},
                {"name",      deck.name},
                {"namespace", deck.namespace_id},
                {"mods",      deck.mods},
                {"valid",     valid}
            });
        }
    }
    return json{{"decks", decks}};
}

json LobbyController::ModsReportJson(
    const match::modload::LoadResult& loaded) {
    json mods = json::array();
    for (const auto& report : loaded.reports) {
        json errors = json::array();
        for (const auto& error : report.errors) {
            errors.push_back({{"check", error.check},
                              {"artifact", error.artifact},
                              {"path", error.path},
                              {"message", error.message}});
        }
        json warnings = json::array();
        for (const auto& warning : report.warnings) {
            warnings.push_back({{"check", warning.check},
                                {"artifact", warning.artifact},
                                {"path", warning.path},
                                {"message", warning.message}});
        }
        mods.push_back({
            {"id",          report.mod_id},
            {"folder",      report.folder},
            {"ok",          report.ok},
            {"errors",      std::move(errors)},
            {"warnings",    std::move(warnings)},
            {"asset_count", report.asset_count},
            {"asset_bytes", report.asset_bytes}
        });
    }
    return json{{"mods", mods}};
}

bool LobbyController::ApplyDeckSnapshot(
    LobbySettings& settings,
    const std::vector<match::modload::LoadedMod>& mods,
    const std::string& deck_id) {
    const match::modload::DeckDef* found = nullptr;
    for (const auto& mod : mods) {
        for (const auto& deck : mod.decks) {
            // INFO: accept both the full `namespace:id` form and the bare
            //       local id that GET /api/decks returns.
            if (deck.deck_id == deck_id || deck.id == deck_id) {
                found = &deck;
                break;
            }
        }
        if (found != nullptr) break;
    }
    if (found == nullptr) return false;

    json snapshot = json::object();
    snapshot["id"]        = found->id;
    snapshot["name"]      = found->name;
    snapshot["namespace"] = found->namespace_id;
    snapshot["mods"]      = found->mods;
    snapshot["cards"]     = json::object();
    for (const auto& [kind, count] : found->cards) {
        snapshot["cards"][kind] = count;
    }
    snapshot["settings"] = found->settings;
    settings.deck = std::move(snapshot);
    return true;
}

std::shared_ptr<const LobbyController::ModsSnapshot>
LobbyController::HttpModsSnapshot() {
    constexpr auto kTtl = std::chrono::seconds(5);
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> guard(mods_snapshot_mutex_);
    if (mods_snapshot_ && now - mods_snapshot_->built_at < kTtl) {
    auto index = match::modload::AssetIndex::Build(loaded);
    mods_snapshot_ = std::make_shared<const ModsSnapshot>(
        ModsSnapshot{std::move(loaded), std::move(index), now});
    return mods_snapshot_;
}

bool LobbyController::UserInOtherLobby(const std::string& username, uint32_t except_lobby_id) {
    const uint32_t lobby_id = presence_.GetUserLobbyId(username);
    if (lobby_id == 0) return false;

    const auto it = lobbies_.find(lobby_id);
    if (it == lobbies_.end() || it->second.FindMember(username) == nullptr) {
        // INFO: stale mapping (lobby gone or seat removed): drop it so the
        //       user is not locked out of creating or joining.
        presence_.ClearUserLobby(username);
        return false;
    }
    return lobby_id != except_lobby_id;
}

void LobbyController::ApplyFirstDeck(LobbySettings& settings) {
    match::modload::LoadResult loaded =
        match::modload::ScanModsDirectory(mods_root_);
    if (loaded.fatal()) return;
    for (const auto& mod : loaded.mods) {
        if (mod.decks.empty()) continue;
        ApplyDeckSnapshot(settings, loaded.mods, mod.decks.front().deck_id);
        return;
    }
}

void LobbyController::HandleListDecks(AppResponse* res) {
    const std::shared_ptr<const ModsSnapshot> snapshot = HttpModsSnapshot();
    const match::modload::LoadResult& loaded = snapshot->loaded;
        return mods_snapshot_;
    }
    // INFO: unauthenticated routes share one short-lived scan so request rate
    //       cannot drive filesystem parsing and validation.
    match::modload::LoadResult loaded =
        match::modload::ScanModsDirectory(mods_root_);
    if (loaded.fatal()) {
        Logger::Error("[Deck] Mods root unreadable for '", mods_root_, "'");
        res->writeStatus("500 Internal Server Error")
           ->writeHeader("Content-Type", "application/json")
           ->end(json{{"error", "mods scan failed"}}.dump());
        return;
    }

    res->writeHeader("Content-Type", "application/json")
       ->end(DeckCatalogJson(loaded.mods).dump());
}

void LobbyController::HandleListMods(AppResponse* res) {
    const std::shared_ptr<const ModsSnapshot> snapshot = HttpModsSnapshot();
    const match::modload::LoadResult& loaded = snapshot->loaded;
    if (loaded.fatal()) {
        Logger::Error("[Mods] Mods root unreadable for '", mods_root_, "'");
        res->writeStatus("500 Internal Server Error")
           ->writeHeader("Content-Type", "application/json")
           ->end(json{{"error", "mods scan failed"}}.dump());
        return;
    }

    res->writeHeader("Content-Type", "application/json")
       ->end(ModsReportJson(loaded).dump());
}

void LobbyController::HandleAsset(AppResponse* res, AppRequest* req) {
    const std::string mod_id(req->getParameter(0));
    const std::string bundle_id(req->getParameter(1));
    const std::string slot(req->getParameter(2));
    const std::string tier(req->getParameter(3));
    const std::string hash(req->getParameter(4));

    const std::shared_ptr<const ModsSnapshot> snapshot = HttpModsSnapshot();
    const match::modload::LoadResult& loaded = snapshot->loaded;
    if (loaded.fatal()) {
        res->writeStatus("500 Internal Server Error")
           ->writeHeader("Content-Type", "application/json")
           ->end(json{{"error", "mods scan failed"}}.dump());
        return;
    }

    // INFO: only indexed id+hash tuples can ever resolve, so no path is
    //       reachable from the URL.
    const match::modload::AssetIndex& index = snapshot->index;
    const match::modload::AssetEntry* entry =
        index.Resolve(mod_id, bundle_id, slot, tier, hash);
    if (entry == nullptr || entry->content_type.empty()) {
        res->writeStatus("404 Not Found")->end();
        return;
    }

    std::ifstream in(entry->path, std::ios::binary);
    if (!in.is_open()) {
        res->writeStatus("404 Not Found")->end();
        return;
    }
    const std::string bytes((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    // INFO: recompute and verify; a changed asset is a new URL, so a mismatch
    //       means the id/hash pair is stale or forged.
    if (match::modload::ShortContentHash(bytes) != entry->hash) {
        res->writeStatus("404 Not Found")->end();
        return;
    }

    res->writeStatus("200 OK")
       ->writeHeader("Content-Type", entry->content_type)
       ->writeHeader("Cache-Control", "public, max-age=31536000, immutable")
       ->writeHeader("ETag", "\"" + entry->hash + "\"")
       ->end(bytes);
}

/**
 * @brief Transfers room ownership permissions to another human user in the lobby.
 * @param ctx Payload context wrapping request sockets and raw buffers.
 * @param message Payload specifying targeted user label parameters.
 */
void LobbyController::HandlePromote(WsContext ctx, const json& message) {
    const std::string& code       = ctx.socket_data->lobby_code;
    const std::string& request_id = ws::GetOr<std::string>(message, "request_id", "");
    auto payload_res = ws::ParsePayload<ws::LobbyPromotePayload>(message);

    if (!payload_res) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kInvalidPayload,
                               request_id);
        return;
    }

    const std::string& username = payload_res->username;

    Lobby* lobby_ptr = GetLobbyByCode(code);
    if (!lobby_ptr) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kLobbyNotFound,
                               request_id);
        return;
    }
    Lobby& lobby = *lobby_ptr;

    if (ctx.socket_data->username != lobby.host) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kNotHost, request_id);
        return;
    }

    auto target_it = std::ranges::find(lobby.members, username, &LobbyMember::username);

    if (target_it == lobby.members.end()) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kUserNotInLobby,
                               request_id);
        return;
    }

    if (target_it->is_bot) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kCannotPromoteBot,
                               request_id);
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
    const std::string& code       = ctx.socket_data->lobby_code;
    const std::string& request_id = ws::GetOr<std::string>(message, "request_id", "");
    auto payload_res = ws::ParsePayload<ws::LobbyKickPayload>(message);

    if (!payload_res) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kInvalidPayload,
                               request_id);
        return;
    }

    const std::string& username = payload_res->username;

    Lobby* lobby_ptr = GetLobbyByCode(code);
    if (!lobby_ptr) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kLobbyNotFound,
                               request_id);
        return;
    }
    Lobby& lobby = *lobby_ptr;

    if (ctx.socket_data->username != lobby.host) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kNotHost, request_id);
        return;
    }

    if (username == lobby.host) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kCannotKickSelf,
                               request_id);
        return;
    }

    auto target_it = std::ranges::find(lobby.members, username, &LobbyMember::username);

    if (target_it == lobby.members.end()) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kUserNotInLobby,
                               request_id);
        return;
    }

    const bool was_bot = target_it->is_bot;

    uint32_t lobby_id = lobby.id;
    bool lobby_still_exists = RemoveMember(lobby_id, username, false, request_id);


    if (lobby_still_exists) {
        Lobby* remaining = GetLobbyById(lobby_id);
        if (!remaining) return;
        // The targeted removal is authoritative: re-derive bot_count from the
        // members that actually remain so drift (e.g. a prior bot takeover)
        // self-heals and LIFO reconciliation can no longer drop a different bot.
        if (was_bot) {
            remaining->settings.bot_count = static_cast<int>(
                std::ranges::count_if(remaining->members, [](const LobbyMember& member) {
                    return member.is_bot;
                }));
        }
        remaining->SyncBots(rng_);
        broadcaster_.SendSuccess(ctx.socket, ctx.op_code, request_id);
        BroadcastUpdate(*remaining);
    }
}

/**
 * @brief Parses room modification payloads and adjusts internal structures on mutations.
 * @param ctx Payload context wrapping request sockets and raw buffers.
 * @param message Document declaration holding updated setting keys.
 */
void LobbyController::HandleUpdateSettings(WsContext ctx, const json& message) {
    const std::string& code       = ctx.socket_data->lobby_code;
    const std::string& request_id = ws::GetOr<std::string>(message, "request_id", "");

    Lobby* lobby_ptr = GetLobbyByCode(code);
    if (!lobby_ptr) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kLobbyNotFound,
                               request_id);
        return;
    }
    Lobby& lobby = *lobby_ptr;

    if (ctx.socket_data->username != lobby.host) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kNotHost, request_id);
    const bool freestyle_allowed = match::modload::IsDevContentAllowed();
        return;
    }

            if (!freestyle_allowed) {
                broadcaster_.SendError(
                    ctx.socket, ctx.op_code,
                    contract::ErrorCode::kInvalidPayload, request_id,
                    "a deck is required");
                return;
            }
    // INFO: Every change is built on copies and committed at the end, so a
    //       frame that fails validation or deserialization leaves the lobby
    //       untouched instead of half-applied.
    std::optional<std::string> new_name;
    if (message.contains("name")) {
        const json& name_value = message["name"];
        if (!name_value.is_string() || name_value.get_ref<const std::string&>().empty() ||
            name_value.get_ref<const std::string&>().size() >
                static_cast<size_t>(contract::kLobbyNameMax)) {
            broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kInvalidPayload,
                                   request_id, "invalid lobby name");
            return;
        }
        new_name = name_value.get<std::string>();
    }

    LobbySettings new_settings = lobby.settings;

    // INFO: A `deck_id` selects a whole snapshot at once (mods + cards +
    //       settings). Resolve it against the mods folder before the generic
    //       merge; an empty string clears the selection (freestyle, dev only).
    if (message.contains("deck_id")) {
        if (!message["deck_id"].is_string()) {
            broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kInvalidPayload,
                                   request_id, "invalid deck_id");
            return;
        }
        const std::string deck_id = message["deck_id"].get<std::string>();
        if (deck_id.empty()) {
            if (!match::modload::IsDevContentAllowed()) {
                broadcaster_.SendError(
                    ctx.socket, ctx.op_code,
                    contract::ErrorCode::kInvalidPayload, request_id,
                    "a deck is required");
                return;
            }
            new_settings.deck = json::object();
        } else {
            const std::shared_ptr<const ModsSnapshot> snapshot = HttpModsSnapshot();
            if (snapshot->loaded.fatal()) {
                broadcaster_.SendError(
                    ctx.socket, ctx.op_code,
                    contract::ErrorCode::kInternalError, request_id);
                return;
            }
            if (!ApplyDeckSnapshot(new_settings, snapshot->loaded.mods, deck_id)) {
                broadcaster_.SendError(
                    ctx.socket, ctx.op_code,
                    contract::ErrorCode::kInvalidPayload, request_id,
                    "unknown deck: " + deck_id);
                return;
            }
        }
    }

    const int old_bot_count = lobby.settings.bot_count;
    const int old_max_players = lobby.settings.max_players;

    // INFO: Strip envelope fields then apply the patch. Fields not present
    //       in the message are left unchanged; unknown fields are ignored by
    //       nlohmann when deserializing back into LobbySettings, so the
    //       struct is always correct.
    json patch = message;
    patch.erase("action");
    patch.erase("request_id");
    patch.erase("deck_id");
    // INFO: the deck snapshot is server-authored (see `deck_id` above); a raw
    //       `deck` patch would bypass the catalogue and the prod freestyle gate.
    patch.erase("deck");
    patch.erase("name");  // name lives on Lobby, not LobbySettings

    json current_settings = new_settings;
    current_settings.merge_patch(patch);
    try {
        new_settings = current_settings.get<LobbySettings>();
    } catch (const json::exception&) {
        broadcaster_.SendError(ctx.socket, ctx.op_code, contract::ErrorCode::kInvalidPayload,
                               request_id, "invalid settings");
        return;
    }
    new_settings.Sanitize(absolute_max_lobby_members_);

    lobby.settings = std::move(new_settings);
    if (new_name) lobby.name = std::move(*new_name);

    if (old_bot_count != lobby.settings.bot_count) {
        lobby.SyncBots(rng_);
    }

    broadcaster_.SendSuccess(ctx.socket, uWS::OpCode::TEXT, request_id);
    BroadcastUpdate(lobby);
}

/**
 * @brief Flips the caller's own ready state and broadcasts the updated lobby.
 * @param context Caller's socket/session context.
 * @param message Incoming lobby_toggle_ready payload.
 */
void LobbyController::HandleToggleReady(WsContext context, const nlohmann::json& message) {
    const std::string& code       = context.socket_data->lobby_code;
    const std::string& request_id = ws::GetOr<std::string>(message, "request_id", "");

    Lobby* lobby_ptr = GetLobbyByCode(code);
    if (!lobby_ptr) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kLobbyNotFound, request_id);
        return;
    }
    Lobby& lobby = *lobby_ptr;

    for (auto& member : lobby.members) {
        if (member.username == context.socket_data->username) {
            member.is_ready = !member.is_ready;
            break;
        }
    }

    BroadcastUpdate(lobby);
    broadcaster_.SendSuccess(context.socket, context.op_code, request_id);
}

/**
 * @brief Commits initialization arrays instantiating a fresh active MatchInstance environment.
 * @param context Payload context wrapping request sockets and raw buffers.
 * @param message Structured message parameter data from client frames.
 */
void LobbyController::HandleStartGame(WsContext context, const nlohmann::json& message) {
    const std::string& code       = context.socket_data->lobby_code;
    const std::string& request_id = ws::GetOr<std::string>(message, "request_id", "");

    Lobby* lobby_ptr = GetLobbyByCode(code);
    if (!lobby_ptr) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kLobbyNotFound, request_id);
        return;
    }
    Lobby& lobby = *lobby_ptr;

    if (lobby.host != context.socket_data->username) {
        broadcaster_.SendError(context.socket, context.op_code, contract::ErrorCode::kNotHost,
                               request_id);
        return;
    }

    if (lobby.session != nullptr) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kMatchAlreadyStarted, request_id);
        return;
    }

    if (lobby.members.size() < 2) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kNotEnoughPlayers, request_id);
        return;
    }

    const bool everyone_ready = std::ranges::all_of(lobby.members, [](const LobbyMember& m) {
        return m.is_bot || m.is_ready;
    });
    if (!everyone_ready) {
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kNotEnoughReady, request_id);
        return;
    }

    // Clears the is_spectator flag left over from a previous match's elimination
    // (match_controller.cpp sets it on knockout). Only seated members are reset;
    // voluntary spectators (seat_index == -1, joined mid-match) stay spectators.
    for (auto& lobby_member : lobby.members) {
        if (lobby_member.seat_index != -1) {
            lobby_member.is_spectator = false;
        }
    }

    // Tell clients the flags changed. The match-state broadcast alone cannot,
    // since is_spectator lives only on the lobby member; without this a client
    // caches is_spectator=true from the previous match and stays pinned in
    // spectator view even after being seated in the new one.
    BroadcastUpdate(lobby);

    // INFO: Swap: the assembled `match::server::MatchSession` is the sole
    //       match owner. A scan or assembly failure aborts the start (there is
    //       no legacy fallback) and is reported as an internal error.
    lobby.session.reset();

    match::modload::LoadResult loaded =
        match::modload::ScanModsDirectory(mods_root_);
    if (loaded.fatal()) {
        Logger::Error("[Lobby] Start failed: mods root unreadable for '",
                      mods_root_, "'");
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kInternalError, request_id);
        return;
    }

    match::modload::DeckDef deck_def =
        DeckSnapshotHasCards(lobby.settings.deck)
            ? DeckDefFromSnapshot(lobby.settings.deck)
            : SynthesizeFreestyleDeck(lobby.settings);
    if (deck_def.mods.empty()) deck_def.mods.push_back("vanilla");

    // INFO: fail closed at the match boundary: every mod the deck
    //       requires must be present and valid, otherwise a mod that failed
    //       validation would be silently dropped from the match.
    for (const auto& required : deck_def.mods) {
        bool present = false;
        for (const auto& mod : loaded.mods) {
            if (mod.manifest.id == required) {
                present = true;
                break;
            }
        }
        if (!present) {
            Logger::Error("[Lobby] Start failed: deck requires unavailable mod '",
                          required, "'");
            broadcaster_.SendError(context.socket, context.op_code,
                                   contract::ErrorCode::kInvalidPayload,
                                   request_id,
                                   "deck requires unavailable mod: " + required);
            return;
        }
    }

    std::vector<match::modload::LoadedMod> active_mods;
    for (const auto& mod : loaded.mods) {
        if (std::ranges::find(deck_def.mods, mod.manifest.id)
            != deck_def.mods.end()) {
            active_mods.push_back(mod);
        }
    }

    match::engine::MatchAssemblyOptions options;
    options.starting_cards = lobby.settings.starting_cards;

    std::vector<const LobbyMember*> seated;
    for (const auto& member : lobby.members) {
        if (member.seat_index >= 0) seated.push_back(&member);
    }
    std::ranges::sort(seated, {}, &LobbyMember::seat_index);
    for (const LobbyMember* member : seated) {
        options.players.push_back({member->username, member->is_bot,
                                   member->is_connected,
                                   member->is_ready});
    }

    match::engine::AssemblyResult assembly =
        match::engine::MatchAssembler::Assemble(active_mods, deck_def,
                                                options);
    if (!assembly.ok()) {
        Logger::Error("[Lobby] Start failed: assembly error (",
                      assembly.error->check, "): ", assembly.error->message);
        broadcaster_.SendError(context.socket, context.op_code,
                               contract::ErrorCode::kInternalError, request_id);
        return;
    }

    auto engine = std::make_unique<match::engine::MatchInstance>(
        std::move(assembly.assembly));

    match::server::MatchSession::SocketMap sockets;
    for (const LobbyMember* member : seated) {
        if (!member->is_connected || member->socket == nullptr) {
            continue;
        }
        sockets[member->username] = member->socket;
    }

    lobby.session = std::make_unique<match::server::MatchSession>(
        std::move(engine), std::move(active_mods), std::move(sockets));
    lobby.match_id =
        "match_" + Lobby::GenerateInviteCode() + Lobby::GenerateInviteCode();

    for (auto& cb : on_game_started_) cb(&lobby);

    Logger::Info("[Lobby] Match started by host '", lobby.host, "' in lobby ", lobby.id);

    lobby.session->EmitEvents(broadcaster_);
    lobby.session->BroadcastSnapshot(broadcaster_);

    broadcaster_.SendSuccess(context.socket, uWS::OpCode::TEXT, request_id);
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
            {"is_bot", m.is_bot},
            {"seat_index", m.seat_index},
            {"is_ready", m.is_bot || m.is_ready},
            {"is_spectator", m.is_spectator},
            {"privacy_mode", m.privacy_mode}
        });
    }
    return arr;
}

/**
 * @brief Blasts generalized configuration status changes down to all users currently subscribed to the room thread.
 * @param lobby Targeted state structure tracking information.
 */
void LobbyController::SendMatchStateToSocket(const Lobby& lobby, AppWebSocket* ws,
                                              const std::string& username,
                                              uWS::OpCode op_code) const {
    if (!lobby.session) return;
    // INFO: the view snapshot owns per-recipient filtering and the pending
    //       prompt; `op_code` is retained for signature parity but the
    //       transport wrapper always emits TEXT.
    (void)op_code;

    const LobbyMember* member = lobby.FindMember(username);
    const bool is_spectator = (member != nullptr) && member->is_spectator;
    lobby.session->SendSnapshot(broadcaster_, ws, username, is_spectator);
}

void LobbyController::BroadcastUpdate(const Lobby& lobby) const {
    auto notification = MakeResponse(ws::ServerAction::kLobbyUpdated);
    notification["lobby"] = json{
        {"name", lobby.name},
        {"host", lobby.host},
        {"invite_code", lobby.invite_code},
        {"members", MemberListJson(lobby)},
        {"settings", lobby.settings}
    };

    broadcaster_.Publish("lobby_" + lobby.invite_code, notification.dump(), uWS::OpCode::TEXT);
}

/**
 * @brief Removes an active member and handles hot-swapping logic or graceful database saving.
 * @param lobby_id Unique internal numerical tracker identifying the instance folder.
 * @param username String literal targeting unique tracking tag of the client entity.
 * @param explicit_leave True if the action represents an intentional exit command.
 * @param request_id Contextual callback tracker for matching active frontend promises.
 * @return true If the lobby survived the removal mutation.
 */
bool LobbyController::RemoveMember(uint32_t lobby_id, const std::string& username,
                                   bool explicit_leave, const std::string& request_id,
                                   PerSocketData* self_sd) {
    auto it = lobbies_.find(lobby_id);
    if (it == lobbies_.end()) return false;

    Lobby& lobby = it->second;

    MemberRemovalResult result = lobby.RemoveMember(username, rng_);

    if (result.found) {
        presence_.ClearUserLobby(username);

        if (result.was_connected && result.socket) {
            PerSocketData* sd = self_sd ? self_sd : result.socket->getUserData();
            if (sd) {
                sd->lobby_code.clear();
                sd->lobby_id = 0;
            }

            std::string topic = "lobby_" + lobby.invite_code;
            broadcaster_.Unsubscribe(result.socket, topic);

            json response;
            if (explicit_leave) {
                response = MakeResponse(ws::ServerAction::kLobbyLeft, request_id);
            } else {
                response = MakeResponse(ws::ServerAction::kLobbyEvicted);
                response["reason"] = "Kicked by host";
            }
            broadcaster_.Send(result.socket, response.dump(), uWS::OpCode::TEXT);
        }

        // INFO: an explicit leave / kick during the loading barrier counts the
        //       seat as loaded so the survivors do not wait out the `ready_`
        //       timeout. The bot-replacement path has
        //       already renamed and cleared the pending entry, so drive the
        //       hook off the barrier's completion, not MarkSeatReady's return.
        //       The abort path tears the session down instead.
        if (lobby.session &&
            result.match_outcome != MemberRemovalOutcome::kMatchAborted) {
            lobby.session->MarkSeatReady(result.old_username, broadcaster_);
            if (lobby.session->ReadyBarrierComplete()) {
                for (auto& cb : on_match_seat_disconnected_) cb(&lobby);
            }
        }

        switch (result.match_outcome) {
            case MemberRemovalOutcome::kMatchAborted: {
                Logger::Info("[Match] Human '", result.old_username,
                             "' quit. Aborting match.");

                json game_over_payload = ws::MakeResponse(ws::ServerAction::kMatchOver);
                game_over_payload["winner"] = "";
                game_over_payload["reason"] =
                    "A player left. Match aborted.";

                for (const auto& m : lobby.members) {
                    if (m.is_connected && m.socket && m.username != result.old_username) {
                        broadcaster_.Send(m.socket, game_over_payload.dump(), uWS::OpCode::TEXT);
                    }
                }

                // INFO: Persist the abort before the session is
                //       released (no winner: ledger rows only).
                RecordMatchAborted(lobby);
                lobby.session.reset();
                lobby.match_id.clear();
                break;
            }
            case MemberRemovalOutcome::kPlayerReplacedByBot:
                Logger::Info("[Match] Human '", result.old_username,
                             "' evicted mid-game. Replaced by ", result.new_bot_name);
                if (result.was_their_turn) for (auto& cb : on_player_replaced_) cb(&lobby);
                break;
            case MemberRemovalOutcome::kPlayerDroppedFromEngine:
                Logger::Info("[Match] Human '", result.old_username,
                             "' evicted mid-game. Dropped from engine.");
                if (result.was_their_turn) for (auto& cb : on_player_replaced_) cb(&lobby);
                break;
            case MemberRemovalOutcome::kMatchUnaffected:
                break;
        }

        CheckMatchIntegrity(lobby);
    }

    bool has_humans = std::ranges::any_of(lobby.members, [](const LobbyMember& m) {
        return !m.is_bot;
    });

    if (!has_humans) {
        const std::string invite_code = lobby.invite_code;
        code_to_id_.erase(invite_code);
        lobbies_.erase(it);
        for (auto& cb : on_lobby_destroyed_) cb(invite_code);
        Logger::Log("[Lobby] Destroyed lobby ", lobby_id, " (no humans left)");
        return false;
    }
    return true;
}

/**
 * @brief Scans active registries matching connected users back into their hosted environment pointer.
 * @param username Unique std::string tracking key mapping current entity identity.
 * @return Lobby* Pointer to target lobby framework, or nullptr on map miss.
 */
Lobby* LobbyController::FindLobbyForUser(const std::string& username) {
    uint32_t lobby_id = presence_.GetUserLobbyId(username);
    if (lobby_id == 0) return nullptr;
    return GetLobbyById(lobby_id);
}

void LobbyController::HandleUserUpdatePrivacy(WsContext context, const nlohmann::json& message) {
    bool privacy = ws::GetOr<bool>(message, "privacy_mode", false);
    if (context.socket_data) {
        context.socket_data->privacy_mode = privacy;
    }
    Lobby* lobby = GetLobbyById(context.socket_data->lobby_id);
    if (lobby) {
        LobbyMember* member = lobby->FindMember(context.socket_data->username);
        if (member) {
            member->privacy_mode = privacy;
            BroadcastUpdate(*lobby);
        }
    }
}
