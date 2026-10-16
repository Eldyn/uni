#pragma once

#include <array>
#include <string_view>

/**
 * @file state_values.hpp
 * @brief The closed list of state values that a streak condition can read.
 *
 * ONE list. The streak validator, the streak engine, and the tests read it
 * from here. A state value is public table state: every seat already sees it
 * in the snapshot. Ticket 15 may add names; a new name must be public too.
 *
 * - `hand_size`: the card count of the seat that caused the event (the actor
 *   of the causal frame), after the event. A count never names a card.
 * - `turn`: the turn counter that the engine stamped on the event.
 * - `round`: the round counter that the engine stamped on the event.
 */

namespace match::streak {

inline constexpr std::string_view kStateHandSize = "hand_size";
inline constexpr std::string_view kStateTurn = "turn";
inline constexpr std::string_view kStateRound = "round";

inline constexpr std::array<std::string_view, 3> kStateValueNames = {
    kStateHandSize, kStateTurn, kStateRound};

/** @brief True when `name` is a state value that the engine defines. */
constexpr bool IsStateValueName(std::string_view name) {
    for (const std::string_view known : kStateValueNames) {
        if (known == name) return true;
    }
    return false;
}

}  // namespace match::streak
