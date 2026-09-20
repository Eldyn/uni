#include "match/engine/match_instance.hpp"

#include "match/modload/play_conditions.hpp"
#include "match/modload/restriction.hpp"
#include "match/ops/op_helpers.hpp"
#include "match/rng.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * @file match_instance.cpp
 * @brief Engine `MatchInstance` implementation (
 * ).
 *
 * The engine default play / draw / turn advance / win are implemented here
 * directly. Hook dispatch and Resolver graph draining are intentionally left
 * as documented seams; windows and prompts.
 */

namespace match::engine {
namespace {

using nlohmann::json;

/** @brief Entity handle as a JSON object. */
json EntityJson(ecs::Entity entity) {
    return json{{"index", entity.index}, {"generation", entity.generation}};
}

/** @brief A `ZoneRef` as the zone descriptor. */
json ZoneJson(const ecs::ZoneRef& zone) {
    json out = json::object();
    switch (zone.kind) {
        case ecs::ZoneKind::kHand:
            out["kind"] = "hand";
            out["owner"] = EntityJson(zone.owner);
            break;
        case ecs::ZoneKind::kDrawPile:
            out["kind"] = "draw_pile";
            break;
        case ecs::ZoneKind::kDiscardPile:
            out["kind"] = "discard_pile";
            break;
        case ecs::ZoneKind::kLimbo:
            out["kind"] = "limbo";
            break;
    }
    return out;
}

/** @brief Username of a player entity, or empty when not a live player. */
std::string PlayerUsername(const ecs::EntityStore& store,
                           ecs::Entity player) {
    const ecs::PlayerInfo* info = store.Get<ecs::PlayerInfo>(player);
    return info == nullptr ? std::string() : info->username;
}

}  // namespace

// --- construction / start --------------------------------------------------

MatchInstance::MatchInstance(std::unique_ptr<MatchAssembly> assembly)
    : assembly_(std::move(assembly)) {
    if (assembly_ == nullptr) {
        // ERROR: a null assembly cannot run; treat it as a finished match so
        //        every input is refused rather than dereferencing null.
        finished_ = true;
        return;
    }
    Start();
}

void MatchInstance::Start() {
    if (started_ || assembly_ == nullptr) return;
    started_ = true;

    ecs::EntityStore& store = assembly_->store;
    MatchRegistries& registries = assembly_->registries;

    // INFO: assembly seats player 0 as current; re-assert it only when no
    //       current player survived, so Start is safe to call twice.
    if (!CurrentPlayer().has_value() && !registries.players.empty()) {
        if (ecs::TurnState* turn =
                store.Get<ecs::TurnState>(registries.players.front())) {
            turn->is_current = true;
        }
    }

    // TODO: dispatch before/after `turn_start` (and `round_start` for
    //               round 0) here once the EventBus hook path is wired.
}

// --- input flow ------------------------------------------------------------

bool MatchInstance::PlayCard(const std::string& username, ecs::Entity card) {
    if (!started_ || finished_) return false;

    ecs::EntityStore& store = assembly_->store;
    MatchRegistries& registries = assembly_->registries;

    const std::optional<ecs::Entity> player = FindPlayer(username);
    if (!player.has_value()) return false;
    if (!store.IsAlive(card) || !store.Has<ecs::CardIdentity>(card)) {
        return false;
    }

    const ecs::Hand* hand = store.Get<ecs::Hand>(*player);
    if (hand == nullptr) return false;

    int hand_index = -1;
    for (std::size_t i = 0; i < hand->cards.size(); ++i) {
        if (hand->cards[i] == card) {
            hand_index = static_cast<int>(i);
            break;
        }
    }

    const std::optional<ecs::Entity> current = CurrentPlayer();
    const bool in_turn = current.has_value() && (*current == *player);

    // -- restriction pipeline over true engine state -------------
    modload::PlayAttempt attempt;
    attempt.player = username;
    const ecs::CardIdentity* identity = store.Get<ecs::CardIdentity>(card);
    attempt.card_kind = identity == nullptr ? std::string() : identity->kind_id;
    attempt.in_turn = in_turn;

    json context = json::object();
    if (const std::optional<ecs::Entity> top = TopDiscard();
        top.has_value()) {
        if (const ecs::CardIdentity* top_id =
                store.Get<ecs::CardIdentity>(*top)) {
            context["top_kind"] = top_id->kind_id;
        }
    }
    if (const ecs::ActiveTypeReq* active =
            store.Get<ecs::ActiveTypeReq>(registries.match)) {
        context["active_type"] =
            active->type.has_value() ? *active->type : std::string();
    } else {
        context["active_type"] = std::string();
    }
    json hand_kinds = json::array();
    for (ecs::Entity held : hand->cards) {
        if (const ecs::CardIdentity* held_id =
                store.Get<ecs::CardIdentity>(held)) {
            hand_kinds.push_back(held_id->kind_id);
        }
    }
    context["hand"] = std::move(hand_kinds);
    attempt.context = std::move(context);

    std::vector<modload::RestrictionEntry> entries;
    if (const ecs::PlayRestriction* pipeline =
            store.Get<ecs::PlayRestriction>(registries.match)) {
        entries.reserve(pipeline->entries.size());
        for (const ecs::RestrictionEntry& entry : pipeline->entries) {
            modload::RestrictionEntry converted;
            converted.id = entry.id;
            converted.phase = entry.phase == ecs::RestrictionPhase::kAllow
                                  ? "allow"
                                  : "deny";
            converted.condition = entry.condition;
            entries.push_back(std::move(converted));
        }
    }

    modload::ConditionMatcher matcher =
        [this](const json& condition, const modload::PlayAttempt& a) {
            return assembly_->play_matcher != nullptr
                && assembly_->play_matcher->Matches(condition, a);
        };
    const modload::PlayDecision decision =
        modload::EvaluatePlayRestrictions(entries, attempt, matcher);
    if (!decision.allowed) {
        Emit("play_rejected",
             json{{"player", username}, {"reason_id", decision.reason_id}});
        return false;
    }
    if (hand_index < 0) {
        // INFO: the pipeline allowed but the card is not in the actor's hand;
        //       only reachable once `must_own_card` is removed. Refuse rather
        //       than move a card the player does not hold.
        return false;
    }

    // --- engine default play ------------------------------------------
    const std::string kind_id = attempt.card_kind;
    const json from_zone =
        ZoneJson(ecs::ZoneRef{ecs::ZoneKind::kHand, *player});
    const json to_zone =
        ZoneJson(ecs::ZoneRef{ecs::ZoneKind::kDiscardPile, ecs::Entity{}});

    ops::MoveCardToZone(store, card,
                        ecs::ZoneRef{ecs::ZoneKind::kDiscardPile,
                                     ecs::Entity{}});

    Emit("card_left_zone", json{{"card", EntityJson(card)},
                                {"from", from_zone},
                                {"to", to_zone}});
    Emit("card_entered_zone", json{{"card", EntityJson(card)},
                                   {"from", from_zone},
                                   {"to", to_zone}});
    Emit("card_played", json{{"player", username},
                             {"card", EntityJson(card)},
                             {"kind", kind_id},
                             {"from_ordinal", hand_index}});

    last_play_ = LastPlay{*player, card, static_cast<uint32_t>(hand_index)};

    if (ecs::ActiveTypeReq* req =
            store.Get<ecs::ActiveTypeReq>(registries.match)) {
        const ecs::FaceSpec* face = store.Get<ecs::FaceSpec>(card);
        if (face != nullptr && !face->color.empty()) {
            req->type = face->color;
        } else {
            const auto facts = assembly_->card_facts.find(kind_id);
            if (facts != assembly_->card_facts.end()) {
                req->type = facts->second.color;
            }
        }
    }

    // TODO: dispatch before/after `play` and run the played card's
    //               behavior graph through the Resolver before settling.
    // TODO: dispatch the veto-capable `hand_empty` / `win_check` hooks
    //               before applying the default win.

    const ecs::Hand* after = store.Get<ecs::Hand>(*player);
    if (after != nullptr && after->cards.empty()) {
        DeclareHandEmptyWin(*player);
    } else {
        AdvanceTurn();
    }
    return true;
}

bool MatchInstance::DrawCard(const std::string& username) {
    if (!started_ || finished_) return false;

    ecs::EntityStore& store = assembly_->store;
    MatchRegistries& registries = assembly_->registries;

    const std::optional<ecs::Entity> player = FindPlayer(username);
    if (!player.has_value()) return false;
    const std::optional<ecs::Entity> current = CurrentPlayer();
    if (!current.has_value() || !(*current == *player)) return false;

    // TODO: dispatch the veto-capable `draw_attempt` hook per card and
    //               the `draw` hook after it lands; the engine owns draw
    //               stacking.
    const ecs::PileContents* draw =
        store.Get<ecs::PileContents>(registries.draw_pile);
    if (draw == nullptr) return false;
    if (draw->cards.empty() && !ReshuffleDiscardIntoDraw()) return false;

    std::optional<ecs::Entity> card =
        ops::DrawTop(store, registries.draw_pile);
    if (!card.has_value()) {
        if (!ReshuffleDiscardIntoDraw()) return false;
        card = ops::DrawTop(store, registries.draw_pile);
        if (!card.has_value()) return false;
    }
    if (!ops::MoveCardToZone(
            store, *card,
            ecs::ZoneRef{ecs::ZoneKind::kHand, *player})) {
        return false;
    }

    Emit("cards_drawn", json{{"player", username},
                             {"count", 1},
                             {"source", "draw_pile"}});

    // INFO: the drawn-card play decision (legacy DecideDrawnCard /
    //       progressive / force_play) is a seam; this slice always
    //       passes the turn after the draw.
    AdvanceTurn();
    return true;
}

bool MatchInstance::SubmitInput(const std::string& username,
                                const nlohmann::json& value) {
    (void)username;
    (void)value;
    // TODO: bind a window/prompt response and resume the Resolver.
    return false;
}

// --- tick / round boundary -------------------------------------------------

void MatchInstance::Tick() {
    if (!started_ || finished_ || assembly_ == nullptr) return;

    ecs::EntityStore& store = assembly_->store;
    MatchRegistries& registries = assembly_->registries;

    const std::size_t player_count = registries.players.size();
    if (player_count == 0) return;
    ecs::MatchMeta* meta = store.Get<ecs::MatchMeta>(registries.match);
    if (meta == nullptr) return;

    const uint32_t round =
        static_cast<uint32_t>(turns_elapsed_ / player_count);
    if (round > meta->round) {
        meta->round = round;
        Emit("round_advance", json{{"round", round}});
    }

    // TODO: advance the turn/window/status timers and run scheduled
    //             graphs whose duration elapsed; open windows when a graph
    //             reaches a `window` node.
}

// --- turn advance ----------------------------------------------------------

void MatchInstance::AdvanceTurn() {
    const std::optional<ecs::Entity> before = CurrentPlayer();

    // INFO: Reuse the `advance_turn` op so the direction / extra-turn /
    //       one-shot-skip bookkeeping exists in exactly one place.
    ops::ResolutionFrame frame;
    ops::OpContext context(assembly_->bus, assembly_->budget, frame);
    ops::OpArgs args("advance_turn");
    ops::OpResult result = assembly_->runtime.Invoke(
        "advance_turn", assembly_->store, args, context);
    for (json& event : result.events) {
        events_.push_back(std::move(event));
    }

    // INFO: only a real seat change counts toward a round; an extra turn
    //       replays the same player (the op's `from == to`) and does not.
    const std::optional<ecs::Entity> after = CurrentPlayer();
    if (before.has_value() && after.has_value()
        && !(*before == *after)) {
        ++turns_elapsed_;
    }
}

// --- reshuffle -------------------------------------------------------------

bool MatchInstance::ReshuffleDiscardIntoDraw() {
    ecs::EntityStore& store = assembly_->store;
    MatchRegistries& registries = assembly_->registries;

    const ecs::PileContents* discard =
        store.Get<ecs::PileContents>(registries.discard_pile);
    if (discard == nullptr || discard->cards.size() <= 1) return false;

    const ecs::Entity top = discard->cards.back();
    std::vector<ecs::Entity> moved(discard->cards.begin(),
                                   discard->cards.end() - 1);
    for (ecs::Entity card : moved) {
        ops::MoveCardToZone(
            store, card,
            ecs::ZoneRef{ecs::ZoneKind::kDrawPile, ecs::Entity{}});
    }
    ShuffleDrawPile();

    const ecs::PileContents* draw =
        store.Get<ecs::PileContents>(registries.draw_pile);
    Emit("reshuffle",
         json{{"draw_size", draw == nullptr ? 0 : draw->cards.size()},
              {"discard_size", discard->cards.size()}});
    return true;
}

void MatchInstance::ShuffleDrawPile() {
    ecs::EntityStore& store = assembly_->store;
    MatchRegistries& registries = assembly_->registries;

    ecs::PileContents* draw =
        store.Get<ecs::PileContents>(registries.draw_pile);
    if (draw == nullptr) return;

    if (std::optional<Rng> rng = RngFor(store, registries.match);
        rng.has_value()) {
        std::vector<ecs::Entity>& cards = draw->cards;
        for (std::size_t i = cards.size(); i > 1; --i) {
            const std::size_t j =
                static_cast<std::size_t>(rng->NextDraw() % i);
            std::swap(cards[i - 1], cards[j]);
        }
    }

    for (std::size_t i = 0; i < draw->cards.size(); ++i) {
        if (ecs::InZone* in = store.Get<ecs::InZone>(draw->cards[i])) {
            in->zone =
                ecs::ZoneRef{ecs::ZoneKind::kDrawPile, ecs::Entity{}};
            in->ordinal = static_cast<uint32_t>(i);
        }
    }
}

// --- win / placement -------------------------------------------------------

void MatchInstance::DeclareHandEmptyWin(ecs::Entity player) {
    ecs::EntityStore& store = assembly_->store;
    MatchRegistries& registries = assembly_->registries;

    ecs::Placements* placements =
        store.Get<ecs::Placements>(registries.match);
    const uint32_t place =
        placements == nullptr
            ? 1u
            : static_cast<uint32_t>(placements->order.size()) + 1u;
    if (placements != nullptr) placements->order.push_back(player);

    winner_ = player;
    finished_ = true;
    Emit("placement",
         json{{"player", PlayerUsername(store, player)}, {"place", place}});

    // TODO: emit `match_end` and dispatch the `match_end` hooks once
    //               the hook path owns the settle step.
}

// --- read-only accessors ---------------------------------------------------

bool MatchInstance::IsMatchOver() const { return finished_; }

std::string MatchInstance::GetWinner() const {
    if (!winner_.has_value()) return std::string();
    const ecs::EntityStore& store = assembly_->store;
    return PlayerUsername(store, *winner_);
}

std::vector<std::string> MatchInstance::GetPlacements() const {
    std::vector<std::string> names;
    if (assembly_ == nullptr) return names;
    const ecs::EntityStore& store = assembly_->store;
    const MatchRegistries& registries = assembly_->registries;
    const ecs::Placements* placements =
        store.Get<ecs::Placements>(registries.match);
    if (placements == nullptr) return names;
    names.reserve(placements->order.size());
    for (ecs::Entity player : placements->order) {
        names.push_back(PlayerUsername(store, player));
    }
    return names;
}

std::optional<ecs::Entity> MatchInstance::GetCurrentPlayer() const {
    return CurrentPlayer();
}

std::string MatchInstance::GetCurrentPlayerUsername() const {
    const std::optional<ecs::Entity> current = CurrentPlayer();
    if (!current.has_value()) return std::string();
    const ecs::EntityStore& store = assembly_->store;
    return PlayerUsername(store, *current);
}

std::optional<ecs::Entity> MatchInstance::FindPlayer(
    const std::string& username) const {
    if (assembly_ == nullptr) return std::nullopt;
    const ecs::EntityStore& store = assembly_->store;
    const MatchRegistries& registries = assembly_->registries;
    for (ecs::Entity player : registries.players) {
        if (PlayerUsername(store, player) == username) return player;
    }
    return std::nullopt;
}

std::optional<ecs::Entity> MatchInstance::CurrentPlayer() const {
    if (assembly_ == nullptr) return std::nullopt;
    const ecs::EntityStore& store = assembly_->store;
    const MatchRegistries& registries = assembly_->registries;
    for (ecs::Entity player : registries.players) {
        const ecs::TurnState* turn = store.Get<ecs::TurnState>(player);
        if (turn != nullptr && turn->is_current) return player;
    }
    return std::nullopt;
}

std::optional<ecs::Entity> MatchInstance::TopDiscard() const {
    if (assembly_ == nullptr) return std::nullopt;
    const ecs::EntityStore& store = assembly_->store;
    const MatchRegistries& registries = assembly_->registries;
    const ecs::PileContents* discard =
        store.Get<ecs::PileContents>(registries.discard_pile);
    if (discard == nullptr || discard->cards.empty()) return std::nullopt;
    return discard->cards.back();
}

json MatchInstance::ExportState() const {
    if (assembly_ == nullptr) {
        return json{{"status", "finished"}, {"started", false}};
    }

    const ecs::EntityStore& store = assembly_->store;
    const MatchRegistries& registries = assembly_->registries;

    json out = json::object();
    out["status"] = finished_ ? "finished" : "playing";
    out["started"] = started_;

    const ecs::MatchMeta* meta =
        store.Get<ecs::MatchMeta>(registries.match);
    out["round"] = meta == nullptr ? 0u : meta->round;
    out["direction"] =
        meta == nullptr ? 1 : static_cast<int>(meta->direction);

    const ecs::ActiveTypeReq* active =
        store.Get<ecs::ActiveTypeReq>(registries.match);
    out["active_type"] = (active != nullptr && active->type.has_value())
                             ? json(*active->type)
                             : json(nullptr);

    out["current_player"] = GetCurrentPlayerUsername();
    out["winner"] = GetWinner();
    out["placements"] = GetPlacements();

    const ecs::PileContents* draw =
        store.Get<ecs::PileContents>(registries.draw_pile);
    const ecs::PileContents* discard =
        store.Get<ecs::PileContents>(registries.discard_pile);
    out["draw_pile_size"] = draw == nullptr ? 0 : draw->cards.size();
    out["discard_pile_size"] =
        discard == nullptr ? 0 : discard->cards.size();

    if (const std::optional<ecs::Entity> top = TopDiscard();
        top.has_value()) {
        const ecs::CardIdentity* identity =
            store.Get<ecs::CardIdentity>(*top);
        const std::string kind =
            identity == nullptr ? std::string() : identity->kind_id;
        json top_json = json{{"kind", kind}};
        const auto facts = assembly_->card_facts.find(kind);
        if (facts != assembly_->card_facts.end()) {
            top_json["color"] = facts->second.color;
            top_json["value"] = facts->second.value;
        }
        out["top_card"] = std::move(top_json);
    } else {
        out["top_card"] = nullptr;
    }

    if (last_play_.has_value()) {
        const ecs::CardIdentity* identity =
            store.Get<ecs::CardIdentity>(last_play_->card);
        out["last_play"] = json{
            {"player", PlayerUsername(store, last_play_->player)},
            {"card", identity == nullptr ? std::string()
                                         : identity->kind_id},
            {"hand_ordinal", last_play_->hand_ordinal}};
    } else {
        out["last_play"] = nullptr;
    }

    json players = json::array();
    for (ecs::Entity player : registries.players) {
        const ecs::PlayerInfo* info =
            store.Get<ecs::PlayerInfo>(player);
        const ecs::Hand* hand = store.Get<ecs::Hand>(player);
        const ecs::TurnState* turn =
            store.Get<ecs::TurnState>(player);
        players.push_back(
            json{{"username", info == nullptr ? std::string()
                                              : info->username},
                 {"seat", info == nullptr ? 0u : info->seat},
                 {"is_bot", info != nullptr && info->is_bot},
                 {"card_count", hand == nullptr ? 0 : hand->cards.size()},
                 {"is_current", turn != nullptr && turn->is_current}});
    }
    out["players"] = std::move(players);
    return out;
}

std::optional<nlohmann::json> MatchInstance::PendingInput() const {
    // INFO: no window or prompt can be open before the engine.
    return std::nullopt;
}

std::vector<nlohmann::json> MatchInstance::TakeEvents() {
    std::vector<nlohmann::json> drained = std::move(events_);
    events_.clear();
    return drained;
}

// --- private helpers -------------------------------------------------------

void MatchInstance::Emit(std::string_view type, nlohmann::json payload) {
    events_.push_back(ops::MakeEvent(type, std::move(payload)));
}

}  // namespace match::engine
