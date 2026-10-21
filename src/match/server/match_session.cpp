#include <match/server/match_session.hpp>

#include <common/contract.hpp>
#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/view/defs_builder.hpp>
#include <match/view/view_util.hpp>
#include <match/view/wire.hpp>

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
    : MatchSession(
          std::move(engine),
          std::make_shared<const std::vector<match::modload::LoadedMod>>(
              std::move(mods)),
          std::move(sockets)) {}

MatchSession::MatchSession(
    std::unique_ptr<match::engine::MatchInstance> engine,
    std::shared_ptr<const std::vector<match::modload::LoadedMod>> mods,
    SocketMap sockets)
    : engine_(std::move(engine)),
      mods_(std::move(mods)),
      builder_(*engine_, *mods_),
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
    //       EmitPendingPrompt turns this into the prompt_answered.
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
    if (old_username.empty() || new_username.empty()) {
        return false;
    }

    const std::optional<match::ecs::Entity> player =
        engine_->FindPlayer(old_username);
    if (!player.has_value()) return false;
    match::ecs::PlayerInfo* info =
        engine_->Store().Get<match::ecs::PlayerInfo>(*player);
    if (info == nullptr) return false;
    if (old_username == new_username) {
        // INFO: same-seat Creative flip back to human: no keys move (the
        //       seat keeps its stream and prompt state); only the flags, and
        //       the socket when the caller binds one, change.
        info->is_bot = false;
        info->connected = true;
        if (socket != nullptr) sockets_[old_username] = socket;
        sinks_.try_emplace(old_username);
        prompt_signature_.try_emplace(old_username);
        prompt_outcome_.try_emplace(old_username, "cancelled");
        ready_barrier_.MarkReady(old_username);
        return true;
    }
    if (engine_->FindPlayer(new_username).has_value()
        || sockets_.contains(new_username)) {
        // INFO: the target key is taken; refusing beats merging two seats'
        //       sockets and streams into one map entry.
        return false;
    }
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

    MoveSeatKeys(old_username, new_username);
    return true;
}

bool MatchSession::HandSeatToBot(const std::string& old_username,
                                 const std::string& bot_name) {
    if (old_username.empty() || bot_name.empty()) return false;
    if (old_username == bot_name) {
        // INFO: same-seat Creative flip to bot: the seat keeps its name,
        //       hand and turn state; it just stops receiving the stream and
        //       counts as ready like any bot seat.
        const std::optional<match::ecs::Entity> same =
            engine_->FindPlayer(old_username);
        if (!same.has_value()) return false;
        match::ecs::PlayerInfo* same_info =
            engine_->Store().Get<match::ecs::PlayerInfo>(*same);
        if (same_info == nullptr) return false;
        same_info->is_bot = true;
        sockets_[old_username] = nullptr;
        ready_barrier_.MarkReady(old_username);
        return true;
    }
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

bool MatchSession::RenameSeat(const std::string& old_username,
                              const std::string& new_username) {
    if (old_username.empty() || new_username.empty()
        || old_username == new_username) {
        return false;
    }
    if (new_username.size() > contract::kUsernameMax) return false;
    const std::optional<match::ecs::Entity> player =
        engine_->FindPlayer(old_username);
    if (!player.has_value()) return false;
    if (engine_->FindPlayer(new_username).has_value()
        || sockets_.contains(new_username)) {
        return false;
    }
    match::ecs::PlayerInfo* info =
        engine_->Store().Get<match::ecs::PlayerInfo>(*player);
    if (info == nullptr) return false;
    // INFO: a rename is display identity only: is_bot/connected stay as the
    //       seat had them (a hijack that humanizes is RebindPlayer's job).
    info->username = new_username;
    ready_barrier_.Rename(old_username, new_username);
    // INFO: socket, stream, prompt and display keying follow (MoveSeatKeys).

    auto socket_it = sockets_.find(old_username);
    AppWebSocket* bound =
        socket_it != sockets_.end() ? socket_it->second : nullptr;
    if (socket_it != sockets_.end()) sockets_.erase(socket_it);
    sockets_[new_username] = bound;

    MoveSeatKeys(old_username, new_username);
    return true;
}

bool MatchSession::SetSeatSkin(const std::string& username,
                               const std::string& skin) {
    if (!IsValidSkinValue(skin)) return false;
    if (!engine_->FindPlayer(username).has_value()) return false;
    skin_overrides_[username] = skin;
    return true;
}

std::string MatchSession::SeatSkin(const std::string& username) const {
    const auto it = skin_overrides_.find(username);
    return it == skin_overrides_.end() ? std::string() : it->second;
}

std::unordered_map<std::string, std::string> MatchSession::SkinOverrides()
    const {
    return skin_overrides_;
}

bool MatchSession::IsValidSkinValue(const std::string& skin) {
    return skin == "bot" || skin == "player";
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

void MatchSession::SetSeatPrivacy(const std::string& username, bool enabled) {
    const auto position = std::ranges::find(private_seats_, username);
    if (enabled && position == private_seats_.end()) {
        private_seats_.push_back(username);
    } else if (!enabled && position != private_seats_.end()) {
        private_seats_.erase(position);
    }
}

void MatchSession::SetDirector(const std::string& username, bool enabled) {
    const auto pos = std::ranges::find(directors_, username);
    if (enabled && pos == directors_.end()) directors_.push_back(username);
    else if (!enabled && pos != directors_.end()) directors_.erase(pos);
}

bool MatchSession::IsDirector(const std::string& username) const {
    return std::ranges::find(directors_, username) != directors_.end();
}

bool MatchSession::StackDrawPile(const std::vector<std::string>& kinds) {
    if (kinds.empty()) return false;
    ecs::EntityStore& store = engine_->Store();
    const auto& registries = engine_->Registries();
    for (auto it = kinds.rbegin(); it != kinds.rend(); ++it) {
        const auto pos = registries.content->kind_index.find(*it);
        if (pos == registries.content->kind_index.end()) return false;
        const ecs::Entity card = store.Create();
        ecs::CardIdentity identity;
        identity.mod_index = pos->second.mod_index;
        identity.kind_index = pos->second.kind_index;
        identity.kind_id = *it;
        store.Add(card, identity);
        store.Add(card, ecs::InZone{ecs::ZoneRef{ecs::ZoneKind::kLimbo, ecs::Entity{}}, 0});
        store.Add(card, ecs::FaceSpec{});
        if (match::ops::MoveCardToZone(
                store, card,
                ecs::ZoneRef{ecs::ZoneKind::kDrawPile, ecs::Entity{}}) &&
            engine_->RegisterStackedCard(card, *it).has_value()) {
            continue;
        }
        store.Destroy(card);
        return false;
    }
    return true;
}

bool MatchSession::ApplyScene(const nlohmann::json& scene,
                              std::map<std::string, nlohmann::json>* out_intents) {
    if (!scene.is_object() || !scene.contains("seats") ||
        !scene["seats"].is_array()) {
        return false;
    }
    ecs::EntityStore& store = engine_->Store();
    const match::engine::MatchRegistries& registries = engine_->Registries();

    struct SeatSetup {
        ecs::Entity player;
        std::string username;
        std::vector<std::string> hand;
        nlohmann::json intents = nlohmann::json::array();
    };
    std::vector<SeatSetup> setups;
    for (const nlohmann::json& seat : scene["seats"]) {
        if (!seat.is_object()) return false;
        const std::string username = seat.value("username", std::string());
        const std::optional<ecs::Entity> player = engine_->FindPlayer(username);
        if (!player.has_value()) return false;
        SeatSetup setup{*player, username, {}, nlohmann::json::array()};
        if (seat.contains("hand")) {
            if (!seat["hand"].is_array()) return false;
            for (const nlohmann::json& kind : seat["hand"]) {
                if (!kind.is_string()) return false;
                if (!registries.FindKind(kind.get<std::string>()).has_value()) {
                    return false;
                }
                setup.hand.push_back(kind.get<std::string>());
            }
        }
        if (seat.contains("intents")) {
            if (!seat["intents"].is_array()) return false;
            setup.intents = seat["intents"];
        }
        setups.push_back(std::move(setup));
    }

    std::vector<std::string> pile_top;
    if (scene.contains("pile_top")) {
        if (!scene["pile_top"].is_array()) return false;
        for (const nlohmann::json& kind : scene["pile_top"]) {
            if (!kind.is_string()) return false;
            if (!registries.FindKind(kind.get<std::string>()).has_value()) {
                return false;
            }
            pile_top.push_back(kind.get<std::string>());
        }
    }
    const std::string discard_top = scene.value("discard_top", std::string());
    if (!discard_top.empty() &&
        !registries.FindKind(discard_top).has_value()) {
        return false;
    }

    // INFO: validation above rejects unknown seats and kinds, so the
    //       application below cannot leave a half-built scene behind.
    const ecs::ZoneRef draw_ref{ecs::ZoneKind::kDrawPile, ecs::Entity{}};
    for (const SeatSetup& setup : setups) {
        for (ecs::Entity card : match::ops::HandOf(store, setup.player)) {
            match::ops::MoveCardToZone(store, card, draw_ref);
        }
    }
    for (const SeatSetup& setup : setups) {
        for (const std::string& kind : setup.hand) {
            if (!StackDrawPile({kind})) return false;
            const std::optional<ecs::Entity> top =
                match::ops::DrawTop(store, registries.draw_pile);
            if (!top.has_value()) return false;
            match::ops::MoveCardToZone(
                store, *top,
                ecs::ZoneRef{ecs::ZoneKind::kHand, setup.player});
        }
        if (out_intents != nullptr && setup.intents.is_array() &&
            !setup.intents.empty()) {
            (*out_intents)[setup.username] = setup.intents;
        }
    }
    if (!pile_top.empty() && !StackDrawPile(pile_top)) return false;
    if (!discard_top.empty()) {
        if (!StackDrawPile({discard_top})) return false;
        const std::optional<ecs::Entity> top =
            match::ops::DrawTop(store, registries.draw_pile);
        if (!top.has_value()) return false;
        const std::optional<ecs::Entity> pile =
            match::ops::FindPile(store, ecs::PileKind::kDiscard);
        if (!pile.has_value()) return false;
        match::ops::MoveCardToZone(
            store, *top,
            ecs::ZoneRef{ecs::ZoneKind::kDiscardPile, ecs::Entity{}});
    }
    return true;
}

nlohmann::json MatchSession::SaveCheckpoint() {
    ecs::EntityStore& store = engine_->Store();
    nlohmann::json seats = nlohmann::json::array();
    for (ecs::Entity player : match::ops::ActivePlayersBySeat(store)) {
        const ecs::PlayerInfo* info = store.Get<ecs::PlayerInfo>(player);
        if (info == nullptr) continue;
        nlohmann::json hand = nlohmann::json::array();
        for (ecs::Entity card : match::ops::HandOf(store, player)) {
            const ecs::CardIdentity* identity =
                store.Get<ecs::CardIdentity>(card);
            if (identity != nullptr) hand.push_back(identity->kind_id);
        }
        seats.push_back({{"username", info->username}, {"hand", hand}});
    }
    nlohmann::json pile_top = nlohmann::json::array();
    const std::optional<ecs::Entity> draw =
        match::ops::FindPile(store, ecs::PileKind::kDraw);
    if (draw.has_value()) {
        const ecs::PileContents* contents =
            store.Get<ecs::PileContents>(*draw);
        if (contents != nullptr) {
            for (auto it = contents->cards.rbegin();
                 it != contents->cards.rend(); ++it) {
                const ecs::CardIdentity* identity =
                    store.Get<ecs::CardIdentity>(*it);
                if (identity != nullptr) pile_top.push_back(identity->kind_id);
            }
        }
    }
    std::string discard_top;
    const std::optional<ecs::Entity> discard =
        match::ops::FindPile(store, ecs::PileKind::kDiscard);
    if (discard.has_value()) {
        const ecs::PileContents* contents =
            store.Get<ecs::PileContents>(*discard);
        if (contents != nullptr && !contents->cards.empty()) {
            const ecs::CardIdentity* identity = store.Get<ecs::CardIdentity>(
                contents->cards.back());
            if (identity != nullptr) discard_top = identity->kind_id;
        }
    }
    return {{"seats", seats},
            {"pile_top", pile_top},
            {"discard_top", discard_top}};
}

bool MatchSession::RestoreCheckpoint(
    const nlohmann::json& checkpoint,
    std::map<std::string, nlohmann::json>* out_intents) {
    return ApplyScene(checkpoint, out_intents);
}

void MatchSession::StoreCheckpoint(nlohmann::json checkpoint) {
    checkpoint_ = std::move(checkpoint);
}

bool MatchSession::HasCheckpoint() const {
    return !checkpoint_.is_null();
}

const nlohmann::json& MatchSession::Checkpoint() const {
    return checkpoint_;
}

void MatchSession::SetSceneIntents(
    std::map<std::string, nlohmann::json> intents) {
    scene_intents_ = std::move(intents);
}

std::map<std::string, nlohmann::json> MatchSession::TakeSceneIntents() {
    std::map<std::string, nlohmann::json> intents;
    intents.swap(scene_intents_);
    return intents;
}

match::view::Viewer MatchSession::SpectatorViewer() const {
    return match::view::Viewer::Spectator(private_seats_);
}

bool MatchSession::DemoteSeatToSpectator(const std::string& username) {
    auto seat = sockets_.extract(username);
    if (seat.empty()) return false;
    auto sink = sinks_.extract(username);
    viewers_[username] = seat.mapped();
    if (!sink.empty()) {
        viewer_sinks_.insert_or_assign(username, std::move(sink.mapped()));
    }
    prompt_signature_.erase(username);
    prompt_outcome_.erase(username);
    return true;
}

void MatchSession::SendSnapshot(IBroadcaster& broadcaster, AppWebSocket* socket,
                                const std::string& username,
                                bool is_spectator) {
    if (socket == nullptr) return;

    match::view::SnapshotOptions warm_up = SnapshotOptionsWithSkins({});
    warm_up.tail = &tail_;
    if (!is_spectator) {
        const auto it = sinks_.find(username);
        if (it != sinks_.end()) {
            // INFO: a seat that missed `defs` (disconnected at start) still
            //       gets it before its first snapshot so card bits decode.
            EnsureDefs(broadcaster, socket, it->second);
            broadcaster.SendJson(
                socket, builder_.BuildSnapshot(
                            (IsDirector(username) ? match::view::Viewer::PlayerDirector(username) : match::view::Viewer::Player(username)), it->second,
                            warm_up));
            SendBarrierPacket(
                broadcaster, socket, it->second,
                ready_barrier_.IsOpen() ? "match_begin" : "players_ready",
                ready_barrier_.IsOpen() ? json::object() : ReadyProgressPayload());
            return;
        }
    }

    // INFO: a bound spectator keeps its persistent stream so `defs` is only
    //       sent once and `seq` stays monotonic across later live updates; an
    //       unknown recipient still gets a one-off spectator view.
    const auto viewer = viewer_sinks_.find(username);
    if (viewer != viewer_sinks_.end()) {
        EnsureDefs(broadcaster, socket, viewer->second);
        broadcaster.SendJson(
            socket, builder_.BuildSnapshot(SpectatorViewer(), viewer->second,
                                           warm_up));
        SendBarrierPacket(
            broadcaster, socket, viewer->second,
            ready_barrier_.IsOpen() ? "match_begin" : "players_ready",
            ready_barrier_.IsOpen() ? json::object() : ReadyProgressPayload());
        return;
    }
    match::view::EventSink sink;
    broadcaster.SendJson(
        socket, builder_.BuildSnapshot(SpectatorViewer(), sink, warm_up));
}

bool MatchSession::HandleSnapshotRequest(
    IBroadcaster& broadcaster, const std::string& username,
    const match::view::SnapshotOptions& options) {
    const auto seat_socket = sockets_.find(username);
    const auto viewer_socket = viewers_.find(username);
    const bool is_seat = seat_socket != sockets_.end();
    if (!is_seat && viewer_socket == viewers_.end()) return false;
    AppWebSocket* socket =
        is_seat ? seat_socket->second : viewer_socket->second;
    if (socket == nullptr) return false;

    const int64_t now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
    if (!snapshot_request_limiter_.Allow(username, now_ms)) return false;

    match::view::EventSink& sink =
        is_seat ? sinks_.at(username) : viewer_sinks_.at(username);
    const match::view::Viewer viewer =
        is_seat ? (IsDirector(username) ? match::view::Viewer::PlayerDirector(username) : match::view::Viewer::Player(username))
                : SpectatorViewer();
    match::view::SnapshotOptions warm_up = SnapshotOptionsWithSkins(options);
    warm_up.tail = &tail_;
    broadcaster.SendJson(socket, builder_.BuildSnapshot(viewer, sink, warm_up));
    return true;
}

void MatchSession::EmitMatchStart(IBroadcaster& broadcaster) {
    if (match_start_sent_) return;

    // INFO: `defs` is content-derived and identical for every recipient; the
    //       digest doubles as the `match_start` correlation id. Production
    //       builds it once per assembly content and every match of that
    //       content shares it; CachedDefs only falls back to one lazy build
    //       per session when the content carries no shared DOM (fixtures and
    //       the three-argument `Assemble`). Late joiners reuse the same copy
    //       through EnsureDefs.
    const json& defs = CachedDefs();
    const match::engine::MatchDeckSnapshot& deck = engine_->Assembly().Deck();
    const json start = match::view::DefsBuilder::BuildMatchStart(
        defs, deck.deck_id, deck.name, deck.settings);

    // INFO: `defs` is content-derived and identical for every recipient; the
    //       digest doubles as the `match_start` correlation id. The payloads
    //       below are shared, but each envelope still serializes per recipient
    //       (it carries that sink's `seq`), into one reused frame buffer.
    std::string scratch;
    scratch.reserve(8192);
    for (const auto& [username, socket] : sockets_) {
        if (socket == nullptr) continue;
        match::view::EventSink& sink = sinks_.at(username);
        json defs_packet = sink.Wrap("defs", defs);
        defs_packet["action"] = "match_event";
        scratch = defs_packet.dump();
        broadcaster.Send(socket, scratch, uWS::OpCode::TEXT);
        json start_packet = sink.Wrap("match_start", start);
        start_packet["action"] = "match_event";
        scratch = start_packet.dump();
        broadcaster.Send(socket, scratch, uWS::OpCode::TEXT);
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
    json packet = sink.Wrap("defs", CachedDefs());
    packet["action"] = "match_event";
    broadcaster.SendJson(socket, packet);
}

const nlohmann::json& MatchSession::CachedDefs() const {
    // INFO: production content carries one `defs` DOM shared by every match
    //       with the same mod set; the lazy per-session build below is the
    //       fallback for fixtures and the three-argument `Assemble`, whose
    //       content is never published.
    const std::shared_ptr<const match::engine::AssemblyContent>& content =
        engine_->Registries().content;
    if (content != nullptr && content->defs != nullptr) return *content->defs;
    if (!defs_cache_.has_value()) {
        defs_cache_ = match::view::DefsBuilder::Build(engine_->Registries(),
                                                      *mods_);
    }
    return *defs_cache_;
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
    // INFO: one scratch frame buffer for the whole drain: each per-recipient
    //       packet still serializes on its own (the envelope carries that
    //       recipient's `seq`), but the dump reuses this capacity instead of
    //       allocating a fresh string per SendJson.
    std::string scratch;
    scratch.reserve(4096);
    for (; cursor_ < events.size(); ++cursor_) {
        tail_.Record(events[cursor_]);
        for (const auto& [username, socket] : sockets_) {
            if (socket == nullptr) continue;
            std::optional<json> packet = builder_.Wrap(
                events[cursor_], (IsDirector(username) ? match::view::Viewer::PlayerDirector(username) : match::view::Viewer::Player(username)),
                sinks_.at(username));
            if (!packet.has_value()) continue;
            (*packet)["action"] = "match_event";
            scratch = packet->dump();
            broadcaster.Send(socket, scratch, uWS::OpCode::TEXT);
        }
        // INFO: Spectators get the same event stream through the spectator
        //       view on their own persistent sink. The viewer is built for
        //       each event, so a setting change applies to the next one.
        for (const auto& [username, socket] : viewers_) {
            if (socket == nullptr) continue;
            std::optional<json> packet = builder_.Wrap(
                events[cursor_], SpectatorViewer(), viewer_sinks_.at(username));
            if (!packet.has_value()) continue;
            (*packet)["action"] = "match_event";
            scratch = packet->dump();
            broadcaster.Send(socket, scratch, uWS::OpCode::TEXT);
        }
    }
    // INFO: every event up to here reached its sinks and the tail, so the
    //       engine log can release them; a match would keep them all otherwise.
    engine_->TakeEvents();
    cursor_ = 0;
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
            //       close it for the recipient that was shown prompt_opened.
            if (!last.empty()) {
                EmitPromptClose(broadcaster, username, socket, last);
                last.clear();
            }
            continue;
        }
        // INFO: dedupe by pending signature so a repeated EmitEvents between
        //       the play and the answer cannot re-send the same prompt_opened.
        if (last == signature) continue;
        std::optional<json> packet = builder_.BuildPendingPrompt(
            (IsDirector(username) ? match::view::Viewer::PlayerDirector(username) : match::view::Viewer::Player(username)), sinks_.at(username));
        if (!packet.has_value()) continue;
        // INFO: The engine op leaves `response_schema` to the session layer;
        //       attach the resolved kind schema so `prompt_opened` satisfies
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
    // INFO: `prompt_id` is the prompt kind (the view layer's `prompt_opened`
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
    json packet =
        sinks_.at(username).Wrap("prompt_answered", std::move(payload));
    packet["action"] = "match_event";
    broadcaster.SendJson(socket, packet);
    outcome = "cancelled";
}

void MatchSession::BroadcastSnapshot(IBroadcaster& broadcaster) {
    BroadcastSnapshot(broadcaster, match::view::SnapshotOptions{});
}

void MatchSession::BroadcastSnapshot(IBroadcaster& broadcaster,
                                     const match::view::SnapshotOptions& options) {
    // INFO: same scratch-reuse contract as EmitEvents: snapshots are
    //       per-recipient (own view + own `seq` watermark), so each still
    //       serializes, but into one reused frame buffer.
    //       Snapshots are the ONLY droppable frames: each broadcast supersedes
    //       the last, so a backpressure skip self-heals on the next tick.
    //       Everything else through Send (events, barrier, defs, match_start,
    //       match_over) keeps the non-droppable default and must never drop.
    std::string scratch;
    scratch.reserve(4096);
    const match::view::SnapshotOptions merged =
        SnapshotOptionsWithSkins(options);
    for (const auto& [username, socket] : sockets_) {
        if (socket == nullptr) continue;
        // INFO: a seat that was unreachable at start gets `defs` before its
        //       first snapshot after reconnecting.
        EnsureDefs(broadcaster, socket, sinks_.at(username));
        const json snapshot = builder_.BuildSnapshot(
            IsDirector(username)
                ? match::view::Viewer::PlayerDirector(username)
                : match::view::Viewer::Player(username),
            sinks_.at(username), merged);
        scratch = snapshot.dump();
        broadcaster.Send(socket, scratch, uWS::OpCode::TEXT, true, true);
    }
    // INFO: Spectators keep receiving snapshots after their
    //       initial join snapshot (spectator view, own seq watermark).
    for (const auto& [username, socket] : viewers_) {
        if (socket == nullptr) continue;
        EnsureDefs(broadcaster, socket, viewer_sinks_.at(username));
        const json snapshot = builder_.BuildSnapshot(
            SpectatorViewer(), viewer_sinks_.at(username), merged);
        scratch = snapshot.dump();
        broadcaster.Send(socket, scratch, uWS::OpCode::TEXT, true, true);
    }
    BroadcastMatchOver(broadcaster);
}

bool MatchSession::BroadcastMatchOver(IBroadcaster& broadcaster) {
    if (!engine_->IsMatchOver()) return false;
    if (over_sent_) return true;

    // INFO: the terminal frame is identical for every recipient (no per-seat
    //       view, no `seq`), so glaze serializes it once into `shared` and
    //       every socket sends the same bytes: N recipients, 1 dump.
    const match::view::MatchOverWire over{"match_over", engine_->GetWinner(),
                                          engine_->GetPlacements(), payouts_};
    std::string shared;
    shared.reserve(512);
    if (!match::view::WritePacket(over, shared)) return true;
    // INFO: every Send is checked: a backpressure drop on any recipient
    //       leaves over_sent_ clear, so the next BroadcastSnapshot retries
    //       the one-shot instead of losing it. Snapshots self-heal by
    //       supersession; match_over has no next version, so it must not
    //       latch a partially-dropped broadcast as delivered. (&= keeps
    //       sending to the remaining recipients after a drop.)
    bool all_accepted = true;
    for (const auto& [username, socket] : sockets_) {
        if (socket == nullptr) continue;
        all_accepted &= broadcaster.Send(socket, shared, uWS::OpCode::TEXT);
    }
    for (const auto& [username, socket] : viewers_) {
        if (socket == nullptr) continue;
        all_accepted &= broadcaster.Send(socket, shared, uWS::OpCode::TEXT);
    }
    if (all_accepted) over_sent_ = true;
    return over_sent_;
}

const nlohmann::json* MatchSession::FindPromptSchema(
    const std::string& kind) const {
    const auto it = prompt_schemas_.find(kind);
    if (it == prompt_schemas_.end()) return nullptr;
    return &it->second;
}

void MatchSession::MoveSeatKeys(const std::string& old_username,
                                const std::string& new_username) {
    // INFO: move the per-recipient stream/prompt state to the new key so the
    //       renamed seat keeps its `seq` watermark and prompt dedupe.
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
    // INFO: seat-scoped display state follows the seat too, on every rename
    //       path (hijack, bot handoff and director rename alike).
    for (auto* list : {&directors_, &private_seats_}) {
        const auto pos = std::ranges::find(*list, old_username);
        if (pos != list->end()) *pos = new_username;
    }
    if (auto skin = skin_overrides_.extract(old_username); !skin.empty()) {
        skin.key() = new_username;
        skin_overrides_.insert(std::move(skin));
    }
}

match::view::SnapshotOptions MatchSession::SnapshotOptionsWithSkins(
    const match::view::SnapshotOptions& options) const {
    match::view::SnapshotOptions merged = options;
    for (const auto& [seat, skin] : skin_overrides_) {
        merged.skin_overrides[seat] = skin;
    }
    return merged;
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
    // INFO: Phase-1 engine/uni prompt kinds. Mods
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

    for (const match::modload::LoadedMod& mod : *mods_) {
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
