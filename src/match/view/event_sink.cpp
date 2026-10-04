#include <match/view/event_sink.hpp>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/ecs/entity_store.hpp>
#include <match/engine/match_instance.hpp>
#include <match/view/view_util.hpp>

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

json ProjectCardPlayed(const match::engine::MatchInstance& match,
                       const json& payload) {
    json out = json::object();
    // INFO: `player` is dual-shape - `PlayCard` emits a username string while
    //       `AutoPlayCard` emits an entity object (review fix 1).
    out["player"] = ResolvePlayer(match, payload.value("player", json()));
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
    // INFO: seats that lost their turn to a consumed one-shot skip. Entities
    //       resolve to usernames; the client stamps an X over each before the
    //       incoming turn's highlight is presented.
    json skipped = json::array();
    if (payload.contains("skipped") && payload["skipped"].is_array()) {
        for (const json& entry : payload["skipped"]) {
            const std::string username = UsernameFor(match, entry);
            if (!username.empty()) skipped.push_back(username);
        }
    }
    out["skipped"] = std::move(skipped);
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

json ProjectWindowOpen(const json& payload) {
    json out = json::object();
    // INFO: The packet carries `deadline` as ms REMAINING. The engine
    //       arms an absolute `deadline_ms` and also reports the chosen
    //       `duration_ms`, so the opening remainder is `duration_ms` when
    //       present; an absolute value only ever appears on a replay.
    out["deadline_ms"] =
        payload.value("duration_ms", payload.value("deadline_ms", 0));
    out["responders"] = payload.contains("responders")
                            ? payload["responders"]
                            : json::array();
    out["eligible_filter_digest"] = payload.value("filter_digest", "");
    out["window_id"] = std::to_string(payload.value("id", 0));
    const std::string kind = payload.value("kind", std::string("generic"));
    out["kind"] = kind;
    // INFO: `kinds` lists every member's kind in member order; a bare packet
    //       (replay, older engine) is a single-member group.
    out["kinds"] = payload.contains("kinds") && payload["kinds"].is_array()
                       ? payload["kinds"]
                       : json::array({kind});
    out["duration_ms"] = payload.value(
        "duration_ms", out["deadline_ms"].get<int64_t>());
    if (payload.value("hold_ms", int64_t{0}) > 0) {
        out["hold_ms"] = payload["hold_ms"];
    }
    return out;
}

json ProjectWindowResponse(const match::engine::MatchInstance& match,
                           const json& payload) {
    json out = json::object();
    // INFO: `player` is dual-shape (review fix 3).
    out["player"] = ResolvePlayer(match, payload.value("player", json()));
    if (payload.value("pass", false)) {
        out["passed"] = true;
    } else if (payload.contains("card")) {
        out["card"] = CardBitsFor(match, payload["card"]);
    }
    return out;
}

json ProjectWindowClose(const match::engine::MatchInstance& match,
                        const json& payload) {
    // INFO: the engine outcome is `response` / `all_pass` / `timeout`; the
    //       packet enum is only `winner` / `default`. A collected
    //       response maps to `winner`, everything else to `default`.
    const std::string outcome = payload.value("outcome", std::string());
    const bool has_winner = payload.contains("winner")
                            && payload["winner"].is_string()
                            && !payload["winner"].get<std::string>().empty();
    json out = json::object();
    out["outcome"] =
        (outcome == "response" || has_winner) ? "winner" : "default";
    if (has_winner) out["winner"] = payload["winner"];
    if (payload.contains("card") && payload["card"].is_object()) {
        out["card"] = CardBitsFor(match, payload["card"]);
    }
    return out;
}

// NOTE: patchwork. Server-authored hand-movement events, projected to
//       public data only (usernames and pre-move hand sizes, no card ids).
json ProjectHandsSwapped(const match::engine::MatchInstance& match,
                         const json& payload) {
    return json{{"a", UsernameFor(match, payload.value("a", json()))},
                {"b", UsernameFor(match, payload.value("b", json()))},
                {"a_size", payload.value("a_size", 0)},
                {"b_size", payload.value("b_size", 0)}};
}

json ProjectHandsPassed(const match::engine::MatchInstance& match,
                        const json& payload) {
    json players = json::array();
    if (payload.contains("players") && payload["players"].is_array()) {
        for (const json& entry : payload["players"]) {
            players.push_back(UsernameFor(match, entry));
        }
    }
    json hand_sizes = payload.contains("hand_sizes")
                          ? payload["hand_sizes"]
                          : json::array();
    return json{{"direction", payload.value("direction", std::string())},
                {"players", std::move(players)},
                {"hand_sizes", std::move(hand_sizes)}};
}

json ProjectAutoPlayed(const match::engine::MatchInstance& match,
                       const json& payload) {
    json out = json::object();
    out["player"] = payload.contains("player")
                        ? UsernameFor(match, payload["player"])
                        : std::string();
    out["card"] =
        payload.contains("card") ? CardBitsFor(match, payload["card"]) : 0u;
    // INFO: the engine descriptor key is `trigger`; the packet key is
    //       `trigger_summary`.
    out["trigger_summary"] = payload.value("trigger", std::string());
    return out;
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
                  static_cast<unsigned long long>(hash));  // NOLINT
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
    if (type == "window_open") return ProjectWindowOpen(body);
    if (type == "window_response") return ProjectWindowResponse(match, body);
    if (type == "window_close") return ProjectWindowClose(match, body);
    if (type == "auto_played") return ProjectAutoPlayed(match, body);
    if (type == "hands_swapped") return ProjectHandsSwapped(match, body);
    if (type == "hands_passed") return ProjectHandsPassed(match, body);
    if (type == "match_end") return ProjectMatchEnd(match);
    if (type == "roll_result") return ProjectRollResult(body);
    // INFO: `signal` is NOT `all`-visibility: its audience is
    //       declared per mod manifest and resolved by `ViewBuilder`. It must
    //       not be broadcast from the public projector (review fix 2).
    if (type == "chain_aborted") return ProjectChainAborted(body);
    if (type == "mod_disarmed") return ProjectModDisarmed(body);
    return std::nullopt;
}

}  // namespace match::view
