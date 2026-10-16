#pragma once

#include <match/modload/artifacts.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>

/**
 * @file matcher.hpp
 * @brief The streak matcher and the view of a client event that it reads.
 *
 * The matcher is small and closed: `equals`, `in`, `exists`, `has_tag`, and a
 * `negate` flag. It has no arithmetic and no loops. It reads the payload
 * fields of a `ClientEventView` and the state values of the closed list in
 * `state_values.hpp`.
 *
 * WARN: a `ClientEventView` holds fields with the audience `all` only. The
 * builder of the view (`view/streak_view.hpp`) removes every other field, so
 * a hidden value cannot reach a streak. The `relation` test depends on the
 * viewer and a streak has none, so it never matches.
 */

namespace match::streak {

/** @brief The state values of the closed list, at the time of the event. */
struct StateSnapshot {
    std::optional<int64_t> hand_size;  /**< Hand count of the actor. */
    int64_t turn = 0;
    int64_t round = 0;
};

/** @brief One client event as a streak reads it. */
struct ClientEventView {
    std::string id;                                     /**< Client event id. */
    nlohmann::json payload = nlohmann::json::object();  /**< `all` fields. */
    std::string actor;                                  /**< May be empty. */
    StateSnapshot state;
    /** True for an event that the engine derived from a streak. */
    bool derived = false;
};

/** @brief True when `event` has the id of `matcher` and passes its tests. */
bool MatcherMatches(const modload::StreakMatcherDef& matcher,
                    const ClientEventView& event);

}  // namespace match::streak
