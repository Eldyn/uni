#include <match/view/event_sink.hpp>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/ecs/entity_store.hpp>
#include <match/engine/match_instance.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

/**
 * @file event_sink.cpp
 * @brief Event sink + `all`-visibility public projections.
 */

namespace match::view {

namespace {

using nlohmann::json;

/**
 * @brief Parse the engine's `{index, generation}` entity handle shape.
 * @return true when `value` carried a usable index.
 */
bool EntityFromJson(const json& value, ecs::Entity& out) {
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

/** @brief Username behind an entity descriptor, or empty when unknown. */
std::string UsernameFor(const match::engine::MatchInstance& match,
                        const json& entity_json) {
    if (!entity_json.is_object()) return std::string();
    ecs::Entity entity{};
    if (!EntityFromJson(entity_json, entity)) return std::string();
    const ecs::PlayerInfo* info = match.Store().Get<ecs::PlayerInfo>(entity);
    return info == nullptr ? std::string() : info->username;
}

/** @brief Packed `CompactCardV2` int behind a card descriptor, else 0. */
uint32_t CardBitsFor(const match::engine::MatchInstance& match,
                     const json& entity_json) {
    ecs::Entity entity{};
    if (!EntityFromJson(entity_json, entity)) return 0u;
    const std::optional<ecs::CompactCardV2> id =
        match.Registries().CardId(entity);
    return id.has_value() ? id->bits : 0u;
}

json ProjectCardPlayed(const match::engine::MatchInstance& match,
                       const json& payload) {
    json out = json::object();
    out["player"] = payload.value("player", std::string());
    out["card"] = payload.contains("card")
                      ? CardBitsFor(match, payload["card"])
                      : 0u;
    out["from_zone_ordinal"] = payload.value("from_ordinal", 0);
    return out;
}

json ProjectTurnAdvance(const match::engine::MatchInstance& match,
                        const json& payload) {
    json out = json::object();
    out["from"] = payload.contains("from")
                      ? UsernameFor(match, payload["from"])
                      : std::string();
    out["to"] =
        payload.contains("to") ? UsernameFor(match, payload["to"]) : "";
    out["direction"] = payload.value("direction", 0);
    // INFO: the engine descriptor key is `deadline`; the wire key is
    //       `deadline_ms` (generated TurnAdvancePayload).
    out["deadline_ms"] = payload.value("deadline", 0);
    return out;
}

json ProjectMatchEnd(const match::engine::MatchInstance& match) {
    json placements = json::array();
    int place = 1;
    for (const std::string& user : match.GetPlacements()) {
        placements.push_back(json{{"player", user}, {"place", place++}});
    }
    const std::string winner = match.GetWinner();
    json out = json::object();
    out["winner"] = winner;
    out["placements"] = placements;
    out["final_digest"] = StableDigest(
        json{{"winner", winner}, {"placements", placements}}.dump());
    return out;
}

json ProjectRollResult(const json& payload) {
    json out = json::object();
    out["spec"] = payload.contains("spec") ? payload["spec"].dump()
                                           : std::string("{}");
    out["outcomes"] =
        payload.contains("outcomes") ? payload["outcomes"] : json::array();
    // INFO: the engine key is `counter`; the wire key is `roll_counter`.
    out["roll_counter"] = payload.value("counter", 0);
    return out;
}

json ProjectSignal(const json& payload) {
    json out = json::object();
    out["name"] = payload.value("name", std::string());
    out["payload"] = payload.contains("payload") ? payload["payload"]
                                                 : json::object();
    return out;
}

json ProjectChainAborted(const json& payload) {
    // INFO: the resolver emits a flat `{type, mod, node}` descriptor; the
    //       packet adds `reason` (unset at the abort site today).
    return json{{"mod", payload.value("mod", std::string())},
                {"node", payload.value("node", std::string())},
                {"reason", payload.value("reason", std::string())}};
}

json ProjectModDisarmed(const json& payload) {
    return json{{"mod_id", payload.value("mod", std::string())},
                {"reason", payload.value("reason", std::string())}};
}

}  // namespace

std::string StableDigest(std::string_view data) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char byte : data) {
        hash ^= static_cast<uint64_t>(byte);
        hash *= 1099511628211ull;
    }
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx",
                  static_cast<unsigned long long>(hash));
    return std::string(buffer);
}

nlohmann::json EventSink::Wrap(const std::string& type,
                               nlohmann::json payload) {
    nlohmann::json envelope = nlohmann::json::object();
    envelope["seq"] = next_seq_;
    envelope["type"] = type;
    envelope["payload"] = std::move(payload);
    ++next_seq_;
    return envelope;
}

std::optional<nlohmann::json> EventSink::WrapPublic(
    const nlohmann::json& engine_event,
    const match::engine::MatchInstance& match) {
    if (!engine_event.is_object()) return std::nullopt;
    const std::string type = engine_event.value("type", std::string());
    if (type.empty()) return std::nullopt;

    // INFO: two descriptor shapes exist. `MakeEvent` bodies carry a nested
    //       `payload`; the resolver abort/stale-mod descriptors are flat
    //       (`{type, mod, node}`), so the remainder minus `type` is the body.
    nlohmann::json payload;
    if (engine_event.contains("payload")) {
        payload = engine_event["payload"];
    } else {
        payload = engine_event;
        payload.erase("type");
    }

    const std::optional<nlohmann::json> projected =
        ProjectPublicEvent(type, payload, match);
    if (!projected.has_value()) return std::nullopt;
    return Wrap(type, *projected);
}

std::optional<nlohmann::json> ProjectPublicEvent(
    const std::string& type, const nlohmann::json& payload,
    const match::engine::MatchInstance& match) {
    const nlohmann::json body =
        payload.is_object() ? payload : nlohmann::json::object();

    if (type == "card_played") return ProjectCardPlayed(match, body);
    if (type == "reshuffle") {
        return nlohmann::json{{"draw_size", body.value("draw_size", 0)},
                              {"discard_size",
                               body.value("discard_size", 0)}};
    }
    if (type == "turn_advance") return ProjectTurnAdvance(match, body);
    if (type == "round_advance") {
        return nlohmann::json{{"round", body.value("round", 0)}};
    }
    if (type == "placement") {
        return nlohmann::json{{"player", body.value("player", "")},
                              {"place", body.value("place", 0)}};
    }
    if (type == "match_end") return ProjectMatchEnd(match);
    if (type == "roll_result") return ProjectRollResult(body);
    if (type == "signal") return ProjectSignal(body);
    if (type == "chain_aborted") return ProjectChainAborted(body);
    if (type == "mod_disarmed") return ProjectModDisarmed(body);
    return std::nullopt;
}

}  // namespace match::view
