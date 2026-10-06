#pragma once

#include <span>
#include <string_view>

/**
 * @file event_audience.hpp
 * @brief The audience of every payload field of every core event.
 *
 * ONE table, keyed by event type. A field names who may receive it:
 *
 * - `kAll`:    every seat and every spectator.
 * - `kActor`:  the seat that caused the event.
 * - `kTarget`: the seat that the event is about.
 * - `kOwner`:  the owner of the thing that the field names.
 * - `kNone`:   nobody.
 *
 * A field that the table does not list has audience `kNone`, so a new field
 * never leaks by accident. The per-seat projector in `event_projector.hpp`
 * is the only reader of this table.
 *
 * Field names are the wire names. `target_key` and `owner_key` name the
 * engine descriptor key that holds the seat of that role.
 *
 * How to add a core event (the pattern of `direction_changed`):
 *
 * 1. The op that changes the state pushes `MakeEvent(<id>, <payload>)` into
 *    `OpResult::events`. The event comes from the primitive, so a mutation
 *    that still reaches the primitive still emits it. The engine adds the
 *    causal frame (`actor`, `cause`, `turn`, `round`); the op does not.
 * 2. Add a `FieldRule` array and one `kEvents` row below. The `<id>` names
 *    what changed, not the op.
 * 3. Add one `if (type == ...)` line in `ProjectPublicEvent`
 *    (`event_sink.cpp`) that builds the wire payload.
 * 4. Add the type to `x-packet-types` and its payload schema in
 *    `contract/asyncapi.yaml`, then run `npm run generate:contract`.
 * 5. Client: map the type in `matchEventMap.ts` to a beat, and handle the
 *    beat in `matchEventController.svelte.ts`. Never infer it from a
 *    snapshot.
 * 6. Add a test that asserts one event for each mutation path (replace,
 *    wrap, hook). Copy tests/unit/engine_direction_changed_test.cpp.
 */

namespace match::view {

/** @brief Who may receive a payload field. */
enum class Audience {
    kNone,
    kAll,
    kActor,
    kTarget,
    kOwner,
};

/** @brief What a payload field carries; the projector redacts by kind. */
enum class FieldKind {
    kPlain,       /**< No card identity in the value itself. */
    kCard,        /**< Packed card identity bits. */
    kCardList,    /**< Array of packed card identity bits. */
    kPlayedCard,  /**< Card bits that the act of playing made public. */
    kOfferedCard, /**< Public only when the offer won; else owner only. */
};

/** @brief One declared payload field. */
struct FieldRule {
    std::string_view name;
    Audience audience = Audience::kNone;
    FieldKind kind = FieldKind::kPlain;
};

/** @brief The declared fields of one event type. */
struct EventRule {
    std::string_view event_type;
    std::string_view target_key;
    std::string_view owner_key;
    std::span<const FieldRule> fields;
};

namespace audience_table {

inline constexpr FieldRule kCardPlayed[] = {
    {"player", Audience::kAll},
    {"card", Audience::kAll, FieldKind::kPlayedCard},
    {"from_zone_ordinal", Audience::kAll},
};
inline constexpr FieldRule kReshuffle[] = {
    {"draw_size", Audience::kAll},
    {"discard_size", Audience::kAll},
};
inline constexpr FieldRule kTurnAdvance[] = {
    {"from", Audience::kAll},
    {"to", Audience::kAll},
    {"direction", Audience::kAll},
    {"deadline_ms", Audience::kAll},
    {"skipped", Audience::kAll},
};
inline constexpr FieldRule kDirectionChanged[] = {
    {"direction", Audience::kAll},
};
inline constexpr FieldRule kTurnSkipped[] = {
    {"target", Audience::kAll},
};
inline constexpr FieldRule kExtraTurnGranted[] = {
    {"target", Audience::kAll},
};
inline constexpr FieldRule kActiveTypeChanged[] = {
    {"color", Audience::kAll},
    {"previous_color", Audience::kAll},
};
// INFO: the restriction condition is mod data and is never declared, so it
//       stays `kNone`.
inline constexpr FieldRule kRestrictionAdded[] = {
    {"restriction_id", Audience::kAll},
    {"phase", Audience::kAll},
};
inline constexpr FieldRule kRestrictionRemoved[] = {
    {"restriction_id", Audience::kAll},
};
// INFO: only the `set_turn_timer` op emits this event. The engine arms,
//       suspends, resumes and resets the turn timer every turn and emits
//       nothing for that; the client reads those deadlines from the turn
//       and state packets. `duration_ms` is relative, not an epoch time.
inline constexpr FieldRule kTurnTimerChanged[] = {
    {"target", Audience::kAll},
    {"duration_ms", Audience::kAll},
};
inline constexpr FieldRule kRoundAdvance[] = {
    {"round", Audience::kAll},
};
inline constexpr FieldRule kPlacement[] = {
    {"player", Audience::kAll},
    {"place", Audience::kAll},
};
inline constexpr FieldRule kWindowOpen[] = {
    {"deadline_ms", Audience::kAll},
    {"responders", Audience::kAll},
    {"eligible_filter_digest", Audience::kAll},
    {"window_id", Audience::kAll},
    {"kind", Audience::kAll},
    {"kinds", Audience::kAll},
    {"duration_ms", Audience::kAll},
    {"hold_ms", Audience::kAll},
};
inline constexpr FieldRule kWindowResponse[] = {
    {"player", Audience::kAll},
    {"passed", Audience::kAll},
    {"card", Audience::kAll, FieldKind::kOfferedCard},
};
inline constexpr FieldRule kWindowClose[] = {
    {"outcome", Audience::kAll},
    {"winner", Audience::kAll},
    {"card", Audience::kAll, FieldKind::kPlayedCard},
};
inline constexpr FieldRule kAutoPlayed[] = {
    {"player", Audience::kAll},
    {"card", Audience::kAll, FieldKind::kPlayedCard},
    {"trigger_summary", Audience::kAll},
};
inline constexpr FieldRule kHandsSwapped[] = {
    {"a", Audience::kAll},
    {"b", Audience::kAll},
    {"a_size", Audience::kAll},
    {"b_size", Audience::kAll},
};
inline constexpr FieldRule kHandsPassed[] = {
    {"direction", Audience::kAll},
    {"players", Audience::kAll},
    {"hand_sizes", Audience::kAll},
};
inline constexpr FieldRule kMatchEnd[] = {
    {"winner", Audience::kAll},
    {"placements", Audience::kAll},
    {"final_digest", Audience::kAll},
};
inline constexpr FieldRule kRollResult[] = {
    {"spec", Audience::kAll},
    {"outcomes", Audience::kAll},
    {"roll_counter", Audience::kAll},
};
inline constexpr FieldRule kOutcomeResult[] = {
    {"table", Audience::kAll},
    {"key", Audience::kAll},
    {"value", Audience::kAll},
    {"counter", Audience::kAll},
};
inline constexpr FieldRule kChainAborted[] = {
    {"mod", Audience::kAll},
    {"node", Audience::kAll},
    {"reason", Audience::kAll},
};
inline constexpr FieldRule kModDisarmed[] = {
    {"mod_id", Audience::kAll},
    {"reason", Audience::kAll},
};
inline constexpr FieldRule kPlayRejected[] = {
    {"player", Audience::kTarget},
    {"reason_id", Audience::kTarget},
};
inline constexpr FieldRule kCardsDrawn[] = {
    {"target", Audience::kAll},
    {"player", Audience::kAll},
    {"count", Audience::kAll},
    {"hand_size_after", Audience::kAll},
    {"from_debt", Audience::kAll},
    {"source_pile", Audience::kAll},
    {"held", Audience::kAll},
    {"cards", Audience::kOwner, FieldKind::kCardList},
};
inline constexpr FieldRule kDrawnCardKept[] = {
    {"target", Audience::kAll},
    {"card", Audience::kOwner, FieldKind::kCard},
};
inline constexpr FieldRule kStatusApplied[] = {
    {"target", Audience::kAll},
    {"status_kind", Audience::kAll},
    {"magnitude", Audience::kAll},
    {"duration_unit", Audience::kAll},
    {"instance_id", Audience::kAll},
};
inline constexpr FieldRule kStatusRemoved[] = {
    {"target", Audience::kAll},
    {"status_kind", Audience::kAll},
    {"instance_id", Audience::kAll},
};
inline constexpr FieldRule kPromptOpen[] = {
    {"prompt_id", Audience::kTarget},
    {"kind", Audience::kTarget},
    {"payload", Audience::kTarget},
    {"response_schema", Audience::kTarget},
    {"deadline_ms", Audience::kTarget},
};
inline constexpr FieldRule kPromptClose[] = {
    {"prompt_id", Audience::kTarget},
    {"outcome", Audience::kTarget},
};
inline constexpr FieldRule kVisibility[] = {
    {"target", Audience::kTarget},
    {"aspects", Audience::kTarget},
};
inline constexpr FieldRule kSignal[] = {
    {"name", Audience::kAll},
    {"payload", Audience::kAll},
};

inline constexpr EventRule kEvents[] = {
    {"card_played", "", "", kCardPlayed},
    {"reshuffle", "", "", kReshuffle},
    {"turn_advance", "", "", kTurnAdvance},
    {"direction_changed", "", "", kDirectionChanged},
    {"turn_skipped", "", "", kTurnSkipped},
    {"extra_turn_granted", "", "", kExtraTurnGranted},
    {"active_type_changed", "", "", kActiveTypeChanged},
    {"restriction_added", "", "", kRestrictionAdded},
    {"restriction_removed", "", "", kRestrictionRemoved},
    {"turn_timer_changed", "", "", kTurnTimerChanged},
    {"round_advance", "", "", kRoundAdvance},
    {"placement", "", "", kPlacement},
    {"window_open", "", "", kWindowOpen},
    {"window_response", "", "player", kWindowResponse},
    {"window_close", "", "", kWindowClose},
    {"auto_played", "", "", kAutoPlayed},
    {"hands_swapped", "", "", kHandsSwapped},
    {"hands_passed", "", "", kHandsPassed},
    {"match_end", "", "", kMatchEnd},
    {"roll_result", "", "", kRollResult},
    {"outcome_result", "", "", kOutcomeResult},
    {"chain_aborted", "", "", kChainAborted},
    {"mod_disarmed", "", "", kModDisarmed},
    {"play_rejected", "player", "", kPlayRejected},
    {"cards_drawn", "", "player", kCardsDrawn},
    {"drawn_card_kept", "", "player", kDrawnCardKept},
    {"status_applied", "", "", kStatusApplied},
    {"status_removed", "", "", kStatusRemoved},
    {"prompt_open", "target", "", kPromptOpen},
    {"prompt_close", "target", "", kPromptClose},
    {"visibility_granted", "viewer", "", kVisibility},
    {"visibility_revoked", "viewer", "", kVisibility},
    {"signal", "", "", kSignal},
};

}  // namespace audience_table

/** @brief The rule of `event_type`, or `nullptr` when nobody declared it. */
inline const EventRule* FindEventRule(std::string_view event_type) {
    for (const EventRule& rule : audience_table::kEvents) {
        if (rule.event_type == event_type) return &rule;
    }
    return nullptr;
}

/** @brief The declaration of `field` inside `rule`, or `nullptr`. */
inline const FieldRule* FindFieldRule(const EventRule& rule,
                                      std::string_view field) {
    for (const FieldRule& candidate : rule.fields) {
        if (candidate.name == field) return &candidate;
    }
    return nullptr;
}

/** @brief The audience of a field; `kNone` when the table lacks it. */
inline Audience AudienceOf(std::string_view event_type,
                           std::string_view field) {
    const EventRule* rule = FindEventRule(event_type);
    if (rule == nullptr) return Audience::kNone;
    const FieldRule* declared = FindFieldRule(*rule, field);
    return declared == nullptr ? Audience::kNone : declared->audience;
}

}  // namespace match::view
