#include <doctest/doctest.h>

#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using match::ecs::BudgetLedger;
using match::ecs::Direction;
using match::ecs::Entity;
using match::ecs::EntityStore;
using match::ecs::EventBus;
using match::ecs::Hand;
using match::ecs::MatchMeta;
using match::ecs::PlayerInfo;
using match::ecs::TurnState;
using match::ops::FindCurrentPlayer;
using match::ops::FindMatch;
using match::ops::MakeDefaultRuntime;
using match::ops::OpArgs;
using match::ops::OpContext;
using match::ops::OpResult;
using match::ops::OpRuntime;
using match::ops::OpStatus;
using match::ops::ResolutionFrame;

using nlohmann::json;

namespace {

// INFO: mirrors turn_flow_ops.cpp's reserved skip bit; the tests pin the
//       persisted representation so a later migration notices the contract.
constexpr uint32_t kSkipFlag = 1u << 31;
constexpr uint32_t kExtraMask = kSkipFlag - 1u;

Entity AddPlayer(EntityStore& store, const std::string& name, uint32_t seat) {
    Entity entity = store.Create();
    PlayerInfo info;
    info.username = name;
    info.seat = seat;
    store.Add(entity, info);
    store.Add(entity, Hand{});
    store.Add(entity, TurnState{});
    return entity;
}

Entity AddMatch(EntityStore& store, Direction direction = Direction::kForward) {
    Entity entity = store.Create();
    MatchMeta meta;
    meta.direction = direction;
    store.Add(entity, meta);
    return entity;
}

void MakeCurrent(EntityStore& store, Entity player, bool current = true) {
    TurnState* turn = store.Get<TurnState>(player);
    REQUIRE(turn != nullptr);
    turn->is_current = current;
}

uint32_t PendingRaw(EntityStore& store, Entity player) {
    const TurnState* turn = store.Get<TurnState>(player);
    REQUIRE(turn != nullptr);
    return turn->extra_turns_pending;
}

int64_t Deadline(EntityStore& store, Entity player) {
    const TurnState* turn = store.Get<TurnState>(player);
    REQUIRE(turn != nullptr);
    return turn->turn_deadline_ms;
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

}  // namespace

TEST_CASE("turn_flow_ops: advance_turn steps forward and emits turn_advance") {
    Harness h;
    AddMatch(h.store);
    Entity p0 = AddPlayer(h.store, "p0", 0);
    Entity p1 = AddPlayer(h.store, "p1", 1);
    Entity p2 = AddPlayer(h.store, "p2", 2);
    MakeCurrent(h.store, p0);

    const OpResult result = h.Invoke("advance_turn", json::object());
    CHECK(result.status == OpStatus::kResolved);
    CHECK_FALSE(h.store.Get<TurnState>(p0)->is_current);
    CHECK(h.store.Get<TurnState>(p1)->is_current);
    CHECK_FALSE(h.store.Get<TurnState>(p2)->is_current);
    REQUIRE(FindCurrentPlayer(h.store).has_value());
    CHECK(*FindCurrentPlayer(h.store) == p1);

    const json* event = FindEvent(result, "turn_advance");
    REQUIRE(event != nullptr);
    CHECK((*event)["payload"]["from"]["index"] == p0.index);
    CHECK((*event)["payload"]["to"]["index"] == p1.index);
    CHECK((*event)["payload"]["direction"] == 1);
    CHECK((*event)["payload"]["deadline"] == 0);
}

TEST_CASE("turn_flow_ops: advance_turn clears stale incoming/outgoing deadlines") {
    Harness h;
    AddMatch(h.store);
    Entity p0 = AddPlayer(h.store, "p0", 0);
    Entity p1 = AddPlayer(h.store, "p1", 1);
    MakeCurrent(h.store, p0);
    // INFO: p0's live deadline and a STALE past deadline planted on p1 (as if
    //       it survived from an earlier turn).
    h.store.Get<TurnState>(p0)->turn_deadline_ms = 5000;
    h.store.Get<TurnState>(p1)->turn_deadline_ms = 50;

    const OpResult result = h.Invoke("advance_turn", json::object());
    CHECK(Deadline(h.store, p0) == 0);
    CHECK(Deadline(h.store, p1) == 0);

    const json* event = FindEvent(result, "turn_advance");
    REQUIRE(event != nullptr);
    CHECK((*event)["payload"]["deadline"] == 0);
}

TEST_CASE("turn_flow_ops: advance_turn wraps from the last seat to the first") {
    Harness h;
    AddMatch(h.store);
    Entity p0 = AddPlayer(h.store, "p0", 0);
    AddPlayer(h.store, "p1", 1);
    Entity p2 = AddPlayer(h.store, "p2", 2);
    MakeCurrent(h.store, p2);

    const OpResult result =
        h.Invoke("advance_turn", json::object());
    const json* event = FindEvent(result, "turn_advance");
    REQUIRE(event != nullptr);
    CHECK((*event)["payload"]["from"]["index"] == p2.index);
    CHECK((*event)["payload"]["to"]["index"] == p0.index);
}

TEST_CASE("turn_flow_ops: advance_turn respects reverse direction") {
    Harness h;
    AddMatch(h.store);
    Entity p0 = AddPlayer(h.store, "p0", 0);
    Entity p1 = AddPlayer(h.store, "p1", 1);
    Entity p2 = AddPlayer(h.store, "p2", 2);
    MakeCurrent(h.store, p1);

    CHECK(h.Invoke("reverse_direction", json::object()).ok());
    const OpResult result = h.Invoke("advance_turn", json::object());
    const json* event = FindEvent(result, "turn_advance");
    REQUIRE(event != nullptr);
    CHECK((*event)["payload"]["direction"] == -1);
    CHECK((*event)["payload"]["to"]["index"] == p0.index);

    // INFO: reverse wraps backward from seat 0 to the last seat.
    const OpResult wrap = h.Invoke("advance_turn", json::object());
    const json* wrap_event = FindEvent(wrap, "turn_advance");
    REQUIRE(wrap_event != nullptr);
    CHECK((*wrap_event)["payload"]["to"]["index"] == p2.index);
}

TEST_CASE("turn_flow_ops: advance_turn first turn picks the lowest seat") {
    Harness h;
    AddMatch(h.store);
    Entity high = AddPlayer(h.store, "high", 2);
    Entity low = AddPlayer(h.store, "low", 0);
    Entity mid = AddPlayer(h.store, "mid", 1);

    const OpResult result = h.Invoke("advance_turn", json::object());
    CHECK(h.store.Get<TurnState>(low)->is_current);
    CHECK_FALSE(h.store.Get<TurnState>(mid)->is_current);
    CHECK_FALSE(h.store.Get<TurnState>(high)->is_current);

    const json* event = FindEvent(result, "turn_advance");
    REQUIRE(event != nullptr);
    CHECK((*event)["payload"]["from"].is_null());
    CHECK((*event)["payload"]["to"]["index"] == low.index);
}

TEST_CASE("turn_flow_ops: advance_turn consumes one queued extra turn") {
    Harness h;
    AddMatch(h.store);
    Entity p0 = AddPlayer(h.store, "p0", 0);
    AddPlayer(h.store, "p1", 1);
    MakeCurrent(h.store, p0);
    h.store.Get<TurnState>(p0)->extra_turns_pending = 2;

    const OpResult result = h.Invoke("advance_turn", json::object());
    CHECK(h.store.Get<TurnState>(p0)->is_current);
    CHECK((PendingRaw(h.store, p0) & kExtraMask) == 1u);

    const json* event = FindEvent(result, "turn_advance");
    REQUIRE(event != nullptr);
    CHECK((*event)["payload"]["from"]["index"] == p0.index);
    CHECK((*event)["payload"]["to"]["index"] == p0.index);
}

TEST_CASE("turn_flow_ops: skip_turn is consumed by the next advance") {
    Harness h;
    AddMatch(h.store);
    Entity p0 = AddPlayer(h.store, "p0", 0);
    Entity p1 = AddPlayer(h.store, "p1", 1);
    Entity p2 = AddPlayer(h.store, "p2", 2);
    MakeCurrent(h.store, p0);

    const OpResult skip =
        h.Invoke("skip_turn", json::object(), {{"target", {p1}}});
    CHECK(skip.status == OpStatus::kResolved);
    CHECK((PendingRaw(h.store, p1) & kSkipFlag) != 0u);

    const OpResult first = h.Invoke("advance_turn", json::object());
    const json* first_event = FindEvent(first, "turn_advance");
    REQUIRE(first_event != nullptr);
    CHECK((*first_event)["payload"]["to"]["index"] == p2.index);
    // INFO: the consumed skip names the stepped-over seat, which the client
    //       stamps an X over before the turn highlight moves.
    REQUIRE((*first_event)["payload"]["skipped"].is_array());
    REQUIRE((*first_event)["payload"]["skipped"].size() == 1);
    CHECK((*first_event)["payload"]["skipped"][0]["index"] == p1.index);
    CHECK((PendingRaw(h.store, p1) & kSkipFlag) == 0u);

    // INFO: the skip was one-shot; the next lap reaches p1 normally.
    const OpResult second = h.Invoke("advance_turn", json::object());
    const json* second_event = FindEvent(second, "turn_advance");
    REQUIRE(second_event != nullptr);
    CHECK((*second_event)["payload"]["to"]["index"] == p0.index);
    const OpResult third = h.Invoke("advance_turn", json::object());
    const json* third_event = FindEvent(third, "turn_advance");
    REQUIRE(third_event != nullptr);
    CHECK((*third_event)["payload"]["to"]["index"] == p1.index);
}

TEST_CASE("turn_flow_ops: all-skipped table falls back deterministically") {
    Harness h;
    AddMatch(h.store);
    Entity p0 = AddPlayer(h.store, "p0", 0);
    Entity p1 = AddPlayer(h.store, "p1", 1);
    Entity p2 = AddPlayer(h.store, "p2", 2);
    MakeCurrent(h.store, p0);
    h.Invoke("skip_turn", json::object(), {{"target", {p1}}});
    h.Invoke("skip_turn", json::object(), {{"target", {p2}}});

    const OpResult result = h.Invoke("advance_turn", json::object());
    CHECK(h.store.Get<TurnState>(p0)->is_current);
    CHECK((PendingRaw(h.store, p1) & kSkipFlag) == 0u);
    CHECK((PendingRaw(h.store, p2) & kSkipFlag) == 0u);
    const json* event = FindEvent(result, "turn_advance");
    REQUIRE(event != nullptr);
    CHECK((*event)["payload"]["to"]["index"] == p0.index);
    REQUIRE((*event)["payload"]["skipped"].is_array());
    CHECK((*event)["payload"]["skipped"].size() == 2);
}

TEST_CASE("turn_flow_ops: reverse_direction flips forward and reverse") {
    Harness h;
    Entity match = AddMatch(h.store, Direction::kForward);

    const OpResult first = h.Invoke("reverse_direction", json::object());
    CHECK(first.value["direction"] == -1);
    CHECK(h.store.Get<MatchMeta>(match)->direction == Direction::kReverse);

    const OpResult second = h.Invoke("reverse_direction", json::object());
    CHECK(second.value["direction"] == 1);
    CHECK(h.store.Get<MatchMeta>(match)->direction == Direction::kForward);
    CHECK(first.events.empty());
    CHECK(second.events.empty());
}

TEST_CASE("turn_flow_ops: extra_turn increments and preserves the skip bit") {
    Harness h;
    AddMatch(h.store);
    Entity p0 = AddPlayer(h.store, "p0", 0);
    AddPlayer(h.store, "p1", 1);

    const OpResult one =
        h.Invoke("extra_turn", json::object(), {{"target", {p0}}});
    CHECK(one.value["extra_turns_pending"] == 1);
    const OpResult two =
        h.Invoke("extra_turn", json::object(), {{"target", {p0}}});
    CHECK(two.value["extra_turns_pending"] == 2);
    CHECK((PendingRaw(h.store, p0) & kExtraMask) == 2u);

    // INFO: an armed skip survives an extra-turn increment.
    h.Invoke("skip_turn", json::object(), {{"target", {p0}}});
    h.Invoke("extra_turn", json::object(), {{"target", {p0}}});
    CHECK((PendingRaw(h.store, p0) & kSkipFlag) != 0u);
    CHECK((PendingRaw(h.store, p0) & kExtraMask) == 3u);
}

TEST_CASE("turn_flow_ops: redirect_turn jumps the turn to the target") {
    Harness h;
    AddMatch(h.store);
    Entity p0 = AddPlayer(h.store, "p0", 0);
    Entity p1 = AddPlayer(h.store, "p1", 1);
    Entity p2 = AddPlayer(h.store, "p2", 2);
    MakeCurrent(h.store, p0);

    const OpResult jump =
        h.Invoke("redirect_turn", json::object(), {{"target", {p2}}});
    CHECK(jump.status == OpStatus::kResolved);
    CHECK_FALSE(h.store.Get<TurnState>(p0)->is_current);
    CHECK_FALSE(h.store.Get<TurnState>(p1)->is_current);
    CHECK(h.store.Get<TurnState>(p2)->is_current);
    const json* event = FindEvent(jump, "turn_advance");
    REQUIRE(event != nullptr);
    CHECK((*event)["payload"]["from"]["index"] == p0.index);
    CHECK((*event)["payload"]["to"]["index"] == p2.index);

    // INFO: jumping onto the already-current player emits nothing.
    const OpResult same =
        h.Invoke("redirect_turn", json::object(), {{"target", {p2}}});
    CHECK(same.ok());
    CHECK(same.events.empty());
    CHECK(h.store.Get<TurnState>(p2)->is_current);
}

TEST_CASE("turn_flow_ops: set_turn_timer stores ms and rejects other units") {
    Harness h;
    AddMatch(h.store);
    Entity p0 = AddPlayer(h.store, "p0", 0);

    const OpResult ms = h.Invoke(
        "set_turn_timer",
        json{{"duration", {{"unit", "ms"}, {"value", 30000}}}},
        {{"target", {p0}}});
    CHECK(ms.status == OpStatus::kResolved);
    CHECK(ms.value["unit"] == "ms");
    CHECK(Deadline(h.store, p0) == 30000);

    // INFO: a relative non-ms leg has no absolute-ms slot; fail loud and leave
    //       the stored deadline untouched.
    const OpResult turns = h.Invoke(
        "set_turn_timer", json{{"duration", {{"unit", "turns"}, {"value", 2}}}},
        {{"target", {p0}}});
    CHECK(turns.status == OpStatus::kError);
    CHECK_FALSE(turns.error.empty());
    CHECK(Deadline(h.store, p0) == 30000);

    // INFO: a compound duration keeps its ms leg (the shortest ms leg) and is
    //       accepted.
    const OpResult compound = h.Invoke(
        "set_turn_timer",
        json{{"duration",
              json::array({{{"unit", "turns"}, {"value", 5}},
                           {{"unit", "ms"}, {"value", 12000}}})}},
        {{"target", {p0}}});
    CHECK(compound.status == OpStatus::kResolved);
    CHECK(compound.value["unit"] == "ms");
    CHECK(Deadline(h.store, p0) == 12000);

    // INFO: a compound with no ms leg is rejected like a single non-ms leg.
    const OpResult no_ms = h.Invoke(
        "set_turn_timer",
        json{{"duration",
              json::array({{{"unit", "turns"}, {"value", 1}},
                           {{"unit", "rounds"}, {"value", 9}}})}},
        {{"target", {p0}}});
    CHECK(no_ms.status == OpStatus::kError);
    CHECK(Deadline(h.store, p0) == 12000);
}

TEST_CASE("turn_flow_ops: fail-safe no-ops never crash") {
    Harness h;
    AddMatch(h.store);
    Entity p0 = AddPlayer(h.store, "p0", 0);
    Entity other = AddPlayer(h.store, "q", 1);

    // INFO: unbound and missing selector args.
    CHECK(h.Invoke("skip_turn", json::object()).status
          == OpStatus::kResolved);
    CHECK(h.Invoke("extra_turn", json::object()).status
          == OpStatus::kResolved);
    CHECK(h.Invoke("redirect_turn", json::object()).status
          == OpStatus::kResolved);
    CHECK(h.Invoke("set_turn_timer", json{{"duration", {{"unit", "ms"},
                                                       {"value", 1}}}})
              .status == OpStatus::kResolved);

    // INFO: dead and non-player targets are ignored.
    Entity dead = h.store.Create();
    h.store.Destroy(dead);
    CHECK(h.Invoke("skip_turn", json::object(), {{"target", {dead}}}).events
              .empty());
    Entity not_player = h.store.Create();
    CHECK(h.Invoke("extra_turn", json::object(), {{"target", {not_player}}})
              .status == OpStatus::kResolved);
    CHECK_FALSE(h.store.Has<TurnState>(not_player));

    // INFO: malformed durations leave the stored deadline untouched.
    CHECK(Deadline(h.store, p0) == 0);
    CHECK(h.Invoke("set_turn_timer",
                   json{{"duration", {{"unit", "eons"}, {"value", 5}}}},
                   {{"target", {p0}}})
              .events.empty());
    CHECK(h.Invoke("set_turn_timer", json::object(), {{"target", {p0}}})
              .events.empty());
    CHECK(h.Invoke("set_turn_timer",
                   json{{"duration", {{"unit", "ms"}, {"value", -1}}}},
                   {{"target", {p0}}})
              .events.empty());
    CHECK(Deadline(h.store, p0) == 0);

    // INFO: no match entity makes advance/reverse fail safe.
    EntityStore bare;
    EventBus bus;
    BudgetLedger ledger;
    ResolutionFrame frame;
    OpContext ctx(bus, ledger, frame);
    OpArgs advance("advance_turn", json::object());
    CHECK(MakeDefaultRuntime().Invoke("advance_turn", bare, advance, ctx).ok());
    OpArgs reverse("reverse_direction", json::object());
    CHECK(MakeDefaultRuntime().Invoke("reverse_direction", bare, reverse, ctx)
              .ok());

    // INFO: neither target was made current by any fail-safe path.
    CHECK_FALSE(h.store.Get<TurnState>(p0)->is_current);
    CHECK_FALSE(h.store.Get<TurnState>(other)->is_current);
}
