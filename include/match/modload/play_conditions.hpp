#pragma once

#include "match/modload/restriction.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

/**
 * @file play_conditions.hpp
 * @brief Play-context condition evaluators for the restriction pipeline.
 *
 *  ships vanilla play legality as restriction entries whose
 * conditions the pipeline evaluates via a `ConditionMatcher`. The generic
 * catalog reasons over the entity store; these predicates instead
 * reason over a `PlayAttempt` — who is acting, what card they are trying, and
 * whether it is their turn. The validator adds them to `ConditionCatalog()`
 * with `play_context = true`.
 *
 * The attempted card's colour/value/tags are never hardcoded: the caller
 * supplies a `PlayCardLookup` resolving a full kind id to its content facts.
 * The top-of-discard kind, the match's active type and the acting player's
 * hand kind ids ride in `PlayAttempt::context`:
 *
 * - `context["top_kind"]`   string  — top discard kind id (optional).
 * - `context["active_type"]` string — current active colour/type (optional).
 * - `context["hand"]`       array<string> — kind ids held by `attempt.player`.
 *
 * Every predicate is fail-safe false: an unbound attempt, an empty lookup, a
 * missing/malformed context key or an unknown kind all yield false rather
 * than throwing. Evaluated without a `PlayAttempt` (the generic registry path)
 * every keyword is false.
 */

namespace match::modload {

/**
 * @struct PlayCardFacts
 * @brief Content facts a play predicate reads for one card kind.
 *
 * `color` is the face colour (`"red"`, `"white"` for wild, ...); `value` is
 * the face label (`"7"`, `"jolly_draw4"`, ...); `tags` are the kind's declared
 * tags. Empty fields mean "unknown".
 */
struct PlayCardFacts {
    std::string color;
    std::string value;
    std::vector<std::string> tags;
};

/**
 * @brief Caller-supplied kind -> facts lookup.
 *
 * Returns false when the kind is unknown; `out` is then unspecified.
 */
using PlayCardLookup =
    std::function<bool(const std::string& kind_id, PlayCardFacts& out)>;

/**
 * @brief Evaluate one play-context condition against `attempt`.
 *
 * `attempt` may be null, in which case every predicate is false (the
 * fail-safe "no PlayAttempt bound" path). The condition is the single-keyword
 * object form used across the catalog: `{"<keyword>": {<args>}}`. Unknown or
 * malformed conditions are false.
 */
bool EvaluatePlayCondition(const nlohmann::json& condition,
                           const PlayAttempt* attempt,
                           const PlayCardLookup& lookup);

/**
 * @class PlayConditionMatcher
 * @brief `ConditionMatcher` the engine installs on the restriction pipeline.
 */
class PlayConditionMatcher {
   public:
    explicit PlayConditionMatcher(PlayCardLookup lookup);

    /** @brief Pipeline entry point; same shape as `ConditionMatcher`. */
    bool operator()(const nlohmann::json& condition,
                    const PlayAttempt& attempt) const;

    /** @brief Named alias for `operator()` (readability at call sites). */
    bool Matches(const nlohmann::json& condition,
                 const PlayAttempt& attempt) const;

   private:
    PlayCardLookup lookup_;
};

/** @brief True when `keyword` is a `play_context` member of the catalog. */
bool IsPlayConditionKeyword(const std::string& keyword);

}  // namespace match::modload
