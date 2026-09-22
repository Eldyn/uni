#include <doctest/doctest.h>

#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using match::ecs::BudgetLedger;
using match::ecs::CardIdentity;
using match::ecs::Entity;
using match::ecs::EntityStore;
using match::ecs::EventBus;
using match::ecs::Hand;
using match::ecs::HookCallback;
using match::ecs::HookId;
using match::ecs::HookPayload;
using match::ecs::HookPhase;
using match::ecs::InZone;
using match::ecs::MatchMeta;
using match::ecs::Placements;
using match::ecs::PlayerInfo;
using match::ecs::ZoneKind;
using match::ecs::ZoneRef;
using match::ops::MakeDefaultRuntime;
using match::ops::OpArgs;
using match::ops::OpContext;
using match::ops::OpResult;
using match::ops::OpRuntime;
using match::ops::OpStatus;
using match::ops::ResolutionFrame;

using nlohmann::json;

namespace {

Entity AddMatch(EntityStore& store) {
    Entity entity = store.Create();
    store.Add(entity, MatchMeta{});
    return entity;
}

Entity AddPlayer(EntityStore& store, const std::string& name, uint32_t seat) {
    Entity entity = store.Create();
    PlayerInfo info;
    info.username = name;
    info.seat = seat;
    store.Add(entity, info);
    store.Add(entity, Hand{});
    return entity;
}

Entity MakeCard(EntityStore& store, const std::string& kind_id) {
    Entity card = store.Create();
    CardIdentity identity;
    identity.kind_id = kind_id;
    store.Add(card, identity);
    store.Add(card, InZone{ZoneRef{ZoneKind::kLimbo, Entity{}}, 0});
    return card;
}

void PutInHand(EntityStore& store, Entity player, Entity card) {
    Hand* hand = store.Get<Hand>(player);
    REQUIRE(hand != nullptr);
    hand->cards.push_back(card);
    InZone* in = store.Get<InZone>(card);
    REQUIRE(in != nullptr);
    in->zone = ZoneRef{ZoneKind::kHand, player};
    in->ordinal = static_cast<uint32_t>(hand->cards.size() - 1);
}

/** INFO: one store/bus/runtime fixture per case. */
struct Harness {
    EntityStore store;
    BudgetLedger ledger;
    EventBus bus = EventBus(std::vector<std::string>{"m"});
    ResolutionFrame frame;
    OpRuntime runtime = MakeDefaultRuntime();
    uint32_t next_index = 0;

    void On(std::string name, HookPhase phase, HookCallback callback) {
        bus.Subscribe("m", next_index++, HookId{std::move(name), phase},
                      std::move(callback));
    }

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

const Placements* PlacementsOf(const EntityStore& store, Entity match) {
    return store.Get<Placements>(match);
}

bool SameEntity(const json& value, Entity entity) {
    return value.is_object() && value.value("index", 0u) == entity.index
        && value.value("generation", 0u) == entity.generation;
}

}  // namespace

TEST_CASE("win_ops: check_win records the hand-empty player by seat order") {
    Harness h;
    Entity match = AddMatch(h.store);
    Entity p1 = AddPlayer(h.store, "p1", 1);
    Entity p0 = AddPlayer(h.store, "p0", 0);
    PutInHand(h.store, p1, MakeCard(h.store, "vanilla:red_1"));

    const OpResult result = h.Invoke("check_win", json::object());
    CHECK(result.status == OpStatus::kResolved);
    REQUIRE(result.events.size() == 1);
    CHECK(result.events[0]["type"] == "placement");
    CHECK(SameEntity(result.events[0]["payload"]["player"], p0));
    CHECK(result.events[0]["payload"]["place"] == 1);
    CHECK(result.events[0]["payload"]["kind"] == "normal");

    const Placements* placements = PlacementsOf(h.store, match);
    REQUIRE(placements != nullptr);
    CHECK(placements->order == std::vector<Entity>{p0});
}

TEST_CASE("win_ops: check_win tie-breaks multiple finishers by seat") {
    Harness h;
    Entity match = AddMatch(h.store);
    Entity late = AddPlayer(h.store, "late", 2);
    Entity first = AddPlayer(h.store, "first", 0);
    Entity second = AddPlayer(h.store, "second", 1);

    const OpResult result = h.Invoke("check_win", json::object());
    REQUIRE(result.events.size() == 1);
    CHECK(SameEntity(result.events[0]["payload"]["player"], first));
    const Placements* placements = PlacementsOf(h.store, match);
    REQUIRE(placements != nullptr);
    CHECK(placements->order == std::vector<Entity>{first});
    CHECK(second != first);
    CHECK(late != first);
}

TEST_CASE("win_ops: check_win before-hook veto blocks the default") {
    Harness h;
    Entity match = AddMatch(h.store);
    Entity p0 = AddPlayer(h.store, "p0", 0);

    int before = 0;
    int after = 0;
    h.On("win_check", HookPhase::kBefore, [&](HookPayload& payload) {
        ++before;
        payload.veto = true;
    });
    h.On("win_check", HookPhase::kAfter,
         [&](HookPayload&) { ++after; });

    const OpResult result = h.Invoke("check_win", json::object());
    CHECK(result.status == OpStatus::kResolved);
    CHECK(result.events.empty());
    CHECK(before == 1);
    CHECK(after == 1);
    CHECK(PlacementsOf(h.store, match) == nullptr);
    (void)p0;
}

TEST_CASE("win_ops: check_win honors a before-hook declaring another") {
    Harness h;
    Entity match = AddMatch(h.store);
    Entity p0 = AddPlayer(h.store, "p0", 0);
    Entity p1 = AddPlayer(h.store, "p1", 1);

    h.On("win_check", HookPhase::kBefore, [&](HookPayload& payload) {
        payload.data["player"] =
            json{{"index", p1.index}, {"generation", p1.generation}};
    });

    const OpResult result = h.Invoke("check_win", json::object());
    REQUIRE(result.events.size() == 1);
    CHECK(SameEntity(result.events[0]["payload"]["player"], p1));
    const Placements* placements = PlacementsOf(h.store, match);
    REQUIRE(placements != nullptr);
    CHECK(placements->order == std::vector<Entity>{p1});
    (void)p0;
}

TEST_CASE("win_ops: check_win no candidate is a no-op") {
    Harness h;
    Entity match = AddMatch(h.store);
    Entity p0 = AddPlayer(h.store, "p0", 0);
    PutInHand(h.store, p0, MakeCard(h.store, "vanilla:red_1"));

    const OpResult result = h.Invoke("check_win", json::object());
    CHECK(result.events.empty());
    CHECK(result.value.is_null());
    CHECK(PlacementsOf(h.store, match) == nullptr);
}

TEST_CASE("win_ops: check_win without a match entity is fail-safe") {
    Harness h;
    Entity p0 = AddPlayer(h.store, "p0", 0);

    const OpResult result = h.Invoke("check_win", json::object());
    CHECK(result.status == OpStatus::kResolved);
    CHECK(result.events.empty());
    (void)p0;
}

TEST_CASE("win_ops: check_win dedupes a repeated placement") {
    Harness h;
    Entity match = AddMatch(h.store);
    Entity p0 = AddPlayer(h.store, "p0", 0);

    const OpResult first = h.Invoke("check_win", json::object());
    CHECK(first.events.size() == 1);
    const OpResult second = h.Invoke("check_win", json::object());
    CHECK(second.events.empty());

    const Placements* placements = PlacementsOf(h.store, match);
    REQUIRE(placements != nullptr);
    CHECK(placements->order.size() == 1);
}

TEST_CASE("win_ops: declare_winner normal records a placement") {
    Harness h;
    Entity match = AddMatch(h.store);
    Entity p0 = AddPlayer(h.store, "p0", 0);

    const OpResult result =
        h.Invoke("declare_winner", json{{"kind", "normal"}},
                 {{"player", {p0}}});
    CHECK(result.status == OpStatus::kResolved);
    REQUIRE(result.events.size() == 1);
    CHECK(result.events[0]["type"] == "placement");
    CHECK(SameEntity(result.events[0]["payload"]["player"], p0));
    CHECK(result.events[0]["payload"]["place"] == 1);
    CHECK(result.events[0]["payload"]["kind"] == "normal");
    CHECK(result.value["kind"] == "normal");

    const Placements* placements = PlacementsOf(h.store, match);
    REQUIRE(placements != nullptr);
    CHECK(placements->order == std::vector<Entity>{p0});
}

TEST_CASE("win_ops: declare_winner special carries the special kind") {
    Harness h;
    Entity match = AddMatch(h.store);
    Entity black_hole = AddPlayer(h.store, "hole", 0);

    const OpResult result =
        h.Invoke("declare_winner", json{{"kind", "special"}},
                 {{"player", {black_hole}}});
    REQUIRE(result.events.size() == 1);
    CHECK(result.events[0]["payload"]["kind"] == "special");
    CHECK(result.value["kind"] == "special");
    CHECK(result.value["place"] == 1);

    const Placements* placements = PlacementsOf(h.store, match);
    REQUIRE(placements != nullptr);
    CHECK(placements->order == std::vector<Entity>{black_hole});
}

TEST_CASE("win_ops: declare_winner fail-safe paths") {
    Harness h;
    AddMatch(h.store);
    Entity p0 = AddPlayer(h.store, "p0", 0);
    Entity card = MakeCard(h.store, "vanilla:red_1");

    const OpResult unbound =
        h.Invoke("declare_winner", json{{"kind", "normal"}});
    CHECK(unbound.events.empty());

    const OpResult no_kind =
        h.Invoke("declare_winner", json::object(), {{"player", {p0}}});
    CHECK(no_kind.events.empty());

    const OpResult bad_kind =
        h.Invoke("declare_winner", json{{"kind", "weird"}},
                 {{"player", {p0}}});
    CHECK(bad_kind.events.empty());

    const OpResult non_player =
        h.Invoke("declare_winner", json{{"kind", "normal"}},
                 {{"player", {card}}});
    CHECK(non_player.events.empty());

    Entity dead = AddPlayer(h.store, "dead", 3);
    h.store.Destroy(dead);
    const OpResult dead_player =
        h.Invoke("declare_winner", json{{"kind", "normal"}},
                 {{"player", {dead}}});
    CHECK(dead_player.events.empty());
}

TEST_CASE("win_ops: add_placement appends in order and dedupes") {
    Harness h;
    Entity match = AddMatch(h.store);
    Entity p0 = AddPlayer(h.store, "p0", 0);
    Entity p1 = AddPlayer(h.store, "p1", 1);
    Entity p2 = AddPlayer(h.store, "p2", 2);

    const OpResult first =
        h.Invoke("add_placement", json::object(), {{"player", {p0}}});
    REQUIRE(first.events.size() == 1);
    CHECK(first.events[0]["payload"]["place"] == 1);

    const OpResult second =
        h.Invoke("add_placement", json::object(), {{"player", {p1}}});
    REQUIRE(second.events.size() == 1);
    CHECK(second.events[0]["payload"]["place"] == 2);

    const OpResult duplicate =
        h.Invoke("add_placement", json::object(), {{"player", {p0}}});
    CHECK(duplicate.events.empty());

    const OpResult third =
        h.Invoke("add_placement", json::object(), {{"player", {p2}}});
    REQUIRE(third.events.size() == 1);
    CHECK(third.events[0]["payload"]["place"] == 3);

    const Placements* placements = PlacementsOf(h.store, match);
    REQUIRE(placements != nullptr);
    CHECK(placements->order == std::vector<Entity>{p0, p1, p2});
}

TEST_CASE("win_ops: add_placement fail-safe paths") {
    Harness h;
    AddMatch(h.store);
    Entity card = MakeCard(h.store, "vanilla:red_1");

    const OpResult unbound = h.Invoke("add_placement", json::object());
    CHECK(unbound.status == OpStatus::kResolved);
    CHECK(unbound.events.empty());

    const OpResult non_player =
        h.Invoke("add_placement", json::object(), {{"player", {card}}});
    CHECK(non_player.events.empty());

    Entity dead = AddPlayer(h.store, "dead", 0);
    h.store.Destroy(dead);
    const OpResult dead_player =
        h.Invoke("add_placement", json::object(), {{"player", {dead}}});
    CHECK(dead_player.events.empty());
}
