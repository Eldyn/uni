#pragma once

#include <match/ecs/components.hpp>
#include <match/ecs/entity_store.hpp>
#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file op_helpers.hpp
 * @brief Shared card/pile/player traversal and mutation helpers for the ops.
 *
 * Every 10.x op family shares the same view of the entity store: a match
 * entity, player entities ordered by seat, player hands, draw/discard pile
 * entities, cards with an `in_zone` component and a moving card that must keep
 * `in_zone` / `hand` / `pile_contents` consistent. The op layer freezes that
 * vocabulary here so later slices and the timer layer and the engine consume it
 * rather than re-walking the store.
 *
 * Also frozen here:
 * - the event-descriptor shape (`{ "type", "payload" }`) via `MakeEvent`;
 * - the `rolled` / `turns_elapsed` condition seams (reserved `ResolutionFrame`
 *   prompt keys) so the op layer's `roll` op and the engine's turn tracking can
 *   bind them;
 * - the content-static card tag table `has_card_tag` reads (the timer layer
 *   populates it from loaded `CardDef`s);
 * - `RegisterDefaultConditions`, the one place the evaluators are bound
 *   to the `ConditionRegistry` (the condition analogue of
 *   `RegisterDefaultOps`).
 */

namespace match::resolver {
class ConditionRegistry;
}  // namespace match::resolver

namespace match::ops {

// --- reserved ResolutionFrame keys -----------------------------------------
//
// INFO: Frame exposes `prompt_values` (node id -> result) and `selectors`
//       (token -> entities). Conditions are single-purpose and carry no new
//       state slot, so the two cross-invocation facts they need (`rolled`,
//       `turns_elapsed`) ride the frame's prompt-value map under reserved keys.
//       Node ids never start with `$`, so there is no collision.

/** @brief Frame key holding the most recent `roll` result object. */
inline constexpr std::string_view kLastRollFrameKey = "$last_roll";

/** @brief Frame key holding the match's turns-elapsed counter. */
inline constexpr std::string_view kTurnsElapsedFrameKey = "$turns_elapsed";

// --- traversal -------------------------------------------------------------

/**
 * @brief The match entity (first `match_meta` carrier), or nullopt.
 */
std::optional<ecs::Entity> FindMatch(ecs::EntityStore& store);

/**
 * @brief Every player entity in ascending seat order, ties by slot index.
 */
std::vector<ecs::Entity> PlayersBySeat(ecs::EntityStore& store);

/**
 * @brief The player whose `turn_state.is_current` is set, or nullopt.
 */
std::optional<ecs::Entity> FindCurrentPlayer(ecs::EntityStore& store);

/**
 * @brief The pile entity carrying `pile_contents.kind == kind`, or nullopt.
 */
std::optional<ecs::Entity> FindPile(ecs::EntityStore& store, ecs::PileKind kind);

/**
 * @brief Copy of `player`'s hand card refs; empty when not a hand carrier.
 */
std::vector<ecs::Entity> HandOf(const ecs::EntityStore& store,
                                ecs::Entity player);

/**
 * @brief `player`'s live `hand` component, or nullptr (dead/missing).
 */
const ecs::Hand* HandComponent(const ecs::EntityStore& store,
                               ecs::Entity player);

/**
 * @brief Copy of `pile`'s card refs; empty when not a pile carrier.
 */
std::vector<ecs::Entity> PileOf(const ecs::EntityStore& store,
                                ecs::Entity pile);

/**
 * @brief `pile`'s mutable `pile_contents`, or nullptr (dead/missing).
 */
ecs::PileContents* PileComponent(ecs::EntityStore& store, ecs::Entity pile);

/**
 * @brief Copy of a card's current zone, or nullopt (dead / not a card).
 */
std::optional<ecs::ZoneRef> FindCardZone(const ecs::EntityStore& store,
                                         ecs::Entity card);

/**
 * @brief A card's ordinal within its zone, or nullopt (dead / not a card).
 */
std::optional<uint32_t> CardOrdinal(const ecs::EntityStore& store,
                                    ecs::Entity card);

/** @brief True when a card's `in_zone.kind` is `kind`. */
bool CardInZone(const ecs::EntityStore& store, ecs::Entity card,
                ecs::ZoneKind kind);

/** @brief A card's frozen `kind_id`, or empty string. */
std::string CardKindId(const ecs::EntityStore& store, ecs::Entity card);

/**
 * @brief Cards "held by" an entity: a hand player, a pile, or a card itself.
 *
 * Lets a single target selector (player / pile / card) feed the card queries
 * the conditions and ops share. Unknown carriers yield an empty
 * vector (fail-safe).
 */
std::vector<ecs::Entity> CardsHeldBy(const ecs::EntityStore& store,
                                     ecs::Entity owner);

// --- mutation --------------------------------------------------------------

/**
 * @brief Move a card into `to`, keeping zone / hand / pile in sync.
 *
 * Removes the card from its old container, updates its `in_zone`, appends it
 * to the destination container and renumbers ordinals. A pile destination
 * pushes at the back (top); a hand destination appends (display order end).
 *
 * @return false on a dead card, a card without `in_zone`, a hand destination
 *         whose owner lacks a `hand`, or a pile destination with no pile
 *         entity. A rejected move leaves the card in `limbo` rather than
 *         stranded in its old container.
 */
bool MoveCardToZone(ecs::EntityStore& store, ecs::Entity card,
                    const ecs::ZoneRef& to);

/**
 * @brief Remove and return the top (back) card of a pile; nullopt when empty.
 *
 * The drawn card is placed in `limbo`; the caller routes it onward with
 * `MoveCardToZone`.
 */
std::optional<ecs::Entity> DrawTop(ecs::EntityStore& store, ecs::Entity pile);

/**
 * @brief Replace a player's hand order and renumber the listed cards.
 *
 * Sets `hand.cards = cards`; for each listed card present in the store, sets
 * its `in_zone` to that player's hand with the matching ordinal.
 *
 * @return false when `player` has no `hand` component.
 */
bool SetHandOrder(ecs::EntityStore& store, ecs::Entity player,
                  const std::vector<ecs::Entity>& cards);

// --- event descriptors ------------------------------------------------

/**
 * @brief Build `{ "type": type, "payload": payload }` for `OpResult.events`.
 *
 * No `seq`; the view layer assigns ordering. `effects` stay reserved and empty.
 */
nlohmann::json MakeEvent(std::string_view type,
                         nlohmann::json payload = nlohmann::json::object());

// --- `rolled` / `turns_elapsed` seams --------------------------------------

/**
 * @brief Bind the most recent roll result (total + outcomes) to `frame`.
 *
 * The op layer's `roll` op calls this after computing outcomes; the `rolled`
 * condition reads it back through `LastRollTotal`.
 */
void BindLastRoll(ResolutionFrame& frame, int64_t total,
                  nlohmann::json outcomes = nlohmann::json::array());

/** @brief The raw last-roll object, or nullptr when none is bound. */
const nlohmann::json* LastRoll(const ResolutionFrame& frame);

/** @brief The last roll's total, or nullopt when unbound / malformed. */
std::optional<int64_t> LastRollTotal(const ResolutionFrame& frame);

/**
 * @brief Bind the match's turns-elapsed counter to `frame`.
 *
 * The engine's turn tracking owns the counter;
 * the `turns_elapsed` condition reads it back here.
 */
void BindTurnsElapsed(ResolutionFrame& frame, int64_t turns);

/** @brief The bound turns-elapsed counter, or nullopt when unbound. */
std::optional<int64_t> TurnsElapsed(const ResolutionFrame& frame);

// --- card tag table (`has_card_tag`) ---------------------------------------

/**
 * @brief Content-static map of card kind id -> declared tags.
 *
 * Tags live in a card's definition and are not part of any the store
 * component, so the runtime keeps one process-wide table. The timer layer and
 * the engine populate it once per loaded match set; it is not per-match state
 * and never mutated during resolution. Not thread-safe for concurrent
 * registration; reads after load are safe to share.
 */
using CardTagTable = std::map<std::string, std::vector<std::string>, std::less<>>;

/** @brief Replace the whole tag table (content load). */
void SetCardTagTable(CardTagTable table);

/** @brief Insert/overwrite one kind's tags. */
void RegisterCardTags(std::string_view kind_id,
                      std::vector<std::string> tags);

/** @brief Drop every registered tag (tests / reload). */
void ClearCardTags();

/** @brief True when `kind_id` declares `tag`. */
bool CardHasTag(std::string_view kind_id, std::string_view tag);

/** @brief The whole tag table (read-only). */
const CardTagTable& CardTags();

// --- condition registration -----------------------------------------

/**
 * @brief Install every condition evaluator into `registry`.
 *
 * The condition analogue of `RegisterDefaultOps`: The op layer-d, the engine
 * and tests call this once so the Resolver's `branch` / `where` / `veto` paths
 * have real bodies. Registration is idempotent (replace-on-conflict), matching
 * `ConditionRegistry::Register`.
 */
void RegisterDefaultConditions(resolver::ConditionRegistry& registry);

}  // namespace match::ops
