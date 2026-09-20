#include <match/scheduler.hpp>

#include <match/ecs/components.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

/**
 * @file scheduler.cpp
 * @brief Scheduler bodies: arm entries, extract elapsed subgraphs.
 */

namespace match {

Scheduler::Scheduler(NowMs clock) : clock_(std::move(clock)) {}

std::optional<uint32_t> Scheduler::Arm(ecs::EntityStore& store,
                                       ecs::Entity match,
                                       const nlohmann::json& graph,
                                       const ecs::DurationSpec& duration) {
    if (duration.value < 0) return std::nullopt;
    ecs::PendingSchedule* schedule =
        store.Get<ecs::PendingSchedule>(match);
    if (schedule == nullptr) {
        schedule = store.Add(match, ecs::PendingSchedule{});
    }
    if (schedule == nullptr) return std::nullopt;

    const uint32_t id = next_id_++;
    ecs::PendingSchedule::Entry entry;
    entry.id = id;
    entry.graph = graph;
    entry.duration = duration;
    entry.started_ms = clock_();
    schedule->entries.push_back(std::move(entry));
    return id;
}

std::optional<uint32_t> Scheduler::Arm(ecs::EntityStore& store,
                                       ecs::Entity match,
                                       const nlohmann::json& graph,
                                       const Duration& duration) {
    if (duration.Empty()) return std::nullopt;
    for (const ecs::DurationSpec& leg : duration.legs) {
        if (leg.value < 0) return std::nullopt;
    }

    ecs::PendingSchedule* schedule =
        store.Get<ecs::PendingSchedule>(match);
    if (schedule == nullptr) {
        schedule = store.Add(match, ecs::PendingSchedule{});
    }
    if (schedule == nullptr) return std::nullopt;

    const uint32_t id = next_id_++;
    const int64_t started_ms = clock_();
    for (const ecs::DurationSpec& leg : duration.legs) {
        ecs::PendingSchedule::Entry entry;
        entry.id = id;
        entry.graph = graph;
        entry.duration = leg;
        entry.started_ms = started_ms;
        schedule->entries.push_back(std::move(entry));
    }
    return id;
}

std::vector<nlohmann::json> Scheduler::Run(ecs::EntityStore& store,
                                           ecs::Entity match, int64_t now_ms,
                                           Drive drive) {
    ecs::PendingSchedule* schedule =
        store.Get<ecs::PendingSchedule>(match);
    if (schedule == nullptr) {
        progress_.clear();
        return {};
    }

    struct Group {
        nlohmann::json graph;
        int64_t started_ms = 0;
        std::vector<ecs::DurationSpec> legs;
        bool elapsed = false;
    };

    // INFO: grouping by id keeps a compound schedule as one logical unit;
    //       `std::map` orders output by ascending id, i.e. arm order.
    std::map<uint32_t, Group> groups;
    for (const ecs::PendingSchedule::Entry& entry : schedule->entries) {
        Group& group = groups[entry.id];
        if (group.legs.empty()) {
            group.graph = entry.graph;
            group.started_ms = entry.started_ms;
        }
        group.legs.push_back(entry.duration);
    }

    for (auto& [id, group] : groups) {
        DurationProgress& progress = progress_[id];
        // INFO: wall-clock is refreshed on every driver so a compound ms leg
        //       that elapsed between calls still fires on an event.
        progress.elapsed_ms = now_ms - group.started_ms;
        switch (drive) {
            case Drive::kTurns:
                progress.turns += 1;
                break;
            case Drive::kRounds:
                progress.rounds += 1;
                break;
            case Drive::kCards:
                progress.cards_played += 1;
                break;
            case Drive::kMs:
                break;
        }
        for (const ecs::DurationSpec& leg : group.legs) {
            if (LegElapsed(leg, progress)) {
                group.elapsed = true;
                break;
            }
        }
    }

    std::vector<nlohmann::json> elapsed_graphs;
    std::set<uint32_t> elapsed_ids;
    for (const auto& [id, group] : groups) {
        if (!group.elapsed) continue;
        elapsed_graphs.push_back(group.graph);
        elapsed_ids.insert(id);
    }
    if (!elapsed_ids.empty()) {
        std::vector<ecs::PendingSchedule::Entry>& entries =
            schedule->entries;
        entries.erase(
            std::remove_if(entries.begin(), entries.end(),
                           [&elapsed_ids](
                               const ecs::PendingSchedule::Entry& entry) {
                               return elapsed_ids.count(entry.id) > 0;
                           }),
            entries.end());
    }

    // INFO: drop counters for groups no longer armed (expired or removed).
    std::set<uint32_t> live;
    for (const ecs::PendingSchedule::Entry& entry : schedule->entries) {
        live.insert(entry.id);
    }
    for (auto it = progress_.begin(); it != progress_.end();) {
        if (live.count(it->first) == 0) {
            it = progress_.erase(it);
        } else {
            ++it;
        }
    }
    return elapsed_graphs;
}

std::vector<nlohmann::json> Scheduler::Tick(ecs::EntityStore& store,
                                            ecs::Entity match,
                                            int64_t now_ms) {
    return Run(store, match, now_ms, Drive::kMs);
}

std::vector<nlohmann::json> Scheduler::OnTurnEnd(ecs::EntityStore& store,
                                                 ecs::Entity match,
                                                 ecs::Entity owner) {
    // INFO: schedules are match-scoped (the frozen entry has no owner slot),
    //       so every `turns` leg advances; `owner` names the ended turn for
    //       parity with status::StatusSystem::OnTurnEnd.
    (void)owner;
    return Run(store, match, clock_(), Drive::kTurns);
}

std::vector<nlohmann::json> Scheduler::OnRoundEnd(ecs::EntityStore& store,
                                                  ecs::Entity match) {
    return Run(store, match, clock_(), Drive::kRounds);
}

std::vector<nlohmann::json> Scheduler::OnCardPlayed(ecs::EntityStore& store,
                                                    ecs::Entity match) {
    return Run(store, match, clock_(), Drive::kCards);
}

std::size_t Scheduler::Pending(const ecs::EntityStore& store,
                               ecs::Entity match) const {
    const ecs::PendingSchedule* schedule =
        store.Get<ecs::PendingSchedule>(match);
    if (schedule == nullptr) return 0;
    return schedule->entries.size();
}

}  // namespace match
