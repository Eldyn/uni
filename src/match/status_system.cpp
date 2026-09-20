#include <match/status.hpp>
#include <match/status_system.hpp>

#include <match/ops/op_helpers.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

/**
 * @file status_system.cpp
 * @brief Status expiry driver bodies.
 */

namespace match::status {
namespace {

using nlohmann::json;

/** @brief Entity handle as `{index, generation}` (wire shape). */
json EntityJson(ecs::Entity entity) {
    return json{{"index", entity.index}, {"generation", entity.generation}};
}

/**
 * @brief Build the `status_removed` descriptor.
 *
 * Payload matches the status-op shape (`target`, `status_kind`,
 * `instance`) plus `hidden` so the packet layer can apply filtering.
 */
json StatusRemovedEvent(ecs::Entity owner, const ecs::Status& instance,
                        bool hidden) {
    json payload = {{"target", EntityJson(owner)},
                    {"status_kind", instance.status_id},
                    {"instance", instance.instance_id},
                    {"hidden", hidden}};
    return ops::MakeEvent("status_removed", std::move(payload));
}

/** @brief True when any leg of `duration` has a positive threshold. */
bool HasPositiveLeg(const Duration& duration) {
    for (const ecs::DurationSpec& leg : duration.legs) {
        if (leg.value > 0) return true;
    }
    return false;
}

/** @brief True when two stored duration legs are identical. */
bool SameLeg(const ecs::DurationSpec& left, const ecs::DurationSpec& right) {
    return left.unit == right.unit && left.value == right.value;
}

}  // namespace

StatusSystem::StatusSystem(NowMs clock) : clock_(std::move(clock)) {}

void StatusSystem::SetDefLookup(StatusDefLookup lookup) {
    lookup_ = std::move(lookup);
}

void StatusSystem::SetExpireHandler(ExpireHandler handler) {
    expire_handler_ = std::move(handler);
}

Duration StatusSystem::ResolveDuration(
    const ecs::Status& instance,
    const std::optional<StatusDefView>& def) const {
    // INFO: A def-declared compound duration is the only way to express
    //       "whichever leg elapses first": the frozen Status::duration holds
    //       one leg. An all-nonpositive def
    //       duration is treated as absent, so a def cannot accidentally make
    //       a status permanent-by-accident or immediate-by-accident.
    if (def.has_value() && HasPositiveLeg(def->duration)) {
        return def->duration;
    }
    Duration duration;
    // INFO: a non-positive stored leg is the timer layer's "no duration"
    //       marker; an empty Duration means permanent, not immediately elapsed.
    if (instance.duration.value > 0) {
        duration.legs.push_back(instance.duration);
    }
    return duration;
}

std::vector<StatusExpiry> StatusSystem::Run(ecs::EntityStore& store,
                                            int64_t now_ms, Drive drive,
                                            ecs::Entity owner,
                                            bool has_owner) {
    std::vector<StatusExpiry> expired;
    std::set<Key> present;

    const std::vector<ecs::Entity> carriers =
        store.EntitiesWith<ecs::StatusList>();
    for (ecs::Entity carrier : carriers) {
        const bool advance =
            drive != Drive::kTurns || (has_owner && carrier == owner);

        const std::vector<ecs::Status> instances = status::List(store, carrier);
        for (const ecs::Status& instance : instances) {
            std::optional<StatusDefView> def;
            if (lookup_) def = lookup_(instance.status_id);

            const Duration duration = ResolveDuration(instance, def);
            if (duration.Empty()) continue;  // permanent: no expiry leg.

            const Key key{carrier.index, carrier.generation,
                          instance.instance_id};
            present.insert(key);

            TrackedInstance& tracked = tracked_[key];
            const bool changed = !tracked.initialized ||
                !SameLeg(tracked.stored_leg, instance.duration);
            if (changed) {
                tracked.initialized = true;
                tracked.armed_ms = now_ms;
                tracked.progress = DurationProgress{};
                tracked.stored_leg = instance.duration;
            }

            // INFO: wall-clock is refreshed on every driver so a compound ms
            //       leg that elapsed between Ticks still fires on an event.
            tracked.progress.elapsed_ms = now_ms - tracked.armed_ms;
            if (!advance) continue;

            switch (drive) {
                case Drive::kTurns:
                    tracked.progress.turns += 1;
                    break;
                case Drive::kRounds:
                    tracked.progress.rounds += 1;
                    break;
                case Drive::kCards:
                    tracked.progress.cards_played += 1;
                    break;
                case Drive::kMs:
                    break;
            }

            if (!DurationElapsed(duration, tracked.progress)) continue;

            const std::optional<ecs::Status> removed =
                status::RemoveByInstance(store, carrier,
                                         instance.instance_id);
            if (!removed.has_value()) {
                tracked_.erase(key);
                present.erase(key);
                continue;
            }

            const bool hidden =
                removed->hidden || (def.has_value() && def->hidden);
            StatusExpiry record;
            record.owner = carrier;
            record.instance = *removed;
            record.hidden = hidden;
            record.event = StatusRemovedEvent(carrier, *removed, hidden);

            if (def.has_value() && !def->on_expire.is_null() &&
                expire_handler_) {
                expire_handler_(store, carrier, *removed, def->on_expire);
            }
            tracked_.erase(key);
            present.erase(key);
            expired.push_back(std::move(record));
        }
    }

    // INFO: drop tracking for instances the store no longer carries (removed
    //       by an op, a dead owner, or a re-armed permanent status).
    for (auto it = tracked_.begin(); it != tracked_.end();) {
        if (present.find(it->first) == present.end()) {
            it = tracked_.erase(it);
        } else {
            ++it;
        }
    }
    return expired;
}

std::vector<StatusExpiry> StatusSystem::Tick(ecs::EntityStore& store,
                                             int64_t now_ms) {
    return Run(store, now_ms, Drive::kMs, ecs::Entity{}, false);
}

std::vector<StatusExpiry> StatusSystem::OnTurnEnd(ecs::EntityStore& store,
                                                  ecs::Entity owner) {
    return Run(store, clock_(), Drive::kTurns, owner, true);
}

std::vector<StatusExpiry> StatusSystem::OnRoundEnd(ecs::EntityStore& store) {
    return Run(store, clock_(), Drive::kRounds, ecs::Entity{}, false);
}

std::vector<StatusExpiry> StatusSystem::OnCardPlayed(
    ecs::EntityStore& store) {
    return Run(store, clock_(), Drive::kCards, ecs::Entity{}, false);
}

}  // namespace match::status
