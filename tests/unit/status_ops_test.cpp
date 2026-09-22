#include <doctest/doctest.h>

#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>
#include <match/status.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using match::ecs::BudgetLedger;
using match::ecs::DurationUnit;
using match::ecs::Entity;
using match::ecs::EntityStore;
using match::ecs::EventBus;
using match::ecs::PlayerInfo;
using match::ecs::StackPolicy;
using match::ecs::Status;
using match::ops::MakeDefaultRuntime;
using match::ops::OpArgs;
using match::ops::OpContext;
using match::ops::OpResult;
using match::ops::OpRuntime;
using match::ops::OpStatus;
using match::ops::ResolutionFrame;

using nlohmann::json;

namespace {

Entity AddPlayer(EntityStore& store, const std::string& name, uint32_t seat) {
    Entity entity = store.Create();
    PlayerInfo info;
    info.username = name;
    info.seat = seat;
    store.Add(entity, info);
    return entity;
}

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

const json* FindEvent(const OpResult& result, const std::string& type) {
    for (const json& event : result.events) {
        if (event.value("type", std::string()) == type) return &event;
    }
    return nullptr;
}

const Status* FindStatus(EntityStore& store, Entity entity,
                         const std::string& kind) {
    return match::status::Find(store, entity, kind);
}

std::size_t StatusCount(EntityStore& store, Entity entity) {
    return match::status::List(store, entity).size();
}

}  // namespace

TEST_CASE("status_ops: apply_status stores a replace instance and emits") {
    Harness h;
    Entity target = AddPlayer(h.store, "p0", 0);

    const OpResult result = h.Invoke(
        "apply_status",
        json{{"status_kind", "vanilla:draw_debt"},
             {"params", {{"magnitude", 2}}},
             {"duration", {{"unit", "turns"}, {"value", 3}}}},
        {{"target", {target}}});

    CHECK(result.status == OpStatus::kResolved);
    const Status* status = FindStatus(h.store, target, "vanilla:draw_debt");
    REQUIRE(status != nullptr);
    CHECK(status->status_id == "vanilla:draw_debt");
    CHECK(status->magnitude == 2);
    CHECK(status->stack_policy == StackPolicy::kReplace);
    CHECK(status->duration.unit == DurationUnit::kTurns);
    CHECK(status->duration.value == 3);
    CHECK(status->instance_id == 1);
    CHECK(StatusCount(h.store, target) == 1);

    const json* event = FindEvent(result, "status_applied");
    REQUIRE(event != nullptr);
    CHECK((*event)["payload"]["status_kind"] == "vanilla:draw_debt");
    CHECK((*event)["payload"]["magnitude"] == 2);
    CHECK((*event)["payload"]["duration_unit"] == "turns");
    CHECK((*event)["payload"]["instance"] == 1);
    CHECK((*event)["payload"]["target"]["index"] == target.index);
}

TEST_CASE("status_ops: apply_status defaults magnitude 1 and policy replace") {
    Harness h;
    Entity target = AddPlayer(h.store, "p0", 0);

    const OpResult result = h.Invoke(
        "apply_status", json{{"status_kind", "space:shielded"}},
        {{"target", {target}}});

    CHECK(result.status == OpStatus::kResolved);
    const Status* status = FindStatus(h.store, target, "space:shielded");
    REQUIRE(status != nullptr);
    CHECK(status->magnitude == 1);
    CHECK(status->stack_policy == StackPolicy::kReplace);
    CHECK(status->duration.unit == DurationUnit::kTurns);
    CHECK(status->duration.value == 0);
    CHECK(status->instance_id == 1);
}

TEST_CASE("status_ops: apply_status stack_policy arg overrides existing") {
    Harness h;
    Entity target = AddPlayer(h.store, "p0", 0);

    h.Invoke("apply_status",
             json{{"status_kind", "vanilla:draw_debt"},
                  {"stack_policy", "accumulate"},
                  {"params", {{"magnitude", 2}}}},
             {{"target", {target}}});

    // INFO: an omitted arg falls back to the existing instance's policy, so
    //       magnitudes still add.
    const OpResult fallback = h.Invoke(
        "apply_status",
        json{{"status_kind", "vanilla:draw_debt"},
             {"params", {{"magnitude", 3}}}},
        {{"target", {target}}});
    CHECK(fallback.status == OpStatus::kResolved);
    const Status* accumulated =
        FindStatus(h.store, target, "vanilla:draw_debt");
    REQUIRE(accumulated != nullptr);
    CHECK(accumulated->magnitude == 5);
    CHECK(accumulated->stack_policy == StackPolicy::kAccumulate);

    // INFO: a present arg is authoritative and overrides the stored policy.
    const OpResult result = h.Invoke(
        "apply_status",
        json{{"status_kind", "vanilla:draw_debt"},
             {"stack_policy", "replace"},
             {"params", {{"magnitude", 4}}}},
        {{"target", {target}}});
    CHECK(result.status == OpStatus::kResolved);
    const Status* replaced = FindStatus(h.store, target, "vanilla:draw_debt");
    REQUIRE(replaced != nullptr);
    CHECK(replaced->magnitude == 4);
    CHECK(replaced->stack_policy == StackPolicy::kReplace);
    CHECK(replaced->instance_id == 1);
}

TEST_CASE("status_ops: apply_status replace resets magnitude and duration") {
    Harness h;
    Entity target = AddPlayer(h.store, "p0", 0);

    h.Invoke("apply_status",
             json{{"status_kind", "vanilla:draw_debt"},
                  {"stack_policy", "replace"},
                  {"params", {{"magnitude", 5}}},
                  {"duration", {{"unit", "turns"}, {"value", 3}}}},
             {{"target", {target}}});
    h.Invoke("apply_status",
             json{{"status_kind", "vanilla:draw_debt"},
                  {"params", {{"magnitude", 2}}},
                  {"duration", {{"unit", "rounds"}, {"value", 7}}}},
             {{"target", {target}}});

    const Status* status = FindStatus(h.store, target, "vanilla:draw_debt");
    REQUIRE(status != nullptr);
    CHECK(status->magnitude == 2);
    CHECK(status->duration.unit == DurationUnit::kRounds);
    CHECK(status->duration.value == 7);
    CHECK(status->instance_id == 1);
}

TEST_CASE("status_ops: apply_status independent keeps separate instances") {
    Harness h;
    Entity target = AddPlayer(h.store, "p0", 0);

    h.Invoke("apply_status",
             json{{"status_kind", "space:shielded"},
                  {"stack_policy", "independent"},
                  {"params", {{"magnitude", 2}}},
                  {"duration", {{"unit", "turns"}, {"value", 2}}}},
             {{"target", {target}}});
    const Status* first = FindStatus(h.store, target, "space:shielded");
    REQUIRE(first != nullptr);
    const uint32_t first_id = first->instance_id;
    CHECK(first_id == 1);

    h.Invoke("apply_status",
             json{{"status_kind", "space:shielded"},
                  {"stack_policy", "independent"},
                  {"params", {{"magnitude", 3}}},
                  {"duration", {{"unit", "rounds"}, {"value", 5}}}},
             {{"target", {target}}});

    // INFO: true multi-instance — the first instance is retained alongside the
    //       second, each with its own magnitude, duration and instance id.
    CHECK(StatusCount(h.store, target) == 2);
    const Status* retained = match::status::FindByInstance(
        h.store, target, first_id);
    REQUIRE(retained != nullptr);
    CHECK(retained->magnitude == 2);
    CHECK(retained->duration.unit == DurationUnit::kTurns);
    CHECK(retained->duration.value == 2);

    const Status* second = match::status::FindByInstance(
        h.store, target, first_id + 1);
    REQUIRE(second != nullptr);
    CHECK(second->instance_id == first_id + 1);
    CHECK(second->magnitude == 3);
    CHECK(second->duration.unit == DurationUnit::kRounds);
    CHECK(second->duration.value == 5);
    CHECK(second->stack_policy == StackPolicy::kIndependent);
}

TEST_CASE("status_ops: apply_status cap:N accumulates up to the cap") {
    Harness h;
    Entity target = AddPlayer(h.store, "p0", 0);

    h.Invoke("apply_status",
             json{{"status_kind", "vanilla:draw_debt"},
                  {"stack_policy", "cap:5"},
                  {"params", {{"magnitude", 3}}}},
             {{"target", {target}}});
    const Status* status = FindStatus(h.store, target, "vanilla:draw_debt");
    REQUIRE(status != nullptr);
    CHECK(status->stack_policy == StackPolicy::kCap);
    CHECK(status->cap == 5);
    CHECK(status->magnitude == 3);

    h.Invoke("apply_status",
             json{{"status_kind", "vanilla:draw_debt"},
                  {"params", {{"magnitude", 4}}}},
             {{"target", {target}}});
    CHECK(FindStatus(h.store, target, "vanilla:draw_debt")->magnitude == 5);

    h.Invoke("apply_status",
             json{{"status_kind", "vanilla:draw_debt"},
                  {"params", {{"magnitude", 1}}}},
             {{"target", {target}}});
    CHECK(FindStatus(h.store, target, "vanilla:draw_debt")->magnitude == 5);
}

TEST_CASE("status_ops: apply_status a different kind coexists") {
    Harness h;
    Entity target = AddPlayer(h.store, "p0", 0);

    h.Invoke("apply_status",
             json{{"status_kind", "vanilla:draw_debt"},
                  {"params", {{"magnitude", 2}}}},
             {{"target", {target}}});
    h.Invoke("apply_status",
             json{{"status_kind", "space:shielded"},
                  {"params", {{"magnitude", 4}}}},
             {{"target", {target}}});

    CHECK(StatusCount(h.store, target) == 2);
    const Status* debt = FindStatus(h.store, target, "vanilla:draw_debt");
    REQUIRE(debt != nullptr);
    CHECK(debt->magnitude == 2);
    CHECK(debt->instance_id == 1);
    const Status* shield = FindStatus(h.store, target, "space:shielded");
    REQUIRE(shield != nullptr);
    CHECK(shield->magnitude == 4);
    CHECK(shield->instance_id == 2);
}

TEST_CASE("status_ops: apply_status compound duration keeps the ms leg") {
    Harness h;
    Entity target = AddPlayer(h.store, "p0", 0);

    h.Invoke("apply_status",
             json{{"status_kind", "space:shielded"},
                  {"duration",
                   json::array(
                       {json{{"unit", "turns"}, {"value", 2}},
                        json{{"unit", "ms"}, {"value", 30000}}})}},
             {{"target", {target}}});

    const Status* status = FindStatus(h.store, target, "space:shielded");
    REQUIRE(status != nullptr);
    CHECK(status->duration.unit == DurationUnit::kMs);
    CHECK(status->duration.value == 30000);
}

TEST_CASE("status_ops: apply_status fail-safe paths leave state untouched") {
    Harness h;
    Entity target = AddPlayer(h.store, "p0", 0);
    Entity dead = AddPlayer(h.store, "ghost", 1);
    h.store.Destroy(dead);

    SUBCASE("unbound selector") {
        const OpResult result =
            h.Invoke("apply_status", json{{"status_kind", "space:shielded"}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.events.empty());
        CHECK(FindStatus(h.store, target, "space:shielded") == nullptr);
    }
    SUBCASE("dead entity") {
        const OpResult result = h.Invoke(
            "apply_status", json{{"status_kind", "space:shielded"}},
            {{"target", {dead}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.events.empty());
    }
    SUBCASE("empty status_kind") {
        const OpResult result = h.Invoke(
            "apply_status", json{{"status_kind", ""}}, {{"target", {target}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.events.empty());
    }
    SUBCASE("missing status_kind") {
        const OpResult result =
            h.Invoke("apply_status", json::object(), {{"target", {target}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.events.empty());
    }
    SUBCASE("invalid stack policy") {
        const OpResult result = h.Invoke(
            "apply_status",
            json{{"status_kind", "space:shielded"}, {"stack_policy", "bogus"}},
            {{"target", {target}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.events.empty());
        CHECK(FindStatus(h.store, target, "space:shielded") == nullptr);
    }
    SUBCASE("malformed duration") {
        const OpResult result = h.Invoke(
            "apply_status",
            json{{"status_kind", "space:shielded"},
                 {"duration", {{"unit", "eons"}, {"value", 3}}}},
            {{"target", {target}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.events.empty());
        CHECK(FindStatus(h.store, target, "space:shielded") == nullptr);
    }
}

TEST_CASE("status_ops: remove_status removes by kind and emits") {
    Harness h;
    Entity target = AddPlayer(h.store, "p0", 0);
    h.Invoke("apply_status",
             json{{"status_kind", "vanilla:draw_debt"},
                  {"params", {{"magnitude", 2}}}},
             {{"target", {target}}});

    const OpResult result = h.Invoke(
        "remove_status", json{{"status_kind", "vanilla:draw_debt"}},
        {{"target", {target}}});

    CHECK(result.status == OpStatus::kResolved);
    CHECK(FindStatus(h.store, target, "vanilla:draw_debt") == nullptr);
    CHECK(StatusCount(h.store, target) == 0);
    const json* event = FindEvent(result, "status_removed");
    REQUIRE(event != nullptr);
    CHECK((*event)["payload"]["status_kind"] == "vanilla:draw_debt");
    CHECK((*event)["payload"]["instance"] == 1);
    CHECK((*event)["payload"]["target"]["index"] == target.index);
}

TEST_CASE("status_ops: remove_status by kind clears every instance") {
    Harness h;
    Entity target = AddPlayer(h.store, "p0", 0);
    h.Invoke("apply_status",
             json{{"status_kind", "space:shielded"},
                  {"stack_policy", "independent"}},
             {{"target", {target}}});
    h.Invoke("apply_status",
             json{{"status_kind", "space:shielded"},
                  {"stack_policy", "independent"}},
             {{"target", {target}}});
    REQUIRE(StatusCount(h.store, target) == 2);

    const OpResult result = h.Invoke(
        "remove_status", json{{"status_kind", "space:shielded"}},
        {{"target", {target}}});

    CHECK(result.status == OpStatus::kResolved);
    CHECK(StatusCount(h.store, target) == 0);
    CHECK(result.events.size() == 2);
    CHECK(FindEvent(result, "status_removed") != nullptr);
}

TEST_CASE("status_ops: remove_status removes by instance id") {
    Harness h;
    Entity target = AddPlayer(h.store, "p0", 0);
    h.Invoke("apply_status",
             json{{"status_kind", "space:shielded"},
                  {"stack_policy", "independent"}},
             {{"target", {target}}});

    SUBCASE("matching instance") {
        const OpResult result = h.Invoke(
            "remove_status", json{{"instance", 1}}, {{"target", {target}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(StatusCount(h.store, target) == 0);
        CHECK(FindEvent(result, "status_removed") != nullptr);
    }
    SUBCASE("non-matching instance is a no-op") {
        const OpResult result = h.Invoke(
            "remove_status", json{{"instance", 99}}, {{"target", {target}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.events.empty());
        CHECK(StatusCount(h.store, target) == 1);
    }
}

TEST_CASE("status_ops: remove_status either_of contract") {
    Harness h;
    Entity target = AddPlayer(h.store, "p0", 0);
    h.Invoke("apply_status", json{{"status_kind", "vanilla:draw_debt"}},
             {{"target", {target}}});

    SUBCASE("status_kind wins when both are present") {
        const OpResult result = h.Invoke(
            "remove_status",
            json{{"status_kind", "vanilla:draw_debt"}, {"instance", 99}},
            {{"target", {target}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(StatusCount(h.store, target) == 0);
    }
    SUBCASE("neither arg is a no-op") {
        const OpResult result =
            h.Invoke("remove_status", json::object(), {{"target", {target}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.events.empty());
        CHECK(StatusCount(h.store, target) == 1);
    }
    SUBCASE("mismatched kind is a no-op") {
        const OpResult result = h.Invoke(
            "remove_status", json{{"status_kind", "space:shielded"}},
            {{"target", {target}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.events.empty());
        CHECK(StatusCount(h.store, target) == 1);
    }
}

TEST_CASE("status_ops: remove_status fail-safe paths are no-ops") {
    Harness h;
    Entity target = AddPlayer(h.store, "p0", 0);
    Entity dead = AddPlayer(h.store, "ghost", 1);
    h.store.Destroy(dead);

    SUBCASE("unbound selector") {
        const OpResult result =
            h.Invoke("remove_status", json{{"status_kind", "space:shielded"}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.events.empty());
    }
    SUBCASE("dead entity") {
        const OpResult result = h.Invoke(
            "remove_status", json{{"status_kind", "space:shielded"}},
            {{"target", {dead}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.events.empty());
    }
    SUBCASE("no status present") {
        const OpResult result = h.Invoke(
            "remove_status", json{{"status_kind", "space:shielded"}},
            {{"target", {target}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.events.empty());
    }
    SUBCASE("negative instance is a miss") {
        const OpResult result = h.Invoke(
            "remove_status", json{{"instance", -1}}, {{"target", {target}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.events.empty());
    }
}

TEST_CASE("status_ops: modify_status adjusts and reports the magnitude") {
    Harness h;
    Entity target = AddPlayer(h.store, "p0", 0);
    h.Invoke("apply_status",
             json{{"status_kind", "vanilla:draw_debt"},
                  {"stack_policy", "accumulate"},
                  {"params", {{"magnitude", 2}}}},
             {{"target", {target}}});

    const OpResult result = h.Invoke(
        "modify_status",
        json{{"kind", "vanilla:draw_debt"}, {"delta", 3}},
        {{"target", {target}}});

    CHECK(result.status == OpStatus::kResolved);
    CHECK(FindStatus(h.store, target, "vanilla:draw_debt")->magnitude == 5);
    CHECK(result.value["magnitude"] == 5);
    CHECK(result.value["delta"] == 3);
    CHECK(result.events.empty());
}

TEST_CASE("status_ops: modify_status clamps delta to declared bounds") {
    Harness h;
    Entity target = AddPlayer(h.store, "p0", 0);
    h.Invoke("apply_status",
             json{{"status_kind", "vanilla:draw_debt"},
                  {"stack_policy", "accumulate"}},
             {{"target", {target}}});

    h.Invoke("modify_status",
             json{{"kind", "vanilla:draw_debt"}, {"delta", 5000}},
             {{"target", {target}}});
    CHECK(FindStatus(h.store, target, "vanilla:draw_debt")->magnitude == 1001);

    h.Invoke("modify_status",
             json{{"kind", "vanilla:draw_debt"}, {"delta", -5000}},
             {{"target", {target}}});
    CHECK(FindStatus(h.store, target, "vanilla:draw_debt")->magnitude == 1);
}

TEST_CASE("status_ops: modify_status respects the cap ceiling") {
    Harness h;
    Entity target = AddPlayer(h.store, "p0", 0);
    h.Invoke("apply_status",
             json{{"status_kind", "vanilla:draw_debt"},
                  {"stack_policy", "cap:5"},
                  {"params", {{"magnitude", 3}}}},
             {{"target", {target}}});

    h.Invoke("modify_status",
             json{{"kind", "vanilla:draw_debt"}, {"delta", 10}},
             {{"target", {target}}});
    CHECK(FindStatus(h.store, target, "vanilla:draw_debt")->magnitude == 5);
}

TEST_CASE("status_ops: modify_status fail-safe paths are no-ops") {
    Harness h;
    Entity target = AddPlayer(h.store, "p0", 0);
    Entity dead = AddPlayer(h.store, "ghost", 1);
    h.store.Destroy(dead);
    h.Invoke("apply_status",
             json{{"status_kind", "vanilla:draw_debt"},
                  {"params", {{"magnitude", 2}}}},
             {{"target", {target}}});

    SUBCASE("unbound selector") {
        const OpResult result = h.Invoke(
            "modify_status",
            json{{"kind", "vanilla:draw_debt"}, {"delta", 1}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.value.is_null());
    }
    SUBCASE("dead entity") {
        const OpResult result = h.Invoke(
            "modify_status",
            json{{"kind", "vanilla:draw_debt"}, {"delta", 1}},
            {{"target", {dead}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.value.is_null());
    }
    SUBCASE("missing kind") {
        const OpResult result = h.Invoke(
            "modify_status", json{{"delta", 1}}, {{"target", {target}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(FindStatus(h.store, target, "vanilla:draw_debt")->magnitude == 2);
    }
    SUBCASE("mismatched kind") {
        const OpResult result = h.Invoke(
            "modify_status",
            json{{"kind", "space:shielded"}, {"delta", 1}},
            {{"target", {target}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(FindStatus(h.store, target, "vanilla:draw_debt")->magnitude == 2);
    }
    SUBCASE("missing delta") {
        const OpResult result = h.Invoke(
            "modify_status", json{{"kind", "vanilla:draw_debt"}},
            {{"target", {target}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(FindStatus(h.store, target, "vanilla:draw_debt")->magnitude == 2);
    }
}
