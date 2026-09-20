#pragma once

#include <match/duration.hpp>
#include <match/ecs/entity_store.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
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
 * The engine drive time explicitly.
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
     * Only `ms` legs are evaluated from the clock here (an event-driven
     * `turns`/`rounds`/`cards_played` leg is armed but never expires via
     * `Tick` — the event drivers own those counters). A leg with
     * value 0 is already elapsed. Output order is ascending entry id, i.e.
     * arm order.
     */
    std::vector<nlohmann::json> Tick(ecs::EntityStore& store,
                                     ecs::Entity match, int64_t now_ms);

    /**
     * @brief Number of pending entries on `match` (tests / diagnostics).
     */
    std::size_t Pending(const ecs::EntityStore& store,
                        ecs::Entity match) const;

private:
    NowMs clock_;
    uint32_t next_id_ = 1;
};

}  // namespace match
