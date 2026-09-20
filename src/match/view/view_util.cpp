#include <match/view/view_util.hpp>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/ecs/entity_store.hpp>
#include <match/engine/match_instance.hpp>

#include <optional>

/**
 * @file view_util.cpp
 * @brief Shared descriptor-resolving helpers for the view layer.
 */

namespace match::view {

bool EntityFromJson(const nlohmann::json& value, ecs::Entity& out) {
    if (!value.is_object()) return false;
    const auto index = value.find("index");
    if (index == value.end() || !index->is_number_unsigned()) return false;
    out.index = index->get<uint32_t>();
    const auto generation = value.find("generation");
    out.generation =
        (generation != value.end() && generation->is_number_unsigned())
            ? generation->get<uint32_t>()
            : 0u;
    return true;
}

std::string UsernameFor(const match::engine::MatchInstance& match,
                        const nlohmann::json& value) {
    if (!value.is_object()) return std::string();
    ecs::Entity entity{};
    if (!EntityFromJson(value, entity)) return std::string();
    const ecs::PlayerInfo* info = match.Store().Get<ecs::PlayerInfo>(entity);
    return info == nullptr ? std::string() : info->username;
}

std::string ResolvePlayer(const match::engine::MatchInstance& match,
                          const nlohmann::json& value) {
    if (value.is_string()) return value.get<std::string>();
    return UsernameFor(match, value);
}

uint32_t CardBitsFor(const match::engine::MatchInstance& match,
                     const nlohmann::json& value) {
    ecs::Entity entity{};
    if (!EntityFromJson(value, entity)) return 0u;
    const std::optional<ecs::CompactCardV2> id =
        match.Registries().CardId(entity);
    return id.has_value() ? id->bits : 0u;
}

}  // namespace match::view
