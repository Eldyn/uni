#include <doctest/doctest.h>

#include <match/status.hpp>
#include <match/status_system.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using match::ecs::DurationSpec;
using match::ecs::DurationUnit;
using match::ecs::Entity;
using match::ecs::EntityStore;
using match::ecs::StackPolicy;
using match::ecs::Status;
using match::status::ApplyRequest;
using match::status::StatusDefView;
using match::status::StatusExpiry;
using match::status::StatusSystem;

namespace {

ApplyRequest Request(const std::string& id, DurationUnit unit, int64_t value) {
    ApplyRequest request;
    request.status_id = id;
    request.duration.unit = unit;
    request.duration.value = value;
    return request;
}

ApplyRequest Independent(const std::string& id, DurationUnit unit,
                         int64_t value) {
    ApplyRequest request = Request(id, unit, value);
    request.has_stack_policy = true;
    request.stack_policy = StackPolicy::kIndependent;
    return request;
}

DurationSpec Leg(DurationUnit unit, int64_t value) {
    DurationSpec spec;
    spec.unit = unit;
    spec.value = value;
    return spec;
}

}  // namespace

TEST_CASE("status_expiry: ms leg expires in Tick once the clock passes") {
    EntityStore store;
    Entity owner = store.Create();
    int64_t now = 1000;
    StatusSystem system([&]() { return now; });

    match::status::Apply(store, owner, Request("space:shielded",
                                               DurationUnit::kMs, 1000));
    CHECK(system.Tick(store, 1000).empty());  // arm at 1000
    CHECK(system.Tick(store, 1500).empty());

    const std::vector<StatusExpiry> expired = system.Tick(store, 2000);
    REQUIRE(expired.size() == 1);
    CHECK(expired[0].owner == owner);
    CHECK(expired[0].instance.status_id == "space:shielded");
    CHECK(expired[0].event["type"] == "status_removed");
    CHECK(expired[0].event["payload"]["status_kind"] == "space:shielded");
    CHECK(expired[0].event["payload"]["instance"] == 1);
    CHECK_FALSE(match::status::Has(store, owner, "space:shielded"));
    CHECK(system.Tracked() == 0);
}

TEST_CASE("status_expiry: turns leg advances only on the owner's turn_end") {
    EntityStore store;
    Entity owner = store.Create();
    Entity other = store.Create();
    int64_t now = 5000;
    StatusSystem system([&]() { return now; });

    match::status::Apply(store, owner, Request("space:shielded",
                                               DurationUnit::kTurns, 1));
    CHECK(system.Tick(store, 5000).empty());  // arm
    CHECK(system.OnTurnEnd(store, other).empty());

    const std::vector<StatusExpiry> expired = system.OnTurnEnd(store, owner);
    REQUIRE(expired.size() == 1);
    CHECK(expired[0].owner == owner);
    CHECK_FALSE(match::status::Has(store, owner, "space:shielded"));
}

TEST_CASE("status_expiry: rounds leg expires on round_end") {
    EntityStore store;
    Entity owner = store.Create();
    int64_t now = 0;
    StatusSystem system([&]() { return now; });

    match::status::Apply(store, owner, Request("space:shielded",
                                               DurationUnit::kRounds, 2));
    CHECK(system.Tick(store, 0).empty());
    CHECK(system.OnRoundEnd(store).empty());

    const std::vector<StatusExpiry> expired = system.OnRoundEnd(store);
    REQUIRE(expired.size() == 1);
    CHECK_FALSE(match::status::Has(store, owner, "space:shielded"));
}

TEST_CASE("status_expiry: cards_played leg expires on after:play") {
    EntityStore store;
    Entity owner = store.Create();
    int64_t now = 0;
    StatusSystem system([&]() { return now; });

    match::status::Apply(store, owner,
                         Request("space:shielded",
                                 DurationUnit::kCardsPlayed, 2));
    CHECK(system.Tick(store, 0).empty());
    CHECK(system.OnCardPlayed(store).empty());

    const std::vector<StatusExpiry> expired = system.OnCardPlayed(store);
    REQUIRE(expired.size() == 1);
    CHECK_FALSE(match::status::Has(store, owner, "space:shielded"));
}

TEST_CASE("status_expiry: compound duration expires on the first elapsed leg") {
    EntityStore store;
    Entity owner = store.Create();
    int64_t now = 0;
    StatusSystem system([&]() { return now; });

    system.SetDefLookup([](std::string_view id)
                            -> std::optional<StatusDefView> {
        if (id != "space:burn" && id != "space:freeze") return std::nullopt;
        StatusDefView def;
        if (id == "space:burn") {
            def.duration.legs.push_back(Leg(DurationUnit::kTurns, 5));
            def.duration.legs.push_back(Leg(DurationUnit::kMs, 100));
        } else {
            def.duration.legs.push_back(Leg(DurationUnit::kTurns, 1));
            def.duration.legs.push_back(Leg(DurationUnit::kMs, 100000));
        }
        return def;
    });

    // INFO: ms leg wins over the turns leg.
    match::status::Apply(store, owner,
                         Request("space:burn", DurationUnit::kTurns, 5));
    CHECK(system.Tick(store, 0).empty());
    CHECK(system.Tick(store, 50).empty());
    const std::vector<StatusExpiry> by_ms = system.Tick(store, 100);
    REQUIRE(by_ms.size() == 1);
    CHECK_FALSE(match::status::Has(store, owner, "space:burn"));

    // INFO: turns leg wins over the (far) ms leg.
    now = 100;
    match::status::Apply(store, owner,
                         Request("space:freeze", DurationUnit::kTurns, 1));
    CHECK(system.Tick(store, 100).empty());
    const std::vector<StatusExpiry> by_turns =
        system.OnTurnEnd(store, owner);
    REQUIRE(by_turns.size() == 1);
    CHECK_FALSE(match::status::Has(store, owner, "space:freeze"));
}

TEST_CASE("status_expiry: on_expire runs through the injected handle") {
    EntityStore store;
    Entity owner = store.Create();
    int64_t now = 0;
    StatusSystem system([&]() { return now; });

    int calls = 0;
    Status captured;
    nlohmann::json captured_graph;
    system.SetExpireHandler([&](EntityStore&, Entity called_owner,
                                const Status& instance,
                                const nlohmann::json& graph) {
        ++calls;
        captured = instance;
        captured_graph = graph;
        CHECK(called_owner == owner);
    });
    system.SetDefLookup([](std::string_view id)
                            -> std::optional<StatusDefView> {
        if (id != "space:birth") return std::nullopt;
        StatusDefView def;
        def.duration.legs.push_back(Leg(DurationUnit::kTurns, 1));
        def.on_expire = nlohmann::json{{"nodes", {"run_expiry"}}};
        return def;
    });

    match::status::Apply(store, owner,
                         Request("space:birth", DurationUnit::kTurns, 1));
    CHECK(system.Tick(store, 0).empty());
    const std::vector<StatusExpiry> expired =
        system.OnTurnEnd(store, owner);
    REQUIRE(expired.size() == 1);
    CHECK(calls == 1);
    CHECK(captured.status_id == "space:birth");
    CHECK(captured_graph["nodes"][0] == "run_expiry");
}

TEST_CASE("status_expiry: no def source still removes and emits") {
    EntityStore store;
    Entity owner = store.Create();
    int64_t now = 0;
    StatusSystem system([&]() { return now; });

    match::status::Apply(store, owner,
                         Request("space:orphan", DurationUnit::kTurns, 1));
    CHECK(system.Tick(store, 0).empty());
    const std::vector<StatusExpiry> expired =
        system.OnTurnEnd(store, owner);
    REQUIRE(expired.size() == 1);
    CHECK(expired[0].hidden == false);
    CHECK(expired[0].event["payload"]["status_kind"] == "space:orphan");
}

TEST_CASE("status_expiry: hidden status is preserved on expiry") {
    EntityStore store;
    Entity owner = store.Create();
    int64_t now = 0;
    StatusSystem system([&]() { return now; });

    ApplyRequest hidden = Request("space:spy", DurationUnit::kTurns, 1);
    hidden.hidden = true;
    match::status::Apply(store, owner, hidden);
    CHECK(system.Tick(store, 0).empty());
    std::vector<StatusExpiry> expired = system.OnTurnEnd(store, owner);
    REQUIRE(expired.size() == 1);
    CHECK(expired[0].hidden);
    CHECK(expired[0].event["payload"]["hidden"] == true);

    // INFO: a def-level hidden flag is honoured even when the instance
    //  says false.
    system.SetDefLookup([](std::string_view id)
                            -> std::optional<StatusDefView> {
        if (id != "space:shadow") return std::nullopt;
        StatusDefView def;
        def.hidden = true;
        def.duration.legs.push_back(Leg(DurationUnit::kTurns, 1));
        return def;
    });
    match::status::Apply(store, owner,
                         Request("space:shadow", DurationUnit::kTurns, 1));
    CHECK(system.Tick(store, 0).empty());
    expired = system.OnTurnEnd(store, owner);
    REQUIRE(expired.size() == 1);
    CHECK(expired[0].hidden);
    CHECK(expired[0].event["payload"]["hidden"] == true);
}

TEST_CASE("status_expiry: a duration-less status never expires") {
    EntityStore store;
    Entity owner = store.Create();
    int64_t now = 0;
    StatusSystem system([&]() { return now; });

    match::status::Apply(store, owner,
                         Request("space:enduring", DurationUnit::kTurns, 0));
    CHECK(system.Tick(store, 0).empty());
    CHECK(system.Tick(store, 100000).empty());
    CHECK(system.OnTurnEnd(store, owner).empty());
    CHECK(system.OnRoundEnd(store).empty());
    CHECK(system.OnCardPlayed(store).empty());
    CHECK(match::status::Has(store, owner, "space:enduring"));
}

TEST_CASE("status_expiry: independent instances expire on their own legs") {
    EntityStore store;
    Entity owner = store.Create();
    int64_t now = 0;
    StatusSystem system([&]() { return now; });

    match::status::Apply(store, owner,
                         Independent("space:shielded", DurationUnit::kMs, 100));
    match::status::Apply(
        store, owner,
        Independent("space:shielded", DurationUnit::kMs, 500));

    CHECK(system.Tick(store, 0).empty());
    const std::vector<StatusExpiry> first = system.Tick(store, 100);
    REQUIRE(first.size() == 1);
    CHECK(first[0].instance.instance_id == 1);
    CHECK(match::status::Has(store, owner, "space:shielded"));

    const std::vector<StatusExpiry> second = system.Tick(store, 500);
    REQUIRE(second.size() == 1);
    CHECK(second[0].instance.instance_id == 2);
    CHECK_FALSE(match::status::Has(store, owner, "space:shielded"));
}
