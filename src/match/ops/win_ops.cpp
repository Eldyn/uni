#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

/**
 * @file win_ops.cpp
 * @brief Win op bodies.
 *
 * `check_win` is the engine's own win evaluator: it dispatches the
 * veto-capable `win_check` hook pair, then (unless a before-hook vetoed)
 * records the engine default winner — the first hand-empty player in seat
 * order — through the `Placements` component and emits a `placement`
 * descriptor. `declare_winner` records an explicit winner with a `normal` or
 * `special` kind (Black Hole), and `add_placement` appends a shed-order
 * finisher. None of them assembles the `match_end` packet.
 */

namespace match::ops::detail {
namespace {

using nlohmann::json;

/** @brief Entity handle as a JSON object. */
json EntityJson(ecs::Entity entity) {
    return json{{"index", entity.index}, {"generation", entity.generation}};
}

/**
 * @brief Parse an `{index, generation}` entity handle; nullopt when malformed.
 *
 * Used to read a candidate back out of the mutable `win_check` payload so a
 * before-hook can "declare other". Only the shape is validated here
 * liveness is the caller's check.
 */
std::optional<ecs::Entity> EntityFromJson(const json& value) {
    if (!value.is_object()) return std::nullopt;
    const auto index = value.find("index");
    const auto generation = value.find("generation");
    if (index == value.end() || generation == value.end()) return std::nullopt;
    if (!index->is_number_integer() || !generation->is_number_integer()) {
        return std::nullopt;
    }
    const int64_t raw_index = index->get<int64_t>();
    const int64_t raw_generation = generation->get<int64_t>();
    if (raw_index < 0 || raw_generation < 0) return std::nullopt;
    ecs::Entity entity;
    entity.index = static_cast<uint32_t>(raw_index);
    entity.generation = static_cast<uint32_t>(raw_generation);
    return entity;
}

/**
 * @brief First player (seat order) whose hand is present and empty.
 *
 * The engine default. A player without a `hand` component is not a
 * candidate (fail-safe). Seat order is the tie-break.
 */
std::optional<ecs::Entity> FirstHandEmptyBySeat(ecs::EntityStore& store) {
    for (ecs::Entity player : PlayersBySeat(store)) {
        const ecs::Hand* hand = store.Get<ecs::Hand>(player);
        if (hand != nullptr && hand->cards.empty()) return player;
    }
    return std::nullopt;
}

/** @brief Whether a placement was newly appended and its 1-based place. */
struct PlacementRecord {
    uint32_t place = 0; /**< 1-based finish position. */
    bool added = false; /**< false when the player was already placed. */
};

/**
 * @brief Append `player` to the match `Placements`, dedupe by entity.
 *
 * Returns nullopt when there is no match entity to carry placements (a
 * fail-safe no-op for the caller). An already-placed player is not appended
 * again and `added` is false.
 */
std::optional<PlacementRecord> RecordPlacement(ecs::EntityStore& store,
                                               ecs::Entity player) {
    const std::optional<ecs::Entity> match = FindMatch(store);
    if (!match.has_value()) return std::nullopt;

    ecs::Placements* placements = store.Get<ecs::Placements>(*match);
    if (placements == nullptr) {
        store.Add(*match, ecs::Placements{});
        placements = store.Get<ecs::Placements>(*match);
    }
    if (placements == nullptr) return std::nullopt;

    for (std::size_t i = 0; i < placements->order.size(); ++i) {
        if (placements->order[i] == player) {
            PlacementRecord existing;
            existing.place = static_cast<uint32_t>(i + 1);
            existing.added = false;
            return existing;
        }
    }
    placements->order.push_back(player);
    PlacementRecord record;
    record.place = static_cast<uint32_t>(placements->order.size());
    record.added = true;
    return record;
}

/**
 * @brief Build the `placement {player, place, kind}` payload.
 *
 * `kind` is the engine-readable seam for the Black Hole `special` win: the
 * store `Placements` component only stores an order, so the finish kind travels
 * on the descriptor (and the op `value`) instead of a new component.
 */
json PlacementJson(ecs::Entity player, uint32_t place,
                   std::string_view kind) {
    return json{{"player", EntityJson(player)},
                {"place", place},
                {"kind", std::string(kind)}};
}

}  // namespace

OpResult OpCheckWin(ecs::EntityStore& store, const OpArgs& args,
                    OpContext& ctx) {
    (void)args;

    // INFO: candidate = engine default (first hand-empty player by seat, or
    //       none). Before-hooks may rewrite `data.player` to declare another
    //       finisher or null it; a veto blocks the default entirely.
    const std::optional<ecs::Entity> candidate = FirstHandEmptyBySeat(store);

    ecs::HookPayload payload;
    payload.hook = ecs::HookId{"win_check", ecs::HookPhase::kBefore};
    payload.data =
        json{{"player", candidate.has_value() ? EntityJson(*candidate)
                                              : json(nullptr)}};
    const ecs::HookDispatchResult before =
        ctx.event_bus.DispatchBefore(payload.hook, payload);

    std::optional<ecs::Entity> winner = candidate;
    if (payload.data.is_object()) {
        const auto it = payload.data.find("player");
        if (it != payload.data.end()) {
            if (it->is_null()) {
                winner = std::nullopt;
            } else if (const std::optional<ecs::Entity> rewritten =
                           EntityFromJson(*it);
                       rewritten.has_value()) {
                winner = rewritten;
            }
            // WARN: a malformed non-null rewrite keeps the engine default.
        }
    }

    OpResult result = OpResult::Resolved();
    if (!before.vetoed && winner.has_value() && store.IsAlive(*winner)
        && store.Has<ecs::PlayerInfo>(*winner)) {
        const std::optional<PlacementRecord> recorded =
            RecordPlacement(store, *winner);
        if (recorded.has_value() && recorded->added) {
            const json placement =
                PlacementJson(*winner, recorded->place, "normal");
            result.value = placement;
            result.events.push_back(MakeEvent("placement", placement));
        }
    }

    // INFO: the after-hook observes the settled state, vetoed or not.
    ecs::HookPayload after = payload;
    after.hook = ecs::HookId{"win_check", ecs::HookPhase::kAfter};
    after.veto = false;
    ctx.event_bus.DispatchAfter(after.hook, after);

    return result;
}

OpResult OpDeclareWinner(ecs::EntityStore& store, const OpArgs& args,
                         OpContext& ctx) {
    (void)ctx;
    const std::optional<ecs::Entity> player = args.FirstEntity("player");
    if (!player.has_value() || !store.IsAlive(*player)) {
        return OpResult::Resolved();
    }
    if (!store.Has<ecs::PlayerInfo>(*player)) return OpResult::Resolved();

    std::string kind;
    if (!args.GetString("kind", kind)) return OpResult::Resolved();
    if (kind != "normal" && kind != "special") return OpResult::Resolved();

    const std::optional<PlacementRecord> recorded =
        RecordPlacement(store, *player);
    if (!recorded.has_value() || !recorded->added) return OpResult::Resolved();

    const json placement =
        PlacementJson(*player, recorded->place, kind);
    OpResult result = OpResult::Resolved(placement);
    result.events.push_back(MakeEvent("placement", placement));
    return result;
}

OpResult OpAddPlacement(ecs::EntityStore& store, const OpArgs& args,
                        OpContext& ctx) {
    (void)ctx;
    const std::optional<ecs::Entity> player = args.FirstEntity("player");
    if (!player.has_value() || !store.IsAlive(*player)) {
        return OpResult::Resolved();
    }
    if (!store.Has<ecs::PlayerInfo>(*player)) return OpResult::Resolved();

    const std::optional<PlacementRecord> recorded =
        RecordPlacement(store, *player);
    if (!recorded.has_value() || !recorded->added) return OpResult::Resolved();

    const json placement = PlacementJson(*player, recorded->place, "normal");
    OpResult result = OpResult::Resolved(placement);
    result.events.push_back(MakeEvent("placement", placement));
    return result;
}

}  // namespace match::ops::detail
