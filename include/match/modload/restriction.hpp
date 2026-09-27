#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

#include "match/modload/mod_loader.hpp"

/**
 * @file restriction.hpp
 * @brief Play-restriction pipeline.
 *
 * Entries are ordered data components on the match entity. Vanilla ships its
 * entries as data; mods add and remove entries through `add_restriction`
 * / `remove_restriction`. The evaluation rule is pure and deterministic:
 *
 * - `deny` entries veto; the FIRST matching deny supplies the reason id.
 * - `allow` entries rescue an otherwise-denied play (jump-in pattern:
 *   deny out-of-turn, allow identical-card).
 * - A `deny` that fires together with any matching `allow` is rescued
 *   regardless of the entries' relative order; the reason is still the first
 *   matching deny, which keeps the outcome independent of allow placement.
 *
 * Condition evaluation itself belongs to the condition ops. The engine supplies
 * a `ConditionMatcher` bound to its engine context; this file owns only the
 * ordered-deny/allow decision.
 */

namespace match::modload {

/**
 * @struct RestrictionEntry
 * @brief `{id, phase: allow|deny, condition}`.
 */
struct RestrictionEntry {
    std::string id;              /**< stable entry id, e.g. `vanilla:turn_order`. */
    std::string phase;           /**< "allow" | "deny". */
    nlohmann::json condition;    /**< condition object; null = always. */
};

/**
 * @struct PlayAttempt
 * @brief The minimal play context the pipeline reasons over.
 *
 * The engine fills this from its engine state; `context` carries whatever extra
 * data the injected matcher needs (hand contents, turn ownership, ...).
 */
struct PlayAttempt {
    std::string player;         /**< acting player identifier. */
    std::string card_kind;      /**< full kind id of the attempted card. */
    bool in_turn = false;       /**< attempt is the acting player's own turn. */
    bool responding = false;    /**< attempt answers an open response window. */
    nlohmann::json context = nlohmann::json::object();
};

/**
 * @struct PlayDecision
 * @brief Pipeline outcome; `reason_id` is set only when denied.
 */
struct PlayDecision {
    bool allowed = true;
    std::string reason_id;
};

/**
 * @brief Predicate deciding whether an entry's condition matches an attempt.
 */
using ConditionMatcher =
    std::function<bool(const nlohmann::json& condition,
                       const PlayAttempt& attempt)>;

/**
 * @brief Evaluate the ordered restriction pipeline.
 */
inline PlayDecision EvaluatePlayRestrictions(
    const std::vector<RestrictionEntry>& entries,
    const PlayAttempt& attempt,
    const ConditionMatcher& condition_matches) {
    bool denied = false;
    bool rescued = false;
    std::string reason;

    for (const auto& entry : entries) {
        bool matches = entry.condition.is_null() || entry.condition.empty();
        if (!matches && condition_matches) {
            matches = condition_matches(entry.condition, attempt);
        }
        if (!matches) continue;

        if (entry.phase == "deny") {
            if (!denied) {
                denied = true;
                reason = entry.id;
            }
        } else if (entry.phase == "allow") {
            rescued = true;
        }
    }

    PlayDecision decision;
    if (denied && !rescued) {
        decision.allowed = false;
        decision.reason_id = reason;
    }
    return decision;
}

/**
 * @brief Parse and shape-check a `{id, phase, condition}` entry.
 *
 * Phase is one of `allow|deny`; id must be a `namespace:local` kind id.
 * Returns false with a message describing the first problem.
 */
inline bool ParseRestrictionEntry(const nlohmann::json& value,
                                  RestrictionEntry& out,
                                  std::string& error) {
    if (!value.is_object()) {
        error = "restriction entry must be an object";
        return false;
    }
    auto id = value.find("id");
    if (id == value.end() || !id->is_string()
        || !IsValidKindId(id->get<std::string>())) {
        error = "restriction entry id must be a `namespace:local` kind id";
        return false;
    }
    auto phase = value.find("phase");
    if (phase == value.end() || !phase->is_string()) {
        error = "restriction entry requires a string 'phase'";
        return false;
    }
    const std::string phase_token = phase->get<std::string>();
    if (phase_token != "allow" && phase_token != "deny") {
        error = "restriction entry phase must be 'allow' or 'deny'";
        return false;
    }
    out.id = id->get<std::string>();
    out.phase = phase_token;
    auto condition = value.find("condition");
    if (condition != value.end()) out.condition = *condition;
    return true;
}

}  // namespace match::modload
