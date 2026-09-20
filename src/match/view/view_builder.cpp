#include <match/view/view_builder.hpp>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/artifacts.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/**
 * @file view_builder.cpp
 * @brief Per-recipient filtering.
 */

namespace match::view {

namespace {

using nlohmann::json;

/** @brief Parse the engine's `{index, generation}` entity handle shape. */
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
                        const json& value) {
    if (!value.is_object()) return std::string();
    ecs::Entity entity{};
    if (!EntityFromJson(value, entity)) return std::string();
    const ecs::PlayerInfo* info = match.Store().Get<ecs::PlayerInfo>(entity);
    return info == nullptr ? std::string() : info->username;
}

/**
 * @brief A player key from either descriptor shape the engine emits.
 *
 * Engine `Emit` sites pass a username string; op sites pass an entity handle.
 * A string is returned verbatim, an entity is resolved through `PlayerInfo`.
 */
std::string ResolvePlayer(const match::engine::MatchInstance& match,
                          const json& value) {
    if (value.is_string()) return value.get<std::string>();
    return UsernameFor(match, value);
}

/** @brief Packed `CompactCardV2` int behind a card descriptor, else 0. */
uint32_t CardBitsFor(const match::engine::MatchInstance& match,
                     const json& value) {
    ecs::Entity entity{};
    if (!EntityFromJson(value, entity)) return 0u;
    const std::optional<ecs::CompactCardV2> id =
        match.Registries().CardId(entity);
    return id.has_value() ? id->bits : 0u;
}

/** @brief True when `viewer` is the player named `username`. */
bool ViewerIs(const Viewer& viewer, const std::string& username) {
    return !viewer.is_spectator && !username.empty()
           && viewer.username == username;
}

/**
 * @brief Wire `target` for a grant packet: username for a player, else the
 *        entity handle verbatim.
 */
json TargetRef(const match::engine::MatchInstance& match, const json& value) {
    if (value.is_string()) return value;
    const std::string username = UsernameFor(match, value);
    if (!username.empty()) return username;
    if (value.is_object()) return value;
    return std::string();
}

/** @brief Normalize a `cards` array to packed ids (entities -> bits). */
json NormalizeCards(const match::engine::MatchInstance& match,
                    const json& cards) {
    json out = json::array();
    if (!cards.is_array()) return out;
    for (const json& entry : cards) {
        if (entry.is_number()) {
            out.push_back(entry);
            continue;
        }
        out.push_back(CardBitsFor(match, entry));
    }
    return out;
}

/** @brief Split a `{type, payload}` / flat abort descriptor. */
bool DescriptorBody(const json& engine_event, std::string& type,
                    json& payload) {
    if (!engine_event.is_object()) return false;
    type = engine_event.value("type", std::string());
    if (type.empty()) return false;
    if (engine_event.contains("payload")) {
        payload = engine_event["payload"];
    } else {
        payload = engine_event;
        payload.erase("type");
    }
    return true;
}

/** @brief Types handled by the per-recipient filter (not `all`). */
bool IsFilteredType(const std::string& type) {
    return type == "play_rejected" || type == "cards_drawn"
           || type == "status_applied" || type == "status_removed"
           || type == "prompt_open" || type == "prompt_close"
           || type == "visibility_granted"
           || type == "visibility_revoked" || type == "signal";
}

std::optional<json> ProjectPlayRejected(
    const match::engine::MatchInstance& match, const json& payload,
    const Viewer& viewer) {
    const std::string player =
        ResolvePlayer(match, payload.value("player", json()));
    if (!ViewerIs(viewer, player)) return std::nullopt;
    return json{{"player", player},
                {"reason_id", payload.value("reason_id", std::string())}};
}

std::optional<json> ProjectCardsDrawn(
    const match::engine::MatchInstance& match, const json& payload,
    const Viewer& viewer) {
    const std::string owner =
        ResolvePlayer(match, payload.value("player", json()));
    json out = json::object();
    out["player"] = owner;
    out["count"] = payload.value("count", 0);
    // INFO: the engine descriptor key is `source`; the packet key is
    //       `source_pile`.
    if (payload.contains("source")) {
        out["source_pile"] = payload["source"];
    } else {
        out["source_pile"] = payload.value("source_pile", std::string());
    }
    // INFO: Owner-only identities. The current emitters carry no `cards`
    //       array, so this is count-only until the descriptor supplies one;
    //       when it does, only the owner ever sees it.
    if (ViewerIs(viewer, owner) && payload.contains("cards")) {
        out["cards"] = NormalizeCards(match, payload["cards"]);
    }
    return out;
}

std::optional<json> ProjectStatusApplied(
    const ViewBuilder& builder, const match::engine::MatchInstance& match,
    const json& payload, const Viewer& viewer) {
    const std::string kind = payload.value("status_kind", std::string());
    // INFO: Hidden is a definition property; the engine never sets a
    //       hidden flag on the instance, so the loaded-mod def table is the
    //       source. A descriptor `hidden` hint is OR-ed in defensively.
    const bool hidden =
        builder.StatusHidden(kind) || payload.value("hidden", false);
    const std::string target =
        ResolvePlayer(match, payload.value("target", json()));
    if (hidden && !ViewerIs(viewer, target)) return std::nullopt;
    json out = json::object();
    out["target"] = target;
    out["status_kind"] = kind;
    out["magnitude"] = payload.value("magnitude", 0);
    out["duration_unit"] = payload.value("duration_unit", std::string());
    out["instance_id"] = payload.value("instance", 0);
    return out;
}

std::optional<json> ProjectStatusRemoved(
    const ViewBuilder& builder, const match::engine::MatchInstance& match,
    const json& payload, const Viewer& viewer) {
    const std::string kind = payload.value("status_kind", std::string());
    const bool hidden =
        builder.StatusHidden(kind) || payload.value("hidden", false);
    const std::string target =
        ResolvePlayer(match, payload.value("target", json()));
    if (hidden && !ViewerIs(viewer, target)) return std::nullopt;
    json out = json::object();
    out["target"] = target;
    out["status_kind"] = kind;
    out["instance_id"] = payload.value("instance", 0);
    return out;
}

std::optional<json> ProjectPromptOpen(
    const match::engine::MatchInstance& match, const json& payload,
    const Viewer& viewer) {
    const std::string target =
        ResolvePlayer(match, payload.value("target", json()));
    if (!ViewerIs(viewer, target)) return std::nullopt;
    json out = json::object();
    out["prompt_id"] = payload.value("prompt_id", std::string());
    out["kind"] = payload.value("kind", std::string());
    out["payload"] =
        payload.contains("payload") ? payload["payload"] : json::object();
    out["response_schema"] = payload.contains("response_schema")
                                 ? payload["response_schema"]
                                 : json::object();
    out["deadline_ms"] =
        payload.value("deadline_ms", payload.value("timeout_ms", 0));
    return out;
}

std::optional<json> ProjectPromptClose(
    const match::engine::MatchInstance& match, const json& payload,
    const Viewer& viewer) {
    const std::string target =
        ResolvePlayer(match, payload.value("target", json()));
    if (!ViewerIs(viewer, target)) return std::nullopt;
    return json{{"prompt_id", payload.value("prompt_id", std::string())},
                {"outcome", payload.value("outcome", std::string())}};
}

std::optional<json> ProjectVisibility(
    const match::engine::MatchInstance& match, const json& payload,
    const Viewer& viewer) {
    // INFO: - the grant/revoke packet is delivered to the named viewer
    //       only. The engine key for that recipient is `viewer`.
    const std::string recipient =
        ResolvePlayer(match, payload.value("viewer", json()));
    if (!ViewerIs(viewer, recipient)) return std::nullopt;
    json out = json::object();
    out["target"] = payload.contains("target")
                        ? TargetRef(match, payload["target"])
                        : json("");
    out["aspects"] =
        payload.contains("aspects") ? payload["aspects"] : json::array();
    return out;
}

std::optional<json> ProjectSignal(const ViewBuilder& builder,
                                  const json& payload,
                                  const Viewer& viewer) {
    const std::string name = payload.value("name", std::string());
    const std::string audience = builder.SignalAudience(name);
    if (audience == "players" && viewer.is_spectator) return std::nullopt;
    if (audience == "spectators" && !viewer.is_spectator) return std::nullopt;
    json out = json::object();
    out["name"] = name;
    out["payload"] =
        payload.contains("payload") ? payload["payload"] : json::object();
    return out;
}

std::optional<json> ProjectFiltered(const ViewBuilder& builder,
                                    const std::string& type,
                                    const json& payload,
                                    const Viewer& viewer) {
    const match::engine::MatchInstance& match = builder.Match();
    const json body = payload.is_object() ? payload : json::object();
    if (type == "play_rejected") {
        return ProjectPlayRejected(match, body, viewer);
    }
    if (type == "cards_drawn") return ProjectCardsDrawn(match, body, viewer);
    if (type == "status_applied") {
        return ProjectStatusApplied(builder, match, body, viewer);
    }
    if (type == "status_removed") {
        return ProjectStatusRemoved(builder, match, body, viewer);
    }
    if (type == "prompt_open") return ProjectPromptOpen(match, body, viewer);
    if (type == "prompt_close") return ProjectPromptClose(match, body, viewer);
    if (type == "visibility_granted" || type == "visibility_revoked") {
        return ProjectVisibility(match, body, viewer);
    }
    if (type == "signal") return ProjectSignal(builder, body, viewer);
    return std::nullopt;
}

}  // namespace

ViewBuilder::ViewBuilder(
    const match::engine::MatchInstance& match,
    const std::vector<match::modload::LoadedMod>& mods)
    : match_(match) {
    for (const match::modload::LoadedMod& mod : mods) {
        for (const match::modload::StatusDef& status : mod.statuses) {
            hidden_status_[status.status_id] = status.hidden;
        }
        if (!mod.manifest.signals.is_array()) continue;
        for (const json& signal : mod.manifest.signals) {
            if (!signal.is_object()) continue;
            const std::string name = signal.value("name", std::string());
            if (name.empty()) continue;
            std::string audience = "all";
            if (signal.contains("audience") && signal["audience"].is_string()) {
                audience = signal["audience"].get<std::string>();
            }
            signal_audience_[name] = audience;
        }
    }
}

std::vector<nlohmann::json> ViewBuilder::BuildPackets(
    const Viewer& viewer) const {
    EventSink sink;
    std::vector<json> packets;
    for (const json& event : match_.Events()) {
        const std::optional<json> wrapped = Wrap(event, viewer, sink);
        if (wrapped.has_value()) packets.push_back(*wrapped);
    }
    return packets;
}

std::optional<nlohmann::json> ViewBuilder::Wrap(
    const nlohmann::json& engine_event, const Viewer& viewer,
    EventSink& sink) const {
    std::string type;
    json payload;
    if (!DescriptorBody(engine_event, type, payload)) return std::nullopt;
    if (IsFilteredType(type)) {
        const std::optional<json> filtered =
            ProjectFiltered(*this, type, payload, viewer);
        if (!filtered.has_value()) return std::nullopt;
        return sink.Wrap(type, *filtered);
    }
    const std::optional<json> projected =
        ProjectPublicEvent(type, payload, match_);
    if (!projected.has_value()) return std::nullopt;
    return sink.Wrap(type, *projected);
}

std::optional<nlohmann::json> ViewBuilder::BuildPendingPrompt(
    const Viewer& viewer, EventSink& sink) const {
    const std::optional<json> pending = match_.PendingInput();
    if (!pending.has_value() || !pending->is_object()) return std::nullopt;
    const std::string target =
        ResolvePlayer(match_, pending->value("target", json()));
    if (!ViewerIs(viewer, target)) return std::nullopt;
    const std::string kind = pending->value("kind", std::string());
    const json body = pending->value("payload", json::object());
    json out = json::object();
    out["prompt_id"] = kind.empty() ? std::string("prompt") : kind;
    out["kind"] = kind;
    out["payload"] = body.is_object() ? body : json::object();
    out["response_schema"] =
        body.is_object() && body.contains("response_schema")
            ? body["response_schema"]
            : json::object();
    out["deadline_ms"] =
        body.is_object() ? body.value("timeout_ms", 0) : 0;
    return sink.Wrap("prompt_open", out);
}

bool ViewBuilder::StatusHidden(const std::string& status_id) const {
    const auto it = hidden_status_.find(status_id);
    return it != hidden_status_.end() && it->second;
}

std::string ViewBuilder::SignalAudience(const std::string& name) const {
    const auto it = signal_audience_.find(name);
    return it == signal_audience_.end() ? std::string("all") : it->second;
}

}  // namespace match::view
