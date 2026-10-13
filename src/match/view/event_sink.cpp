#include <match/view/event_sink.hpp>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/ecs/entity_store.hpp>
#include <match/engine/match_instance.hpp>
#include <match/view/event_projector.hpp>
#include <match/view/view_util.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
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
    // INFO: the played card is public at this moment (the `card` field above
    //       is public too), so the tags of its kind are public. A streak
    //       reads them with the `has_tag` test.
    json tags = json::array();
    const auto& table = match.Registries().card_tags;
    const auto declared =
        payload.contains("card") ? table.find(KindIdFor(match, payload["card"]))
                                 : table.end();
    if (declared != table.end()) {
        for (const std::string& tag : declared->second) tags.push_back(tag);
    }
    out["tags"] = std::move(tags);
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

json ProjectWindowOpened(const json& payload) {
    json out = json::object();
    // INFO: The packet carries `deadline` as ms REMAINING. The engine
    //       arms an absolute `deadline_ms` and also reports the chosen
    //       `duration_ms`, so the opening remainder is `duration_ms` when
    //       present; an absolute value only ever appears on a replay.
    out["deadline_ms"] =
        payload.value("duration_ms", payload.value("deadline_ms", 0));
    out["targets"] = payload.contains("responders")
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

json ProjectWindowResponded(const match::engine::MatchInstance& match,
                            const json& payload) {
    json out = json::object();
    // INFO: `player` is dual-shape (review fix 3).
    out["target"] = ResolvePlayer(match, payload.value("player", json()));
    out["window_id"] = std::to_string(payload.value("window", 0));
    if (payload.value("pass", false)) {
        out["passed"] = true;
    } else if (payload.contains("card")) {
        out["card"] = CardBitsFor(match, payload["card"]);
    }
    out["window_kind"] = payload.value("window_kind", std::string("generic"));
    const bool passed = payload.value("pass", false);
    out["outcome"] =
        payload.value("outcome", std::string(passed ? "pass" : "winning"));
    return out;
}

json ProjectWindowClosed(const match::engine::MatchInstance& match,
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
    if (has_winner) out["target"] = payload["winner"];
    if (payload.contains("card") && payload["card"].is_object()) {
        out["card"] = CardBitsFor(match, payload["card"]);
    }
    out["window_id"] = std::to_string(payload.value("id", 0));
    return out;
}

json ProjectOutcomeRolled(const json& payload) {
    json out = json::object();
    // INFO: a table pick carries `table`, `key` and `value`; a dice roll
    //       carries `spec` and `outcomes`. The sink stamps `roll_ordinal` with
    //       the public roll ordinal. The engine `counter` also counts hidden
    //       draws and is never sent.
    if (payload.contains("table")) {
        out["table"] = payload.value("table", std::string());
        out["key"] = payload.value("key", std::string());
        out["value"] = payload.value("value", 0);
    } else {
        out["spec"] = payload.contains("spec") ? payload["spec"].dump()
                                               : std::string("{}");
        out["outcomes"] = payload.contains("outcomes") ? payload["outcomes"]
                                                       : json::array();
    }
    out["roll_ordinal"] = 0;
    return out;
}

json ProjectPlacementSet(const match::engine::MatchInstance& match,
                         const json& payload) {
    // INFO: `is_elimination` is the seam for a later elimination mechanic;
    //       no engine path sets it, so a finish is never an elimination.
    const int place = payload.value("place", 0);
    const bool is_elimination = payload.value("is_elimination", false);
    const int finish_limit =
        static_cast<int>(std::max<uint32_t>(1, match.GetRaceTarget()));
    // INFO: a `finished` key from the engine overrides the default, so an
    //       elimination mechanic or a mod win condition can define it.
    const bool finished = payload.value(
        "finished", !is_elimination && place <= finish_limit);
    return json{
        {"target", ResolvePlayer(match, payload.value("player", json()))},
        {"place", place},
        {"is_elimination", is_elimination},
        {"finished", finished}};
}

json ProjectMatchEnded(const match::engine::MatchInstance& match) {
    json targets = json::array();
    json placements = json::array();
    int place = 1;
    for (const std::string& user : match.GetPlacements()) {
        targets.push_back(user);
        placements.push_back(json{{"player", user}, {"place", place++}});
    }
    const std::string winner = match.GetWinner();
    json ended = json::object();
    ended["target"] = winner;
    ended["targets"] = std::move(targets);
    ended["final_digest"] = StableDigest(
        json{{"winner", winner}, {"placements", placements}}.dump());
    return ended;
}

/** @brief Copy `key` as a username when the descriptor names a seat. */
void CopySeat(const match::engine::MatchInstance& match, const json& payload,
              const std::string& key, json& out) {
    if (!payload.contains(key)) return;
    const std::string username = UsernameFor(match, payload[key]);
    if (!username.empty()) out[key] = username;
}

json UsernameList(const match::engine::MatchInstance& match,
                  const json& payload, const std::string& key) {
    json usernames = json::array();
    if (!payload.contains(key) || !payload[key].is_array()) return usernames;
    for (const json& entry : payload[key]) {
        usernames.push_back(UsernameFor(match, entry));
    }
    return usernames;
}

json ProjectHandsSwapped(const match::engine::MatchInstance& match,
                         const json& payload) {
    return json{{"source", UsernameFor(match, payload.value("source", json()))},
                {"target", UsernameFor(match, payload.value("target", json()))},
                {"count", payload.value("count", 0)},
                {"target_count", payload.value("target_count", 0)}};
}

json ProjectHandsPassed(const match::engine::MatchInstance& match,
                        const json& payload) {
    return json{{"direction", payload.value("direction", std::string())},
                {"targets", UsernameList(match, payload, "targets")},
                {"counts", payload.value("counts", json::array())}};
}

json ProjectHandsRedistributed(const match::engine::MatchInstance& match,
                               const json& payload) {
    return json{{"targets", UsernameList(match, payload, "targets")},
                {"counts", payload.value("counts", json::array())}};
}

/**
 * @brief Add the packed identity of `payload.card` when one exists.
 *
 * A card that the assembly never indexed (a created card) has no packed
 * identity, and bits 0 name nothing, so the field stays out.
 */
void CopyCardBits(const match::engine::MatchInstance& match,
                  const json& payload, json& out) {
    if (!payload.contains("card")) return;
    const uint32_t bits = CardBitsFor(match, payload["card"]);
    if (bits != 0u) out["card"] = bits;
}

json ProjectCardMoved(const match::engine::MatchInstance& match,
                      const json& payload) {
    json out = json::object();
    CopyCardBits(match, payload, out);
    out["from_zone"] = payload.value("from_zone", std::string());
    out["to_zone"] = payload.value("to_zone", std::string());
    CopySeat(match, payload, "source", out);
    CopySeat(match, payload, "target", out);
    return out;
}

json ProjectCardRemoved(const match::engine::MatchInstance& match,
                        const json& payload) {
    json out = json::object();
    CopyCardBits(match, payload, out);
    out["from_zone"] = payload.value("from_zone", std::string());
    CopySeat(match, payload, "target", out);
    return out;
}

/** @brief The `card_created` and `card_replaced` body: a zone and a seat. */
json ProjectCardZone(const match::engine::MatchInstance& match,
                     const json& payload) {
    json out = json::object();
    out["zone"] = payload.value("zone", std::string());
    CopySeat(match, payload, "target", out);
    return out;
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
    // INFO: the auto-played card is public at this moment, so the tags of its
    //       kind are public too. The `auto_play` binding reads them to skip a
    //       wild (same contract as the `card_played` tags).
    json tags = json::array();
    const auto& table = match.Registries().card_tags;
    const auto declared =
        payload.contains("card") ? table.find(KindIdFor(match, payload["card"]))
                                 : table.end();
    if (declared != table.end()) {
        for (const std::string& tag : declared->second) tags.push_back(tag);
    }
    out["tags"] = std::move(tags);
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
    envelope["sequence"] = next_seq_;
    envelope["id"] = type;
    envelope["payload"] = std::move(payload);
    ++next_seq_;
    return envelope;
}

nlohmann::json EventSink::WrapEngineEvent(const std::string& type,
                                          nlohmann::json payload,
                                          const nlohmann::json& engine_event) {
    const uint32_t sequence = next_seq_;
    // INFO: the engine counter counts every random draw, hidden ones too. A
    //       seat gets the number of public rolls it has seen instead.
    if (type == "outcome_rolled") {
        payload["roll_ordinal"] = ++roll_ordinal_;
    }
    nlohmann::json envelope = Wrap(type, std::move(payload));

    const nlohmann::json frame = engine_event.value("causal_frame",
                                                     nlohmann::json::object());
    if (frame.contains("actor") && frame["actor"].is_string()) {
        envelope["actor"] = frame["actor"];
    }
    // INFO: the streak values after the event. They are public to every
    //       seat, so every viewer gets the same object.
    if (engine_event.contains("context")
        && engine_event["context"].is_object()) {
        envelope["context"] = engine_event["context"];
    }
    if (frame.contains("turn")) envelope["turn"] = frame["turn"];
    if (frame.contains("round")) envelope["round"] = frame["round"];

    if (frame.contains("cause_event") && frame["cause_event"].is_number()) {
        const auto cause = sequence_by_event_id_.find(
            frame["cause_event"].get<int64_t>());
        if (cause != sequence_by_event_id_.end()) {
            envelope["cause"] = cause->second;
        }
    }
    if (engine_event.contains("event_id")
        && engine_event["event_id"].is_number()) {
        RememberSequence(engine_event["event_id"].get<int64_t>(), sequence);
    }
    return envelope;
}

void EventSink::RememberSequence(int64_t event_id, uint32_t sequence) {
    sequence_by_event_id_[event_id] = sequence;
    remembered_event_ids_.push_back(event_id);
    if (remembered_event_ids_.size() > kCauseMemoryLimit) {
        sequence_by_event_id_.erase(remembered_event_ids_.front());
        remembered_event_ids_.pop_front();
    }
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
    return WrapEngineEvent(
        type,
        ProjectForViewer(match, type, *projected, SeatRoles{},
                         Viewer::PublicSpectator()),
        engine_event);
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
    if (type == "direction_changed") {
        return nlohmann::json{{"direction", body.value("direction", 0)}};
    }
    if (type == "turn_skipped" || type == "extra_turn_granted") {
        return nlohmann::json{
            {"target", body.contains("target")
                           ? UsernameFor(match, body["target"])
                           : std::string()}};
    }
    // INFO: a derived event names its seat by username already.
    if (IsDerivedEvent(type)) {
        return nlohmann::json{
            {"target", ResolvePlayer(match, body.value("target", json()))}};
    }
    if (type == "turn_timer_changed") {
        return nlohmann::json{
            {"target", body.contains("target")
                           ? UsernameFor(match, body["target"])
                           : std::string()},
            {"duration_ms", body.value("duration_ms", 0)}};
    }
    if (type == "active_type_changed") {
        return nlohmann::json{
            {"color", body.value("color", std::string())},
            {"previous_color", body.value("previous_color", std::string())}};
    }
    if (type == "restriction_added") {
        return nlohmann::json{
            {"restriction_id", body.value("restriction_id", std::string())},
            {"phase", body.value("phase", std::string())}};
    }
    if (type == "restriction_removed") {
        return nlohmann::json{
            {"restriction_id", body.value("restriction_id", std::string())}};
    }
    if (type == "round_advance") {
        return nlohmann::json{{"round", body.value("round", 0)}};
    }
    if (type == "placement_set") return ProjectPlacementSet(match, body);
    if (type == "window_opened") return ProjectWindowOpened(body);
    if (type == "window_responded") {
        return ProjectWindowResponded(match, body);
    }
    if (type == "window_closed") return ProjectWindowClosed(match, body);
    if (type == "auto_played") return ProjectAutoPlayed(match, body);
    if (type == "hands_swapped") return ProjectHandsSwapped(match, body);
    if (type == "hands_passed") return ProjectHandsPassed(match, body);
    if (type == "hands_redistributed") {
        return ProjectHandsRedistributed(match, body);
    }
    if (type == "card_moved") return ProjectCardMoved(match, body);
    if (type == "card_removed") return ProjectCardRemoved(match, body);
    if (type == "card_created" || type == "card_replaced") {
        return ProjectCardZone(match, body);
    }
    if (type == "match_ended") return ProjectMatchEnded(match);
    if (type == "outcome_rolled") return ProjectOutcomeRolled(body);
    // INFO: `signal` is NOT `all`-visibility: its audience is
    //       declared per mod manifest and resolved by `ViewBuilder`. It must
    //       not be broadcast from the public projector (review fix 2).
    //       `chain_aborted` and `mod_disarmed` have audience none and no
    //       projection, so they never reach a seat or a spectator.
    return std::nullopt;
}

}  // namespace match::view
