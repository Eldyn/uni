#include <doctest/doctest.h>

#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using match::ecs::BudgetLedger;
using match::ecs::Entity;
using match::ecs::EntityStore;
using match::ecs::EventBus;
using match::ecs::MatchMeta;
using match::ecs::RngState;
using match::ops::LastRollTotal;
using match::ops::MakeDefaultRuntime;
using match::ops::OpArgs;
using match::ops::OpContext;
using match::ops::OpResult;
using match::ops::OpRuntime;
using match::ops::OpStatus;
using match::ops::ResolutionFrame;

using nlohmann::json;

namespace {

/** INFO: one store/bus/runtime fixture per case. */
struct Harness {
    EntityStore store;
    BudgetLedger ledger;
    EventBus bus = EventBus(std::vector<std::string>{"m"});
    ResolutionFrame frame;
    OpRuntime runtime = MakeDefaultRuntime();

    OpResult Invoke(
        const std::string& op, json raw,
        std::initializer_list<std::pair<std::string, std::vector<Entity>>>
            bindings = {}) {
        OpArgs args(op, std::move(raw));
        for (const auto& binding : bindings) {
            args.BindSelector(binding.first, binding.second);
        }
        OpContext ctx(bus, ledger, frame);
        return runtime.Invoke(op, store, args, ctx);
    }
};

Entity AddMatch(EntityStore& store, uint64_t seed, uint64_t counter = 0) {
    Entity entity = store.Create();
    store.Add(entity, MatchMeta{});
    RngState rng;
    rng.seed = seed;
    rng.op_counter = counter;
    store.Add(entity, rng);
    return entity;
}

uint64_t Counter(EntityStore& store, Entity match) {
    const RngState* rng = store.Get<RngState>(match);
    REQUIRE(rng != nullptr);
    return rng->op_counter;
}

const json* FindEvent(const OpResult& result, const std::string& type) {
    for (const json& event : result.events) {
        if (event.value("type", std::string()) == type) return &event;
    }
    return nullptr;
}

json RollSpec(int64_t sides, int64_t count) {
    return json{{"sides", sides}, {"count", count}};
}

}  // namespace

TEST_CASE("random_ops: same seed and counter replay identically") {
    Harness first;
    Harness second;
    AddMatch(first.store, 0x5EED1234ULL);
    AddMatch(second.store, 0x5EED1234ULL);

    const OpResult a = first.Invoke("roll", json{{"spec", RollSpec(6, 4)}});
    const OpResult b = second.Invoke("roll", json{{"spec", RollSpec(6, 4)}});

    CHECK(a.status == OpStatus::kResolved);
    CHECK(a.value == b.value);
    REQUIRE(a.value.is_array());
    CHECK(a.value.size() == 4);
    REQUIRE(LastRollTotal(first.frame).has_value());
    CHECK(*LastRollTotal(first.frame) == *LastRollTotal(second.frame));
}

TEST_CASE("random_ops: counter increments and seeds the stream") {
    Harness h;
    Entity match = AddMatch(h.store, 777);
    CHECK(Counter(h.store, match) == 0);

    const OpResult first =
        h.Invoke("roll", json{{"spec", RollSpec(6, 2)}});
    CHECK(Counter(h.store, match) == 1);
    const OpResult second =
        h.Invoke("roll", json{{"spec", RollSpec(6, 2)}});
    CHECK(Counter(h.store, match) == 2);

    // INFO: a fresh store pre-set to counter 1 must reproduce the second roll,
    //       proving the stream is a pure function of (seed, counter).
    Harness replay;
    Entity replay_match = AddMatch(replay.store, 777, 1);
    const OpResult replayed =
        replay.Invoke("roll", json{{"spec", RollSpec(6, 2)}});
    CHECK(second.value == replayed.value);
    CHECK(first.value != second.value);
    CHECK(Counter(replay.store, replay_match) == 2);
}

TEST_CASE("random_ops: spec shapes drive outcomes and total") {
    Harness h;
    Entity match = AddMatch(h.store, 99);

    const OpResult keep_all =
        h.Invoke("roll", json{{"spec", RollSpec(20, 5)}});
    REQUIRE(keep_all.value.is_array());
    CHECK(keep_all.value.size() == 5);
    int64_t sum = 0;
    for (const json& outcome : keep_all.value) {
        REQUIRE(outcome.is_number_integer());
        const int64_t value = outcome.get<int64_t>();
        CHECK(value >= 1);
        CHECK(value <= 20);
        sum += value;
    }
    REQUIRE(LastRollTotal(h.frame).has_value());
    CHECK(*LastRollTotal(h.frame) == sum);

    const OpResult keep_one =
        h.Invoke("roll", json{{"spec", {{"sides", 20},
                                        {"count", 5},
                                        {"keep", 1}}}});
    REQUIRE(keep_one.value.is_array());
    int64_t max = 0;
    for (const json& outcome : keep_one.value) {
        max = std::max(max, outcome.get<int64_t>());
    }
    REQUIRE(LastRollTotal(h.frame).has_value());
    CHECK(*LastRollTotal(h.frame) == max);
    CHECK(Counter(h.store, match) == 2);
}

TEST_CASE("random_ops: a one-sided die is deterministic") {
    Harness h;
    AddMatch(h.store, 3);

    const OpResult result =
        h.Invoke("roll", json{{"spec", RollSpec(1, 7)}});

    REQUIRE(result.value.is_array());
    CHECK(result.value.size() == 7);
    for (const json& outcome : result.value) CHECK(outcome == 1);
    REQUIRE(LastRollTotal(h.frame).has_value());
    CHECK(*LastRollTotal(h.frame) == 7);
}

TEST_CASE("random_ops: emits roll_result with normalized spec and counter") {
    Harness h;
    AddMatch(h.store, 42);

    const OpResult result =
        h.Invoke("roll", json{{"spec", RollSpec(6, 3)}});

    const json* event = FindEvent(result, "roll_result");
    REQUIRE(event != nullptr);
    const json& payload = (*event)["payload"];
    CHECK(payload["spec"] == json{{"sides", 6}, {"count", 3}, {"keep", 3}});
    CHECK(payload["outcomes"] == result.value);
    CHECK(payload["counter"] == 1);
}

TEST_CASE("random_ops: no match or no rng_state is a fail-safe no-op") {
    Harness no_match;
    const OpResult missing =
        no_match.Invoke("roll", json{{"spec", RollSpec(6, 1)}});
    CHECK(missing.status == OpStatus::kResolved);
    CHECK(missing.value.is_null());
    CHECK(missing.events.empty());

    Harness no_rng;
    Entity match = no_rng.store.Create();
    no_rng.store.Add(match, MatchMeta{});
    const OpResult idle =
        no_rng.Invoke("roll", json{{"spec", RollSpec(6, 1)}});
    CHECK(idle.status == OpStatus::kResolved);
    CHECK(idle.value.is_null());
    CHECK(idle.events.empty());
}

TEST_CASE("random_ops: a bad spec is a no-op and leaves the counter") {
    Harness h;
    Entity match = AddMatch(h.store, 11);

    const std::vector<json> bad_specs = {
        json{{"sides", 0}, {"count", 1}},
        json{{"sides", 1000001}, {"count", 1}},
        json{{"sides", 6}, {"count", 0}},
        json{{"sides", 6}, {"count", 1001}},
        json{{"sides", 6}, {"count", 2}, {"keep", 0}},
        json{{"sides", 6}, {"count", 2}, {"keep", 3}},
        json{{"count", 2}},
        json{{"sides", 6}},
        json{{"sides", "six"}, {"count", 1}},
        json{{"sides", 6}, {"count", 2}, {"keep", 1.5}}};

    for (const json& spec : bad_specs) {
        const OpResult result = h.Invoke("roll", json{{"spec", spec}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.value.is_null());
        CHECK(result.events.empty());
        CHECK(Counter(h.store, match) == 0);
    }

    const OpResult non_object =
        h.Invoke("roll", json{{"spec", 5}});
    CHECK(non_object.status == OpStatus::kResolved);
    CHECK(non_object.value.is_null());
    CHECK(Counter(h.store, match) == 0);

    const OpResult missing =
        h.Invoke("roll", json::object());
    CHECK(missing.status == OpStatus::kResolved);
    CHECK(missing.value.is_null());
    CHECK(Counter(h.store, match) == 0);
}

TEST_CASE("random_ops: boundary spec values are accepted") {
    Harness h;
    AddMatch(h.store, 5);

    const OpResult result =
        h.Invoke("roll", json{{"spec", RollSpec(1000000, 1000)}});
    CHECK(result.status == OpStatus::kResolved);
    REQUIRE(result.value.is_array());
    CHECK(result.value.size() == 1000);
    for (const json& outcome : result.value) {
        CHECK(outcome.get<int64_t>() >= 1);
        CHECK(outcome.get<int64_t>() <= 1000000);
    }
}
