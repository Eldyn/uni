#include <doctest/doctest.h>

#include <match/status.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using match::ecs::DurationSpec;
using match::ecs::DurationUnit;
using match::ecs::Entity;
using match::ecs::EntityStore;
using match::ecs::StackPolicy;
using match::ecs::Status;
using match::ecs::StatusList;
using match::status::ApplyRequest;
using match::status::ApplyResult;
using match::status::ModifyResult;

namespace {

ApplyRequest Request(const std::string& id, int32_t magnitude = 1) {
    ApplyRequest request;
    request.status_id = id;
    request.magnitude = magnitude;
    return request;
}

DurationSpec Duration(DurationUnit unit, int64_t value) {
    DurationSpec spec;
    spec.unit = unit;
    spec.value = value;
    return spec;
}

/** INFO: a fully-independent apply request with its own duration. */
ApplyRequest Independent(const std::string& id, int32_t magnitude,
                         DurationUnit unit, int64_t value) {
    ApplyRequest request = Request(id, magnitude);
    request.has_stack_policy = true;
    request.stack_policy = StackPolicy::kIndependent;
    request.duration = Duration(unit, value);
    return request;
}

}  // namespace

TEST_CASE("status: apply creates an instance and mints id 1") {
    EntityStore store;
    Entity target = store.Create();

    ApplyRequest request = Request("vanilla:draw_debt", 2);
    request.duration = Duration(DurationUnit::kTurns, 3);
    const ApplyResult result = match::status::Apply(store, target, request);

    CHECK(result.applied);
    CHECK(result.created);
    CHECK(result.instance_id == 1);
    CHECK(result.magnitude == 2);
    CHECK(result.duration.unit == DurationUnit::kTurns);
    CHECK(result.duration.value == 3);
    CHECK(result.status_id == "vanilla:draw_debt");

    const std::vector<Status> list = match::status::List(store, target);
    REQUIRE(list.size() == 1);
    CHECK(list[0].status_id == "vanilla:draw_debt");
    CHECK(list[0].instance_id == 1);
    CHECK(match::status::Has(store, target, "vanilla:draw_debt"));
}

TEST_CASE("status: replace re-apply resets magnitude and duration") {
    EntityStore store;
    Entity target = store.Create();

    ApplyRequest first = Request("vanilla:draw_debt", 5);
    first.has_stack_policy = true;
    first.stack_policy = StackPolicy::kReplace;
    first.duration = Duration(DurationUnit::kTurns, 3);
    match::status::Apply(store, target, first);

    ApplyRequest second = Request("vanilla:draw_debt", 2);
    second.has_stack_policy = true;
    second.stack_policy = StackPolicy::kReplace;
    second.duration = Duration(DurationUnit::kRounds, 7);
    const ApplyResult result = match::status::Apply(store, target, second);

    CHECK(result.applied);
    CHECK_FALSE(result.created);
    CHECK(result.instance_id == 1);
    CHECK(result.magnitude == 2);
    CHECK(result.duration.unit == DurationUnit::kRounds);
    CHECK(result.duration.value == 7);
    CHECK(match::status::List(store, target).size() == 1);
}

TEST_CASE("status: accumulate adds magnitudes and keeps the duration") {
    EntityStore store;
    Entity target = store.Create();

    ApplyRequest first = Request("vanilla:draw_debt", 2);
    first.has_stack_policy = true;
    first.stack_policy = StackPolicy::kAccumulate;
    first.duration = Duration(DurationUnit::kTurns, 3);
    match::status::Apply(store, target, first);

    ApplyRequest second = Request("vanilla:draw_debt", 4);
    second.has_stack_policy = true;
    second.stack_policy = StackPolicy::kAccumulate;
    const ApplyResult result = match::status::Apply(store, target, second);

    CHECK(result.magnitude == 6);
    CHECK(result.instance_id == 1);
    CHECK(result.duration.unit == DurationUnit::kTurns);
    CHECK(result.duration.value == 3);
    CHECK(match::status::List(store, target).size() == 1);
}

TEST_CASE("status: independent keeps separate instances with own durations") {
    EntityStore store;
    Entity target = store.Create();

    const ApplyResult first = match::status::Apply(
        store, target,
        Independent("space:shielded", 2, DurationUnit::kTurns, 2));
    CHECK(first.instance_id == 1);
    CHECK(first.created);

    const ApplyResult second = match::status::Apply(
        store, target,
        Independent("space:shielded", 3, DurationUnit::kRounds, 5));
    CHECK(second.instance_id == 2);
    CHECK(second.created);

    const std::vector<Status> list = match::status::List(store, target);
    REQUIRE(list.size() == 2);
    CHECK(list[0].instance_id == 1);
    CHECK(list[0].magnitude == 2);
    CHECK(list[0].duration.unit == DurationUnit::kTurns);
    CHECK(list[0].duration.value == 2);
    CHECK(list[1].instance_id == 2);
    CHECK(list[1].magnitude == 3);
    CHECK(list[1].duration.unit == DurationUnit::kRounds);
    CHECK(list[1].duration.value == 5);

    CHECK(match::status::FindByInstance(store, target, 2)->magnitude == 3);
}

TEST_CASE("status: cap accumulates up to the ceiling") {
    EntityStore store;
    Entity target = store.Create();

    ApplyRequest first = Request("vanilla:draw_debt", 3);
    first.has_stack_policy = true;
    first.stack_policy = StackPolicy::kCap;
    first.cap = 5;
    match::status::Apply(store, target, first);

    ApplyRequest second = Request("vanilla:draw_debt", 4);
    second.has_stack_policy = true;
    second.stack_policy = StackPolicy::kCap;
    second.cap = 5;
    CHECK(match::status::Apply(store, target, second).magnitude == 5);

    ApplyRequest third = Request("vanilla:draw_debt", 1);
    third.has_stack_policy = true;
    third.stack_policy = StackPolicy::kCap;
    third.cap = 5;
    CHECK(match::status::Apply(store, target, third).magnitude == 5);
    CHECK(match::status::List(store, target).size() == 1);
}

TEST_CASE("status: distinct kinds coexist in the list") {
    EntityStore store;
    Entity target = store.Create();

    match::status::Apply(store, target, Request("vanilla:draw_debt", 2));
    match::status::Apply(store, target, Request("space:shielded", 4));

    const std::vector<Status> list = match::status::List(store, target);
    REQUIRE(list.size() == 2);
    const Status* debt =
        match::status::Find(store, target, "vanilla:draw_debt");
    REQUIRE(debt != nullptr);
    CHECK(debt->magnitude == 2);
    CHECK(debt->instance_id == 1);
    const Status* shield = match::status::Find(store, target, "space:shielded");
    REQUIRE(shield != nullptr);
    CHECK(shield->magnitude == 4);
    CHECK(shield->instance_id == 2);
}

TEST_CASE("status: omitted policy falls back to the stored policy") {
    EntityStore store;
    Entity target = store.Create();

    ApplyRequest seeded = Request("vanilla:draw_debt", 2);
    seeded.has_stack_policy = true;
    seeded.stack_policy = StackPolicy::kAccumulate;
    match::status::Apply(store, target, seeded);

    // INFO: has_stack_policy defaults false, so the stored policy applies.
    const ApplyResult result =
        match::status::Apply(store, target, Request("vanilla:draw_debt", 3));
    CHECK(result.magnitude == 5);
    CHECK(result.stack_policy == StackPolicy::kAccumulate);
}

TEST_CASE("status: omitted policy reuses the stored cap ceiling") {
    EntityStore store;
    Entity target = store.Create();

    ApplyRequest seeded = Request("vanilla:draw_debt", 3);
    seeded.has_stack_policy = true;
    seeded.stack_policy = StackPolicy::kCap;
    seeded.cap = 5;
    match::status::Apply(store, target, seeded);

    const ApplyResult result =
        match::status::Apply(store, target, Request("vanilla:draw_debt", 4));
    CHECK(result.magnitude == 5);
    CHECK(result.cap == 5);
}

TEST_CASE("status: hidden flag is stored and reported") {
    EntityStore store;
    Entity target = store.Create();

    ApplyRequest hidden = Request("space:shielded", 1);
    hidden.hidden = true;
    match::status::Apply(store, target, hidden);
    CHECK(match::status::IsHidden(store, target, "space:shielded"));

    match::status::Apply(store, target, Request("vanilla:draw_debt", 1));
    CHECK_FALSE(match::status::IsHidden(store, target, "vanilla:draw_debt"));
    CHECK_FALSE(match::status::IsHidden(store, target, "space:missing"));
}

TEST_CASE("status: mint instance id is max plus one and saturates") {
    StatusList empty;
    CHECK(match::status::MintInstanceId(empty) == 1);

    empty.instances.push_back(Status{});
    empty.instances[0].instance_id = 4;
    CHECK(match::status::MintInstanceId(empty) == 5);

    empty.instances[0].instance_id = std::numeric_limits<uint32_t>::max();
    CHECK(match::status::MintInstanceId(empty)
          == std::numeric_limits<uint32_t>::max());
}

TEST_CASE("status: remove by kind removes every matching instance") {
    EntityStore store;
    Entity target = store.Create();

    match::status::Apply(
        store, target,
        Independent("space:shielded", 2, DurationUnit::kTurns, 1));
    match::status::Apply(
        store, target,
        Independent("space:shielded", 3, DurationUnit::kTurns, 2));
    match::status::Apply(store, target, Request("vanilla:draw_debt", 1));

    const std::vector<Status> removed =
        match::status::Remove(store, target, "space:shielded");
    REQUIRE(removed.size() == 2);
    CHECK(removed[0].instance_id == 1);
    CHECK(removed[1].instance_id == 2);
    CHECK(match::status::List(store, target).size() == 1);
    CHECK_FALSE(match::status::Has(store, target, "space:shielded"));
    CHECK(match::status::Remove(store, target, "space:shielded").empty());
}

TEST_CASE("status: remove by instance removes exactly one") {
    EntityStore store;
    Entity target = store.Create();

    match::status::Apply(
        store, target,
        Independent("space:shielded", 2, DurationUnit::kTurns, 1));
    match::status::Apply(
        store, target,
        Independent("space:shielded", 3, DurationUnit::kTurns, 2));

    const std::optional<Status> removed =
        match::status::RemoveByInstance(store, target, 2);
    REQUIRE(removed.has_value());
    CHECK(removed->instance_id == 2);
    CHECK(removed->magnitude == 3);
    CHECK(match::status::List(store, target).size() == 1);
    CHECK_FALSE(match::status::RemoveByInstance(store, target, 99)
                    .has_value());
}

TEST_CASE("status: dead and missing entities are structured misses") {
    EntityStore store;
    Entity dead = store.Create();
    store.Destroy(dead);

    CHECK(match::status::List(store, dead).empty());
    CHECK(match::status::Find(store, dead, "space:shielded") == nullptr);
    CHECK_FALSE(match::status::Has(store, dead, "space:shielded"));
    CHECK_FALSE(match::status::IsHidden(store, dead, "space:shielded"));
    CHECK_FALSE(
        match::status::Apply(store, dead, Request("space:shielded")).applied);
    CHECK(match::status::Remove(store, dead, "space:shielded").empty());
    CHECK_FALSE(match::status::RemoveByInstance(store, dead, 1).has_value());
    CHECK_FALSE(
        match::status::Modify(store, dead, "space:shielded", 1).has_value());

    Entity live = store.Create();
    CHECK(match::status::List(store, live).empty());
    CHECK(match::status::Find(store, live, "space:shielded") == nullptr);
    CHECK_FALSE(match::status::Apply(store, live, Request("", 1)).applied);
    CHECK(match::status::Remove(store, live, "").empty());
    CHECK_FALSE(match::status::Modify(store, live, "", 1).has_value());
}

TEST_CASE("status: modify adjusts and clamps to the cap ceiling") {
    EntityStore store;
    Entity target = store.Create();

    ApplyRequest seeded = Request("vanilla:draw_debt", 3);
    seeded.has_stack_policy = true;
    seeded.stack_policy = StackPolicy::kCap;
    seeded.cap = 5;
    match::status::Apply(store, target, seeded);

    const std::optional<ModifyResult> up =
        match::status::Modify(store, target, "vanilla:draw_debt", 10);
    REQUIRE(up.has_value());
    REQUIRE(up->adjusted.size() == 1);
    CHECK(up->adjusted[0].magnitude == 5);
}

TEST_CASE("status: modify reaches every independent instance") {
    EntityStore store;
    Entity target = store.Create();

    match::status::Apply(
        store, target,
        Independent("space:shielded", 2, DurationUnit::kTurns, 1));
    match::status::Apply(
        store, target,
        Independent("space:shielded", 3, DurationUnit::kTurns, 2));

    const std::optional<ModifyResult> result =
        match::status::Modify(store, target, "space:shielded", 1);
    REQUIRE(result.has_value());
    REQUIRE(result->adjusted.size() == 2);
    CHECK(result->adjusted[0].magnitude == 3);
    CHECK(result->adjusted[1].magnitude == 4);
}

TEST_CASE("status: modify a missing kind is nullopt") {
    EntityStore store;
    Entity target = store.Create();

    CHECK_FALSE(
        match::status::Modify(store, target, "space:shielded", 1).has_value());
}
