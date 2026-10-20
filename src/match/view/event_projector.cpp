#include <match/view/event_projector.hpp>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_instance.hpp>
#include <match/view/view_util.hpp>

#include <cstdint>
#include <optional>
#include <string>

/**
 * @file event_projector.cpp
 * @brief Audience filter and hidden-card redaction for one seat.
 */

namespace match::view {

namespace {

using nlohmann::json;

constexpr uint32_t kIdentityAspect =
    static_cast<uint32_t>(ecs::Aspect::kIdentity);

bool IsSeat(const std::string& seat, const Viewer& viewer) {
    return !viewer.is_spectator && !seat.empty() && viewer.username == seat;
}

/** @brief True for a spectator of a seat whose owner has privacy mode off. */
bool SpectatorSeesOwner(const std::string& owner, const Viewer& viewer) {
    return viewer.is_spectator && !owner.empty()
           && !viewer.SeesCardBacksOf(owner);
}

bool GrantsIdentity(const match::engine::MatchInstance& match,
                    ecs::Entity target, ecs::Entity viewer) {
    const ecs::VisibilityGrant* grant =
        match.Store().Get<ecs::VisibilityGrant>(target);
    if (grant == nullptr) return false;
    for (const ecs::VisibilityGrant::Entry& entry : grant->entries) {
        if (entry.viewer == viewer
            && (entry.aspect_mask & kIdentityAspect) != 0u) {
            return true;
        }
    }
    return false;
}

std::optional<ecs::Entity> CardForBits(
    const match::engine::MatchInstance& match, uint32_t bits) {
    for (ecs::Entity card : match.Registries().cards) {
        const std::optional<ecs::CompactCardV2> id =
            match.Registries().CardId(card);
        if (id.has_value() && id->bits == bits) return card;
    }
    return std::nullopt;
}

bool IsHiddenBits(const match::engine::MatchInstance& match,
                  const json& value, const Viewer& viewer) {
    if (!value.is_number_unsigned()) return false;
    const std::optional<ecs::Entity> card =
        CardForBits(match, value.get<uint32_t>());
    return card.has_value() && IsCardHiddenFrom(match, *card, viewer);
}

/** @brief Replace the handle of every hidden card inside `value` by null. */
void ScrubHiddenHandles(const match::engine::MatchInstance& match,
                        const Viewer& viewer, json& value) {
    if (value.is_array()) {
        for (json& child : value) ScrubHiddenHandles(match, viewer, child);
        return;
    }
    if (!value.is_object()) return;
    ecs::Entity entity{};
    if (EntityFromJson(value, entity) && value.contains("generation")
        && match.Store().IsAlive(entity)
        && match.Store().Get<ecs::CardIdentity>(entity) != nullptr
        && IsCardHiddenFrom(match, entity, viewer)) {
        value = json(nullptr);
        return;
    }
    for (auto& [key, child] : value.items()) {
        ScrubHiddenHandles(match, viewer, child);
    }
}

/**
 * @brief True when a spectator must not see a card in this zone.
 *
 * The draw pile has no owner, so its order stays hidden. A hand follows the
 * Streamer/Privacy Mode of its owner.
 */
bool IsCardHiddenFromSpectator(const match::engine::MatchInstance& match,
                               const ecs::InZone& in_zone,
                               const Viewer& viewer) {
    switch (in_zone.zone.kind) {
        case ecs::ZoneKind::kDrawPile:
            return true;
        case ecs::ZoneKind::kHand: {
            const ecs::PlayerInfo* owner =
                match.Store().Get<ecs::PlayerInfo>(in_zone.zone.owner);
            return owner == nullptr || viewer.SeesCardBacksOf(owner->username);
        }
        case ecs::ZoneKind::kDiscardPile:
        case ecs::ZoneKind::kLimbo:
            return false;
    }
    return false;
}

}  // namespace

bool AudienceAllows(Audience audience, const SeatRoles& roles,
                    const Viewer& viewer) {
    switch (audience) {
        case Audience::kAll:
            return true;
        case Audience::kActor:
            return IsSeat(roles.actor, viewer);
        case Audience::kTarget:
            return IsSeat(roles.target, viewer);
        case Audience::kOwner:
            return IsSeat(roles.owner, viewer)
                   || SpectatorSeesOwner(roles.owner, viewer);
        case Audience::kNone:
            return false;
    }
    return false;
}

SeatRoles ResolveSeatRoles(const match::engine::MatchInstance& match,
                           std::string_view event_type,
                           const json& engine_payload,
                           const std::string& actor) {
    SeatRoles roles;
    roles.actor = actor;
    const EventRule* rule = FindEventRule(event_type);
    if (rule == nullptr || !engine_payload.is_object()) return roles;
    const auto resolve = [&](std::string_view key) {
        if (key.empty()) return std::string();
        const auto entry = engine_payload.find(std::string(key));
        if (entry == engine_payload.end()) return std::string();
        return ResolvePlayer(match, *entry);
    };
    roles.target = resolve(rule->target_key);
    roles.owner = resolve(rule->owner_key);
    roles.zone_card_public = engine_payload.value("card_public", false);
    // INFO: the schema default is `public`, but a missing engine key is
    //       `hidden`. A non-string value counts as missing, so a malformed
    //       descriptor resolves to `hidden` instead of throwing.
    std::string visibility;
    const auto visibility_entry = engine_payload.find("visibility");
    if (visibility_entry != engine_payload.end()
        && visibility_entry->is_string()) {
        visibility = visibility_entry->get<std::string>();
    }
    roles.state_public = visibility == "public";
    roles.state_owner = visibility == "owner";
    roles.state_strict_owner = visibility == "strict_owner";
    roles.offered_card_public =
        engine_payload.value("outcome", std::string()) != "lost";
    return roles;
}

bool IsCardHiddenFrom(const match::engine::MatchInstance& match,
                      ecs::Entity card, const Viewer& viewer) {
    const ecs::InZone* in_zone = match.Store().Get<ecs::InZone>(card);
    if (in_zone == nullptr) return false;
    if (viewer.is_spectator) {
        return IsCardHiddenFromSpectator(match, *in_zone, viewer);
    }
    const std::optional<ecs::Entity> viewer_entity =
        match.FindPlayer(viewer.username);

    ecs::Entity holder{};
    switch (in_zone->zone.kind) {
        case ecs::ZoneKind::kHand:
            holder = in_zone->zone.owner;
            if (viewer_entity.has_value() && *viewer_entity == holder) {
                return false;
            }
            break;
        case ecs::ZoneKind::kDrawPile:
            holder = match.Registries().draw_pile;
            break;
        case ecs::ZoneKind::kDiscardPile:
        case ecs::ZoneKind::kLimbo:
            return false;
    }
    if (!viewer_entity.has_value()) return true;
    return !GrantsIdentity(match, card, *viewer_entity)
           && !GrantsIdentity(match, holder, *viewer_entity);
}

json ProjectForViewer(const match::engine::MatchInstance& match,
                      std::string_view event_type,
                      const json& wire_payload, const SeatRoles& roles,
                      const Viewer& viewer) {
    json projected = json::object();
    const EventRule* rule = FindEventRule(event_type);
    if (rule == nullptr || !wire_payload.is_object()) return projected;

    for (const auto& [name, value] : wire_payload.items()) {
        const FieldRule* field = FindFieldRule(*rule, name);
        if (field == nullptr) continue;
        if (!AudienceAllows(field->audience, roles, viewer)) continue;

        if (field->kind == FieldKind::kOfferedCard
            && !roles.offered_card_public && !IsSeat(roles.owner, viewer)) {
            continue;
        }
        if (field->kind == FieldKind::kZoneCard && !roles.zone_card_public
            && !AudienceAllows(Audience::kOwner, roles, viewer)) {
            continue;
        }
        if (field->kind == FieldKind::kStateValue) {
            const bool allowed =
                roles.state_public
                || (roles.state_owner
                    && AudienceAllows(Audience::kOwner, roles, viewer))
                || (roles.state_strict_owner && IsSeat(roles.owner, viewer));
            if (!allowed) continue;
        }
        json kept = value;
        if (field->kind == FieldKind::kCard
            && IsHiddenBits(match, value, viewer)) {
            continue;
        }
        if (field->kind == FieldKind::kCardList && value.is_array()) {
            kept = json::array();
            for (const json& entry : value) {
                if (!IsHiddenBits(match, entry, viewer)) kept.push_back(entry);
            }
        }
        ScrubHiddenHandles(match, viewer, kept);
        projected[name] = std::move(kept);
    }

    // Drop a card_changed whose card this viewer cannot see when no value
    // survives.
    const bool has_value = projected.contains("member")
                           || projected.contains("value")
                           || projected.contains("previous");
    if (!has_value && wire_payload.contains("_card")) {
        ecs::Entity card{};
        if (EntityFromJson(wire_payload["_card"], card)
            && match.Store().IsAlive(card)
            && IsCardHiddenFrom(match, card, viewer)) {
            return json::object();
        }
    }
    return projected;
}

namespace {

std::string SeatInField(const modload::EventDef& definition,
                        const json& payload, const std::string& name) {
    for (const modload::EventFieldDef& field : definition.fields) {
        if (field.name != name || field.type != "seat") continue;
        const auto value = payload.find(name);
        if (value != payload.end() && value->is_string()) {
            return value->get<std::string>();
        }
    }
    return std::string();
}

}  // namespace

json ProjectModEventForViewer(const match::engine::MatchInstance& match,
                              const json& engine_body,
                              const std::string& actor, const Viewer& viewer) {
    json projected = json::object();
    if (!engine_body.is_object()) return projected;
    const auto definition = match.Registries().events.find(
        engine_body.value("id", std::string()));
    const auto payload = engine_body.find("payload");
    if (definition == match.Registries().events.end()
        || payload == engine_body.end() || !payload->is_object()) {
        return projected;
    }
    SeatRoles roles;
    roles.actor = actor;
    // INFO: a role comes only from a field that the definition types `seat`.
    roles.target = SeatInField(definition->second, *payload, "target");
    roles.owner = SeatInField(definition->second, *payload, "owner");
    for (const modload::EventFieldDef& field : definition->second.fields) {
        const auto value = payload->find(field.name);
        if (value == payload->end()) continue;
        if (!AudienceAllows(AudienceFromWord(field.audience), roles, viewer)) {
            continue;
        }
        projected[field.name] = *value;
    }
    return projected;
}

}  // namespace match::view
