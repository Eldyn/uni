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

std::vector<nlohmann::json> Scheduler::Tick(ecs::EntityStore& store,
                                            ecs::Entity match,
                                            int64_t now_ms) {
    ecs::PendingSchedule* schedule =
        store.Get<ecs::PendingSchedule>(match);
    if (schedule == nullptr) return {};

    struct Group {
        nlohmann::json graph;
        bool elapsed = false;
    };

    // INFO: grouping by id keeps a compound schedule as one logical unit;
    //       `std::map` orders output by ascending id, i.e. arm order.
    std::map<uint32_t, Group> groups;
    for (const ecs::PendingSchedule::Entry& entry : schedule->entries) {
        Group& group = groups[entry.id];
        if (group.graph.is_null()) group.graph = entry.graph;
        DurationProgress progress;
        progress.elapsed_ms = now_ms - entry.started_ms;
        if (LegElapsed(entry.duration, progress)) group.elapsed = true;
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
    return elapsed_graphs;
}

std::size_t Scheduler::Pending(const ecs::EntityStore& store,
                               ecs::Entity match) const {
    const ecs::PendingSchedule* schedule =
        store.Get<ecs::PendingSchedule>(match);
    if (schedule == nullptr) return 0;
    return schedule->entries.size();
}

}  // namespace match
