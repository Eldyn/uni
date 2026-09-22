#pragma once

#include <match/duration.hpp>
#include <match/ecs/entity_store.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace match {

/**
 * @file scheduler.hpp
 * @brief Scheduler: arm deferred graphs, extract on Tick.
 *
 * A `schedule` node arms a `PendingSchedule` entry carrying the subgraph, its
 * duration spec and the `started_ms` stamp taken from the clock. `Tick`
 * returns the subgraphs whose duration elapsed and removes their entries — the
 * caller is responsible for running them through the Resolver. The
 * scheduler never executes a graph and owns no match loop.
 *
 * `ms` legs are driven by `Tick`; `turns`/`rounds`/`cards_played` legs are
 * driven by the event entry points (`OnTurnEnd`/`OnRoundEnd`/`OnCardPlayed`),
 * mirroring `status::StatusSystem`'s drivers. All four drivers share one
 * in-memory progress record per logical schedule, because the frozen
 * `PendingSchedule::Entry` has no counter slot.
 *
 * Compound durations are stored as one entry per leg sharing an `id`;
 * the logical schedule elapses when ANY leg does ("whichever elapses first")
 * and all its legs are then removed together.
 */

/**
 * @class Scheduler
 * @brief Arms and expires `pending_schedule` graphs for one match.
 *
 * One instance is expected per match; entry ids are allocated
 * monotonically per instance and are unique within the match. The clock is
 * only read at `Arm` time; `Tick` takes the caller's `now_ms` so tests and
 * The engine drive time explicitly. The event drivers read the injected clock
 * so a compound `ms` leg that elapsed since the last call still fires.
 */
class Scheduler {
public:
    /**
     * @brief Construct with an injected clock.
     * @param clock Wall-clock seam; defaults to the epoch-ms system clock.
     */
    explicit Scheduler(NowMs clock = DefaultNowMs());

    /**
     * @brief Arm `graph` under a single-leg duration.
     *
     * Stamps `started_ms` from the clock. Returns the new entry id, or
     * `nullopt` for a negative value or a match with no live entity.
     */
    std::optional<uint32_t> Arm(ecs::EntityStore& store, ecs::Entity match,
                                const nlohmann::json& graph,
                                const ecs::DurationSpec& duration);

    /**
     * @brief Arm `graph` under a compound duration (one entry per leg).
     *
     * Every leg shares one id and one `started_ms`; `Tick` expires the group on
     * the first elapsed leg. Returns the group id, or `nullopt` for an empty
     * duration or any negative leg.
     */
    std::optional<uint32_t> Arm(ecs::EntityStore& store, ecs::Entity match,
                                const nlohmann::json& graph,
                                const Duration& duration);

    /**
     * @brief Remove and return the subgraphs whose duration elapsed by
     *        `now_ms`.
     *
     * Advances the wall-clock (`ms`) legs only; `turns`/`rounds`/
     * `cards_played` legs are advanced by the event drivers below, but any
     * leg whose counter already reached its threshold still expires here. A
     * leg with value 0 is already elapsed. Output order is ascending entry
     * id, i.e. arm order.
     */
    std::vector<nlohmann::json> Tick(ecs::EntityStore& store,
                                     ecs::Entity match, int64_t now_ms);

    /**
     * @brief Advance the `turns` legs and return the now-elapsed subgraphs.
     *
     * Mirrors `status::StatusSystem::OnTurnEnd`: the caller
     * invokes it once per ended turn. `owner` names whose turn ended; the
     * frozen `PendingSchedule::Entry` carries no owner slot, so every armed
     * schedule is match-scoped and all `turns` legs advance. Wall-clock is
     * refreshed so a compound `ms` leg that already elapsed still fires.
     */
    std::vector<nlohmann::json> OnTurnEnd(ecs::EntityStore& store,
                                          ecs::Entity match,
                                          ecs::Entity owner);

    /**
     * @brief Advance every `rounds` leg and return the now-elapsed subgraphs.
     */
    std::vector<nlohmann::json> OnRoundEnd(ecs::EntityStore& store,
                                           ecs::Entity match);

    /**
     * @brief Advance every `cards_played` leg and return the now-elapsed
     *        subgraphs.
     */
    std::vector<nlohmann::json> OnCardPlayed(ecs::EntityStore& store,
                                             ecs::Entity match);

    /**
     * @brief Number of pending entries on `match` (tests / diagnostics).
     */
    std::size_t Pending(const ecs::EntityStore& store,
                        ecs::Entity match) const;

private:
    /** @brief Which counter a driver advances. */
    enum class Drive { kMs, kTurns, kRounds, kCards };

    /**
     * @brief Shared body of `Tick` and the event drivers.
     *
     * Groups entries by id, advances the drive's counter once per group,
     * refreshes `elapsed_ms` from the group's `started_ms`, expires any group
     * with a leg at/over threshold and returns its graph. `kMs` does not
     * advance an event counter.
     */
    std::vector<nlohmann::json> Run(ecs::EntityStore& store,
                                    ecs::Entity match, int64_t now_ms,
                                    Drive drive);

    NowMs clock_;
    uint32_t next_id_ = 1;
    /**
     * INFO: per-group counters live here because the frozen
     *       `PendingSchedule::Entry` has no counter slot; pruned whenever a
     *       group is no longer armed.
     */
    std::map<uint32_t, DurationProgress> progress_;
};

}  // namespace match
