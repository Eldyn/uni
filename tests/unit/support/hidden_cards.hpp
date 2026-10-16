#pragma once

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_instance.hpp>
#include <match/view/view_util.hpp>

#include "leak_scanner.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

/**
 * @file hidden_cards.hpp
 * @brief Test tool: the cards that a seat or a spectator must not know.
 *
 * The leak tests and the late join tests share it. The set follows the live
 * engine state at the moment of the call.
 */

namespace hidden_cards {

using leak_scanner::HiddenCard;
using match::engine::MatchInstance;
namespace ecs = match::ecs;
using nlohmann::json;

inline HiddenCard HiddenCardOf(const MatchInstance& engine, ecs::Entity card) {
    HiddenCard hidden;
    hidden.entity = card;
    const std::optional<ecs::CompactCardV2> id =
        engine.Registries().CardId(card);
    hidden.bits = id.has_value() ? id->bits : 0u;
    return hidden;
}

/** @brief True when `viewer` holds any grant on `target`. */
inline bool HasGrant(const MatchInstance& engine, ecs::Entity target,
              ecs::Entity viewer) {
    const ecs::VisibilityGrant* grant =
        engine.Store().Get<ecs::VisibilityGrant>(target);
    if (grant == nullptr) return false;
    for (const ecs::VisibilityGrant::Entry& entry : grant->entries) {
        if (entry.viewer == viewer) return true;
    }
    return false;
}

/** @brief Cards in a window response are public by the act of responding. */
inline std::vector<ecs::Entity> PubliclyOfferedCards(
    const MatchInstance& engine) {
    std::vector<ecs::Entity> offered;
    if (!engine.WindowOpen()) return offered;
    const json window = engine.ExportWindow();
    for (const json& response : window.value("responses", json::array())) {
        ecs::Entity card{};
        if (response.contains("card")
            && match::view::EntityFromJson(response["card"], card)) {
            offered.push_back(card);
        }
    }
    return offered;
}

/**
 * @brief The cards `username` must not learn about right now.
 *
 * Every card in another seat's hand, and every card of the draw pile, unless
 * a visibility grant lets the seat see it.
 */
inline std::vector<HiddenCard> HiddenFrom(const MatchInstance& engine,
                                   const std::string& username) {
    const ecs::Entity viewer = *engine.FindPlayer(username);
    const std::vector<ecs::Entity> offered = PubliclyOfferedCards(engine);
    std::vector<HiddenCard> hidden;
    const auto add = [&](ecs::Entity holder,
                         const std::vector<ecs::Entity>& cards) {
        if (HasGrant(engine, holder, viewer)) return;
        for (ecs::Entity card : cards) {
            if (HasGrant(engine, card, viewer)) continue;
            bool is_offered = false;
            for (ecs::Entity entry : offered) is_offered |= entry == card;
            if (!is_offered) hidden.push_back(HiddenCardOf(engine, card));
        }
    };
    for (ecs::Entity player : engine.Registries().players) {
        if (player == viewer) continue;
        const ecs::Hand* hand = engine.Store().Get<ecs::Hand>(player);
        if (hand != nullptr) add(player, hand->cards);
    }
    const ecs::PileContents* draw_pile = engine.Store().Get<ecs::PileContents>(
        engine.Registries().draw_pile);
    if (draw_pile != nullptr) {
        add(engine.Registries().draw_pile, draw_pile->cards);
    }
    return hidden;
}

/**
 * @brief The cards a spectator must not learn about right now.
 *
 * Every card of the draw pile, and every card in the hand of a seat whose
 * owner has Streamer/Privacy Mode on.
 */
inline std::vector<HiddenCard> HiddenFromSpectator(
    const MatchInstance& engine,
    const std::vector<std::string>& private_seats) {
    const std::vector<ecs::Entity> offered = PubliclyOfferedCards(engine);
    std::vector<HiddenCard> hidden;
    const auto add = [&](const std::vector<ecs::Entity>& cards) {
        for (ecs::Entity card : cards) {
            bool is_offered = false;
            for (ecs::Entity entry : offered) is_offered |= entry == card;
            if (!is_offered) hidden.push_back(HiddenCardOf(engine, card));
        }
    };
    for (const std::string& username : private_seats) {
        const ecs::Hand* hand = engine.Store().Get<ecs::Hand>(
            *engine.FindPlayer(username));
        if (hand != nullptr) add(hand->cards);
    }
    const ecs::PileContents* draw_pile = engine.Store().Get<ecs::PileContents>(
        engine.Registries().draw_pile);
    if (draw_pile != nullptr) add(draw_pile->cards);
    return hidden;
}

}  // namespace hidden_cards
