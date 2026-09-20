#include <doctest/doctest.h>

#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using match::ecs::Aspect;
using match::ecs::BudgetLedger;
using match::ecs::Entity;
using match::ecs::EntityStore;
using match::ecs::EventBus;
using match::ecs::MatchMeta;
using match::ecs::PlayerInfo;
using match::ecs::VisibilityGrant;
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

Entity AddMatch(EntityStore& store) {
    Entity match = store.Create();
    store.Add(match, MatchMeta{});
    return match;
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

const VisibilityGrant* GetGrant(EntityStore& store, Entity entity) {
    return store.Get<VisibilityGrant>(entity);
}

uint32_t Mask(Aspect aspect) {
    return static_cast<uint32_t>(aspect);
}

}  // namespace

TEST_CASE("visibility_ops: grant single aspect stores and emits") {
    Harness h;
    Entity viewer = AddPlayer(h.store, "p0", 0);
    Entity target = AddPlayer(h.store, "p1", 1);

    const OpResult result =
        h.Invoke("grant_visibility", json{{"aspects", "identity"}},
                 {{"viewer", {viewer}}, {"target", {target}}});

    CHECK(result.status == OpStatus::kResolved);
    const VisibilityGrant* grant = GetGrant(h.store, target);
    REQUIRE(grant != nullptr);
    REQUIRE(grant->entries.size() == 1);
    CHECK(grant->entries[0].viewer == viewer);
    CHECK(grant->entries[0].aspect_mask == Mask(Aspect::kIdentity));
    CHECK(grant->entries[0].expires_ms == 0);

    const json* event = FindEvent(result, "visibility_granted");
    REQUIRE(event != nullptr);
    CHECK((*event)["payload"]["viewer"]["index"] == viewer.index);
    CHECK((*event)["payload"]["target"]["index"] == target.index);
    CHECK((*event)["payload"]["aspects"] == json::array({"identity"}));
}

TEST_CASE("visibility_ops: grant array ORs the aspect bits") {
    Harness h;
    Entity viewer = AddPlayer(h.store, "p0", 0);
    Entity target = AddPlayer(h.store, "p1", 1);

    const OpResult result = h.Invoke(
        "grant_visibility", json{{"aspects", {"count", "color"}}},
        {{"viewer", {viewer}}, {"target", {target}}});

    CHECK(result.status == OpStatus::kResolved);
    const VisibilityGrant* grant = GetGrant(h.store, target);
    REQUIRE(grant != nullptr);
    REQUIRE(grant->entries.size() == 1);
    CHECK(grant->entries[0].aspect_mask ==
          (Mask(Aspect::kCount) | Mask(Aspect::kColor)));

    const json* event = FindEvent(result, "visibility_granted");
    REQUIRE(event != nullptr);
    CHECK((*event)["payload"]["aspects"] ==
          json::array({"count", "color"}));
}

TEST_CASE("visibility_ops: a second grant merges into one entry") {
    Harness h;
    Entity viewer = AddPlayer(h.store, "p0", 0);
    Entity target = AddPlayer(h.store, "p1", 1);

    h.Invoke("grant_visibility", json{{"aspects", "identity"}},
             {{"viewer", {viewer}}, {"target", {target}}});
    const OpResult result =
        h.Invoke("grant_visibility", json{{"aspects", "position"}},
                 {{"viewer", {viewer}}, {"target", {target}}});

    CHECK(result.status == OpStatus::kResolved);
    const VisibilityGrant* grant = GetGrant(h.store, target);
    REQUIRE(grant != nullptr);
    REQUIRE(grant->entries.size() == 1);
    CHECK(grant->entries[0].aspect_mask ==
          (Mask(Aspect::kIdentity) | Mask(Aspect::kPosition)));

    const json* event = FindEvent(result, "visibility_granted");
    REQUIRE(event != nullptr);
    // INFO: the event reports the requested aspects, not the merged mask.
    CHECK((*event)["payload"]["aspects"] == json::array({"position"}));
}

TEST_CASE("visibility_ops: distinct viewers keep distinct entries") {
    Harness h;
    Entity viewer_a = AddPlayer(h.store, "p0", 0);
    Entity viewer_b = AddPlayer(h.store, "p1", 1);
    Entity target = AddPlayer(h.store, "p2", 2);

    h.Invoke("grant_visibility", json{{"aspects", "count"}},
             {{"viewer", {viewer_a}}, {"target", {target}}});
    h.Invoke("grant_visibility", json{{"aspects", "identity"}},
             {{"viewer", {viewer_b}}, {"target", {target}}});

    const VisibilityGrant* grant = GetGrant(h.store, target);
    REQUIRE(grant != nullptr);
    REQUIRE(grant->entries.size() == 2);
    CHECK(grant->entries[0].viewer == viewer_a);
    CHECK(grant->entries[0].aspect_mask == Mask(Aspect::kCount));
    CHECK(grant->entries[1].viewer == viewer_b);
    CHECK(grant->entries[1].aspect_mask == Mask(Aspect::kIdentity));
}

TEST_CASE("visibility_ops: grant stores a duration value, not a clock") {
    Harness h;
    Entity viewer = AddPlayer(h.store, "p0", 0);
    Entity target = AddPlayer(h.store, "p1", 1);

    const OpResult result = h.Invoke(
        "grant_visibility",
        json{{"aspects", "identity"},
             {"duration", {{"unit", "turns"}, {"value", 3}}}},
        {{"viewer", {viewer}}, {"target", {target}}});

    CHECK(result.status == OpStatus::kResolved);
    const VisibilityGrant* grant = GetGrant(h.store, target);
    REQUIRE(grant != nullptr);
    REQUIRE(grant->entries.size() == 1);
    CHECK(grant->entries[0].expires_ms == 3);
    CHECK(result.value["duration_unit"] == "turns");
    CHECK(result.value["duration_value"] == 3);
}

TEST_CASE("visibility_ops: a never grant dominates a finite merge") {
    Harness h;
    Entity viewer = AddPlayer(h.store, "p0", 0);
    Entity target = AddPlayer(h.store, "p1", 1);

    h.Invoke("grant_visibility",
             json{{"aspects", "identity"},
                  {"duration", {{"unit", "turns"}, {"value", 3}}}},
             {{"viewer", {viewer}}, {"target", {target}}});
    h.Invoke("grant_visibility", json{{"aspects", "count"}},
             {{"viewer", {viewer}}, {"target", {target}}});

    const VisibilityGrant* grant = GetGrant(h.store, target);
    REQUIRE(grant != nullptr);
    REQUIRE(grant->entries.size() == 1);
    // INFO: absent duration = never (0), which dominates the finite expiry.
    CHECK(grant->entries[0].expires_ms == 0);
}

TEST_CASE("visibility_ops: revoked subset keeps the entry alive") {
    Harness h;
    Entity viewer = AddPlayer(h.store, "p0", 0);
    Entity target = AddPlayer(h.store, "p1", 1);

    h.Invoke("grant_visibility", json{{"aspects", {"count", "color"}}},
             {{"viewer", {viewer}}, {"target", {target}}});
    const OpResult result =
        h.Invoke("revoke_visibility", json{{"aspects", "color"}},
                 {{"viewer", {viewer}}, {"target", {target}}});

    CHECK(result.status == OpStatus::kResolved);
    const VisibilityGrant* grant = GetGrant(h.store, target);
    REQUIRE(grant != nullptr);
    REQUIRE(grant->entries.size() == 1);
    CHECK(grant->entries[0].aspect_mask == Mask(Aspect::kCount));

    const json* event = FindEvent(result, "visibility_revoked");
    REQUIRE(event != nullptr);
    CHECK((*event)["payload"]["aspects"] == json::array({"color"}));
}

TEST_CASE("visibility_ops: revoking the last aspect drops the entry") {
    Harness h;
    Entity viewer = AddPlayer(h.store, "p0", 0);
    Entity target = AddPlayer(h.store, "p1", 1);

    h.Invoke("grant_visibility", json{{"aspects", "identity"}},
             {{"viewer", {viewer}}, {"target", {target}}});
    const OpResult result =
        h.Invoke("revoke_visibility", json{{"aspects", "identity"}},
                 {{"viewer", {viewer}}, {"target", {target}}});

    CHECK(result.status == OpStatus::kResolved);
    const VisibilityGrant* grant = GetGrant(h.store, target);
    REQUIRE(grant != nullptr);
    CHECK(grant->entries.empty());
}

TEST_CASE("visibility_ops: absent aspects revoke every entry for viewer") {
    Harness h;
    Entity viewer = AddPlayer(h.store, "p0", 0);
    Entity other = AddPlayer(h.store, "p2", 2);
    Entity target = AddPlayer(h.store, "p1", 1);

    h.Invoke("grant_visibility", json{{"aspects", {"count", "color"}}},
             {{"viewer", {viewer}}, {"target", {target}}});
    h.Invoke("grant_visibility", json{{"aspects", "identity"}},
             {{"viewer", {other}}, {"target", {target}}});
    const OpResult result =
        h.Invoke("revoke_visibility", json::object(),
                 {{"viewer", {viewer}}, {"target", {target}}});

    CHECK(result.status == OpStatus::kResolved);
    const VisibilityGrant* grant = GetGrant(h.store, target);
    REQUIRE(grant != nullptr);
    REQUIRE(grant->entries.size() == 1);
    CHECK(grant->entries[0].viewer == other);

    const json* event = FindEvent(result, "visibility_revoked");
    REQUIRE(event != nullptr);
    CHECK((*event)["payload"]["aspects"] ==
          json::array({"count", "color"}));
}

TEST_CASE("visibility_ops: revoke with no matching grant is a no-op") {
    Harness h;
    Entity viewer = AddPlayer(h.store, "p0", 0);
    Entity target = AddPlayer(h.store, "p1", 1);

    const OpResult result =
        h.Invoke("revoke_visibility", json::object(),
                 {{"viewer", {viewer}}, {"target", {target}}});

    CHECK(result.status == OpStatus::kResolved);
    CHECK(result.value.is_null());
    CHECK(result.events.empty());
    CHECK(GetGrant(h.store, target) == nullptr);
}

TEST_CASE("visibility_ops: fail-safe on bad aspects, selectors, duration") {
    Harness h;
    Entity viewer = AddPlayer(h.store, "p0", 0);
    Entity target = AddPlayer(h.store, "p1", 1);

    SUBCASE("unknown aspect token") {
        const OpResult result =
            h.Invoke("grant_visibility", json{{"aspects", "smell"}},
                     {{"viewer", {viewer}}, {"target", {target}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.value.is_null());
        CHECK(GetGrant(h.store, target) == nullptr);
    }
    SUBCASE("malformed aspect value") {
        const std::vector<json> bad_values = {
            json(5), json{{"a", 1}}, json::array({"count", 2})};
        for (const json& bad : bad_values) {
            const OpResult result =
                h.Invoke("grant_visibility", json{{"aspects", bad}},
                         {{"viewer", {viewer}}, {"target", {target}}});
            CHECK(result.status == OpStatus::kResolved);
            CHECK(GetGrant(h.store, target) == nullptr);
        }
    }
    SUBCASE("missing aspects") {
        const OpResult result =
            h.Invoke("grant_visibility", json::object(),
                     {{"viewer", {viewer}}, {"target", {target}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(GetGrant(h.store, target) == nullptr);
    }
    SUBCASE("malformed duration") {
        const OpResult result = h.Invoke(
            "grant_visibility",
            json{{"aspects", "identity"},
                 {"duration", {{"unit", "fortnight"}, {"value", 1}}}},
            {{"viewer", {viewer}}, {"target", {target}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(GetGrant(h.store, target) == nullptr);
    }
    SUBCASE("unbound viewer or target") {
        const OpResult no_viewer =
            h.Invoke("grant_visibility", json{{"aspects", "identity"}},
                     {{"target", {target}}});
        CHECK(no_viewer.status == OpStatus::kResolved);
        const OpResult no_target =
            h.Invoke("grant_visibility", json{{"aspects", "identity"}},
                     {{"viewer", {viewer}}});
        CHECK(no_target.status == OpStatus::kResolved);
        CHECK(GetGrant(h.store, target) == nullptr);
    }
    SUBCASE("dead viewer or target") {
        Entity dead = AddPlayer(h.store, "p2", 2);
        REQUIRE(h.store.Destroy(dead));
        const OpResult dead_viewer =
            h.Invoke("grant_visibility", json{{"aspects", "identity"}},
                     {{"viewer", {dead}}, {"target", {target}}});
        CHECK(dead_viewer.status == OpStatus::kResolved);
        const OpResult dead_target =
            h.Invoke("grant_visibility", json{{"aspects", "identity"}},
                     {{"viewer", {viewer}}, {"target", {dead}}});
        CHECK(dead_target.status == OpStatus::kResolved);
        CHECK(GetGrant(h.store, target) == nullptr);
    }
    SUBCASE("revoke with a malformed aspect is a no-op") {
        h.Invoke("grant_visibility", json{{"aspects", "identity"}},
                 {{"viewer", {viewer}}, {"target", {target}}});
        const OpResult result =
            h.Invoke("revoke_visibility", json{{"aspects", "smell"}},
                     {{"viewer", {viewer}}, {"target", {target}}});
        CHECK(result.status == OpStatus::kResolved);
        CHECK(result.events.empty());
        const VisibilityGrant* grant = GetGrant(h.store, target);
        REQUIRE(grant != nullptr);
        REQUIRE(grant->entries.size() == 1);
        CHECK(grant->entries[0].aspect_mask == Mask(Aspect::kIdentity));
    }
}
