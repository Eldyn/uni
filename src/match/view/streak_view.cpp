#include <match/view/streak_view.hpp>

#include <match/ecs/components.hpp>
#include <match/engine/match_instance.hpp>
#include <match/view/event_audience.hpp>
#include <match/view/event_sink.hpp>
#include <match/view/view_util.hpp>

#include <string>

namespace match::view {

namespace {

using nlohmann::json;

/**
 * @brief The candidate fields of a draw.
 *
 * INFO: the candidate holds every field of the wire projection, the card
 *       identities too. `KeepAllAudience` is the one filter, so a new field
 *       is hidden until the audience table says `all`.
 */
json DrawFields(const match::engine::MatchInstance& match,
                const json& body) {
    json out = json::object();
    const std::string owner =
        ResolvePlayer(match, body.value("player", json()));
    out["target"] = owner;
    out["player"] = owner;
    out["count"] = body.value("count", 0);
    out["hand_size_after"] = body.value("hand_size_after", 0);
    out["from_debt"] = body.value("from_debt", false);
    out["source_pile"] = body.contains("source")
                             ? body["source"]
                             : json(body.value("source_pile", std::string()));
    if (body.contains("held")) out["held"] = body["held"];
    if (body.contains("cards")) out["cards"] = body["cards"];
    return out;
}

json KeptFields(const match::engine::MatchInstance& match, const json& body) {
    json out = json{
        {"target", ResolvePlayer(match, body.value("player", json()))}};
    if (body.contains("card")) out["card"] = body["card"];
    return out;
}

/** @brief Keep the fields of `fields` that the table gives to everybody. */
json KeepAllAudience(const std::string& type, const json& fields) {
    json kept = json::object();
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        if (AudienceOf(type, it.key()) == Audience::kAll) {
            kept[it.key()] = it.value();
        }
    }
    return kept;
}

json ModEventFields(const match::engine::MatchInstance& match,
                    const json& body, std::string& event_id) {
    json kept = json::object();
    event_id = body.value("id", std::string());
    const auto definition = match.Registries().events.find(event_id);
    const auto payload = body.find("payload");
    if (definition == match.Registries().events.end()
        || payload == body.end() || !payload->is_object()) {
        event_id.clear();
        return kept;
    }
    for (const modload::EventFieldDef& field : definition->second.fields) {
        if (field.audience != "all") continue;
        const auto value = payload->find(field.name);
        if (value != payload->end()) kept[field.name] = *value;
    }
    return kept;
}

}  // namespace

std::optional<match::streak::ClientEventView> BuildStreakEventView(
    const match::engine::MatchInstance& match, const json& engine_event) {
    if (!engine_event.is_object()) return std::nullopt;
    const std::string type = engine_event.value("type", std::string());
    const json body = engine_event.contains("payload")
                          ? engine_event["payload"]
                          : json::object();

    match::streak::ClientEventView view;
    if (type == "mod_event") {
        view.payload = ModEventFields(match, body, view.id);
        if (view.id.empty()) return std::nullopt;
    } else if (IsStreakFeedable(type)) {
        view.id = type;
        std::optional<json> projected;
        if (type == "cards_drawn") {
            projected = DrawFields(match, body);
        } else if (type == "drawn_card_kept") {
            projected = KeptFields(match, body);
        } else {
            projected = ProjectPublicEvent(type, body, match);
        }
        if (!projected.has_value() || !projected->is_object()) {
            return std::nullopt;
        }
        view.payload = KeepAllAudience(type, *projected);
    } else {
        return std::nullopt;
    }

    const json frame = engine_event.value("causal_frame", json::object());
    if (frame.contains("actor") && frame["actor"].is_string()) {
        view.actor = frame["actor"].get<std::string>();
    }
    view.state.turn = frame.value("turn", 0);
    view.state.round = frame.value("round", 0);
    if (!view.actor.empty()) {
        const std::optional<ecs::Entity> actor = match.FindPlayer(view.actor);
        const ecs::Hand* hand =
            actor.has_value() ? match.Store().Get<ecs::Hand>(*actor) : nullptr;
        if (hand != nullptr) {
            view.state.hand_size = static_cast<int64_t>(hand->cards.size());
        }
    }
    view.derived = engine_event.value("derived", false);
    return view;
}

}  // namespace match::view
