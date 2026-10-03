#include <match/server/match_session.hpp>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/view/defs_builder.hpp>
#include <match/view/view_util.hpp>

#include <logger.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

/**
 * @file match_session.cpp
 * @brief `match::server::MatchSession` implementation.
 *
 * Additive server seam: engine ownership, per-recipient `EventSink` streams,
 * wire `CompactCardV2.bits` -> entity resolution, prompt-response schema
 * validation and the `match_event` / `match_state_updated` / `match_over`
 * packets. No controller or lobby wiring lives here.
 */

namespace match::server {

namespace {

using nlohmann::json;

/** @brief Resolve a wire `CompactCardV2.bits` to an assembled card entity. */
std::optional<match::ecs::Entity> CardForBits(
    const match::engine::MatchInstance& engine, uint32_t card_bits) {
    match::ecs::CompactCardV2 id;
    id.bits = card_bits;
    return engine.Registries().CardEntity(id);
}

}  // namespace

MatchSession::MatchSession(std::unique_ptr<match::engine::MatchInstance> engine,
                           std::vector<match::modload::LoadedMod> mods,
                           SocketMap sockets)
    : engine_(std::move(engine)),
      mods_(std::move(mods)),
      builder_(*engine_, mods_),
      sockets_(std::move(sockets)) {
    // INFO: pre-create one sink (and prompt-dedupe) entry per recipient so the
    //       emit loops never insert into the maps while iterating them.
    for (const auto& [username, socket] : sockets_) {
        (void)socket;
        sinks_.try_emplace(username);
        prompt_signature_.try_emplace(username);
        // INFO: a prompt that never reaches `SubmitInput` closes as cancelled
        //       (timeout / abort); an accepted answer overwrites this.
        prompt_outcome_.try_emplace(username, "cancelled");
    }
    BuildPromptSchemas();
}

bool MatchSession::PlayCard(const std::string& username, uint32_t card_bits) {
    const std::optional<match::ecs::Entity> card =
        CardForBits(*engine_, card_bits);
    if (!card.has_value()) return false;
    return engine_->PlayCard(username, *card);
}

bool MatchSession::DrawCard(const std::string& username) {
    return engine_->DrawCard(username);
}

bool MatchSession::KeepDrawn(const std::string& username) {
    return engine_->KeepDrawn(username);
}

bool MatchSession::SubmitInput(const std::string& username,
                               const std::string& prompt_id,
                               const nlohmann::json& value) {
    const std::optional<json> pending = engine_->PendingInput();
    if (!pending.has_value() || !pending->is_object()) return false;

    const std::string kind = pending->value("kind", std::string());
    if (kind.empty() || prompt_id != kind) return false;

    // INFO: the prompt is addressed to its target only; refuse an actor that
    //       is not that player before any schema work.
    const std::string target =
        match::view::ResolvePlayer(*engine_, pending->value("target", json()));
    if (!target.empty() && target != username) return false;

    const json* schema = FindPromptSchema(kind);
    if (schema != nullptr && !MatchesSchema(value, *schema)) return false;

    // INFO: S-4 - the parked prompt may carry a more specific engine-authored
    //       schema (e.g. `choose_card`'s `enum` of the offered bits). Enforce
    //       it too, so the session validator is self-describing and rejects an
    //       answer outside the offered subset before the engine is consulted.
    if (pending->contains("payload") && (*pending)["payload"].is_object()) {
        const json& payload = (*pending)["payload"];
        const auto it = payload.find("response_schema");
        if (it != payload.end() && it->is_object() && !it->empty()
            && !MatchesSchema(value, *it)) {
            return false;
        }
    }

    if (!engine_->SubmitInput(username, value)) return false;
    // INFO: the target answered, so its parked prompt now closes `answered`;
    //       EmitPendingPrompt turns this into the prompt_close.
    prompt_outcome_[username] = "answered";
    return true;
}

bool MatchSession::RespondWindow(const std::string& username,
                                 uint32_t card_bits) {
    const std::optional<match::ecs::Entity> card =
        CardForBits(*engine_, card_bits);
    if (!card.has_value()) return false;
    return engine_->RespondWindow(username, *card);
}

bool MatchSession::PassWindow(const std::string& username) {
    return engine_->PassWindow(username);
}

void MatchSession::Tick() { engine_->Tick(); }

bool MatchSession::ArmTurnTimer(int64_t duration_ms) {
    return engine_->ArmCurrentTurnDeadline(duration_ms);
}

bool MatchSession::RebindPlayer(const std::string& old_username,
                                const std::string& new_username,
                                AppWebSocket* socket) {
    if (old_username.empty() || new_username.empty() ||
        old_username == new_username) {
        return false;
    }

    const std::optional<match::ecs::Entity> player =
        engine_->FindPlayer(old_username);
    if (!player.has_value()) return false;
    match::ecs::PlayerInfo* info =
        engine_->Store().Get<match::ecs::PlayerInfo>(*player);
    if (info == nullptr) return false;
    ready_barrier_.Rename(old_username, new_username);
    info->username = new_username;
    info->is_bot = false;
    info->connected = true;

    auto socket_it = sockets_.find(old_username);
    AppWebSocket* bound =
        (socket != nullptr)
            ? socket
            : (socket_it != sockets_.end() ? socket_it->second : nullptr);
    if (socket_it != sockets_.end()) sockets_.erase(socket_it);
    sockets_[new_username] = bound;

    // INFO: move the per-recipient stream/prompt state to the new key so the
    //       hijacker inherits the seat's `seq` watermark and prompt dedupe.
    auto move_node = [&](auto& map) {
        auto node = map.extract(old_username);
        if (node.empty()) {
            map.try_emplace(new_username);
            return;
        }
        node.key() = new_username;
        map.insert(std::move(node));
    };
    move_node(sinks_);
    move_node(prompt_signature_);
    move_node(prompt_outcome_);
    return true;
}

bool MatchSession::HandSeatToBot(const std::string& old_username,
                                 const std::string& bot_name) {
    if (!RebindPlayer(old_username, bot_name, nullptr)) return false;

    const std::optional<match::ecs::Entity> player =
        engine_->FindPlayer(bot_name);
    if (player.has_value()) {
        match::ecs::PlayerInfo* info =
            engine_->Store().Get<match::ecs::PlayerInfo>(*player);
        if (info != nullptr) info->is_bot = true;
    }
    sockets_[bot_name] = nullptr;
    ready_barrier_.MarkReady(bot_name);
    return true;
}

bool MatchSession::BindSocket(const std::string& username,
                              AppWebSocket* socket) {
    auto it = sockets_.find(username);
    if (it == sockets_.end()) return false;
    it->second = socket;
    return true;
}

void MatchSession::BindViewer(const std::string& username,
                              AppWebSocket* socket) {
    if (username.empty()) return;
    viewers_[username] = socket;
    // INFO: a fresh spectator stream starts at seq 0 (it never received the
    //       earlier `match_event` packets); re-binding the same spectator on
    //       reconnect keeps its existing watermark.
    viewer_sinks_.try_emplace(username);
}

bool MatchSession::UnbindViewer(const std::string& username) {
    if (viewers_.erase(username) == 0) return false;
    viewer_sinks_.erase(username);
    return true;
}

void MatchSession::SendSnapshot(IBroadcaster& broadcaster, AppWebSocket* socket,
                                const std::string& username,
                                bool is_spectator) {
    if (socket == nullptr) return;

    if (!is_spectator) {
        const auto it = sinks_.find(username);
        if (it != sinks_.end()) {
            // INFO: a seat that missed `defs` (disconnected at start) still
            //       gets it before its first snapshot so card bits decode.
            EnsureDefs(broadcaster, socket, it->second);
            broadcaster.SendJson(
                socket, builder_.BuildSnapshot(
                            match::view::Viewer::Player(username), it->second));
            SendBarrierPacket(
                broadcaster, socket, it->second,
                ready_barrier_.IsOpen() ? "match_begin" : "players_ready",
                ready_barrier_.IsOpen() ? json::object() : ReadyProgressPayload());
            return;
        }
    }

    // INFO: a bound spectator keeps its persistent stream so `defs` is only
    //       sent once and `seq` stays monotonic across later live updates; an
    //       unknown recipient still gets a one-off omniscient view.
    const auto viewer = viewer_sinks_.find(username);
    if (viewer != viewer_sinks_.end()) {
        EnsureDefs(broadcaster, socket, viewer->second);
        broadcaster.SendJson(socket,
                             builder_.BuildSnapshot(match::view::Viewer::Spectator(),
                                                    viewer->second));
        SendBarrierPacket(
            broadcaster, socket, viewer->second,
            ready_barrier_.IsOpen() ? "match_begin" : "players_ready",
            ready_barrier_.IsOpen() ? json::object() : ReadyProgressPayload());
        return;
    }
    match::view::EventSink sink;
    broadcaster.SendJson(socket, builder_.BuildSnapshot(
                                     match::view::Viewer::Spectator(), sink));
}

void MatchSession::EmitMatchStart(IBroadcaster& broadcaster) {
    if (match_start_sent_) return;

    // INFO: `defs` is content-derived and identical for every recipient; the
    //       digest doubles as the `match_start` correlation id.
    const json defs =
        match::view::DefsBuilder::Build(engine_->Registries(), mods_);
    const match::engine::MatchDeckSnapshot& deck = engine_->Assembly().Deck();
    const json start = match::view::DefsBuilder::BuildMatchStart(
        engine_->Registries(), mods_, deck.deck_id, deck.name, deck.settings);

    for (const auto& [username, socket] : sockets_) {
        if (socket == nullptr) continue;
        match::view::EventSink& sink = sinks_.at(username);
        json defs_packet = sink.Wrap("defs", defs);
        defs_packet["action"] = "match_event";
        broadcaster.SendJson(socket, defs_packet);
        json start_packet = sink.Wrap("match_start", start);
        start_packet["action"] = "match_event";
        broadcaster.SendJson(socket, start_packet);
    }
    match_start_sent_ = true;
}

void MatchSession::EnsureDefs(IBroadcaster& broadcaster, AppWebSocket* socket,
                              match::view::EventSink& sink) {
    if (!match_start_sent_ || socket == nullptr) return;
    // INFO: seq 0 means this stream has no earlier packet to stay monotonic
    //       with; a non-zero sink already carries the `defs`/`match_start`
    //       prefix (or later events), so re-sending would renumber.
    if (sink.NextSeq() != 0) return;
    json packet = sink.Wrap(
        "defs", match::view::DefsBuilder::Build(engine_->Registries(), mods_));
    packet["action"] = "match_event";
    broadcaster.SendJson(socket, packet);
}

void MatchSession::SendBarrierPacket(IBroadcaster& broadcaster,
                                     AppWebSocket* socket,
                                     match::view::EventSink& sink,
                                     const std::string& type,
                                     const json& payload) {
    if (socket == nullptr) return;
    EnsureDefs(broadcaster, socket, sink);
    json packet = sink.Wrap(type, payload);
    packet["action"] = "match_event";
    broadcaster.SendJson(socket, packet);
}

void MatchSession::BroadcastBarrierPacket(IBroadcaster& broadcaster,
                                          const std::string& type,
                                          const json& payload) {
    for (const auto& [username, socket] : sockets_) {
        SendBarrierPacket(broadcaster, socket, sinks_.at(username), type, payload);
    }
    for (const auto& [username, socket] : viewers_) {
        SendBarrierPacket(broadcaster, socket, viewer_sinks_.at(username), type,
                          payload);
    }
}

json MatchSession::ReadyProgressPayload() const {
    json payload = json{{"ready", ready_barrier_.Ready()},
                        {"total", ready_barrier_.Total()}};
    if (barrier_timeout_ms_ > 0) {
        const int64_t elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - barrier_armed_at_)
                .count();
        payload["timeout_ms"] =
            std::max<int64_t>(0, barrier_timeout_ms_ - elapsed_ms);
    }
    return payload;
}

void MatchSession::BeginReadyBarrier(IBroadcaster& broadcaster,
                                     int64_t timeout_ms) {
    // INFO: only seats holding a live socket at start have a client to wait
    //       for; bots and already-disconnected humans count as ready.
    std::vector<std::string> pending;
    for (const auto& [username, socket] : sockets_) {
        if (socket != nullptr) pending.push_back(username);
    }
    // INFO: `Registries().players` is the seated roster the view layer
    //       iterates; it includes bots and excludes spectators.
    ready_barrier_.Arm(pending, engine_->Registries().players.size());
    barrier_timeout_ms_ = timeout_ms;
    barrier_armed_at_ = std::chrono::steady_clock::now();
    BroadcastBarrierPacket(broadcaster, "players_ready", ReadyProgressPayload());
}

bool MatchSession::MarkSeatReady(const std::string& username,
                                 IBroadcaster& broadcaster) {
    if (!ready_barrier_.MarkReady(username)) return false;
    BroadcastBarrierPacket(broadcaster, "players_ready", ReadyProgressPayload());
    return true;
}

bool MatchSession::ReadyBarrierComplete() const {
    return ready_barrier_.Complete();
}

bool MatchSession::ReadyBarrierOpen() const { return ready_barrier_.IsOpen(); }

bool MatchSession::OpenReadyBarrier(IBroadcaster& broadcaster) {
    if (!ready_barrier_.Open()) return false;
    BroadcastBarrierPacket(broadcaster, "match_begin", json::object());
    return true;
}

void MatchSession::EmitEvents(IBroadcaster& broadcaster) {
    // INFO: a seat or spectator that missed `defs` (unreachable at start, or
    //       bound later) must receive it before the first event wrapped into
    //       its stream; the sink guard makes this a no-op
    //       once the stream carries a packet.
    for (const auto& [username, socket] : sockets_) {
        if (socket == nullptr) continue;
        EnsureDefs(broadcaster, socket, sinks_.at(username));
    }
    for (const auto& [username, socket] : viewers_) {
        if (socket == nullptr) continue;
        EnsureDefs(broadcaster, socket, viewer_sinks_.at(username));
    }
    const std::vector<json>& events = engine_->Events();
    for (; cursor_ < events.size(); ++cursor_) {
        for (const auto& [username, socket] : sockets_) {
            if (socket == nullptr) continue;
            std::optional<json> packet = builder_.Wrap(
                events[cursor_], match::view::Viewer::Player(username),
                sinks_.at(username));
            if (!packet.has_value()) continue;
            (*packet)["action"] = "match_event";
            broadcaster.SendJson(socket, *packet);
        }
        // INFO: Spectators get the same event stream through
        //       the omniscient spectator view on their own persistent sink.
        for (const auto& [username, socket] : viewers_) {
            if (socket == nullptr) continue;
            std::optional<json> packet = builder_.Wrap(
                events[cursor_], match::view::Viewer::Spectator(),
                viewer_sinks_.at(username));
            if (!packet.has_value()) continue;
            (*packet)["action"] = "match_event";
            broadcaster.SendJson(socket, *packet);
        }
    }
    EmitPendingPrompt(broadcaster);
}

void MatchSession::EmitPendingPrompt(IBroadcaster& broadcaster) {
    const std::optional<json> pending = engine_->PendingInput();
    const std::string signature =
        pending.has_value() ? pending->dump() : std::string();
    const std::string kind =
        pending.has_value() ? pending->value("kind", std::string())
                            : std::string();
    const json* schema = kind.empty() ? nullptr : FindPromptSchema(kind);

    for (const auto& [username, socket] : sockets_) {
        if (socket == nullptr) continue;
        std::string& last = prompt_signature_[username];
        if (!pending.has_value()) {
            // INFO: the parked prompt cleared (answer / timeout / cancel);
            //       close it for the recipient that was shown prompt_open.
            if (!last.empty()) {
                EmitPromptClose(broadcaster, username, socket, last);
                last.clear();
            }
            continue;
        }
        // INFO: dedupe by pending signature so a repeated EmitEvents between
        //       the play and the answer cannot re-send the same prompt_open.
        if (last == signature) continue;
        std::optional<json> packet = builder_.BuildPendingPrompt(
            match::view::Viewer::Player(username), sinks_.at(username));
        if (!packet.has_value()) continue;
        // INFO: The engine op leaves `response_schema` to the session layer;
        //       attach the resolved kind schema so `prompt_open` satisfies
        //       and the client sees exactly what `SubmitInput` validates.
        if (schema != nullptr && packet->contains("payload") &&
            (*packet)["payload"].is_object()) {
            json& payload = (*packet)["payload"];
            const bool empty_schema =
                !payload.contains("response_schema") ||
                !payload["response_schema"].is_object() ||
                payload["response_schema"].empty();
            if (empty_schema) payload["response_schema"] = *schema;
        }
        (*packet)["action"] = "match_event";
        broadcaster.SendJson(socket, *packet);
        last = signature;
        // INFO: a fresh prompt starts from the default outcome; a stale
        //       answer from a replaced prompt must not leak forward.
        prompt_outcome_[username] = "cancelled";
    }
}

void MatchSession::EmitPromptClose(IBroadcaster& broadcaster,
                                   const std::string& username,
                                   AppWebSocket* socket,
                                   const std::string& signature) {
    // INFO: `prompt_id` is the prompt kind (the view layer's `prompt_open`
    //       uses the kind as the id); recover it from the stored signature.
    std::string prompt_id;
    try {
        prompt_id = json::parse(signature).value("kind", std::string());
    } catch (const json::exception&) {
        prompt_id.clear();
    }
    if (prompt_id.empty()) prompt_id = "prompt";

    std::string& outcome = prompt_outcome_[username];
    if (outcome.empty()) outcome = "cancelled";
    json payload = {{"prompt_id", prompt_id}, {"outcome", outcome}};
    json packet = sinks_.at(username).Wrap("prompt_close", std::move(payload));
    packet["action"] = "match_event";
    broadcaster.SendJson(socket, packet);
    outcome = "cancelled";
}

void MatchSession::BroadcastSnapshot(IBroadcaster& broadcaster) {
    BroadcastSnapshot(broadcaster, match::view::SnapshotOptions{});
}

void MatchSession::BroadcastSnapshot(IBroadcaster& broadcaster,
                                     const match::view::SnapshotOptions& options) {
    for (const auto& [username, socket] : sockets_) {
        if (socket == nullptr) continue;
        // INFO: a seat that was unreachable at start gets `defs` before its
        //       first snapshot after reconnecting.
        EnsureDefs(broadcaster, socket, sinks_.at(username));
        const json snapshot = builder_.BuildSnapshot(
            match::view::Viewer::Player(username), sinks_.at(username), options);
        broadcaster.SendJson(socket, snapshot);
    }
    // INFO: Spectators keep receiving snapshots after their
    //       initial join snapshot (omniscient view, own seq watermark).
    for (const auto& [username, socket] : viewers_) {
        if (socket == nullptr) continue;
        EnsureDefs(broadcaster, socket, viewer_sinks_.at(username));
        const json snapshot = builder_.BuildSnapshot(
            match::view::Viewer::Spectator(), viewer_sinks_.at(username), options);
        broadcaster.SendJson(socket, snapshot);
    }
    BroadcastMatchOver(broadcaster);
}

bool MatchSession::BroadcastMatchOver(IBroadcaster& broadcaster) {
    if (!engine_->IsMatchOver()) return false;
    if (over_sent_) return true;

    const json packet = {{"action", "match_over"},
                         {"winner", engine_->GetWinner()},
                         {"placements", engine_->GetPlacements()}};
    for (const auto& [username, socket] : sockets_) {
        if (socket == nullptr) continue;
        broadcaster.SendJson(socket, packet);
    }
    for (const auto& [username, socket] : viewers_) {
        if (socket == nullptr) continue;
        broadcaster.SendJson(socket, packet);
    }
    over_sent_ = true;
    return true;
}

const nlohmann::json* MatchSession::FindPromptSchema(
    const std::string& kind) const {
    const auto it = prompt_schemas_.find(kind);
    if (it == prompt_schemas_.end()) return nullptr;
    return &it->second;
}

bool MatchSession::MatchesSchema(const nlohmann::json& value,
                                 const nlohmann::json& schema) {
    if (!schema.is_object() || schema.empty()) return true;

    const std::string type = schema.value("type", std::string());
    if (type == "string" && !value.is_string()) return false;
    if (type == "boolean" && !value.is_boolean()) return false;
    if (type == "integer" && !value.is_number_integer()) return false;
    if (type == "number" && !value.is_number()) return false;
    if (type == "object" && !value.is_object()) return false;
    if (type == "array" && !value.is_array()) return false;

    if (value.is_number() && schema.contains("minimum") &&
        schema["minimum"].is_number()) {
        if (value.get<double>() < schema["minimum"].get<double>()) return false;
    }
    if (value.is_number() && schema.contains("maximum") &&
        schema["maximum"].is_number()) {
        if (value.get<double>() > schema["maximum"].get<double>()) return false;
    }

    if (schema.contains("enum") && schema["enum"].is_array()) {
        bool found = false;
        for (const json& candidate : schema["enum"]) {
            if (candidate == value) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

bool IsBuiltinPromptKind(const std::string& kind) {
    // INFO: Phase-1 engine/vanilla prompt kinds. Mods
    //       may declare NEW kinds; these five are owned by the engine and a
    //       mod declaration for one is ignored so its validator stays fixed.
    static const std::set<std::string, std::less<>> kBuiltins = {
        "choose_card", "choose_color", "choose_player", "choose_yes_no",
        "choose_value"};
    return kBuiltins.count(kind) != 0;
}

void MatchSession::BuildPromptSchemas() {
    // INFO: built-in phase-1 kinds. A mod declaration may add a
    //       NEW kind; it never overwrites a built-in. An
    //       undeclared kind stays permissive.
    prompt_schemas_["choose_color"] = {
        {"type", "string"}, {"enum", {"red", "blue", "green", "yellow"}}};
    prompt_schemas_["choose_yes_no"] = {{"type", "boolean"}};
    // INFO: A card choice is a `CompactCardV2.bits` integer.
    prompt_schemas_["choose_card"] = {{"type", "integer"}};

    for (const match::modload::LoadedMod& mod : mods_) {
        if (!mod.manifest.prompts.is_array()) continue;
        for (const json& decl : mod.manifest.prompts) {
            if (!decl.is_object()) continue;
            const std::string kind = decl.value("kind", std::string());
            if (kind.empty() || !decl.contains("response_schema")) continue;
            if (!decl["response_schema"].is_object()) continue;
            if (IsBuiltinPromptKind(kind)) {
                Logger::Warn("[MatchSession] mod prompt '", kind,
                             "' collides with an engine built-in; "
                             "declaration ignored");
                continue;
            }
            prompt_schemas_[kind] = decl["response_schema"];
        }
    }
}

}  // namespace match::server
