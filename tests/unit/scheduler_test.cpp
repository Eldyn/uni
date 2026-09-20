#include <doctest/doctest.h>

#include <match/duration.hpp>
#include <match/ecs/components.hpp>
#include <match/ecs/entity_store.hpp>
#include <match/scheduler.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <vector>

using match::Duration;
using match::Scheduler;
using match::ecs::DurationSpec;
using match::ecs::DurationUnit;
using match::ecs::Entity;
using match::ecs::EntityStore;
using match::ecs::PendingSchedule;

using nlohmann::json;

namespace {

/**
 * INFO: one store/match/scheduler fixture per case. `now` is the injected
 *       clock's current value; the scheduler reads it only when arming.
 */
struct Harness {
    EntityStore store;
    Entity match = store.Create();
    int64_t now = 1000;
    Scheduler scheduler{[this]() { return now; }};
};

json Graph(const std::string& id) {
    return json{{"id", id}, {"nodes", json::array()}};
}

Duration Compound(std::initializer_list<DurationSpec> legs) {
    Duration duration;
    duration.legs.assign(legs);
    return duration;
}

}  // namespace

TEST_CASE("scheduler: arm stamps started_ms; Tick before expiry is a no-op") {
    Harness h;
    const json graph = Graph("a");
    const std::optional<uint32_t> id = h.scheduler.Arm(
        h.store, h.match, graph, DurationSpec{DurationUnit::kMs, 100});
    REQUIRE(id.has_value());
    CHECK(h.scheduler.Pending(h.store, h.match) == 1);

    const PendingSchedule* schedule =
        h.store.Get<PendingSchedule>(h.match);
    REQUIRE(schedule != nullptr);
    REQUIRE(schedule->entries.size() == 1);
    CHECK(schedule->entries[0].started_ms == 1000);
    CHECK(schedule->entries[0].duration.unit == DurationUnit::kMs);
    CHECK(schedule->entries[0].duration.value == 100);

    CHECK(h.scheduler.Tick(h.store, h.match, 1099).empty());
    CHECK(h.scheduler.Pending(h.store, h.match) == 1);
}

TEST_CASE("scheduler: elapsed entry returns its graph once then is removed") {
    Harness h;
    const json graph = Graph("a");
    REQUIRE(h.scheduler
                .Arm(h.store, h.match, graph,
                     DurationSpec{DurationUnit::kMs, 100})
                .has_value());

    const std::vector<json> elapsed =
        h.scheduler.Tick(h.store, h.match, 1100);
    REQUIRE(elapsed.size() == 1);
    CHECK(elapsed[0] == graph);
    CHECK(h.scheduler.Pending(h.store, h.match) == 0);
    CHECK(h.scheduler.Tick(h.store, h.match, 2000).empty());
}

TEST_CASE("scheduler: compound expires on the first elapsed leg") {
    Harness h;
    const json graph = Graph("compound");
    const std::optional<uint32_t> id = h.scheduler.Arm(
        h.store, h.match, graph,
        Compound({DurationSpec{DurationUnit::kMs, 100},
                  DurationSpec{DurationUnit::kMs, 500}}));
    REQUIRE(id.has_value());
    CHECK(h.scheduler.Pending(h.store, h.match) == 2);

    CHECK(h.scheduler.Tick(h.store, h.match, 1099).empty());
    CHECK(h.scheduler.Pending(h.store, h.match) == 2);

    const std::vector<json> elapsed =
        h.scheduler.Tick(h.store, h.match, 1100);
    REQUIRE(elapsed.size() == 1);
    CHECK(elapsed[0] == graph);
    CHECK(h.scheduler.Pending(h.store, h.match) == 0);
}

TEST_CASE("scheduler: compound with an event-driven leg expires on ms") {
    Harness h;
    const json graph = Graph("mixed");
    REQUIRE(h.scheduler
                .Arm(h.store, h.match, graph,
                     Compound({DurationSpec{DurationUnit::kTurns, 5},
                               DurationSpec{DurationUnit::kMs, 200}}))
                .has_value());

    CHECK(h.scheduler.Tick(h.store, h.match, 1100).empty());
    const std::vector<json> elapsed =
        h.scheduler.Tick(h.store, h.match, 1200);
    REQUIRE(elapsed.size() == 1);
    CHECK(elapsed[0] == graph);
    CHECK(h.scheduler.Pending(h.store, h.match) == 0);
}

TEST_CASE("scheduler: event-driven legs alone do not expire via Tick") {
    Harness h;
    REQUIRE(h.scheduler
                .Arm(h.store, h.match, Graph("turns"),
                     DurationSpec{DurationUnit::kTurns, 3})
                .has_value());
    CHECK(h.scheduler.Tick(h.store, h.match, 999999).empty());
    CHECK(h.scheduler.Pending(h.store, h.match) == 1);
}

TEST_CASE("scheduler: independent schedules expire in arm order") {
    Harness h;
    const json first_graph = Graph("first");
    const json second_graph = Graph("second");
    REQUIRE(h.scheduler
                .Arm(h.store, h.match, first_graph,
                     DurationSpec{DurationUnit::kMs, 100})
                .has_value());
    REQUIRE(h.scheduler
                .Arm(h.store, h.match, second_graph,
                     DurationSpec{DurationUnit::kMs, 200})
                .has_value());

    const std::vector<json> first =
        h.scheduler.Tick(h.store, h.match, 1100);
    REQUIRE(first.size() == 1);
    CHECK(first[0] == first_graph);

    const std::vector<json> second =
        h.scheduler.Tick(h.store, h.match, 1200);
    REQUIRE(second.size() == 1);
    CHECK(second[0] == second_graph);
    CHECK(h.scheduler.Pending(h.store, h.match) == 0);
}

TEST_CASE("scheduler: started_ms is taken per entry from the clock") {
    Harness h;
    const json early = Graph("early");
    const json late = Graph("late");
    REQUIRE(h.scheduler
                .Arm(h.store, h.match, early,
                     DurationSpec{DurationUnit::kMs, 100})
                .has_value());

    h.now = 1500;
    REQUIRE(h.scheduler
                .Arm(h.store, h.match, late,
                     DurationSpec{DurationUnit::kMs, 100})
                .has_value());

    const std::vector<json> first =
        h.scheduler.Tick(h.store, h.match, 1550);
    REQUIRE(first.size() == 1);
    CHECK(first[0] == early);

    const std::vector<json> second =
        h.scheduler.Tick(h.store, h.match, 1650);
    REQUIRE(second.size() == 1);
    CHECK(second[0] == late);
}

TEST_CASE("scheduler: invalid durations are rejected") {
    Harness h;
    CHECK(!h.scheduler
               .Arm(h.store, h.match, Graph("neg"),
                    DurationSpec{DurationUnit::kMs, -1})
               .has_value());
    CHECK(!h.scheduler.Arm(h.store, h.match, Graph("empty"), Duration{})
               .has_value());
    CHECK(h.scheduler.Pending(h.store, h.match) == 0);
}
