#pragma once

#include <match/ecs/entity_store.hpp>
#include <match/view/event_audience.hpp>
#include <match/view/view_builder.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

/**
 * @file event_projector.hpp
 * @brief Per-seat projection of one wire payload (the audience framework).
 *
 * The projector is the last step before a payload enters a seat stream. It
 * keeps a field only when the table in `event_audience.hpp` declares it and
 * the viewer belongs to the audience of the field. It then removes the card
 * identity bits and the entity handle (`index`, `generation`) of every card
 * that the viewer must not see.
 */

namespace match::engine {
class MatchInstance;
}  // namespace match::engine

namespace match::view {

/** @brief The seats that fill the roles of one event. */
struct SeatRoles {
    std::string actor;   /**< Seat that caused the event; empty for none. */
    std::string target;  /**< Seat that the event is about. */
    std::string owner;   /**< Owner of the thing that a field names. */
    /** False when an offered card lost its window; only its owner sees it. */
    bool offered_card_public = true;
};

/** @brief True when `viewer` belongs to `audience` for `roles`. */
bool AudienceAllows(Audience audience, const SeatRoles& roles,
                    const Viewer& viewer);

/**
 * @brief Resolve the roles of one engine event.
 *
 * @param match          The live match (entity to username).
 * @param event_type     Engine event type.
 * @param engine_payload Engine descriptor body.
 * @param actor          Username from the causal frame; empty for none.
 */
SeatRoles ResolveSeatRoles(const match::engine::MatchInstance& match,
                           std::string_view event_type,
                           const nlohmann::json& engine_payload,
                           const std::string& actor);

/**
 * @brief True when `viewer` must not learn the identity of `card`.
 *
 * A card is hidden while it sits in the hand of another seat or in the draw
 * pile, unless a visibility grant with the identity aspect covers it. A
 * spectator never sees the draw pile. A spectator sees a hand unless the owner
 * of the hand has Streamer/Privacy Mode on (`Viewer::private_seats`).
 */
bool IsCardHiddenFrom(const match::engine::MatchInstance& match,
                      ecs::Entity card, const Viewer& viewer);

/**
 * @brief Project a wire payload for one viewer.
 *
 * Drops every undeclared field and every field outside its audience. Drops
 * the hidden entries of card fields and replaces the entity handle of a
 * hidden card, anywhere inside a kept field, by `null`.
 *
 * @param match        The live match.
 * @param event_type   Engine event type (the table key).
 * @param wire_payload Payload as the event projection built it.
 * @param roles        The seats of this event.
 * @param viewer       The recipient.
 * @return The payload for this viewer; empty when nothing may be sent.
 */
nlohmann::json ProjectForViewer(const match::engine::MatchInstance& match,
                                std::string_view event_type,
                                const nlohmann::json& wire_payload,
                                const SeatRoles& roles, const Viewer& viewer);

}  // namespace match::view
