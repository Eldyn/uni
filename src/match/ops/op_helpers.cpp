#include <match/ops/op_helpers.hpp>

#include <match/engine/match_assembler.hpp>
#include <match/rng.hpp>

#include <algorithm>
#include <map>
#include <string>
#include <utility>

/**
 * @file op_helpers.cpp
 * @brief Implementation of the shared op helper vocabulary.
 */

namespace match::ops {
namespace {

/** @brief Erase `card` from `hand.cards`. */
void EraseCard(std::vector<ecs::Entity>& cards, ecs::Entity card) {
    cards.erase(std::remove(cards.begin(), cards.end(), card), cards.end());
}

/** @brief Remove `card` from whichever container its zone names. */
void DetachCard(ecs::EntityStore& store, ecs::Entity card,
                const ecs::ZoneRef& zone) {
    switch (zone.kind) {
        case ecs::ZoneKind::kHand: {
            ecs::Hand* hand = store.Get<ecs::Hand>(zone.owner);
            if (hand != nullptr) EraseCard(hand->cards, card);
            break;
        }
        case ecs::ZoneKind::kDrawPile:
        case ecs::ZoneKind::kDiscardPile: {
            const ecs::PileKind kind =
                zone.kind == ecs::ZoneKind::kDrawPile
                    ? ecs::PileKind::kDraw
                    : ecs::PileKind::kDiscard;
            std::optional<ecs::Entity> pile = FindPile(store, kind);
            if (pile.has_value()) {
                ecs::PileContents* contents =
                    store.Get<ecs::PileContents>(*pile);
                if (contents != nullptr) EraseCard(contents->cards, card);
            }
            break;
        }
        case ecs::ZoneKind::kLimbo:
            break;
    }
}

/** @brief Renumber every hand card's ordinal to its display index. */
void ReindexHand(ecs::EntityStore& store, ecs::Entity owner,
                 const std::vector<ecs::Entity>& cards) {
    for (std::size_t i = 0; i < cards.size(); ++i) {
        ecs::InZone* in = store.Get<ecs::InZone>(cards[i]);
        if (in == nullptr) continue;
        in->zone = ecs::ZoneRef{ecs::ZoneKind::kHand, owner};
        in->ordinal = static_cast<uint32_t>(i);
    }
}

/** @brief Renumber every pile card's ordinal to its position (top = back). */
void ReindexPile(ecs::EntityStore& store, ecs::Entity pile,
                 const std::vector<ecs::Entity>& cards) {
    const ecs::PileContents* contents = store.Get<ecs::PileContents>(pile);
    const ecs::PileKind kind =
        contents != nullptr ? contents->kind : ecs::PileKind::kDraw;
    const ecs::ZoneKind zone_kind = kind == ecs::PileKind::kDraw
                                        ? ecs::ZoneKind::kDrawPile
                                        : ecs::ZoneKind::kDiscardPile;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        ecs::InZone* in = store.Get<ecs::InZone>(cards[i]);
        if (in == nullptr) continue;
        in->zone = ecs::ZoneRef{zone_kind, ecs::Entity{}};
        in->ordinal = static_cast<uint32_t>(i);
    }
}

}  // namespace

std::optional<ecs::Entity> FindMatch(ecs::EntityStore& store) {
    const std::vector<ecs::Entity> matches =
        store.EntitiesWith<ecs::MatchMeta>();
    if (matches.empty()) return std::nullopt;
    return matches.front();
}

uint64_t SplitMix64(uint64_t& state) {
    state += 0x9E3779B97F4A7C15ULL;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

std::optional<RngDraw> AdvanceRng(ecs::EntityStore& store) {
    const std::optional<ecs::Entity> match = FindMatch(store);
    if (!match.has_value()) return std::nullopt;
    ecs::RngState* rng = store.Get<ecs::RngState>(*match);
    if (rng == nullptr) return std::nullopt;
    const Rng::Roll roll = AdvanceRngState(*rng);
    RngDraw draw;
    draw.counter = roll.counter;
    draw.state = roll.state;
    return draw;
}

std::vector<ecs::Entity> PlayersBySeat(ecs::EntityStore& store) {
    std::vector<ecs::Entity> players = store.EntitiesWith<ecs::PlayerInfo>();
    std::stable_sort(players.begin(), players.end(),
                     [&store](ecs::Entity a, ecs::Entity b) {
                         const ecs::PlayerInfo* pa =
                             store.Get<ecs::PlayerInfo>(a);
                         const ecs::PlayerInfo* pb =
                             store.Get<ecs::PlayerInfo>(b);
                         const uint32_t sa = pa == nullptr ? 0 : pa->seat;
                         const uint32_t sb = pb == nullptr ? 0 : pb->seat;
                         if (sa != sb) return sa < sb;
                         return a.index < b.index;
                     });
    return players;
}

std::optional<ecs::Entity> FindCurrentPlayer(ecs::EntityStore& store) {
    for (ecs::Entity entity : store.EntitiesWith<ecs::TurnState>()) {
        const ecs::TurnState* turn = store.Get<ecs::TurnState>(entity);
        if (turn != nullptr && turn->is_current) return entity;
    }
    return std::nullopt;
}

std::optional<ecs::Entity> FindPile(ecs::EntityStore& store,
                                    ecs::PileKind kind) {
    for (ecs::Entity entity : store.EntitiesWith<ecs::PileContents>()) {
        const ecs::PileContents* pile = store.Get<ecs::PileContents>(entity);
        if (pile != nullptr && pile->kind == kind) return entity;
    }
    return std::nullopt;
}

std::vector<ecs::Entity> HandOf(const ecs::EntityStore& store,
                                ecs::Entity player) {
    const ecs::Hand* hand = store.Get<ecs::Hand>(player);
    return hand == nullptr ? std::vector<ecs::Entity>() : hand->cards;
}

const ecs::Hand* HandComponent(const ecs::EntityStore& store,
                               ecs::Entity player) {
    return store.Get<ecs::Hand>(player);
}

std::vector<ecs::Entity> PileOf(const ecs::EntityStore& store,
                                ecs::Entity pile) {
    const ecs::PileContents* contents = store.Get<ecs::PileContents>(pile);
    return contents == nullptr ? std::vector<ecs::Entity>() : contents->cards;
}

ecs::PileContents* PileComponent(ecs::EntityStore& store, ecs::Entity pile) {
    return store.Get<ecs::PileContents>(pile);
}

std::optional<ecs::ZoneRef> FindCardZone(const ecs::EntityStore& store,
                                         ecs::Entity card) {
    const ecs::InZone* in = store.Get<ecs::InZone>(card);
    if (in == nullptr) return std::nullopt;
    return in->zone;
}

std::optional<uint32_t> CardOrdinal(const ecs::EntityStore& store,
                                    ecs::Entity card) {
    const ecs::InZone* in = store.Get<ecs::InZone>(card);
    if (in == nullptr) return std::nullopt;
    return in->ordinal;
}

bool CardInZone(const ecs::EntityStore& store, ecs::Entity card,
                ecs::ZoneKind kind) {
    const std::optional<ecs::ZoneRef> zone = FindCardZone(store, card);
    return zone.has_value() && zone->kind == kind;
}

std::string CardKindId(const ecs::EntityStore& store, ecs::Entity card) {
    const ecs::CardIdentity* identity = store.Get<ecs::CardIdentity>(card);
    return identity == nullptr ? std::string() : identity->kind_id;
}

std::vector<ecs::Entity> CardsHeldBy(const ecs::EntityStore& store,
                                     ecs::Entity owner) {
    if (const ecs::Hand* hand = store.Get<ecs::Hand>(owner)) {
        return hand->cards;
    }
    if (const ecs::PileContents* pile = store.Get<ecs::PileContents>(owner)) {
        return pile->cards;
    }
    if (store.Has<ecs::CardIdentity>(owner)) {
        return std::vector<ecs::Entity>{owner};
    }
    return std::vector<ecs::Entity>();
}

bool MoveCardToZone(ecs::EntityStore& store, ecs::Entity card,
                    const ecs::ZoneRef& to) {
    if (!store.IsAlive(card)) return false;
    const ecs::InZone* current = store.Get<ecs::InZone>(card);
    if (current == nullptr) return false;

    // INFO: detach first so an in-place move (same container) cannot duplicate
    //       the card; the InZone pool is untouched by detach, so the zone copy
    //       below is read before any reindex.
    const ecs::ZoneRef from = current->zone;
    DetachCard(store, card, from);

    ecs::InZone* in = store.Get<ecs::InZone>(card);
    if (in == nullptr) return false;
    in->zone = ecs::ZoneRef{ecs::ZoneKind::kLimbo, ecs::Entity{}};
    in->ordinal = 0;

    switch (to.kind) {
        case ecs::ZoneKind::kHand: {
            ecs::Hand* hand = store.Get<ecs::Hand>(to.owner);
            if (hand == nullptr) return false;
            hand->cards.push_back(card);
            ReindexHand(store, to.owner, hand->cards);
            return true;
        }
        case ecs::ZoneKind::kDrawPile:
        case ecs::ZoneKind::kDiscardPile: {
            const ecs::PileKind kind = to.kind == ecs::ZoneKind::kDrawPile
                                           ? ecs::PileKind::kDraw
                                           : ecs::PileKind::kDiscard;
            std::optional<ecs::Entity> pile = FindPile(store, kind);
            if (!pile.has_value()) return false;
            ecs::PileContents* contents = store.Get<ecs::PileContents>(*pile);
            if (contents == nullptr) return false;
            contents->cards.push_back(card);
            ReindexPile(store, *pile, contents->cards);
            return true;
        }
        case ecs::ZoneKind::kLimbo:
            return true;
    }
    return false;
}

std::optional<ecs::Entity> DrawTop(ecs::EntityStore& store, ecs::Entity pile) {
    ecs::PileContents* contents = store.Get<ecs::PileContents>(pile);
    if (contents == nullptr || contents->cards.empty()) return std::nullopt;
    const ecs::Entity card = contents->cards.back();
    contents->cards.pop_back();
    if (ecs::InZone* in = store.Get<ecs::InZone>(card)) {
        in->zone = ecs::ZoneRef{ecs::ZoneKind::kLimbo, ecs::Entity{}};
        in->ordinal = 0;
    }
    ReindexPile(store, pile, contents->cards);
    return card;
}

bool SetHandOrder(ecs::EntityStore& store, ecs::Entity player,
                  const std::vector<ecs::Entity>& cards) {
    ecs::Hand* hand = store.Get<ecs::Hand>(player);
    if (hand == nullptr) return false;
    hand->cards = cards;
    ReindexHand(store, player, hand->cards);
    return true;
}

nlohmann::json MakeEvent(std::string_view type, nlohmann::json payload) {
    return nlohmann::json{{"type", std::string(type)},
                          {"payload", std::move(payload)}};
}

void BindLastRoll(ResolutionFrame& frame, int64_t total,
                  nlohmann::json outcomes) {
    frame.BindPromptValue(std::string(kLastRollFrameKey),
                          nlohmann::json{{"total", total},
                                         {"outcomes", std::move(outcomes)}});
}

const nlohmann::json* LastRoll(const ResolutionFrame& frame) {
    return frame.FindPromptValue(kLastRollFrameKey);
}

std::optional<int64_t> LastRollTotal(const ResolutionFrame& frame) {
    const nlohmann::json* value = LastRoll(frame);
    if (value == nullptr || !value->is_object()) return std::nullopt;
    auto it = value->find("total");
    if (it == value->end() || !it->is_number_integer()) return std::nullopt;
    return it->get<int64_t>();
}

void BindTurnsElapsed(ResolutionFrame& frame, int64_t turns) {
    frame.BindPromptValue(std::string(kTurnsElapsedFrameKey),
                          nlohmann::json(turns));
}

std::optional<int64_t> TurnsElapsed(const ResolutionFrame& frame) {
    const nlohmann::json* value =
        frame.FindPromptValue(kTurnsElapsedFrameKey);
    if (value == nullptr || !value->is_number_integer()) return std::nullopt;
    return value->get<int64_t>();
}

bool CardHasTag(const engine::MatchRegistries* registries,
                std::string_view kind_id, std::string_view tag) {
    // INFO: The table is per-match (`MatchRegistries::card_tags`),
    //       never a process global, so a concurrent match cannot change this
    //       result. A null handle means no assembled match (bare-op tests).
    if (registries == nullptr) return false;
    const CardTagTable& table = registries->card_tags;
    auto it = table.find(std::string(kind_id));
    if (it == table.end()) return false;
    for (const std::string& declared : it->second) {
        if (declared == tag) return true;
    }
    return false;
}

}  // namespace match::ops
