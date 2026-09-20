#include "match/modload/play_conditions.hpp"

#include "match/modload/vocabulary.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>

/**
 * @file play_conditions.cpp
 * @brief Play-context condition evaluators.
 *
 * Each predicate mirrors a legacy legality check in `src/match/rules/*.cpp`;
 * the comment on each branch cites the source line. Facts are resolved only
 * through the caller's `PlayCardLookup` and the keys documented in
 * `play_conditions.hpp` — no card data is embedded here.
 */

namespace match::modload {
namespace {

using nlohmann::json;

/** @brief String arg or nullopt (absent / wrong type / non-object args). */
std::optional<std::string> ArgString(const json& args, std::string_view key) {
    if (!args.is_object()) return std::nullopt;
    auto it = args.find(std::string(key));
    if (it == args.end() || !it->is_string()) return std::nullopt;
    return it->get<std::string>();
}

/** @brief `context[key]` string or nullopt (absent / wrong type). */
std::optional<std::string> CtxString(const json& context,
                                     std::string_view key) {
    return ArgString(context, key);
}

/** @brief `context[key]` array or nullopt (absent / wrong type). */
std::optional<json> CtxArray(const json& context, std::string_view key) {
    if (!context.is_object()) return std::nullopt;
    auto it = context.find(std::string(key));
    if (it == context.end() || !it->is_array()) return std::nullopt;
    return *it;
}

/**
 * @brief True when a frozen `kind_id` matches a `kKindRef` token.
 *
 * Same lenient tail-match rule as the evaluators: a token carrying `:`
 * must match exactly; a local id matches the tail of a `namespace:id`.
 */
bool KindRefMatches(const std::string& kind_id, const std::string& ref) {
    if (kind_id == ref) return true;
    if (ref.find(':') != std::string::npos) return false;
    if (kind_id.size() <= ref.size()) return false;
    const std::size_t offset = kind_id.size() - ref.size();
    return kind_id[offset - 1] == ':'
        && kind_id.compare(offset, ref.size(), ref) == 0;
}

/** @brief True when `tags` contains `tag`. */
bool HasTag(const std::vector<std::string>& tags, const std::string& tag) {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

/** @brief Wild cards are the `white` face colour (legacy `Type::kWhite`). */
bool IsWild(const PlayCardFacts& facts) { return facts.color == "white"; }

/** @brief Resolve the attempted card's facts; false when unknown/unbound. */
bool ResolveAttempted(const PlayAttempt& attempt, const PlayCardLookup& lookup,
                      PlayCardFacts& out) {
    if (!lookup || attempt.card_kind.empty()) return false;
    return lookup(attempt.card_kind, out);
}

/** @brief Resolve the discard top's facts from `context["top_kind"]`. */
bool ResolveTop(const PlayAttempt& attempt, const PlayCardLookup& lookup,
                PlayCardFacts& out) {
    const std::optional<std::string> kind = CtxString(attempt.context,
                                                      "top_kind");
    if (!kind.has_value() || kind->empty() || !lookup) return false;
    return lookup(*kind, out);
}

/** @brief "attempted value equals the discard top's value" (standard.cpp). */
bool AttemptedMatchesTop(const PlayAttempt& attempt,
                         const PlayCardLookup& lookup,
                         const PlayCardFacts& attempted) {
    if (attempted.value.empty()) return false;
    PlayCardFacts top;
    if (!ResolveTop(attempt, lookup, top)) return false;
    return attempted.value == top.value;
}

/** @brief "attempted colour equals the active type" (standard.cpp:23). */
bool AttemptedMatchesActive(const PlayAttempt& attempt,
                            const PlayCardFacts& attempted) {
    if (attempted.color.empty()) return false;
    const std::optional<std::string> active = CtxString(attempt.context,
                                                        "active_type");
    return active.has_value() && !active->empty()
        && attempted.color == *active;
}

/** @brief "attempted kind is in `context["hand"]`" (must_own_card). */
bool OwnsAttempted(const PlayAttempt& attempt) {
    const std::optional<json> hand = CtxArray(attempt.context, "hand");
    if (!hand.has_value() || attempt.card_kind.empty()) return false;
    for (const auto& item : *hand) {
        if (item.is_string() && item.get<std::string>() == attempt.card_kind) {
            return true;
        }
    }
    return false;
}

/** @brief "player holds a card whose colour equals the active type". */
bool HoldsActiveColor(const PlayAttempt& attempt,
                      const PlayCardLookup& lookup) {
    const std::optional<std::string> active = CtxString(attempt.context,
                                                        "active_type");
    if (!active.has_value() || active->empty() || !lookup) return false;
    const std::optional<json> hand = CtxArray(attempt.context, "hand");
    if (!hand.has_value()) return false;
    for (const auto& item : *hand) {
        if (!item.is_string()) continue;
        PlayCardFacts facts;
        if (lookup(item.get<std::string>(), facts)
            && facts.color == *active) {
            return true;
        }
    }
    return false;
}

/** @brief Keywords whose args are required; used only for early dispatch. */
bool NeedsAttemptedFacts(const std::string& keyword) {
    return keyword == "plays_tag" || keyword == "plays_color"
        || keyword == "plays_value" || keyword == "plays_matches_active"
        || keyword == "plays_matches_top" || keyword == "plays_mismatch"
        || keyword == "plays_identical_to_top" || keyword == "plays_bluffing";
}

}  // namespace

bool EvaluatePlayCondition(const nlohmann::json& condition,
                           const PlayAttempt* attempt,
                           const PlayCardLookup& lookup) {
    // INFO: no PlayAttempt bound -> fail-safe false for every predicate.
    if (attempt == nullptr) return false;
    if (!condition.is_object() || condition.size() != 1) return false;

    const auto it = condition.begin();
    const std::string& keyword = it.key();
    const json args = it.value().is_object() ? it.value() : json::object();

    // Turn context: no card facts required.
    if (keyword == "in_turn") return attempt->in_turn;
    if (keyword == "plays_out_of_turn") return !attempt->in_turn;

    // Identity by kind: reads `card_kind` directly, no facts lookup.
    if (keyword == "plays_kind") {
        const std::optional<std::string> kind = ArgString(args, "kind");
        return kind.has_value()
            && KindRefMatches(attempt->card_kind, *kind);
    }

    // Hand ownership: kind ids in `context["hand"]`.
    if (keyword == "owns_card") return OwnsAttempted(*attempt);
    if (keyword == "plays_unowned") {
        // INFO: an unknown hand must never deny (`must_own_card` fail-safe).
        if (!CtxArray(attempt->context, "hand").has_value()) return false;
        return !OwnsAttempted(*attempt);
    }

    if (!NeedsAttemptedFacts(keyword)) return false;
    PlayCardFacts attempted;
    if (!ResolveAttempted(*attempt, lookup, attempted)) return false;

    if (keyword == "plays_color") {
        const std::optional<std::string> color = ArgString(args, "color");
        return color.has_value() && attempted.color == *color;
    }
    if (keyword == "plays_value") {
        const std::optional<std::string> value = ArgString(args, "value");
        return value.has_value() && attempted.value == *value;
    }
    if (keyword == "plays_tag") {
        const std::optional<std::string> tag = ArgString(args, "tag");
        return tag.has_value() && HasTag(attempted.tags, *tag);
    }
    if (keyword == "plays_matches_active") {
        // standard.cpp:23: played_type == state->active_type.
        return AttemptedMatchesActive(*attempt, attempted);
    }
    if (keyword == "plays_matches_top") {
        // standard.cpp:27-34: played_value == top_value.
        return AttemptedMatchesTop(*attempt, lookup, attempted);
    }
    if (keyword == "plays_identical_to_top") {
        // jump_in.cpp:14-15: same type AND same value as the top.
        if (attempted.color.empty() || attempted.value.empty()) return false;
        PlayCardFacts top;
        if (!ResolveTop(*attempt, lookup, top)) return false;
        return attempted.color == top.color && attempted.value == top.value;
    }
    if (keyword == "plays_mismatch") {
        // standard.cpp:13-36: invalid iff not wild, not active-type match and
        // not top-value match. This is the `match_type_or_value` deny.
        if (IsWild(attempted)) return false;
        const bool matches_active = AttemptedMatchesActive(*attempt, attempted);
        const bool matches_top =
            AttemptedMatchesTop(*attempt, lookup, attempted);
        return !matches_active && !matches_top;
    }
    if (keyword == "plays_bluffing") {
        // no_bluffing.cpp:12-30: value is the +4 AND the player holds a card
        // whose type equals the active type.
        const std::optional<std::string> value = ArgString(args, "value");
        if (!value.has_value() || attempted.value != *value) return false;
        return HoldsActiveColor(*attempt, lookup);
    }
    return false;
}

PlayConditionMatcher::PlayConditionMatcher(PlayCardLookup lookup)
    : lookup_(std::move(lookup)) {}

bool PlayConditionMatcher::operator()(const nlohmann::json& condition,
                                      const PlayAttempt& attempt) const {
    return EvaluatePlayCondition(condition, &attempt, lookup_);
}

bool PlayConditionMatcher::Matches(const nlohmann::json& condition,
                                   const PlayAttempt& attempt) const {
    return EvaluatePlayCondition(condition, &attempt, lookup_);
}

bool IsPlayConditionKeyword(const std::string& keyword) {
    const ConditionSignature* signature = FindCondition(keyword);
    return signature != nullptr && signature->play_context;
}

}  // namespace match::modload
