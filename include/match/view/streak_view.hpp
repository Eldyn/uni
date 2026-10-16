#pragma once

#include <match/streak/matcher.hpp>

#include <nlohmann/json.hpp>

#include <optional>

/**
 * @file streak_view.hpp
 * @brief The view of one client event that the streaks read.
 *
 * A streak value goes to every seat in `context`, so a streak must never read
 * a hidden value. This builder is the one gate. It keeps the payload fields
 * with the audience `all` only (from the audience table for a core event,
 * from the event definition for a mod event). Every other field is removed
 * here, so the engine and the matcher cannot see it.
 */

namespace match::engine {
class MatchInstance;
}  // namespace match::engine

namespace match::view {

/**
 * @brief Build the streak view of an engine event descriptor.
 *
 * @param match        The live match (names, tags, registries).
 * @param engine_event The engine descriptor (`type`, `payload`,
 *                     `causal_frame`; `derived` for a derived event).
 * @return The view, or `nullopt` when no streak may read this event: a core
 *         event outside `kStreakFeedableEvents`, an old name of an alias
 *         pair, or a mod event with no definition.
 */
std::optional<match::streak::ClientEventView> BuildStreakEventView(
    const match::engine::MatchInstance& match,
    const nlohmann::json& engine_event);

}  // namespace match::view
