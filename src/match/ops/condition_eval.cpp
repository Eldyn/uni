#include <match/ops/op_helpers.hpp>
#include <match/status.hpp>

#include <match/modload/vocabulary.hpp>
#include <match/resolver.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file condition_eval.cpp
 * @brief Condition evaluators, registered on the registry.
 *
 * Every evaluator is `(store, args, ctx) -> bool`; `args` is already the
 * condition's argument object (the Resolver unwraps the `{keyword, args}` /
 * single-key sugar form before calling). Selector-typed args are resolved
 * through `ctx.frame` (`@self`, `@target`, ... bound by the Resolver at chain
 * start, `@responder`/`@card` on demand). An unbound or empty selector, a
 * missing/wrong-typed arg or a dead entity all evaluate false; no path throws
 * or crashes.
 *
 * `condition_ops.cpp` stays the family documentation stub (no OpCatalog rows);
 * this file is the real registration home, installed by
 * `RegisterDefaultConditions` (declared in `op_helpers.hpp`).
 */

namespace match::ops {
namespace {

using nlohmann::json;

/** @brief String arg or nullopt (absent / wrong type / non-object args). */
std::optional<std::string> ArgString(const json& args, std::string_view key) {
    if (!args.is_object()) return std::nullopt;
    auto it = args.find(std::string(key));
    if (it == args.end() || !it->is_string()) return std::nullopt;
    return it->get<std::string>();
}

/** @brief Integer arg or nullopt (absent / wrong type / non-object args). */
std::optional<int64_t> ArgInt(const json& args, std::string_view key) {
    if (!args.is_object()) return std::nullopt;
    auto it = args.find(std::string(key));
    if (it == args.end() || !it->is_number_integer()) return std::nullopt;
    return it->get<int64_t>();
}

/**
 * @brief Evaluate a `ComparisonTokens` token; unknown tokens are false.
 *
 * Accepts both the word (`lt`, `gte`) and symbol (`<`, `>=`) spellings from
 * `modload::ComparisonTokens()`.
 */
bool Compare(int64_t lhs, std::string_view cmp, int64_t rhs) {
    if (cmp == "lt" || cmp == "<") return lhs < rhs;
    if (cmp == "lte" || cmp == "<=") return lhs <= rhs;
    if (cmp == "eq" || cmp == "==") return lhs == rhs;
    if (cmp == "ne" || cmp == "!=") return lhs != rhs;
    if (cmp == "gt" || cmp == ">") return lhs > rhs;
    if (cmp == "gte" || cmp == ">=") return lhs >= rhs;
    return false;
}

/** @brief Entities a selector token is bound to in the frame; empty if none. */
std::vector<ecs::Entity> SelectorEntities(const OpContext& ctx,
                                          const std::string& token) {
    if (token.empty()) return std::vector<ecs::Entity>();
    const std::vector<ecs::Entity>* found = ctx.frame.FindSelector(token);
    return found == nullptr ? std::vector<ecs::Entity>() : *found;
}

/**
 * @brief Resolve a `target` arg through the frame; nullopt when unbound/empty.
 *
 * Conditions address a single entity, so the first bound entity is used.
 */
std::optional<ecs::Entity> TargetEntity(const json& args,
                                        const OpContext& ctx) {
    const std::optional<std::string> token = ArgString(args, "target");
    if (!token.has_value()) return std::nullopt;
    const std::vector<ecs::Entity> entities = SelectorEntities(ctx, *token);
    if (entities.empty()) return std::nullopt;
    return entities.front();
}

/**
 * @brief True when a frozen `kind_id` matches a `kKindRef` token.
 *
 * A token carrying `:` is a full `namespace:id` and must match exactly; a
 * local id (no `:`) matches the tail of a `namespace:id` kind id. Runtime
 * graphs never carry the current namespace, so this lenient tail match is the
 * deterministic resolution rule.
 */
bool KindRefMatches(const std::string& kind_id, const std::string& ref) {
    if (kind_id == ref) return true;
    if (ref.find(':') != std::string::npos) return false;
    if (kind_id.size() <= ref.size()) return false;
    const std::size_t offset = kind_id.size() - ref.size();
    return kind_id[offset - 1] == ':'
        && kind_id.compare(offset, ref.size(), ref) == 0;
}

/** @brief True when `card`'s kind matches a `kKindRef` token. */
bool CardMatchesKind(const ecs::EntityStore& store, ecs::Entity card,
                     const std::string& ref) {
    const ecs::CardIdentity* identity = store.Get<ecs::CardIdentity>(card);
    return identity != nullptr && KindRefMatches(identity->kind_id, ref);
}

/** @brief True when `card`'s kind declares `tag` in the per-match table. */
bool CardMatchesTag(const ecs::EntityStore& store, ecs::Entity card,
                    const std::string& tag,
                    const engine::MatchRegistries* registries) {
    const ecs::CardIdentity* identity = store.Get<ecs::CardIdentity>(card);
    return identity != nullptr
        && CardHasTag(registries, identity->kind_id, tag);
}

/** @brief `cmp`/`n` args -> compare `value`; false on missing/bad args. */
bool CompareArg(const json& args, int64_t value) {
    const std::optional<std::string> cmp = ArgString(args, "cmp");
    const std::optional<int64_t> n = ArgInt(args, "n");
    if (!cmp.has_value() || !n.has_value()) return false;
    return Compare(value, *cmp, *n);
}

// --- evaluators -------------------------------------------------------

/** @brief Any card held by the target has the given kind. */
bool EvalHasCardKind(ecs::EntityStore& store, const json& args,
                     OpContext& ctx) {
    const std::optional<std::string> token = ArgString(args, "target");
    const std::optional<std::string> kind = ArgString(args, "kind");
    if (!token.has_value() || !kind.has_value()) return false;
    for (ecs::Entity owner : SelectorEntities(ctx, *token)) {
        for (ecs::Entity card : CardsHeldBy(store, owner)) {
            if (CardMatchesKind(store, card, *kind)) return true;
        }
    }
    return false;
}

/** @brief Any card held by the target declares the given tag. */
bool EvalHasCardTag(ecs::EntityStore& store, const json& args,
                    OpContext& ctx) {
    const std::optional<std::string> token = ArgString(args, "target");
    const std::optional<std::string> tag = ArgString(args, "tag");
    if (!token.has_value() || !tag.has_value()) return false;
    for (ecs::Entity owner : SelectorEntities(ctx, *token)) {
        for (ecs::Entity card : CardsHeldBy(store, owner)) {
            if (CardMatchesTag(store, card, *tag, ctx.registries)) return true;
        }
    }
    return false;
}

/** @brief Target's hand size compared with `cmp`/`n`. */
bool EvalHandSize(ecs::EntityStore& store, const json& args, OpContext& ctx) {
    const std::optional<ecs::Entity> target = TargetEntity(args, ctx);
    if (!target.has_value()) return false;
    const ecs::Hand* hand = store.Get<ecs::Hand>(*target);
    if (hand == nullptr) return false;
    return CompareArg(args, static_cast<int64_t>(hand->cards.size()));
}

/** @brief The live seated-player count compared with `cmp`/`n`. */
bool EvalPlayerCount(ecs::EntityStore& store, const json& args, OpContext&) {
    const int64_t count =
        static_cast<int64_t>(store.EntitiesWith<ecs::PlayerInfo>().size());
    return CompareArg(args, count);
}

/** @brief The match's required active type equals `type`. */
bool EvalActiveTypeIs(ecs::EntityStore& store, const json& args, OpContext&) {
    const std::optional<std::string> type = ArgString(args, "type");
    if (!type.has_value()) return false;
    const std::optional<ecs::Entity> match = FindMatch(store);
    if (!match.has_value()) return false;
    const ecs::ActiveTypeReq* req = store.Get<ecs::ActiveTypeReq>(*match);
    if (req == nullptr || !req->type.has_value()) return false;
    return *req->type == *type;
}

/**
 * @brief The discard pile's top card matches kind, tag or face color.
 *
 * `type` is the card's colour (UNI's card type): the phase-1 face carries it
 * in `face_spec.color`, matching the token `set_active_type` uses.
 */
bool EvalTopOfDiscard(ecs::EntityStore& store, const json& args,
                      OpContext& ctx) {
    const std::optional<std::string> kind = ArgString(args, "kind");
    const std::optional<std::string> tag = ArgString(args, "tag");
    const std::optional<std::string> type = ArgString(args, "type");
    if (!kind.has_value() && !tag.has_value() && !type.has_value()) {
        return false;
    }

    const std::optional<ecs::Entity> pile =
        FindPile(store, ecs::PileKind::kDiscard);
    if (!pile.has_value()) return false;
    const ecs::PileContents* contents = store.Get<ecs::PileContents>(*pile);
    if (contents == nullptr || contents->cards.empty()) return false;
    const ecs::Entity top = contents->cards.back();

    if (kind.has_value()) return CardMatchesKind(store, top, *kind);
    if (tag.has_value()) {
        return CardMatchesTag(store, top, *tag, ctx.registries);
    }
    const ecs::FaceSpec* face = store.Get<ecs::FaceSpec>(top);
    return face != nullptr && face->color == *type;
}

/**
 * @brief The just-drawn card is playable under the vanilla colour/value rules.
 *
 * Reads the frame selector `@drawn_card` (bound by the engine on a `draw` /
 * `draw_attempt` dispatch). Playable means the legacy
 * `StandardRule::ValidatePlay` outcome for an in-turn attempt: a wild card, or
 * colour == active type, or value == the discard top's value. Fail-safe false
 * when no drawn card is bound, it is dead, or its face facts are unknown.
 */
bool EvalDrawnCardPlayable(ecs::EntityStore& store, const json&,
                           OpContext& ctx) {
    const std::vector<ecs::Entity>* drawn =
        ctx.frame.FindSelector("@drawn_card");
    if (drawn == nullptr || drawn->empty()) return false;
    const ecs::Entity card = drawn->front();
    if (!store.IsAlive(card)) return false;

    const ecs::FaceSpec* face = store.Get<ecs::FaceSpec>(card);
    if (face == nullptr) return false;
    // INFO: legacy Type::kWhite is the wild face colour; wild is always legal.
    if (face->color == "white") return true;

    if (const std::optional<ecs::Entity> match = FindMatch(store);
        match.has_value()) {
        const ecs::ActiveTypeReq* req = store.Get<ecs::ActiveTypeReq>(*match);
        if (req != nullptr && req->type.has_value()
            && *req->type == face->color) {
            return true;
        }
    }

    const std::optional<ecs::Entity> pile =
        FindPile(store, ecs::PileKind::kDiscard);
    if (!pile.has_value()) return false;
    const ecs::PileContents* contents = store.Get<ecs::PileContents>(*pile);
    if (contents == nullptr || contents->cards.empty()) return false;
    const ecs::FaceSpec* top =
        store.Get<ecs::FaceSpec>(contents->cards.back());
    return top != nullptr && !face->label.empty() && face->label == top->label;
}

/**
 * @brief The draw hook's cause (frame fact `draw_cause`) equals `is`.
 *
 * The fact is bound by the engine on `draw` / `draw_attempt` dispatches;
 * fail-safe false when it is absent (non-draw hooks).
 */
bool EvalDrawCause(ecs::EntityStore&, const json& args, OpContext& ctx) {
    const std::optional<std::string> expected = ArgString(args, "is");
    const json* cause = ctx.frame.FindFact("draw_cause");
    if (!expected.has_value() || cause == nullptr || !cause->is_string()) {
        return false;
    }
    return cause->get<std::string>() == *expected;
}

/**
 * @brief Target carries a status of the given kind.
 *
 * Addresses the multi-instance `status_list` container through
 * `match::status`; any instance of the kind satisfies the condition.
 */
bool EvalStatusActive(ecs::EntityStore& store, const json& args,
                      OpContext& ctx) {
    const std::optional<std::string> token = ArgString(args, "target");
    const std::optional<std::string> status_kind =
        ArgString(args, "status_kind");
    if (!token.has_value() || !status_kind.has_value()) return false;
    for (ecs::Entity entity : SelectorEntities(ctx, *token)) {
        if (status::Has(store, entity, *status_kind)) return true;
    }
    return false;
}

/** @brief Target's accumulated draw debt compared with `cmp`/`n`. */
bool EvalDrawDebt(ecs::EntityStore& store, const json& args, OpContext& ctx) {
    const std::optional<ecs::Entity> target = TargetEntity(args, ctx);
    if (!target.has_value()) return false;
    const ecs::DrawDebt* debt = store.Get<ecs::DrawDebt>(*target);
    if (debt == nullptr) return false;
    return CompareArg(args, static_cast<int64_t>(debt->count));
}

/** @brief The most recent roll total compared with `cmp`/`n`. */
bool EvalRolled(ecs::EntityStore&, const json& args, OpContext& ctx) {
    const std::optional<int64_t> total = LastRollTotal(ctx.frame);
    if (!total.has_value()) return false;
    return CompareArg(args, *total);
}

/** @brief The match direction equals `fwd` / `rev`. */
bool EvalIsDirection(ecs::EntityStore& store, const json& args, OpContext&) {
    const std::optional<std::string> direction = ArgString(args, "direction");
    if (!direction.has_value()) return false;
    const std::optional<ecs::Entity> match = FindMatch(store);
    if (!match.has_value()) return false;
    const ecs::MatchMeta* meta = store.Get<ecs::MatchMeta>(*match);
    if (meta == nullptr) return false;
    if (*direction == "fwd") return meta->direction == ecs::Direction::kForward;
    if (*direction == "rev") return meta->direction == ecs::Direction::kReverse;
    return false;
}

/** @brief The match round counter compared with `cmp`/`n`. */
bool EvalRound(ecs::EntityStore& store, const json& args, OpContext&) {
    const std::optional<ecs::Entity> match = FindMatch(store);
    if (!match.has_value()) return false;
    const ecs::MatchMeta* meta = store.Get<ecs::MatchMeta>(*match);
    if (meta == nullptr) return false;
    return CompareArg(args, static_cast<int64_t>(meta->round));
}

/** @brief The match turns-elapsed counter compared with `cmp`/`n`. */
bool EvalTurnsElapsed(ecs::EntityStore&, const json& args, OpContext& ctx) {
    const std::optional<int64_t> turns = TurnsElapsed(ctx.frame);
    if (!turns.has_value()) return false;
    return CompareArg(args, *turns);
}

/** @brief Always true. */
bool EvalAlways(ecs::EntityStore&, const json&, OpContext&) { return true; }

/** @brief Always false. */
bool EvalNever(ecs::EntityStore&, const json&, OpContext&) { return false; }

/**
 * @brief Fail-safe false for play-context predicates.
 *
 * Play-context keywords (`ConditionCatalog()` rows with `play_context`) are
 * evaluated against a `PlayAttempt` by the restriction pipeline's
 * `PlayConditionMatcher` (`match/modload/play_conditions.hpp`). The generic
 * resolver never carries an attempt, so binding them here as false keeps the
 * whole catalog registered while never fabricating a match.
 */
bool EvalPlayContextUnbound(ecs::EntityStore&, const json&, OpContext&) {
    return false;
}

}  // namespace

void RegisterDefaultConditions(resolver::ConditionRegistry& registry) {
    // INFO: the single place production condition bodies are bound. Keyed by
    //       the exact ConditionCatalog() keywords; unknown keywords still fall
    //       back to the registry's logged false.
    registry.Register("has_card_kind", &EvalHasCardKind);
    registry.Register("has_card_tag", &EvalHasCardTag);
    registry.Register("hand_size", &EvalHandSize);
    registry.Register("player_count", &EvalPlayerCount);
    registry.Register("active_type_is", &EvalActiveTypeIs);
    registry.Register("top_of_discard", &EvalTopOfDiscard);
    registry.Register("drawn_card_playable", &EvalDrawnCardPlayable);
    registry.Register("draw_cause", &EvalDrawCause);
    registry.Register("status_active", &EvalStatusActive);
    registry.Register("draw_debt", &EvalDrawDebt);
    registry.Register("rolled", &EvalRolled);
    registry.Register("is_direction", &EvalIsDirection);
    registry.Register("round", &EvalRound);
    registry.Register("turns_elapsed", &EvalTurnsElapsed);
    registry.Register("always", &EvalAlways);
    registry.Register("never", &EvalNever);

    // INFO: play-context keywords are owned by the restriction pipeline; the
    //       store-domain resolver has no PlayAttempt, so they stay false here.
    for (const modload::ConditionSignature& signature :
         modload::ConditionCatalog()) {
        if (signature.play_context) {
            registry.Register(signature.keyword, &EvalPlayContextUnbound);
        }
    }
}

}  // namespace match::ops
