#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

/**
 * @file view_util.hpp
 * @brief Shared descriptor-resolving helpers for the view layer.
 *
 * `event_sink` (all-visibility projections) and `view_builder`
 * (per-recipient filtering + snapshot) both turn engine descriptors into wire
 * values. Consolidating the entity / username / card resolvers here keeps the
 * two translation units from drifting on the `{index, generation}` handle
 * shape or on the dual-shape `player` field (username string vs entity
 * object) the engine emits at different sites.
 */

namespace match::ecs {
struct Entity;
}  // namespace match::ecs

namespace match::engine {
class MatchInstance;
}  // namespace match::engine

namespace match::view {

/**
 * @brief Parse the engine's `{index, generation}` entity handle shape.
 *
 * @param value Descriptor value to parse.
 * @param out   Receives the entity handle when `value` is an object with an
 *              unsigned `index`; `generation` defaults to 0 when absent.
 * @return true when `value` carried a usable index.
 */
bool EntityFromJson(const nlohmann::json& value, match::ecs::Entity& out);

/**
 * @brief Username behind an entity descriptor, or empty when unknown.
 *
 * @param match Live match (for the `PlayerInfo` lookup).
 * @param value Entity handle descriptor.
 * @return The player's username, or empty when `value` is not a known player.
 */
std::string UsernameFor(const match::engine::MatchInstance& match,
                        const nlohmann::json& value);

/**
 * @brief A player key from either descriptor shape the engine emits.
 *
 * Engine `Emit` sites pass a username string; op sites pass an entity handle.
 * A string is returned verbatim; an entity is resolved through `PlayerInfo`.
 * This is the single dual-shape resolver both projection modules use.
 *
 * @param match Live match (for entity resolution).
 * @param value Username string or entity handle descriptor.
 * @return The username, or empty when `value` resolves to no player.
 */
std::string ResolvePlayer(const match::engine::MatchInstance& match,
                          const nlohmann::json& value);

/**
 * @brief Packed `CompactCardV2` int behind a card descriptor, else 0.
 *
 * @param match Live match (for the card registry).
 * @param value Card entity handle descriptor.
 * @return The packed card bits, or 0 when `value` is not a known card.
 */
uint32_t CardBitsFor(const match::engine::MatchInstance& match,
                     const nlohmann::json& value);

}  // namespace match::view
