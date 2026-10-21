#include <match/view/view_builder.hpp>

#include <match/ecs/compact_card.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_instance.hpp>
#include <match/engine/play_evaluator.hpp>
#include <match/view/event_projector.hpp>
#include <match/view/event_tail.hpp>
#include <match/modload/artifacts.hpp>
#include <match/modload/play_conditions.hpp>
#include <match/modload/restriction.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/status.hpp>
#include <match/view/view_util.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

/**
 * @file view_builder.cpp
 * @brief the view layer/c per-recipient filtering + snapshot.
 */

namespace match::view {

namespace {

using nlohmann::json;

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
           || type == "drawn_card_kept"
           || type == "status_applied" || type == "status_removed"
           || type == "status_modified"
           || type == "prompt_opened" || type == "prompt_answered"
           || type == "visibility_granted"
           || type == "visibility_revoked" || type == "signal";
}

std::optional<json> ProjectPlayRejected(
    const match::engine::MatchInstance& match, const json& payload,
    const Viewer& viewer) {
    const std::string player =
        ResolvePlayer(match, payload.value("player", json()));
    if (!ViewerIs(viewer, player)) return std::nullopt;
    return json{{"target", player},
                {"reason_id", payload.value("reason_id", std::string())}};
}

std::optional<json> ProjectCardsDrawn(
    const match::engine::MatchInstance& match, const json& payload,
    const Viewer& viewer) {
    const std::string owner =
        ResolvePlayer(match, payload.value("player", json()));
    json out = json::object();
    out["target"] = owner;
    out["count"] = payload.value("count", 0);
    out["hand_size_after"] = payload.value("hand_size_after", 0);
    out["from_debt"] = payload.value("from_debt", false);
    // INFO: the engine descriptor key is `source`; the packet key is
    //       `source_pile`.
    if (payload.contains("source")) {
        out["source_pile"] = payload["source"];
    } else {
        out["source_pile"] = payload.value("source_pile", std::string());
    }
    if (payload.contains("held")) out["held"] = payload["held"];
    // INFO: owner identities. The engine descriptor carries `cards` as
    //       entities; this turns them into packed bits and gives them to the
    //       owner and to the spectators of a seat with privacy mode off. The
    //       `cards` rule in the audience table filters again as the last
    //       gate, so a card that is hidden from a viewer never leaves.
    const bool spectator_sees_owner =
        viewer.is_spectator && !viewer.SeesCardBacksOf(owner);
    if ((ViewerIs(viewer, owner) || spectator_sees_owner)
        && payload.contains("cards")) {
        out["cards"] = NormalizeCards(match, payload["cards"]);
    }
    return out;
}

std::optional<json> ProjectDrawnCardKept(
    const match::engine::MatchInstance& match, const json& payload) {
    json out = json::object();
    out["target"] = ResolvePlayer(match, payload.value("player", json()));
    if (payload.contains("card")) {
        out["card"] = CardBitsFor(match, payload["card"]);
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

std::optional<json> ProjectStatusModified(
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
    out["magnitude"] = payload.value("magnitude", 0);
    out["delta"] = payload.value("delta", 0);
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
    if (type == "drawn_card_kept") return ProjectDrawnCardKept(match, body);
    if (type == "status_applied") {
        return ProjectStatusApplied(builder, match, body, viewer);
    }
    if (type == "status_removed") {
        return ProjectStatusRemoved(builder, match, body, viewer);
    }
    if (type == "status_modified") {
        return ProjectStatusModified(builder, match, body, viewer);
    }
    if (type == "prompt_opened") return ProjectPromptOpen(match, body, viewer);
    if (type == "prompt_answered") return ProjectPromptClose(match, body, viewer);
    if (type == "visibility_granted" || type == "visibility_revoked") {
        return ProjectVisibility(match, body, viewer);
    }
    if (type == "signal") return ProjectSignal(builder, body, viewer);
    return std::nullopt;
}

// --- the view layer snapshot visibility helpers ---------------

/** @brief Aspect bits from the frozen `ecs::Aspect` enum. */
constexpr uint32_t kAspectCount = static_cast<uint32_t>(ecs::Aspect::kCount);
constexpr uint32_t kAspectColor = static_cast<uint32_t>(ecs::Aspect::kColor);
constexpr uint32_t kAspectValue = static_cast<uint32_t>(ecs::Aspect::kValue);
constexpr uint32_t kAspectIdentity =
    static_cast<uint32_t>(ecs::Aspect::kIdentity);
constexpr uint32_t kAspectPosition =
    static_cast<uint32_t>(ecs::Aspect::kPosition);
/** Every aspect a grant can expose. */
constexpr uint32_t kAllAspects =
    kAspectCount | kAspectColor | kAspectValue | kAspectIdentity
    | kAspectPosition;
/** Aspects that turn a count-only view into an entry list. */
constexpr uint32_t kCardAspects =
    kAspectColor | kAspectValue | kAspectIdentity | kAspectPosition;

/** @brief True when any bit of `bits` is set in `mask`. */
bool HasAspect(uint32_t mask, uint32_t bits) {
    return (mask & bits) != 0u;
}

/** @brief duration unit token for a status instance. */
const char* DurationUnitToken(ecs::DurationUnit unit) {
    switch (unit) {
        case ecs::DurationUnit::kMs:
            return "ms";
        case ecs::DurationUnit::kTurns:
            return "turns";
        case ecs::DurationUnit::kRounds:
            return "rounds";
        case ecs::DurationUnit::kCardsPlayed:
            return "cards_played";
    }
    return "turns";
}

/** @brief The viewer's player entity, or nullopt for a spectator. */
std::optional<ecs::Entity> ViewerPlayer(
    const match::engine::MatchInstance& match, const Viewer& viewer) {
    if (viewer.is_spectator || viewer.username.empty()) return std::nullopt;
    return match.FindPlayer(viewer.username);
}

/**
 * @brief OR of every `visibility_grant` aspect `viewer` holds on `target`.
 *
 * INFO: `expires_ms` is not read: no body in the engine interprets it yet
 *  and the view builder has no clock, so
 *       every recorded grant is treated as live. Recorded as a concern.
 */
uint32_t GrantMask(const match::engine::MatchInstance& match,
                   ecs::Entity target, ecs::Entity viewer) {
    const ecs::VisibilityGrant* grant =
        match.Store().Get<ecs::VisibilityGrant>(target);
    if (grant == nullptr) return 0u;
    uint32_t mask = 0u;
    for (const ecs::VisibilityGrant::Entry& entry : grant->entries) {
        if (entry.viewer == viewer) mask |= entry.aspect_mask;
    }
    return mask;
}

/**
 * @brief One card entry carrying exactly the aspects `mask` exposes.
 *
 * `identity` adds the compact wire id and the frozen kind id; `color` /
 * `value` add the content facts; `position` adds the in-zone ordinal.
 * `can_play` is only ever set for the viewer's own hand.
 */
json CardEntry(const match::engine::MatchInstance& match, ecs::Entity card,
               uint32_t mask, bool can_play, bool with_playable) {
    const ecs::EntityStore& store = match.Store();
    json out = json::object();
    if (HasAspect(mask, kAspectPosition)) {
        const ecs::InZone* zone = store.Get<ecs::InZone>(card);
        out["slot"] = zone == nullptr ? 0u : zone->ordinal;
    }
    const ecs::CardIdentity* identity = store.Get<ecs::CardIdentity>(card);
    const std::string kind =
        identity == nullptr ? std::string() : identity->kind_id;
    if (HasAspect(mask, kAspectIdentity)) {
        const std::optional<ecs::CompactCardV2> id =
            match.Registries().CardId(card);
        if (id.has_value()) out["card"] = id->bits;
        if (identity != nullptr) out["kind"] = kind;
    }
    if (HasAspect(mask, kAspectColor) || HasAspect(mask, kAspectValue)) {
        const auto facts = match.Assembly().CardFacts().find(kind);
        if (facts != match.Assembly().CardFacts().end()) {
            if (HasAspect(mask, kAspectColor)) {
                out["color"] = facts->second.color;
            }
            if (HasAspect(mask, kAspectValue)) {
                out["value"] = facts->second.value;
            }
        }
    }
    if (with_playable) out["can_play"] = can_play;
    return out;
}

/**
 * @brief One card's `can_play` verdict from the snapshot's PlayEvaluator.
 *
 * The single legality authority owns the decision: while a
 * window is open the evaluator answers through `CanRespond`, otherwise
 * through `CanPlayInTurn`. `window` is the live window state (null when no
 * window is open) and `filters` are its members' `respond_with` filters.
 */
bool EvaluatorCanPlay(const match::engine::PlayEvaluator& evaluator,
                      ecs::Entity player, ecs::Entity card,
                      const ecs::WindowState* window,
                      const std::vector<
                          match::engine::PlayEvaluator::WindowView::Member>&
                          filters) {
    if (window == nullptr || filters.empty()) {
        return evaluator.CanPlayInTurn(player, card);
    }
    return evaluator.CanRespond(
        match::engine::PlayEvaluator::WindowView{
            window->responders, window->responses, filters},
        player, card);
}

/**
 * @brief Visible hand entries, or nullopt when only the count is visible.
 *
 * A spectator sees every aspect unless the owner opted into
 * privacy mode; a player sees their own hand in full and any
 * other hand through `VisibilityGrant` aspects (the hand entity's grants OR
 * each card's own grants, so a `choose_card` reveal survives the snapshot).
 */
std::optional<json> BuildHand(const match::engine::MatchInstance& match,
                              const match::engine::PlayEvaluator& evaluator,
                              const Viewer& viewer,
                              const SnapshotOptions& options,
                              ecs::Entity player,
                              std::optional<ecs::Entity> viewer_entity) {
    const ecs::Hand* hand = match.Store().Get<ecs::Hand>(player);
    if (hand == nullptr) return std::nullopt;

    const bool own =
        viewer_entity.has_value() && *viewer_entity == player;
    uint32_t hand_mask = 0u;
    if (viewer.director) {
        hand_mask = kAllAspects;
    } else if (viewer.is_spectator) {
        const ecs::PlayerInfo* info =
            match.Store().Get<ecs::PlayerInfo>(player);
        const std::string owner =
            info == nullptr ? std::string() : info->username;
        hand_mask = viewer.SeesCardBacksOf(owner) ? 0u : kAllAspects;
    } else if (own) {
        hand_mask = kAllAspects;
    } else if (viewer_entity.has_value()) {
        hand_mask = GrantMask(match, player, *viewer_entity);
    }

    // INFO: `can_play` is own-hand only. While a window is open the
    //       window state is resolved once for the whole hand, not per card.
    const ecs::WindowState* window = nullptr;
    std::vector<match::engine::PlayEvaluator::WindowView::Member> filters;
    if (own && match.WindowOpen()) {
        window = match.Store().Get<ecs::WindowState>(match.Registries().match);
        filters = match.WindowFilters();
    }

    json out = json::array();
    for (ecs::Entity card : hand->cards) {
        uint32_t mask = hand_mask;
        if (!viewer.director && !viewer.is_spectator && viewer_entity.has_value() && !own) {
            mask |= GrantMask(match, card, *viewer_entity);
        }
        if (!HasAspect(mask, kCardAspects)) continue;
        const bool playable =
            own
            && EvaluatorCanPlay(evaluator, player, card, window, filters);
        out.push_back(CardEntry(match, card, mask, playable, own));
    }
    if (own || !out.empty()) return out;
    return std::nullopt;
}

/** @brief Statuses on `player` visible to `viewer`. */
json BuildStatuses(const ViewBuilder& builder,
                   const match::engine::MatchInstance& match,
                   const Viewer& viewer, const SnapshotOptions& options,
                   ecs::Entity player,
                   std::optional<ecs::Entity> viewer_entity) {
    const ecs::PlayerInfo* info = match.Store().Get<ecs::PlayerInfo>(player);
    const std::string owner =
        info == nullptr ? std::string() : info->username;

    json out = json::array();
    for (const ecs::Status& status :
         match::status::List(match.Store(), player)) {
        const bool hidden =
            builder.StatusHidden(status.status_id) || status.hidden;
        bool visible = !hidden;
        if (hidden && viewer.director) {
            visible = true;
        } else if (hidden && viewer.is_spectator) {
            visible = !viewer.SeesCardBacksOf(owner);
        } else if (hidden && viewer_entity.has_value()) {
            visible = *viewer_entity == player;
        }
        if (!visible) continue;
        out.push_back(
            json{{"status_kind", status.status_id},
                 {"magnitude", status.magnitude},
                 {"duration_unit", DurationUnitToken(status.duration.unit)},
                 {"instance_id", status.instance_id}});
    }
    return out;
}

/** @brief One player row: identity, public counters, hand + statuses. */
json BuildPlayer(const ViewBuilder& builder,
                 const match::engine::MatchInstance& match,
                 const match::engine::PlayEvaluator& evaluator,
                 const Viewer& viewer, const SnapshotOptions& options,
                 ecs::Entity player,
                 std::optional<ecs::Entity> viewer_entity) {
    const ecs::PlayerInfo* info = match.Store().Get<ecs::PlayerInfo>(player);
    const ecs::Hand* hand = match.Store().Get<ecs::Hand>(player);
    const ecs::TurnState* turn = match.Store().Get<ecs::TurnState>(player);

    const std::string username = info == nullptr ? std::string() : info->username;
    json entry = json::object();
    entry["username"] = username;
    entry["seat"] = info == nullptr ? 0u : info->seat;
    entry["is_bot"] = info != nullptr && info->is_bot;
    // INFO: the public avatar loadout (spec 1D), frozen at match start. Null
    //       for bots and for seats assembled without one.
    entry["loadout"] = info == nullptr ? json(nullptr) : info->loadout;
    // INFO: display-only avatar-strip override (Creative skin op); turn
    //       logic keeps reading `is_bot`, never this.
    const auto skin = options.skin_overrides.find(username);
    if (skin != options.skin_overrides.end()) entry["skin"] = skin->second;
    entry["is_current"] = turn != nullptr && turn->is_current;
    entry["card_count"] = hand == nullptr ? 0 : hand->cards.size();
    // INFO: how many connected spectators watch THIS player's POV. Lobby-owned
    //       (SnapshotOptions); absent means nobody is watching this seat.
    const auto watchers = options.spectator_counts.find(username);
    entry["spectator_count"] = watchers == options.spectator_counts.end() ? 0 : watchers->second;
    const std::optional<json> hand_json =
        BuildHand(match, evaluator, viewer, options, player, viewer_entity);
    if (hand_json.has_value()) entry["hand"] = *hand_json;
    entry["statuses"] =
        BuildStatuses(builder, match, viewer, options, player, viewer_entity);
    return entry;
}

/**
 * @brief One pile: count always, top for discard, cards when granted.
 *
 * Base visibility: draw pile count only, discard top identity + count.
 * A `VisibilityGrant` on the pile entity (or a spectator on the discard
 * pile) exposes
 * the listed cards with the granted aspects.
 */
json BuildPile(const match::engine::MatchInstance& match, const Viewer& viewer,
               ecs::Entity pile, std::optional<ecs::Entity> viewer_entity,
               bool is_discard) {
    // INFO: the draw pile has no owner, so a spectator never gets its order.
    const ecs::PileContents* contents =
        match.Store().Get<ecs::PileContents>(pile);
    json out = json::object();
    const std::size_t count =
        contents == nullptr ? 0 : contents->cards.size();
    out["count"] = count;

    if (is_discard && contents != nullptr && !contents->cards.empty()) {
        out["top"] = CardEntry(match, contents->cards.back(),
                               kAspectIdentity | kAspectColor | kAspectValue,
                               false, false);
    }

    uint32_t mask = 0u;
    if (viewer.is_spectator) {
        mask = is_discard ? kAllAspects : 0u;
    } else if (viewer_entity.has_value()) {
        mask = GrantMask(match, pile, *viewer_entity);
    }
    if (HasAspect(mask, kCardAspects) && contents != nullptr) {
        json cards = json::array();
        for (ecs::Entity card : contents->cards) {
            cards.push_back(CardEntry(match, card, mask, false, false));
        }
        out["cards"] = std::move(cards);
    }
    return out;
}

/** @brief The open response window in its uniform wire shape, or null. */
json BuildWindow(const match::engine::MatchInstance& match,
                 const Viewer& viewer) {
    if (!match.WindowOpen()) return json(nullptr);
    const json window = match.ExportWindow();
    if (!window.is_object()) return json(nullptr);

    json responses = json::array();
    if (window.contains("responses") && window["responses"].is_array()) {
        for (const json& response : window["responses"]) {
            json entry = json::object();
            // INFO: `player` is dual-shape, same as the event projection.
            entry["player"] =
                ResolvePlayer(match, response.value("player", json()));
            if (response.value("pass", false)) {
                entry["passed"] = true;
            } else if (response.contains("card")) {
                // INFO: a losing offer stays in its owner's hand, so only the
                //       owner may see which card it was.
                const bool lost = response.value("outcome", std::string())
                                  == "lost";
                if (!lost || ViewerIs(viewer, entry["player"])) {
                    entry["card"] = CardBitsFor(match, response["card"]);
                }
            }
            entry["outcome"] = response.value("outcome", std::string());
            responses.push_back(std::move(entry));
        }
    }
    json out =
        json{{"window_id", std::to_string(window.value("id", 0))},
             {"deadline_ms", window.value("deadline_ms", 0)},
             {"duration_ms", window.value("duration_ms", 0)},
             {"targets", window.value("responders", json::array())},
             {"eligible_filter_digest",
              window.value("filter_digest", std::string())},
             {"kind", window.value("kind", std::string("generic"))},
             {"kinds", window.value("kinds", json::array({"generic"}))},
             {"responses", std::move(responses)}};
    if (window.value("hold_ms", int64_t{0}) > 0) {
        out["hold_ms"] = window["hold_ms"];
    }
    return out;
}

/**
 * @brief The draw debt a viewer may see.
 *
 * Follows the rule of `status_applied`: a hidden debt counts for the seat
 * that owns it only. Another seat and a spectator get the visible debt.
 */
int64_t VisibleDrawDebt(const ViewBuilder& builder,
                        const match::engine::MatchInstance& match,
                        std::optional<ecs::Entity> viewer_entity) {
    int64_t total = 0;
    for (ecs::Entity player : match.Registries().players) {
        for (const ecs::Status& status :
             match::status::List(match.Store(), player)) {
            if (status.status_id != match::ops::kDrawDebtStatusId) continue;
            const bool hidden =
                builder.StatusHidden(status.status_id) || status.hidden;
            const bool owner =
                viewer_entity.has_value() && *viewer_entity == player;
            if (!hidden || owner) total += status.magnitude;
        }
    }
    return total;
}

/**
 * @brief The engine-owned values a late joiner reads instead of rebuilding.
 *
 * Every value comes from the engine, and the viewer sees only what the
 * snapshot already shows it: `hold` carries the card for its owner only, and
 * the timers and the debt are public table state.
 *
 * INFO: `streaks` holds the current value of every `uni` streak, not only the
 *       requested ones. `near_win` holds the derived event windows, keyed by
 *       the event id (`near_win_entered`): `{target, open}`. The windows are
 *       engine-owned state, not streak values, because closing a window
 *       compares an event with the seat of the window; a matcher cannot.
 *       `blocked` has no state of its own: it reads the window.
 */
json BuildContext(const match::engine::MatchInstance& match,
                  const json& state, int64_t draw_debt, json hold,
                  const EventSink& sink) {
    const int64_t now_ms = match.Timers().Turn().Now();
    const auto remaining_until = [now_ms](int64_t deadline_ms) {
        return deadline_ms > now_ms ? deadline_ms - now_ms : int64_t{0};
    };

    const int64_t turn_deadline_ms = state["turn_deadline_ms"].get<int64_t>();
    // INFO: an open window suspends the turn clock (deadline 0); the engine
    //       then holds the time that is left for the turn.
    // INFO: the saved remainder is negative when the deadline had passed
    //       before the window opened.
    const int64_t turn_remaining_ms =
        turn_deadline_ms > 0
            ? remaining_until(turn_deadline_ms)
            : std::max<int64_t>(match.Timers().Turn().RemainingMs(), 0);
    int64_t window_deadline_ms = 0;
    if (state["window"].is_object()) {
        window_deadline_ms = state["window"].value("deadline_ms", int64_t{0});
    }

    json context = json::object();
    context["active_color"] = state["active_type"];
    context["direction"] = state["direction"];
    context["draw_debt"] = draw_debt;
    context["hold"] = std::move(hold);
    context["timers"] =
        json{{"turn_deadline_ms", turn_deadline_ms},
             {"turn_remaining_ms", turn_remaining_ms},
             {"window_deadline_ms", window_deadline_ms},
             {"window_remaining_ms", remaining_until(window_deadline_ms)}};
    context["last_event_sequence"] =
        sink.NextSeq() == 0 ? json(nullptr) : json(sink.NextSeq() - 1);
    context["streaks"] = match.Streaks().AllValues();
    const json windows = match.Derived().WindowsContext();
    if (!windows.is_null()) context["near_win"] = windows;
    return context;
}

/** @brief The viewer's own open op-input prompt, if any (target only). */
json BuildPrompts(const match::engine::MatchInstance& match,
                  const Viewer& viewer) {
    json out = json::array();
    const std::optional<json> pending = match.PendingInput();
    if (!pending.has_value() || !pending->is_object()) return out;
    const std::string target =
        ResolvePlayer(match, pending->value("target", json()));
    if (!ViewerIs(viewer, target)) return out;

    const json body = pending->value("payload", json::object());
    json prompt = json::object();
    prompt["prompt_id"] = pending->value("kind", std::string("prompt"));
    prompt["kind"] = pending->value("kind", std::string());
    prompt["payload"] = body.is_object() ? body : json::object();
    prompt["response_schema"] =
        body.is_object() && body.contains("response_schema")
            ? body["response_schema"]
            : json::object();
    prompt["deadline_ms"] = pending->value("deadline_ms", int64_t{0});
    prompt["duration_ms"] = pending->value("duration_ms", int64_t{0});
    out.push_back(std::move(prompt));
    return out;
}

/**
 * @brief The pending prompt's clock, visible to every viewer.
 *
 * Only the timer (same absolute convention as `prompts[]`), never the payload
 * or the target, so observers can see a wait they cannot answer. Null when no
 * prompt is pending.
 */
json BuildPromptWait(const match::engine::MatchInstance& match) {
    const std::optional<json> pending = match.PendingInput();
    if (!pending.has_value() || !pending->is_object()) return json();
    return json{{"deadline_ms", pending->value("deadline_ms", int64_t{0})},
                {"duration_ms", pending->value("duration_ms", int64_t{0})}};
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
    if (type == "mod_event") {
        return WrapModEvent(engine_event, payload, viewer, sink);
    }
    const std::optional<json> projected =
        IsFilteredType(type) ? ProjectFiltered(*this, type, payload, viewer)
                             : ProjectPublicEvent(type, payload, match_);
    if (!projected.has_value()) return std::nullopt;

    const json frame = engine_event.value("causal_frame", json::object());
    const std::string actor =
        frame.contains("actor") && frame["actor"].is_string()
            ? frame["actor"].get<std::string>()
            : std::string();
    const SeatRoles roles = ResolveSeatRoles(match_, type, payload, actor);
    const json projected_payload =
        ProjectForViewer(match_, type, *projected, roles, viewer);
    // INFO: an empty projection means no field may reach this viewer; drop
    //       the event and do not let it consume a sequence.
    if (projected_payload.empty()) return std::nullopt;
    return sink.WrapEngineEvent(type, projected_payload, engine_event);
}

std::optional<nlohmann::json> ViewBuilder::WrapModEvent(
    const nlohmann::json& engine_event, const nlohmann::json& body,
    const Viewer& viewer, EventSink& sink) const {
    const json frame = engine_event.value("causal_frame", json::object());
    const std::string actor =
        frame.contains("actor") && frame["actor"].is_string()
            ? frame["actor"].get<std::string>()
            : std::string();
    json envelope = sink.WrapEngineEvent(
        "mod_event", ProjectModEventForViewer(match_, body, actor, viewer),
        engine_event);
    // INFO: the `id` names the mod event, not the engine type.
    envelope["id"] = body.value("id", std::string());
    return envelope;
}

nlohmann::json ViewBuilder::BuildTail(const Viewer& viewer,
                                      const EventTail& tail) const {
    EventSink replay_sink;
    json envelopes = json::array();
    for (const TailEntry& entry : tail.Entries()) {
        std::optional<json> wrapped =
            Wrap(entry.engine_event, viewer, replay_sink);
        if (!wrapped.has_value()) continue;
        (*wrapped)["replayed"] = true;
        envelopes.push_back(std::move(*wrapped));
    }
    return envelopes;
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
    // INFO: the prompt clock the server enforces, known even though the
    //       packet leaves before the deadline is armed.
    out["duration_ms"] = pending->value("duration_ms", int64_t{0});
    return sink.Wrap("prompt_opened", out);
}

nlohmann::json ViewBuilder::BuildSnapshot(const Viewer& viewer,
                                          const EventSink& sink) const {
    return BuildSnapshot(viewer, sink, SnapshotOptions{});
}

nlohmann::json ViewBuilder::BuildSnapshot(
    const Viewer& viewer, const EventSink& sink,
    const SnapshotOptions& options) const {
    const match::engine::MatchInstance& match = match_;
    const match::engine::MatchRegistries& registries = match.Registries();
    const ecs::EntityStore& store = match.Store();
    const std::optional<ecs::Entity> viewer_entity =
        ViewerPlayer(match, viewer);

    json state = json::object();
    state["status"] = match.IsMatchOver() ? "finished" : "playing";
    if (const ecs::MatchMeta* meta =
            store.Get<ecs::MatchMeta>(registries.match)) {
        state["round"] = meta->round;
        state["direction"] = static_cast<int>(meta->direction);
    } else {
        state["round"] = 0u;
        state["direction"] = 1;
    }
    if (const ecs::ActiveTypeReq* active =
            store.Get<ecs::ActiveTypeReq>(registries.match);
        active != nullptr && active->type.has_value()) {
        state["active_type"] = *active->type;
    } else {
        state["active_type"] = nullptr;
    }
    state["current_player"] = match.GetCurrentPlayerUsername();
    state["winner"] = match.GetWinner();
    state["placements"] = match.GetPlacements();
    // INFO: `placements` is best-first; in a race it grows as seats finish,
    //       so the client derives finished seats from it mid-match.
    state["mode"] = match.GetMode();
    state["race_target"] = match.GetRaceTarget();

    // INFO: absolute epoch-ms turn deadline for the current player (0 = none);
    //       the reconnect-safe source for the client turn countdown.
    int64_t turn_deadline_ms = 0;
    if (const std::optional<ecs::Entity> current = match.GetCurrentPlayer();
        current.has_value()) {
        if (const ecs::TurnState* turn = store.Get<ecs::TurnState>(*current)) {
            turn_deadline_ms = turn->turn_deadline_ms;
        }
    }
    state["turn_deadline_ms"] = turn_deadline_ms;
    // INFO: the engine clock the absolute deadlines are measured on, so a
    //       client can correct its own clock skew.
    state["server_now_ms"] = match.Timers().Turn().Now();

    // INFO: The outstanding draw-stacking debt; one seat holds it
    //       at a time, the client renders it as the "+N" stack indicator.
    int64_t pending_draws = 0;
    for (ecs::Entity player : registries.players) {
        if (const ecs::Status* debt = match::status::Find(
                store, player, match::ops::kDrawDebtStatusId)) {
            pending_draws += debt->magnitude;
        }
    }
    state["pending_draws"] = pending_draws;
    const int64_t visible_draw_debt =
        VisibleDrawDebt(*this, match, viewer_entity);

    json hold = nullptr;
    // INFO: a playable voluntary draw parks a play/keep choice. Every viewer
    //       sees whose choice it is; only the owner sees which card was drawn,
    //       matching the hand's owner-only identity rule.
    if (const std::optional<match::engine::PendingPlayDrawn>& pending =
            match.PendingPlayDrawnState();
        pending.has_value()) {
        json choice = json::object();
        const ecs::PlayerInfo* info =
            store.Get<ecs::PlayerInfo>(pending->player);
        choice["player"] = info == nullptr ? std::string() : info->username;
        if (viewer_entity.has_value() && *viewer_entity == pending->player) {
            const std::optional<ecs::CompactCardV2> id =
                registries.CardId(pending->card);
            if (id.has_value()) choice["card"] = id->bits;
        }
        hold = choice;
        hold["held"] = true;
        state["pending_play_drawn"] = std::move(choice);
    } else {
        state["pending_play_drawn"] = nullptr;
    }

    // INFO: one legality authority per snapshot; `can_play` is derived from it
    // replacing the deleted view-local copy.
    const match::engine::PlayEvaluator evaluator = match.MakePlayEvaluator();

    json players = json::array();
    for (ecs::Entity player : registries.players) {
        players.push_back(BuildPlayer(*this, match, evaluator, viewer, options,
                                      player, viewer_entity));
    }
    state["players"] = std::move(players);
    state["draw_pile"] =
        BuildPile(match, viewer, registries.draw_pile, viewer_entity, false);
    state["discard_pile"] =
        BuildPile(match, viewer, registries.discard_pile, viewer_entity, true);
    state["window"] = BuildWindow(match, viewer);
    state["context"] = BuildContext(match, state, visible_draw_debt,
                                       std::move(hold), sink);
    state["prompts"] = BuildPrompts(match, viewer);
    if (json prompt_wait = BuildPromptWait(match); !prompt_wait.is_null()) {
        state["prompt_wait"] = std::move(prompt_wait);
    }
    // INFO: non-default card state, redacted per viewer. The key is absent
    //       when no entry survives, so the tail golden and every default
    //       snapshot stay byte-equal.
    json card_state = json::array();
    for (ecs::Entity card : registries.cards) {
        const ecs::CardState* component = store.Get<ecs::CardState>(card);
        if (component == nullptr) continue;
        const ecs::InZone* in_zone = store.Get<ecs::InZone>(card);
        // INFO: a limbo card has no public snapshot identity. A draw-pile
        //       card may carry one only through a per-viewer identity grant;
        //       `IsCardHiddenFrom` below enforces that.
        if (in_zone == nullptr
            || !CardStateIdentityAllowed(in_zone->zone.kind)) {
            continue;
        }
        // INFO: a seated director sees another player's hand through x-ray,
        //       so show the public state for that hand as well. Owner and
        //       strict_owner values still follow the holder rule below.
        const bool director_hand =
            viewer.director && in_zone->zone.kind == ecs::ZoneKind::kHand;
        if (IsCardHiddenFrom(match, card, viewer) && !director_hand) continue;
        const std::optional<ecs::CompactCardV2> identity =
            registries.CardId(card);
        if (!identity.has_value()) continue;

        std::string holder;
        if (in_zone->zone.kind == ecs::ZoneKind::kHand) {
            const ecs::PlayerInfo* info =
                store.Get<ecs::PlayerInfo>(in_zone->zone.owner);
            if (info != nullptr) holder = info->username;
        }
        // INFO: `owner` reaches the holder and a privacy-off spectator (the
        //       Task 8 semantics); `strict_owner` reaches the holder seat
        //       only and never a spectator; a missing visibility is `hidden`.
        const auto visible_to = [&](const std::string& visibility) {
            if (visibility == "public") return true;
            if (visibility == "owner") {
                SeatRoles roles;
                roles.owner = holder;
                return AudienceAllows(Audience::kOwner, roles, viewer);
            }
            if (visibility == "strict_owner") {
                return ViewerIs(viewer, holder);
            }
            return false;
        };

        std::string kind_id;
        if (const ecs::CardIdentity* card_identity =
                store.Get<ecs::CardIdentity>(card)) {
            kind_id = card_identity->kind_id;
        }
        json members = json::object();
        for (const auto& [slot, member] : component->member_overrides) {
            const auto dealt_kind =
                registries.content->kind_slot_members.find(kind_id);
            if (dealt_kind != registries.content->kind_slot_members.end()) {
                const auto dealt = dealt_kind->second.find(slot);
                if (dealt != dealt_kind->second.end()
                    && dealt->second == member) {
                    continue;
                }
            }
            const auto attribute = registries.content->member_attribute.find(member);
            if (attribute == registries.content->member_attribute.end()) continue;
            const auto visibility =
                registries.content->attribute_visibility.find(attribute->second);
            if (visibility == registries.content->attribute_visibility.end()) continue;
            if (!visible_to(visibility->second)) continue;
            members[slot] = member;
        }

        json properties = json::object();
        for (const auto& [property_id, value] : component->properties) {
            const auto property = registries.content->card_properties.find(property_id);
            if (property == registries.content->card_properties.end()) continue;
            if (value == property->second.default_value) continue;
            // INFO: a non-synced property never reaches the wire, even when
            //       its visibility is public.
            if (!property->second.sync_to_clients) continue;
            if (!visible_to(property->second.visibility)) continue;
            properties[property_id] = value;
        }

        if (members.empty() && properties.empty()) continue;
        card_state.push_back(json{{"card", identity->bits},
                                  {"members", std::move(members)},
                                  {"properties", std::move(properties)}});
    }
    if (!card_state.empty()) state["card_state"] = std::move(card_state);

    // INFO: lobby-owned total; the client HUD reads the per-player counts off
    //       `players[]` for the POV-aware eye count.
    state["spectator_count"] = options.spectator_count;
    state["seq_watermark"] = sink.NextSeq();
    if (options.tail != nullptr) {
        state["tail"] = BuildTail(viewer, *options.tail);
    }

    return json{{"action", "match_state_updated"},
                {"match_state", std::move(state)}};
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
