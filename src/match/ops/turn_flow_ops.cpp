#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * @file turn_flow_ops.cpp
 * @brief Turn and flow op bodies.
 *
 * Every body is a total, bounded function over the store: it reads its
 * declared args through the fail-safe `OpArgs` getters, mutates only
 * `TurnState`/`MatchMeta` through the checked component wrappers, and emits
 * event descriptors via `MakeEvent`. An unbound selector, a dead/non-player
 * entity or a malformed duration is a fail-safe `kResolved` no-op, never a
 * crash.
 *
 * Turn order is `PlayersBySeat` (ascending `PlayerInfo.seat`, slot index as the
 * tie-break) stepped by `MatchMeta.direction` (+1 forward / -1 reverse) with
 * wrapping. Timers are the timer layer's; these ops only record deadline state
 * and the emitted `turn_advance` event carries the incoming player's stored
 * deadline.
 */

namespace match::ops::detail {
namespace {

using nlohmann::json;

// --- pending-skip and extra-turn bookkeeping -------------------------------
//
// INFO: The frozen the store `TurnState` has no skip slot, so the op layer
//       reserves the high bit of `extra_turns_pending` as the "skip on next
//       advance" flag and keeps the low 31 bits as the extra-turn count
//       (driver's "(e.g. via TurnState)" hint). The named helpers below are the
//       ONLY place the encoding is read; the timer layer or a later component
//       addition can migrate it to a real field without hunting call sites.
//       `advance_turn` consumes a skip when it steps onto the marked player, so
//       the mark is one-shot.

/** @brief Reserved high bit: target is skipped on the next advance. */
inline constexpr uint32_t kSkipFlag = 1u << 31;

/** @brief Low-bit mask holding the pending extra-turn count. */
inline constexpr uint32_t kExtraMask = kSkipFlag - 1u;

/** @brief Pending extra turns (skip flag masked out). */
uint32_t ExtraTurns(const ecs::TurnState& turn) {
    return turn.extra_turns_pending & kExtraMask;
}

/** @brief True when a one-shot skip is pending for the player. */
bool SkipPending(const ecs::TurnState& turn) {
    return (turn.extra_turns_pending & kSkipFlag) != 0u;
}

/** @brief Arm a one-shot skip, preserving any extra-turn count. */
void SetSkipPending(ecs::TurnState& turn) {
    turn.extra_turns_pending |= kSkipFlag;
}

/** @brief Consume a pending skip, preserving any extra-turn count. */
void ConsumeSkip(ecs::TurnState& turn) {
    turn.extra_turns_pending &= kExtraMask;
}

/** @brief Add one extra turn, saturating at the 31-bit ceiling. */
void AddExtraTurn(ecs::TurnState& turn) {
    const uint32_t count = ExtraTurns(turn);
    if (count < kExtraMask) {
        turn.extra_turns_pending =
            (turn.extra_turns_pending & kSkipFlag) | (count + 1u);
    }
}

/** @brief Consume one extra turn, preserving the skip flag. */
void ConsumeExtraTurn(ecs::TurnState& turn) {
    const uint32_t count = ExtraTurns(turn);
    if (count == 0u) return;
    turn.extra_turns_pending =
        (turn.extra_turns_pending & kSkipFlag) | (count - 1u);
}

// --- small helpers ---------------------------------------------------------

/** @brief Entity handle as a JSON object. */
json EntityJson(ecs::Entity entity) {
    return json{{"index", entity.index}, {"generation", entity.generation}};
}

/** @brief `MatchMeta.direction` as the signed seat step. */
int DirectionStep(ecs::Direction direction) {
    return direction == ecs::Direction::kReverse ? -1 : 1;
}

/** @brief Positive modulo that maps `value` into `[0, modulus)`. */
int PositiveMod(int value, int modulus) {
    if (modulus <= 0) return 0;
    return ((value % modulus) + modulus) % modulus;
}

/**
 * @brief The live `TurnState` for a player, adding a default when absent.
 *
 * @return nullptr only for a dead / non-player entity (validated by callers).
 */
ecs::TurnState* EnsureTurnState(ecs::EntityStore& store, ecs::Entity player) {
    ecs::TurnState* turn = store.Get<ecs::TurnState>(player);
    if (turn != nullptr) return turn;
    store.Add(player, ecs::TurnState{});
    return store.Get<ecs::TurnState>(player);
}

/** @brief Resolve a `target` selector to a live player, or nullopt. */
std::optional<ecs::Entity> TargetPlayer(ecs::EntityStore& store,
                                        const OpArgs& args) {
    const std::optional<ecs::Entity> target = args.FirstEntity("target");
    if (!target.has_value()) return std::nullopt;
    if (!store.IsAlive(*target)) return std::nullopt;
    if (!store.Has<ecs::PlayerInfo>(*target)) return std::nullopt;
    return target;
}

// --- duration parsing ------------------------------------------------------

/** @brief Parse an unit token into its enum, false on an unknown token. */
bool ParseUnit(const std::string& token, ecs::DurationUnit& out) {
    if (!modload::IsDurationUnit(token)) return false;
    if (token == "ms") {
        out = ecs::DurationUnit::kMs;
    } else if (token == "turns") {
        out = ecs::DurationUnit::kTurns;
    } else if (token == "rounds") {
        out = ecs::DurationUnit::kRounds;
    } else if (token == "cards_played") {
        out = ecs::DurationUnit::kCardsPlayed;
    } else {
        return false;
    }
    return true;
}

/** @brief Wire token for a duration unit (result payload). */
std::string_view UnitToken(ecs::DurationUnit unit) {
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

/** @brief Parse one `{unit, value}` leg; false on a malformed leg. */
bool ParseDurationLeg(const json& leg, ecs::DurationSpec& out) {
    if (!leg.is_object()) return false;
    const auto unit_it = leg.find("unit");
    const auto value_it = leg.find("value");
    if (unit_it == leg.end() || value_it == leg.end()) return false;
    if (!unit_it->is_string() || !value_it->is_number_integer()) return false;
    ecs::DurationUnit unit{};
    if (!ParseUnit(unit_it->get<std::string>(), unit)) return false;
    const int64_t value = value_it->get<int64_t>();
    if (value < 0) return false;
    out.unit = unit;
    out.value = value;
    return true;
}

/**
 * @brief Parse a `duration` arg (object or compound array) to one leg.
 *
 * `TurnState` carries a single `int64_t turn_deadline_ms`, so a compound
 * duration collapses to the leg that best fits that slot: the smallest `ms`
 * leg when one exists (the slot is milliseconds), otherwise the smallest value
 * among the remaining units. Every leg must be well formed (known unit,
 * non-negative integer value) or the whole parse fails.
 */
bool ParseDuration(const json& duration, ecs::DurationSpec& out) {
    if (duration.is_object()) return ParseDurationLeg(duration, out);
    if (!duration.is_array() || duration.empty()) return false;

    bool have = false;
    bool best_is_ms = false;
    ecs::DurationSpec best{};
    for (const json& leg : duration) {
        ecs::DurationSpec parsed{};
        if (!ParseDurationLeg(leg, parsed)) return false;
        const bool is_ms = parsed.unit == ecs::DurationUnit::kMs;
        if (!have) {
            best = parsed;
            best_is_ms = is_ms;
            have = true;
            continue;
        }
        if (is_ms && !best_is_ms) {
            best = parsed;
            best_is_ms = true;
        } else if (is_ms == best_is_ms && parsed.value < best.value) {
            best = parsed;
        }
    }
    if (!have) return false;
    out = best;
    return true;
}

/** @brief Build the `turn_advance` payload. */
json TurnAdvancePayload(const std::optional<ecs::Entity>& from,
                        ecs::Entity to, int direction, int64_t deadline) {
    return json{{"from", from.has_value() ? EntityJson(*from) : json(nullptr)},
                {"to", EntityJson(to)},
                {"direction", direction},
                {"deadline", deadline}};
}

}  // namespace

OpResult OpAdvanceTurn(ecs::EntityStore& store, const OpArgs& args,
                       OpContext& ctx) {
    (void)args;
    (void)ctx;
    const std::optional<ecs::Entity> match = FindMatch(store);
    if (!match.has_value()) return OpResult::Resolved();
    const ecs::MatchMeta* meta = store.Get<ecs::MatchMeta>(*match);
    if (meta == nullptr) return OpResult::Resolved();

    const std::vector<ecs::Entity> players = PlayersBySeat(store);
    if (players.empty()) return OpResult::Resolved();
    const int count = static_cast<int>(players.size());
    const int step = DirectionStep(meta->direction);

    std::optional<ecs::Entity> from = FindCurrentPlayer(store);
    int current_index = -1;
    if (from.has_value()) {
        for (int i = 0; i < count; ++i) {
            if (players[static_cast<std::size_t>(i)] == *from) {
                current_index = i;
                break;
            }
        }
        // WARN: a current player not seated in this match cannot be stepped
        //       from; fall back to the first-turn path rather than guessing.
        if (current_index < 0) from = std::nullopt;
    }

    // INFO: with no seated current player this is the first turn; the lowest
    //       seat takes it, independent of direction (there is no "previous").
    int incoming_index = 0;
    if (current_index >= 0) {
        ecs::TurnState* current_turn = store.Get<ecs::TurnState>(*from);
        if (current_turn != nullptr && ExtraTurns(*current_turn) > 0u) {
            // INFO: an extra turn replays the current player; one queued turn
            //       is consumed. The one-shot skip flag is left untouched.
            ConsumeExtraTurn(*current_turn);
        } else {
            int cursor = current_index;
            int chosen = -1;
            // INFO: consume any one-shot skips along the way; bounded by one
            //       full lap so an all-skipped table cannot loop forever (it
            //       falls back to the current player replaying).
            for (int tries = 0; tries < count; ++tries) {
                cursor = PositiveMod(cursor + step, count);
                ecs::TurnState* candidate = store.Get<ecs::TurnState>(
                    players[static_cast<std::size_t>(cursor)]);
                if (candidate != nullptr && SkipPending(*candidate)) {
                    ConsumeSkip(*candidate);
                    continue;
                }
                chosen = cursor;
                break;
            }
            incoming_index = chosen >= 0 ? chosen : PositiveMod(cursor, count);
        }
    }

    const ecs::Entity incoming =
        players[static_cast<std::size_t>(incoming_index)];
    if (from.has_value() && *from != incoming) {
        if (ecs::TurnState* outgoing = store.Get<ecs::TurnState>(*from)) {
            outgoing->is_current = false;
        }
    }
    ecs::TurnState* incoming_turn = EnsureTurnState(store, incoming);
    if (incoming_turn == nullptr) return OpResult::Resolved();
    incoming_turn->is_current = true;

    const json payload = TurnAdvancePayload(from, incoming, step,
                                            incoming_turn->turn_deadline_ms);
    OpResult result = OpResult::Resolved(payload);
    result.events.push_back(MakeEvent("turn_advance", payload));
    return result;
}

OpResult OpSkipTurn(ecs::EntityStore& store, const OpArgs& args,
                    OpContext& ctx) {
    (void)ctx;
    const std::optional<ecs::Entity> target = TargetPlayer(store, args);
    if (!target.has_value()) return OpResult::Resolved();

    ecs::TurnState* turn = EnsureTurnState(store, *target);
    if (turn == nullptr) return OpResult::Resolved();
    SetSkipPending(*turn);
    return OpResult::Resolved(
        json{{"target", EntityJson(*target)}, {"skip_pending", true}});
}

OpResult OpReverseDirection(ecs::EntityStore& store, const OpArgs& args,
                            OpContext& ctx) {
    (void)args;
    (void)ctx;
    const std::optional<ecs::Entity> match = FindMatch(store);
    if (!match.has_value()) return OpResult::Resolved();
    ecs::MatchMeta* meta = store.Get<ecs::MatchMeta>(*match);
    if (meta == nullptr) return OpResult::Resolved();

    meta->direction = meta->direction == ecs::Direction::kForward
                          ? ecs::Direction::kReverse
                          : ecs::Direction::kForward;
    // INFO: No event exists for a direction flip; the new direction rides
    //       the next `turn_advance`. With two players forward/reverse already
    //       land on the same opponent, so no extra advance is queued.
    return OpResult::Resolved(
        json{{"direction", static_cast<int>(meta->direction)}});
}

OpResult OpExtraTurn(ecs::EntityStore& store, const OpArgs& args,
                     OpContext& ctx) {
    (void)ctx;
    const std::optional<ecs::Entity> target = TargetPlayer(store, args);
    if (!target.has_value()) return OpResult::Resolved();

    ecs::TurnState* turn = EnsureTurnState(store, *target);
    if (turn == nullptr) return OpResult::Resolved();
    AddExtraTurn(*turn);
    return OpResult::Resolved(
        json{{"target", EntityJson(*target)},
             {"extra_turns_pending", ExtraTurns(*turn)}});
}

OpResult OpRedirectTurn(ecs::EntityStore& store, const OpArgs& args,
                        OpContext& ctx) {
    (void)ctx;
    const std::optional<ecs::Entity> target = TargetPlayer(store, args);
    if (!target.has_value()) return OpResult::Resolved();
    const std::optional<ecs::Entity> match = FindMatch(store);
    if (!match.has_value()) return OpResult::Resolved();
    const ecs::MatchMeta* meta = store.Get<ecs::MatchMeta>(*match);
    if (meta == nullptr) return OpResult::Resolved();

    std::optional<ecs::Entity> from = FindCurrentPlayer(store);
    if (from.has_value() && *from == *target) {
        // INFO: a jump onto the current player is a no-op; still report the
        //       settled turn rather than emitting a duplicate turn_advance.
        ecs::TurnState* settled = EnsureTurnState(store, *target);
        if (settled == nullptr) return OpResult::Resolved();
        return OpResult::Resolved(TurnAdvancePayload(
            from, *target, DirectionStep(meta->direction),
            settled->turn_deadline_ms));
    }
    if (from.has_value()) {
        if (ecs::TurnState* outgoing = store.Get<ecs::TurnState>(*from)) {
            outgoing->is_current = false;
        }
    }
    ecs::TurnState* jump_turn = EnsureTurnState(store, *target);
    if (jump_turn == nullptr) return OpResult::Resolved();
    jump_turn->is_current = true;

    const json payload = TurnAdvancePayload(from, *target,
                                            DirectionStep(meta->direction),
                                            jump_turn->turn_deadline_ms);
    OpResult result = OpResult::Resolved(payload);
    result.events.push_back(MakeEvent("turn_advance", payload));
    return result;
}

OpResult OpSetTurnTimer(ecs::EntityStore& store, const OpArgs& args,
                        OpContext& ctx) {
    (void)ctx;
    const std::optional<ecs::Entity> target = TargetPlayer(store, args);
    if (!target.has_value()) return OpResult::Resolved();

    // INFO: a kDuration arg is either a single leg object or a compound array;
    //       both are declaration-checked by the typed getters.
    const json* duration = args.GetObject("duration");
    if (duration == nullptr) duration = args.GetArray("duration");
    if (duration == nullptr) return OpResult::Resolved();
    ecs::DurationSpec spec{};
    if (!ParseDuration(*duration, spec)) return OpResult::Resolved();
    // ERROR: Documents `turn_deadline_ms` as an absolute epoch-ms value; a
    //        relative non-ms leg has no slot there. Fail loud instead of
    //        storing a mislabelled number.
    if (spec.unit != ecs::DurationUnit::kMs) {
        const std::string unit(UnitToken(spec.unit));
        return OpResult::Error(
            "set_turn_timer: turn timers must be in milliseconds "
            "(got unit '" + unit + "')");
    }

    ecs::TurnState* turn = EnsureTurnState(store, *target);
    if (turn == nullptr) return OpResult::Resolved();
    // TODO: The timer subsystem converts this ms duration into an
    //            absolute deadline and owns expiry; the op layer records the ms
    //            value only and never reads a clock.
    turn->turn_deadline_ms = spec.value;
    return OpResult::Resolved(
        json{{"target", EntityJson(*target)},
             {"deadline_ms", spec.value},
             {"unit", std::string(UnitToken(spec.unit))}});
}

}  // namespace match::ops::detail
