#pragma once

#include <match/ecs/entity_store.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

/**
 * @file leak_scanner.hpp
 * @brief Test tool: search every value of a packet for hidden card data.
 *
 * A card leaks in two ways. Its packed identity bits carry an instance
 * index, so a client can follow one copy across events. Its entity handle
 * (`index` and `generation`) names the same copy. The scanner walks the whole
 * JSON tree of one packet, including nested arrays, objects and every
 * number, so a field that nobody expected is found too.
 */

namespace leak_scanner {

/** @brief One card that a seat must not learn about. */
struct HiddenCard {
    match::ecs::Entity entity;
    uint32_t bits = 0;
};

/** @brief One place where hidden card data reached a seat. */
struct Leak {
    std::string kind;  /**< `card_bits` or `entity_handle`. */
    std::string path;  /**< JSON pointer of the leaking value. */
};

namespace detail {

/**
 * @brief Identity bits below this value look like a count or a seat number.
 *
 * Kind index 0 packs to a value under 4096. Such a value can equal a plain
 * counter, so the scanner accepts it only under a key that names a card.
 */
inline constexpr int64_t kAmbiguousBitsBelow = 4096;

inline bool NamesCard(const std::string& key) {
    // INFO: `card_count` and `card_size` name a counter, not a card.
    return key.find("card") != std::string::npos
           && key.find("count") == std::string::npos
           && key.find("size") == std::string::npos;
}

inline bool IsHandleOf(const nlohmann::json& object,
                       const HiddenCard& hidden) {
    const auto index = object.find("index");
    if (index == object.end() || !index->is_number_unsigned()) return false;
    if (index->get<uint32_t>() != hidden.entity.index) return false;
    const auto generation = object.find("generation");
    if (generation == object.end()) return false;
    return generation->is_number_unsigned()
           && generation->get<uint32_t>() == hidden.entity.generation;
}

inline void Walk(const nlohmann::json& value, const std::string& path,
                 const std::string& key,
                 const std::vector<HiddenCard>& hidden_cards,
                 std::vector<Leak>& leaks) {
    if (value.is_number_integer()) {
        const int64_t number = value.get<int64_t>();
        for (const HiddenCard& hidden : hidden_cards) {
            if (number != static_cast<int64_t>(hidden.bits)) continue;
            if (number < kAmbiguousBitsBelow && !NamesCard(key)) continue;
            leaks.push_back(Leak{"card_bits", path});
        }
    } else if (value.is_object()) {
        for (const HiddenCard& hidden : hidden_cards) {
            if (IsHandleOf(value, hidden)) {
                leaks.push_back(Leak{"entity_handle", path});
            }
        }
        for (const auto& [child_key, child] : value.items()) {
            Walk(child, path + "/" + child_key, child_key, hidden_cards,
                 leaks);
        }
    } else if (value.is_array()) {
        for (std::size_t position = 0; position < value.size(); ++position) {
            Walk(value[position], path + "/" + std::to_string(position),
                 key, hidden_cards, leaks);
        }
    }
}

}  // namespace detail

/**
 * @brief Every leak of `hidden_cards` inside one packet.
 *
 * @param packet       Parsed JSON that was sent to one seat.
 * @param hidden_cards Cards that seat must not learn about.
 * @return The leaks; empty when the packet is clean.
 */
inline std::vector<Leak> FindLeaks(
    const nlohmann::json& packet,
    const std::vector<HiddenCard>& hidden_cards) {
    std::vector<Leak> leaks;
    detail::Walk(packet, "", "", hidden_cards, leaks);
    return leaks;
}

}  // namespace leak_scanner
