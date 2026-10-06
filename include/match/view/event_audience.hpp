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
    /**
     * Card bits whose visibility the op fixed when it emitted the event: the
     * engine key `card_public` makes it public, else only the owner sees it.
     * The zone of the card at projection time is never read, so a later move
     * of the card cannot reveal it.
     */
    kZoneCard,
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
// INFO: `is_elimination` is false for a normal finish. No engine path sets
//       it yet; a later elimination mechanic sets the engine key of the same
//       name and the field reaches the wire unchanged.
inline constexpr FieldRule kPlacementSet[] = {
    {"target", Audience::kAll},
    {"place", Audience::kAll},
    {"is_elimination", Audience::kAll},
};
inline constexpr FieldRule kMatchEnded[] = {
    {"target", Audience::kAll},
    {"targets", Audience::kAll},
    {"final_digest", Audience::kAll},
};
// INFO: `roll_ordinal` is the public roll ordinal, never the engine counter
//       (the engine counter also counts hidden draws).
inline constexpr FieldRule kOutcomeRolled[] = {
    {"spec", Audience::kAll},
    {"outcomes", Audience::kAll},
    {"table", Audience::kAll},
    {"key", Audience::kAll},
    {"value", Audience::kAll},
    {"roll_ordinal", Audience::kAll},
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
inline constexpr FieldRule kWindowOpened[] = {
    {"window_id", Audience::kAll},
    {"kind", Audience::kAll},
    {"kinds", Audience::kAll},
    {"targets", Audience::kAll},
    {"deadline_ms", Audience::kAll},
    {"duration_ms", Audience::kAll},
    {"hold_ms", Audience::kAll},
    {"eligible_filter_digest", Audience::kAll},
};
// INFO: a losing response keeps its card hidden from every other seat; the
//       `owner` key `player` names the seat that responded.
inline constexpr FieldRule kWindowResponded[] = {
    {"window_id", Audience::kAll},
    {"target", Audience::kAll},
    {"passed", Audience::kAll},
    {"card", Audience::kAll, FieldKind::kOfferedCard},
};
inline constexpr FieldRule kWindowClosed[] = {
    {"window_id", Audience::kAll},
    {"outcome", Audience::kAll},
    {"target", Audience::kAll},
    {"card", Audience::kAll, FieldKind::kPlayedCard},
};
inline constexpr FieldRule kAutoPlayed[] = {
    {"player", Audience::kAll},
    {"card", Audience::kAll, FieldKind::kPlayedCard},
    {"trigger_summary", Audience::kAll},
};
// INFO: the hand events carry seats and counts only. A hand count is public;
//       the cards of a hand are not, so no hand event names a card.
inline constexpr FieldRule kHandsSwapped[] = {
    {"source", Audience::kAll},
    {"target", Audience::kAll},
    {"count", Audience::kAll},
    {"target_count", Audience::kAll},
};
inline constexpr FieldRule kHandsPassed[] = {
    {"direction", Audience::kAll},
    {"targets", Audience::kAll},
    {"counts", Audience::kAll},
};
inline constexpr FieldRule kHandsRedistributed[] = {
    {"targets", Audience::kAll},
    {"counts", Audience::kAll},
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
// INFO: `chain_aborted` and `mod_disarmed` are diagnostics (spec open item
//       6). They name mod nodes and mod ids, which can leak hidden rule
//       data, so their audience is `none`: no seat and no spectator gets
//       them, and the server log keeps them. The rows declare no field and
//       the projector builds no packet for them.
inline constexpr FieldRule kPlayRejected[] = {
    {"target", Audience::kTarget},
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
// INFO: the op decides who sees `card` of `card_moved` when it emits the
//       event: everyone for a move into the discard pile, else only the seat
//       that owns the `target` hand. The draw pile and limbo show it to nobody.
inline constexpr FieldRule kCardMoved[] = {
    {"card", Audience::kAll, FieldKind::kZoneCard},
    {"from_zone", Audience::kAll},
    {"to_zone", Audience::kAll},
    {"source", Audience::kAll},
    {"target", Audience::kAll},
};
inline constexpr FieldRule kCardCreated[] = {
    {"target", Audience::kAll},
    {"zone", Audience::kAll},
};
// INFO: a removed card sits in limbo, which is public. The `owner` audience
//       keeps the identity with the seat that lost the card.
inline constexpr FieldRule kCardRemoved[] = {
    {"target", Audience::kAll},
    {"from_zone", Audience::kAll},
    {"card", Audience::kOwner, FieldKind::kCard},
};
inline constexpr FieldRule kCardReplaced[] = {
    {"target", Audience::kAll},
    {"zone", Audience::kAll},
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
// INFO: a hidden status stays hidden from every seat but its target; the
//       filter in the view builder decides, before this table is read.
inline constexpr FieldRule kStatusModified[] = {
    {"target", Audience::kAll},
    {"status_kind", Audience::kAll},
    {"magnitude", Audience::kAll},
    {"delta", Audience::kAll},
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
// INFO: the session sends the prompt packets to the seat that must answer, so
//       the audience is the target seat. The payload gets the handle scrub
//       only; it is mod data and the table does not widen it.
inline constexpr FieldRule kPromptOpened[] = {
    {"prompt_id", Audience::kTarget},
    {"kind", Audience::kTarget},
    {"payload", Audience::kTarget},
    {"response_schema", Audience::kTarget},
    {"deadline_ms", Audience::kTarget},
    {"duration_ms", Audience::kTarget},
};
inline constexpr FieldRule kPromptAnswered[] = {
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
    {"placement_set", "", "", kPlacementSet},
    {"match_ended", "", "", kMatchEnded},
    {"outcome_rolled", "", "", kOutcomeRolled},
    {"status_modified", "", "", kStatusModified},
    {"window_opened", "", "", kWindowOpened},
    {"window_responded", "", "player", kWindowResponded},
    {"window_closed", "", "", kWindowClosed},
    {"auto_played", "", "", kAutoPlayed},
    {"hands_swapped", "", "", kHandsSwapped},
    {"hands_passed", "", "", kHandsPassed},
    {"hands_redistributed", "", "", kHandsRedistributed},
    {"card_moved", "", "target", kCardMoved},
    {"card_created", "", "", kCardCreated},
    {"card_removed", "", "target", kCardRemoved},
    {"card_replaced", "", "", kCardReplaced},
    {"match_end", "", "", kMatchEnd},
    {"roll_result", "", "", kRollResult},
    {"outcome_result", "", "", kOutcomeResult},
    {"chain_aborted", "", "", {}},
    {"mod_disarmed", "", "", {}},
    {"play_rejected", "player", "", kPlayRejected},
    {"cards_drawn", "", "player", kCardsDrawn},
    {"drawn_card_kept", "", "player", kDrawnCardKept},
    {"status_applied", "", "", kStatusApplied},
    {"status_removed", "", "", kStatusRemoved},
    {"prompt_open", "target", "", kPromptOpen},
    {"prompt_close", "target", "", kPromptClose},
    {"prompt_opened", "target", "", kPromptOpened},
    {"prompt_answered", "target", "", kPromptAnswered},
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
