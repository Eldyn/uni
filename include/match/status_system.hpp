#pragma once

#include <match/duration.hpp>
#include <match/ecs/components.hpp>
#include <match/ecs/entity_store.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

/**
 * @file status_system.hpp
 * @brief Status expiry drivers.
 *
 * The storage API in `match/status.hpp` owns the policies but
 * has no timing: nothing removes an expired instance. This header adds the
 * driver that does, from the three sources names:
 *
 * - `ms` legs are checked on `Tick(now_ms)` (the clock seam);
 * - `turns` legs advance on the owner's `turn_end`;
 * - `rounds` legs advance on `round_end`;
 * - `cards_played` legs advance on any `after:play`.
 *
 * A compound duration expires on the first elapsed leg (`DurationElapsed`).
 * On expiry the instance is removed, a `status_removed` descriptor is
 * produced for the caller to route, and the status definition's optional
 * `on_expire` graph is handed to the caller-supplied `ExpireHandler`.
 *
 * The driver owns NO match loop and executes no graph itself:
 * it is a pure function of the store, the current time and the caller's
 * event, exactly like `Scheduler::Tick`.
 */

namespace match::status {

/**
 * @struct StatusDefView
 * @brief The status-definition fields the expiry driver consumes.
 *
 * The caller supplies a lookup; the driver has no built-in def source (the
 * frozen `Status` carries only per-instance state). Fields:
 *
 * - `hidden` is the privacy flag. It is OR-ed with the instance's own
 *   flag on the emitted `status_removed` so the view layer can filter, and so a
 *   def-level `hidden:true` survives the apply path (which cannot see
 *   the def);
 * - `stack_policy` / `has_stack_policy` mirror the def, for callers that
 *   share one lookup between apply and expiry;
 * - `duration`, when non-empty, is the definition's (possibly compound)
 *   duration and wins over the instance's collapsed one-leg `Status::duration`
 * When empty the
 *   instance's stored leg is used;
 * - `on_expire` is the optional graph run on expiry; null means none.
 */
struct StatusDefView {
    bool hidden = false;
    ecs::StackPolicy stack_policy = ecs::StackPolicy::kReplace;
    bool has_stack_policy = false;
    Duration duration;          /**< empty = use the instance's leg. */
    nlohmann::json on_expire;   /**< null = no expiry graph. */
};

/**
 * @brief Caller-supplied status-definition source, keyed by status id.
 *
 * Returns `nullopt` for an unknown id. The driver still removes and emits
 * when no lookup is installed, so a missing def source is not a failure.
 */
using StatusDefLookup =
    std::function<std::optional<StatusDefView>(std::string_view)>;

/**
 * @brief Caller-supplied handle that runs a def's `on_expire` graph.
 *
 * Invoked after the instance is removed and its `status_removed` descriptor
 * built. The engine routes `graph` through the Resolver with `owner` as the
 * status bearer; a test injects a recording fake to observe the call.
 */
using ExpireHandler = std::function<void(
    ecs::EntityStore& store, ecs::Entity owner,
    const ecs::Status& instance, const nlohmann::json& graph)>;

/**
 * @struct StatusExpiry
 * @brief One instance removed by a driver, plus its emitted event.
 */
struct StatusExpiry {
    ecs::Entity owner;          /**< entity that carried the status. */
    ecs::Status instance;       /**< the removed instance, verbatim. */
    bool hidden = false;        /**< 11.3-effective privacy flag. */
    nlohmann::json event;       /**< `status_removed` descriptor. */
};

/**
 * @class StatusSystem
 * @brief Drives status expiry for one match.
 *
 * One instance is expected per match. Progress is tracked
 * per `(entity, instance_id)` in memory: the frozen `Status` has no arm-time
 * or counter slot, so `Tick`/event calls lazily arm an instance the first
 * time it is seen. A re-arm is detected when the instance's stored leg
 * changes (a `replace` apply); an `independent` instance has a fresh
 * `instance_id` and arms on its own. Tracking is pruned when an instance
 * disappears from the store.
 *
 * `now_ms` comes from the `NowMs` seam. `Tick` takes it explicitly (the
 * caller's loop already has it); the event drivers read the injected clock.
 */
class StatusSystem {
public:
    /**
     * @brief Construct with an injected clock.
     * @param clock Wall-clock seam; defaults to the epoch-ms system clock.
     */
    explicit StatusSystem(NowMs clock = DefaultNowMs());

    /** @brief Install the status-definition lookup (optional). */
    void SetDefLookup(StatusDefLookup lookup);

    /** @brief Install the `on_expire` graph handle (optional). */
    void SetExpireHandler(ExpireHandler handler);

    /**
     * @brief Advance `ms` legs (and any already-elapsed compound leg) to
     *        `now_ms`.
     *
     * @return Every instance that expired, in store order.
     */
    std::vector<StatusExpiry> Tick(ecs::EntityStore& store, int64_t now_ms);

    /**
     * @brief Advance the `turns` leg of `owner`'s statuses.
     *
     * Only statuses on `owner` advance: a `turn_end` of any other player is
     * not the owner's turn. Wall-clock is refreshed so a compound `ms` leg
     * that already elapsed still fires.
     */
    std::vector<StatusExpiry> OnTurnEnd(ecs::EntityStore& store,
                                        ecs::Entity owner);

    /**
     * @brief Advance every status's `rounds` leg.
     */
    std::vector<StatusExpiry> OnRoundEnd(ecs::EntityStore& store);

    /**
     * @brief Advance every status's `cards_played` leg.
     */
    std::vector<StatusExpiry> OnCardPlayed(ecs::EntityStore& store);

    /** @brief Number of instances with live progress tracking. */
    std::size_t Tracked() const { return tracked_.size(); }

private:
    /** @brief Which counter a driver advances. */
    enum class Drive { kMs, kTurns, kRounds, kCards };

    struct Key {
        uint32_t index = 0;
        uint32_t generation = 0;
        uint32_t instance_id = 0;

        bool operator<(const Key& other) const {
            if (index != other.index) return index < other.index;
            if (generation != other.generation) {
                return generation < other.generation;
            }
            return instance_id < other.instance_id;
        }
    };

    struct TrackedInstance {
        bool initialized = false;
        int64_t armed_ms = 0;
        DurationProgress progress;
        ecs::DurationSpec stored_leg;
    };

    std::vector<StatusExpiry> Run(ecs::EntityStore& store, int64_t now_ms,
                                  Drive drive, ecs::Entity owner,
                                  bool has_owner);

    Duration ResolveDuration(const ecs::Status& instance,
                             const std::optional<StatusDefView>& def) const;

    NowMs clock_;
    StatusDefLookup lookup_;
    ExpireHandler expire_handler_;
    std::map<Key, TrackedInstance> tracked_;
};

}  // namespace match::status
