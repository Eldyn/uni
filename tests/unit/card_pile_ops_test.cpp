#include <doctest/doctest.h>

#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>
#include <match/status.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using match::ecs::ActiveTypeReq;
using match::ecs::BudgetLedger;
using match::ecs::CardIdentity;
using match::ecs::Entity;
using match::ecs::EntityStore;
using match::ecs::EventBus;
using match::ecs::FaceSpec;
using match::ecs::Hand;
using match::ecs::HookCallback;
using match::ecs::HookId;
using match::ecs::HookPayload;
using match::ecs::HookPhase;
using match::ecs::InZone;
using match::ecs::MatchMeta;
using match::ecs::PileContents;
using match::ecs::PileKind;
using match::ecs::PlayerInfo;
using match::ecs::RngState;
using match::ecs::StackPolicy;
using match::ecs::VisibilityGrant;
using match::ecs::ZoneKind;
using match::ecs::ZoneRef;
using match::ops::CardInZone;
using match::ops::CardKindId;
using match::ops::ClearCardTags;
using match::ops::HandOf;
using match::ops::MakeDefaultRuntime;
using match::ops::OpArgs;
using match::ops::OpContext;
using match::ops::OpResult;
using match::ops::OpRuntime;
using match::ops::OpStatus;
using match::ops::PileOf;
using match::ops::RegisterCardTags;
using match::ops::ResolutionFrame;

using nlohmann::json;

namespace {

Entity AddPlayer(EntityStore& store, const std::string& name, uint32_t seat) {
    Entity entity = store.Create();
    PlayerInfo info;
    info.username = name;
    info.seat = seat;
    store.Add(entity, info);
    store.Add(entity, Hand{});
    return entity;
}

Entity AddPile(EntityStore& store, PileKind kind) {
    Entity entity = store.Create();
    PileContents contents;
    contents.kind = kind;
    store.Add(entity, contents);
    return entity;
}

Entity AddMatch(EntityStore& store, uint64_t seed, uint64_t counter = 0) {
    Entity entity = store.Create();
    store.Add(entity, MatchMeta{});
    RngState rng;
    rng.seed = seed;
    rng.op_counter = counter;
    store.Add(entity, rng);
    return entity;
}

Entity MakeCard(EntityStore& store, const std::string& kind_id,
                const std::string& color) {
    Entity card = store.Create();
    CardIdentity identity;
    identity.kind_id = kind_id;
    store.Add(card, identity);
    store.Add(card, InZone{ZoneRef{ZoneKind::kLimbo, Entity{}}, 0});
    FaceSpec face;
    face.color = color;
    store.Add(card, face);
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

void PutInPile(EntityStore& store, Entity pile, Entity card) {
    PileContents* contents = store.Get<PileContents>(pile);
    REQUIRE(contents != nullptr);
    contents->cards.push_back(card);
    InZone* in = store.Get<InZone>(card);
    REQUIRE(in != nullptr);
    in->zone = ZoneRef{contents->kind == PileKind::kDraw
                           ? ZoneKind::kDrawPile
                           : ZoneKind::kDiscardPile,
                       Entity{}};
    in->ordinal = static_cast<uint32_t>(contents->cards.size() - 1);
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

}  // namespace

TEST_CASE("card_pile_ops: draw_cards moves n and fires per-card hooks") {
    Harness h;
    Entity player = AddPlayer(h.store, "p", 0);
    Entity draw = AddPile(h.store, PileKind::kDraw);
    Entity c1 = MakeCard(h.store, "vanilla:red_1", "red");
    Entity c2 = MakeCard(h.store, "vanilla:red_2", "red");
    Entity c3 = MakeCard(h.store, "vanilla:red_3", "red");
    PutInPile(h.store, draw, c1);
    PutInPile(h.store, draw, c2);
    PutInPile(h.store, draw, c3);

    int before_attempt = 0;
    int after_attempt = 0;
    int before_draw = 0;
    int after_draw = 0;
    h.On("draw_attempt", HookPhase::kBefore,
         [&](HookPayload&) { ++before_attempt; });
    h.On("draw_attempt", HookPhase::kAfter,
         [&](HookPayload&) { ++after_attempt; });
    h.On("draw", HookPhase::kBefore, [&](HookPayload&) { ++before_draw; });
    h.On("draw", HookPhase::kAfter, [&](HookPayload&) { ++after_draw; });

    const OpResult result =
        h.Invoke("draw_cards", json{{"n", 2}}, {{"target", {player}}});
    CHECK(result.status == OpStatus::kResolved);
    CHECK(HandOf(h.store, player) == std::vector<Entity>{c3, c2});
    CHECK(before_attempt == 2);
    CHECK(after_attempt == 2);
    CHECK(before_draw == 2);
    CHECK(after_draw == 2);

    REQUIRE(result.events.size() == 1);
    CHECK(result.events[0]["type"] == "cards_drawn");
    CHECK(result.events[0]["payload"]["count"] == 2);
    CHECK(result.events[0]["payload"]["source"] == "draw");
}

TEST_CASE("card_pile_ops: draw_cards bounds n=0 and n over 1000") {
    Harness h;
    Entity player = AddPlayer(h.store, "p", 0);
    Entity draw = AddPile(h.store, PileKind::kDraw);
    Entity c1 = MakeCard(h.store, "vanilla:red_1", "red");
    Entity c2 = MakeCard(h.store, "vanilla:red_2", "red");
    Entity c3 = MakeCard(h.store, "vanilla:red_3", "red");
    PutInPile(h.store, draw, c1);
    PutInPile(h.store, draw, c2);
    PutInPile(h.store, draw, c3);

    const OpResult zero =
        h.Invoke("draw_cards", json{{"n", 0}}, {{"target", {player}}});
    CHECK(zero.status == OpStatus::kResolved);
    CHECK(HandOf(h.store, player).empty());
    REQUIRE(zero.events.size() == 1);
    CHECK(zero.events[0]["payload"]["count"] == 0);

    const OpResult over =
        h.Invoke("draw_cards", json{{"n", 1001}}, {{"target", {player}}});
    CHECK(over.status == OpStatus::kResolved);
    CHECK(over.events.empty());
    CHECK(HandOf(h.store, player).empty());

    const OpResult at_max =
        h.Invoke("draw_cards", json{{"n", 1000}}, {{"target", {player}}});
    CHECK(HandOf(h.store, player).size() == 3);
    REQUIRE(at_max.events.size() == 1);
    CHECK(at_max.events[0]["payload"]["count"] == 3);
}

/* INFO: apply an accumulated `vanilla:draw_debt` status for n_from_debt. */
void ApplyDebt(EntityStore& store, Entity target, int32_t magnitude) {
    match::status::ApplyRequest request;
    request.status_id = std::string(match::ops::kDrawDebtStatusId);
    request.magnitude = magnitude;
    request.has_stack_policy = true;
    request.stack_policy = StackPolicy::kAccumulate;
    REQUIRE(match::status::Apply(store, target, request).applied);
}

TEST_CASE("card_pile_ops: draw_cards n_from_debt draws the debt magnitude") {
    Harness h;
    Entity player = AddPlayer(h.store, "p", 0);
    Entity draw = AddPile(h.store, PileKind::kDraw);
    Entity c1 = MakeCard(h.store, "vanilla:red_1", "red");
    Entity c2 = MakeCard(h.store, "vanilla:red_2", "red");
    Entity c3 = MakeCard(h.store, "vanilla:red_3", "red");
    PutInPile(h.store, draw, c1);
    PutInPile(h.store, draw, c2);
    PutInPile(h.store, draw, c3);
    ApplyDebt(h.store, player, 3);

    const OpResult result = h.Invoke(
        "draw_cards", json{{"n_from_debt", true}}, {{"target", {player}}});
    CHECK(result.status == OpStatus::kResolved);
    CHECK(HandOf(h.store, player).size() == 3);
    CHECK(result.value["drawn"] == 3);
    REQUIRE(result.events.size() == 1);
    CHECK(result.events[0]["payload"]["count"] == 3);
}

TEST_CASE("card_pile_ops: draw_cards n_from_debt with no debt is a no-op") {
    Harness h;
    Entity player = AddPlayer(h.store, "p", 0);
    Entity draw = AddPile(h.store, PileKind::kDraw);
    Entity c1 = MakeCard(h.store, "vanilla:red_1", "red");
    PutInPile(h.store, draw, c1);

    const OpResult result = h.Invoke(
        "draw_cards", json{{"n_from_debt", true}}, {{"target", {player}}});
    CHECK(result.status == OpStatus::kResolved);
    CHECK(HandOf(h.store, player).empty());
    REQUIRE(result.events.size() == 1);
    CHECK(result.events[0]["payload"]["count"] == 0);
}

TEST_CASE("card_pile_ops: draw_cards n_from_debt fail-safe cases") {
    Harness h;
    Entity player = AddPlayer(h.store, "p", 0);
    Entity draw = AddPile(h.store, PileKind::kDraw);
    Entity c1 = MakeCard(h.store, "vanilla:red_1", "red");
    PutInPile(h.store, draw, c1);

    /* INFO: a negative stored magnitude clamps to zero, never underflows. */
    ApplyDebt(h.store, player, -5);
    const OpResult negative = h.Invoke(
        "draw_cards", json{{"n_from_debt", true}}, {{"target", {player}}});
    CHECK(negative.status == OpStatus::kResolved);
    CHECK(HandOf(h.store, player).empty());

    /* INFO: a malformed (non-bool) arg is a fail-safe no-op. */
    const OpResult malformed = h.Invoke(
        "draw_cards", json{{"n_from_debt", "yes"}}, {{"target", {player}}});
    CHECK(malformed.status == OpStatus::kResolved);
    CHECK(malformed.events.empty());
    CHECK(HandOf(h.store, player).empty());
}

TEST_CASE("card_pile_ops: draw_cards iterates a set selector in order") {
    Harness h;
    Entity a = AddPlayer(h.store, "a", 0);
    Entity b = AddPlayer(h.store, "b", 1);
    Entity draw = AddPile(h.store, PileKind::kDraw);
    Entity c1 = MakeCard(h.store, "vanilla:red_1", "red");
    Entity c2 = MakeCard(h.store, "vanilla:red_2", "red");
    PutInPile(h.store, draw, c1);
    PutInPile(h.store, draw, c2);

    const OpResult result =
        h.Invoke("draw_cards", json{{"n", 1}}, {{"target", {a, b}}});
    CHECK(HandOf(h.store, a).size() == 1);
    CHECK(HandOf(h.store, b).size() == 1);
    REQUIRE(result.events.size() == 2);
    CHECK(result.events[0]["payload"]["player"]["index"] == a.index);
    CHECK(result.events[1]["payload"]["player"]["index"] == b.index);
    CHECK(result.value["drawn"] == 2);
}

TEST_CASE("card_pile_ops: draw_cards reshuffles discard when draw empties") {
    Harness h;
    Entity player = AddPlayer(h.store, "p", 0);
    Entity draw = AddPile(h.store, PileKind::kDraw);
    Entity discard = AddPile(h.store, PileKind::kDiscard);
    Entity d1 = MakeCard(h.store, "vanilla:red_1", "red");
    Entity d2 = MakeCard(h.store, "vanilla:red_2", "red");
    PutInPile(h.store, discard, d1);
    PutInPile(h.store, discard, d2);

    const OpResult result =
        h.Invoke("draw_cards", json{{"n", 1}}, {{"target", {player}}});
    REQUIRE(result.events.size() == 2);
    CHECK(result.events[0]["type"] == "reshuffle");
    CHECK(result.events[0]["payload"]["draw_size"] == 2);
    CHECK(result.events[1]["type"] == "cards_drawn");
    CHECK(result.events[1]["payload"]["count"] == 1);

    CHECK(HandOf(h.store, player) == std::vector<Entity>{d2});
    CHECK(PileOf(h.store, discard).empty());
    CHECK(PileOf(h.store, draw) == std::vector<Entity>{d1});
}

TEST_CASE("card_pile_ops: draw_cards honors a vetoed reshuffle") {
    Harness h;
    Entity player = AddPlayer(h.store, "p", 0);
    Entity draw = AddPile(h.store, PileKind::kDraw);
    Entity discard = AddPile(h.store, PileKind::kDiscard);
    Entity d1 = MakeCard(h.store, "vanilla:red_1", "red");
    Entity d2 = MakeCard(h.store, "vanilla:red_2", "red");
    PutInPile(h.store, discard, d1);
    PutInPile(h.store, discard, d2);

    h.On("pile_empty", HookPhase::kBefore,
         [](HookPayload& payload) { payload.veto = true; });

    const OpResult result =
        h.Invoke("draw_cards", json{{"n", 1}}, {{"target", {player}}});
    CHECK(FindEvent(result, "reshuffle") == nullptr);
    REQUIRE(result.events.size() == 1);
    CHECK(result.events[0]["payload"]["count"] == 0);
    CHECK(HandOf(h.store, player).empty());
    CHECK(PileOf(h.store, discard).size() == 2);
}

TEST_CASE("card_pile_ops: draw_cards filter selects a matching candidate") {
    Harness h;
    Entity player = AddPlayer(h.store, "p", 0);
    Entity draw = AddPile(h.store, PileKind::kDraw);
    Entity red_bottom = MakeCard(h.store, "vanilla:red_1", "red");
    Entity blue = MakeCard(h.store, "vanilla:blue_2", "blue");
    Entity red_top = MakeCard(h.store, "vanilla:red_3", "red");
    PutInPile(h.store, draw, red_bottom);
    PutInPile(h.store, draw, blue);
    PutInPile(h.store, draw, red_top);

    const OpResult result =
        h.Invoke("draw_cards", json{{"n", 1}, {"filter", {{"color", "blue"}}}},
                 {{"target", {player}}});
    CHECK(HandOf(h.store, player) == std::vector<Entity>{blue});
    CHECK(PileOf(h.store, draw) == std::vector<Entity>{red_bottom, red_top});
    REQUIRE(result.events.size() == 1);
    CHECK(result.events[0]["payload"]["count"] == 1);
}

TEST_CASE("card_pile_ops: draw_cards filter accepts a 10.2 condition") {
    Harness h;
    Entity player = AddPlayer(h.store, "p", 0);
    Entity draw = AddPile(h.store, PileKind::kDraw);
    Entity blue = MakeCard(h.store, "vanilla:blue_2", "blue");
    Entity red = MakeCard(h.store, "vanilla:red_5", "red");
    PutInPile(h.store, draw, blue);
    PutInPile(h.store, draw, red);

    const json filter = json{{"has_card_kind",
                              {{"target", "@card"}, {"kind", "red_5"}}}};
    const OpResult result =
        h.Invoke("draw_cards", json{{"n", 1}, {"filter", filter}},
                 {{"target", {player}}});
    CHECK(HandOf(h.store, player) == std::vector<Entity>{red});
    CHECK(PileOf(h.store, draw) == std::vector<Entity>{blue});
}

TEST_CASE("card_pile_ops: draw_cards rejects an unevaluable filter") {
    Harness h;
    Entity player = AddPlayer(h.store, "p", 0);
    Entity draw = AddPile(h.store, PileKind::kDraw);
    Entity c1 = MakeCard(h.store, "vanilla:red_1", "red");
    PutInPile(h.store, draw, c1);

    const OpResult result =
        h.Invoke("draw_cards", json{{"n", 1}, {"filter", 42}},
                 {{"target", {player}}});
    CHECK(result.events.empty());
    CHECK(HandOf(h.store, player).empty());
    CHECK(PileOf(h.store, draw).size() == 1);
}

TEST_CASE("card_pile_ops: move_card relocates and fires zone hooks") {
    Harness h;
    Entity a = AddPlayer(h.store, "a", 0);
    Entity discard = AddPile(h.store, PileKind::kDiscard);
    Entity card = MakeCard(h.store, "vanilla:red_1", "red");
    PutInHand(h.store, a, card);

    int left_before = 0;
    int left_after = 0;
    int entered_before = 0;
    int entered_after = 0;
    h.On("card_left_zone", HookPhase::kBefore,
         [&](HookPayload&) { ++left_before; });
    h.On("card_left_zone", HookPhase::kAfter,
         [&](HookPayload&) { ++left_after; });
    h.On("card_entered_zone", HookPhase::kBefore,
         [&](HookPayload&) { ++entered_before; });
    h.On("card_entered_zone", HookPhase::kAfter,
         [&](HookPayload&) { ++entered_after; });

    const OpResult result =
        h.Invoke("move_card", json{{"to_zone", "discard_pile"}},
                 {{"card", {card}}});
    CHECK(result.status == OpStatus::kResolved);
    CHECK(CardInZone(h.store, card, ZoneKind::kDiscardPile));
    CHECK(HandOf(h.store, a).empty());
    CHECK(PileOf(h.store, discard) == std::vector<Entity>{card});
    CHECK(left_before == 1);
    CHECK(left_after == 1);
    CHECK(entered_before == 1);
    CHECK(entered_after == 1);
}

TEST_CASE("card_pile_ops: move_card to hand uses the acting player") {
    Harness h;
    Entity a = AddPlayer(h.store, "a", 0);
    Entity discard = AddPile(h.store, PileKind::kDiscard);
    Entity card = MakeCard(h.store, "vanilla:red_1", "red");
    PutInPile(h.store, discard, card);

    // INFO: `@self` is a frame selector (bound by the Resolver), not an arg.
    h.frame.BindSelector("@self", {a});
    const OpResult result = h.Invoke(
        "move_card", json{{"to_zone", "hand"}}, {{"card", {card}}});
    CHECK(result.status == OpStatus::kResolved);
    CHECK(CardInZone(h.store, card, ZoneKind::kHand));
    CHECK(HandOf(h.store, a) == std::vector<Entity>{card});
    CHECK(PileOf(h.store, discard).empty());
}

TEST_CASE("card_pile_ops: transfer_card by random, tag and kind") {
    Harness h;
    AddMatch(h.store, 0x5EED1234ULL);
    Entity from = AddPlayer(h.store, "from", 0);
    Entity to = AddPlayer(h.store, "to", 1);
    Entity red = MakeCard(h.store, "vanilla:red_5", "red");
    Entity blue = MakeCard(h.store, "vanilla:blue_2", "blue");
    PutInHand(h.store, from, red);
    PutInHand(h.store, from, blue);

    ClearCardTags();
    RegisterCardTags("vanilla:red_5", {"stackable"});

    const OpResult by_tag = h.Invoke(
        "transfer_card", json{{"selector", "tag:stackable"}},
        {{"from_player", {from}}, {"to_player", {to}}});
    CHECK(by_tag.status == OpStatus::kResolved);
    CHECK(HandOf(h.store, to) == std::vector<Entity>{red});
    CHECK(HandOf(h.store, from) == std::vector<Entity>{blue});

    const OpResult by_kind = h.Invoke(
        "transfer_card", json{{"selector", "kind:blue_2"}},
        {{"from_player", {from}}, {"to_player", {to}}});
    CHECK(by_kind.status == OpStatus::kResolved);
    CHECK(HandOf(h.store, to) == std::vector<Entity>{red, blue});
    CHECK(HandOf(h.store, from).empty());

    Entity c3 = MakeCard(h.store, "vanilla:green_1", "green");
    PutInHand(h.store, from, c3);
    const OpResult by_random = h.Invoke(
        "transfer_card", json{{"selector", "random"}},
        {{"from_player", {from}}, {"to_player", {to}}});
    CHECK(by_random.status == OpStatus::kResolved);
    CHECK(HandOf(h.store, to) == std::vector<Entity>{red, blue, c3});
    CHECK(HandOf(h.store, from).empty());

    ClearCardTags();
}

TEST_CASE("card_pile_ops: transfer_card random draws from the shared stream") {
    const std::vector<std::string> kinds = {
        "vanilla:red_1", "vanilla:red_2", "vanilla:red_3", "vanilla:red_4"};
    // INFO: seed/counter chosen so counters 1 and 2 draw different indices.
    const uint64_t seed = 0x5EED1234ULL;

    auto pick_with_counter = [&](uint64_t counter) -> std::string {
        Harness h;
        AddMatch(h.store, seed, counter);
        Entity from = AddPlayer(h.store, "from", 0);
        Entity to = AddPlayer(h.store, "to", 1);
        for (const std::string& kind : kinds) {
            PutInHand(h.store, from, MakeCard(h.store, kind, "red"));
        }
        const OpResult result = h.Invoke(
            "transfer_card", json{{"selector", "random"}},
            {{"from_player", {from}}, {"to_player", {to}}});
        REQUIRE(result.status == OpStatus::kResolved);
        const std::vector<Entity> moved = HandOf(h.store, to);
        REQUIRE(moved.size() == 1);
        CHECK(CardInZone(h.store, moved[0], ZoneKind::kHand));
        const std::string moved_kind = CardKindId(h.store, moved[0]);
        // INFO: the draw stays in-bounds: one of the candidate kinds.
        CHECK(std::find(kinds.begin(), kinds.end(), moved_kind) != kinds.end());
        return moved_kind;
    };

    // INFO: identical seed + counter replay the identical selection...
    CHECK(pick_with_counter(0) == pick_with_counter(0));
    // INFO: ...and a different counter (fresh stream) can select differently.
    CHECK(pick_with_counter(0) != pick_with_counter(1));
}

TEST_CASE("card_pile_ops: transfer_card chosen opens a choose_card prompt") {
    Harness h;
    Entity from = AddPlayer(h.store, "from", 0);
    Entity to = AddPlayer(h.store, "to", 1);
    Entity card = MakeCard(h.store, "vanilla:red_5", "red");
    PutInHand(h.store, from, card);

    OpArgs args("transfer_card", json{{"selector", "chosen"}});
    args.BindSelector("from_player", {from});
    args.BindSelector("to_player", {to});
    OpContext ctx(h.bus, h.ledger, h.frame);
    const OpResult result =
        h.runtime.Invoke("transfer_card", h.store, args, ctx);

    CHECK(result.status == OpStatus::kNeedsInput);
    REQUIRE(ctx.input_request.has_value());
    CHECK(ctx.input_request->kind == "choose_card");
    REQUIRE(ctx.input_request->target.has_value());
    CHECK(*ctx.input_request->target == to);
    CHECK(result.value["kind"] == "choose_card");

    const VisibilityGrant* grant = h.store.Get<VisibilityGrant>(card);
    REQUIRE(grant != nullptr);
    REQUIRE(grant->entries.size() == 1);
    CHECK(grant->entries[0].viewer == to);

    REQUIRE(result.events.size() == 1);
    CHECK(result.events[0]["type"] == "visibility_granted");
    CHECK(result.events[0]["payload"]["viewer"]["index"] == to.index);
}

TEST_CASE("card_pile_ops: pass_hands rotates in both directions") {
    Harness h;
    Entity a = AddPlayer(h.store, "a", 0);
    Entity b = AddPlayer(h.store, "b", 1);
    Entity c = AddPlayer(h.store, "c", 2);
    Entity a1 = MakeCard(h.store, "vanilla:red_1", "red");
    Entity b1 = MakeCard(h.store, "vanilla:red_2", "red");
    Entity c1 = MakeCard(h.store, "vanilla:red_3", "red");
    PutInHand(h.store, a, a1);
    PutInHand(h.store, b, b1);
    PutInHand(h.store, c, c1);

    const OpResult forward =
        h.Invoke("pass_hands", json{{"direction", "forward"}});
    CHECK(forward.status == OpStatus::kResolved);
    CHECK(HandOf(h.store, a) == std::vector<Entity>{c1});
    CHECK(HandOf(h.store, b) == std::vector<Entity>{a1});
    CHECK(HandOf(h.store, c) == std::vector<Entity>{b1});

    // INFO: backward is the inverse of forward, restoring the start state.
    const OpResult backward =
        h.Invoke("pass_hands", json{{"direction", "backward"}});
    CHECK(backward.status == OpStatus::kResolved);
    CHECK(HandOf(h.store, a) == std::vector<Entity>{a1});
    CHECK(HandOf(h.store, b) == std::vector<Entity>{b1});
    CHECK(HandOf(h.store, c) == std::vector<Entity>{c1});
}

TEST_CASE("card_pile_ops: swap_hands exchanges contents") {
    Harness h;
    Entity a = AddPlayer(h.store, "a", 0);
    Entity b = AddPlayer(h.store, "b", 1);
    Entity a1 = MakeCard(h.store, "vanilla:red_1", "red");
    Entity a2 = MakeCard(h.store, "vanilla:red_2", "red");
    Entity b1 = MakeCard(h.store, "vanilla:blue_1", "blue");
    PutInHand(h.store, a, a1);
    PutInHand(h.store, a, a2);
    PutInHand(h.store, b, b1);

    const OpResult result = h.Invoke("swap_hands", json::object(),
                                     {{"a", {a}}, {"b", {b}}});
    CHECK(result.status == OpStatus::kResolved);
    CHECK(HandOf(h.store, a) == std::vector<Entity>{b1});
    CHECK(HandOf(h.store, b) == std::vector<Entity>{a1, a2});
}

TEST_CASE("card_pile_ops: redistribute_hands deals evenly by seat order") {
    Harness h;
    Entity a = AddPlayer(h.store, "a", 0);
    Entity b = AddPlayer(h.store, "b", 1);
    Entity c = AddPlayer(h.store, "c", 2);
    Entity a1 = MakeCard(h.store, "vanilla:red_1", "red");
    Entity b1 = MakeCard(h.store, "vanilla:red_2", "red");
    Entity b2 = MakeCard(h.store, "vanilla:red_3", "red");
    Entity c1 = MakeCard(h.store, "vanilla:red_4", "red");
    Entity c2 = MakeCard(h.store, "vanilla:red_5", "red");
    Entity c3 = MakeCard(h.store, "vanilla:blue_1", "blue");
    Entity c4 = MakeCard(h.store, "vanilla:blue_2", "blue");
    PutInHand(h.store, a, a1);
    PutInHand(h.store, b, b1);
    PutInHand(h.store, b, b2);
    PutInHand(h.store, c, c1);
    PutInHand(h.store, c, c2);
    PutInHand(h.store, c, c3);
    PutInHand(h.store, c, c4);

    const OpResult result =
        h.Invoke("redistribute_hands", json{{"mode", "even"}});
    CHECK(result.status == OpStatus::kResolved);
    // INFO: 7 cards / 3 seats = 2 each, remainder 1 to the first seat.
    CHECK(HandOf(h.store, a) == std::vector<Entity>{a1, b1, b2});
    CHECK(HandOf(h.store, b) == std::vector<Entity>{c1, c2});
    CHECK(HandOf(h.store, c) == std::vector<Entity>{c3, c4});
}

TEST_CASE("card_pile_ops: materialize_card creates a new hand instance") {
    Harness h;
    Entity player = AddPlayer(h.store, "p", 0);

    const OpResult result = h.Invoke(
        "materialize_card", json{{"kind", "space:black_hole"}},
        {{"target_player", {player}}});
    CHECK(result.status == OpStatus::kResolved);
    REQUIRE(HandOf(h.store, player).size() == 1);
    const Entity card = HandOf(h.store, player).front();
    CHECK(CardKindId(h.store, card) == "space:black_hole");
    CHECK(CardInZone(h.store, card, ZoneKind::kHand));
    CHECK(result.value["card"]["index"] == card.index);
}

TEST_CASE("card_pile_ops: remove_card sends a card to limbo") {
    Harness h;
    Entity player = AddPlayer(h.store, "p", 0);
    Entity card = MakeCard(h.store, "vanilla:red_1", "red");
    PutInHand(h.store, player, card);

    const OpResult result =
        h.Invoke("remove_card", json::object(), {{"card", {card}}});
    CHECK(result.status == OpStatus::kResolved);
    CHECK(CardInZone(h.store, card, ZoneKind::kLimbo));
    CHECK(HandOf(h.store, player).empty());
}

TEST_CASE("card_pile_ops: replace_card transmutes the kind") {
    Harness h;
    Entity player = AddPlayer(h.store, "p", 0);
    Entity card = MakeCard(h.store, "vanilla:red_5", "red");
    PutInHand(h.store, player, card);

    const OpResult result = h.Invoke(
        "replace_card", json{{"kind", "space:black_hole"}},
        {{"card", {card}}});
    CHECK(result.status == OpStatus::kResolved);
    CHECK(CardKindId(h.store, card) == "space:black_hole");
    CHECK(HandOf(h.store, player) == std::vector<Entity>{card});
}

TEST_CASE("card_pile_ops: peek_pile reveals top-n and emits an event") {
    Harness h;
    Entity viewer = AddPlayer(h.store, "v", 0);
    Entity draw = AddPile(h.store, PileKind::kDraw);
    Entity d1 = MakeCard(h.store, "vanilla:red_1", "red");
    Entity d2 = MakeCard(h.store, "vanilla:red_2", "red");
    Entity d3 = MakeCard(h.store, "vanilla:red_3", "red");
    PutInPile(h.store, draw, d1);
    PutInPile(h.store, draw, d2);
    PutInPile(h.store, draw, d3);

    const OpResult result = h.Invoke(
        "peek_pile", json{{"pile", "draw"}, {"n", 2}}, {{"viewer", {viewer}}});
    CHECK(result.status == OpStatus::kResolved);
    CHECK(result.value["revealed"] == 2);

    const VisibilityGrant* top = h.store.Get<VisibilityGrant>(d3);
    REQUIRE(top != nullptr);
    REQUIRE(top->entries.size() == 1);
    CHECK(top->entries[0].viewer == viewer);
    const VisibilityGrant* second = h.store.Get<VisibilityGrant>(d2);
    REQUIRE(second != nullptr);
    CHECK(second->entries.size() == 1);
    CHECK(h.store.Get<VisibilityGrant>(d1) == nullptr);

    REQUIRE(result.events.size() == 1);
    CHECK(result.events[0]["type"] == "visibility_granted");
    CHECK(result.events[0]["payload"]["count"] == 2);
}

TEST_CASE("card_pile_ops: fail-safe no-ops never crash") {
    Harness h;
    Entity player = AddPlayer(h.store, "p", 0);
    Entity other = AddPlayer(h.store, "q", 1);
    Entity draw = AddPile(h.store, PileKind::kDraw);
    Entity card = MakeCard(h.store, "vanilla:red_1", "red");
    PutInPile(h.store, draw, card);

    // INFO: unbound target and missing n.
    CHECK(h.Invoke("draw_cards", json{{"n", 1}}).events.empty());
    CHECK(h.Invoke("draw_cards", json::object(), {{"target", {player}}})
              .events.empty());

    // INFO: dead entity and unbound card.
    Entity dead = h.store.Create();
    h.store.Destroy(dead);
    CHECK(h.Invoke("move_card", json{{"to_zone", "limbo"}}, {{"card", {dead}}})
              .status == OpStatus::kResolved);
    CHECK(h.Invoke("move_card", json{{"to_zone", "limbo"}}).status
          == OpStatus::kResolved);

    // INFO: missing selector and invalid direction.
    CHECK(h.Invoke("transfer_card", json::object(),
                   {{"from_player", {player}}, {"to_player", {other}}})
              .status == OpStatus::kResolved);
    CHECK(h.Invoke("pass_hands", json{{"direction", "sideways"}}).status
          == OpStatus::kResolved);

    // INFO: out-of-bounds peek and empty materialize kind.
    CHECK(h.Invoke("peek_pile", json{{"pile", "draw"}, {"n", 1001}},
                   {{"viewer", {player}}})
              .events.empty());
    CHECK(h.Invoke("materialize_card", json{{"kind", ""}},
                   {{"target_player", {player}}})
              .status == OpStatus::kResolved);

    // INFO: unbound replace and unbound remove.
    CHECK(h.Invoke("replace_card", json{{"kind", "x"}}).status
          == OpStatus::kResolved);
    CHECK(h.Invoke("remove_card", json::object()).status
          == OpStatus::kResolved);

    // INFO: state is untouched by every fail-safe path.
    CHECK(PileOf(h.store, draw) == std::vector<Entity>{card});
    CHECK(HandOf(h.store, player).empty());
}

TEST_CASE("card_pile_ops: play_card emits a play effect for the hand owner") {
    Harness h;
    Entity player = AddPlayer(h.store, "p", 0);
    Entity other = AddPlayer(h.store, "o", 1);
    Entity card = MakeCard(h.store, "vanilla:red_5", "red");
    PutInHand(h.store, player, card);

    // INFO: no explicit player -> the card's hand owner is the actor.
    const OpResult result =
        h.Invoke("play_card", json::object(), {{"card", {card}}});
    CHECK(result.status == OpStatus::kResolved);
    REQUIRE(result.effects.size() == 1);
    CHECK(result.effects[0]["type"] == "play_card");
    CHECK(result.effects[0]["payload"]["card"]["index"] == card.index);
    CHECK(result.effects[0]["payload"]["player"]["index"] == player.index);
    // INFO: the op never mutates the store itself; the engine routes the play.
    CHECK(HandOf(h.store, player) == std::vector<Entity>{card});

    // INFO: an explicit player selector overrides the hand owner.
    const OpResult explicit_player =
        h.Invoke("play_card", json::object(),
                 {{"card", {card}}, {"player", {other}}});
    REQUIRE(explicit_player.effects.size() == 1);
    CHECK(explicit_player.effects[0]["payload"]["player"]["index"]
          == other.index);
}

TEST_CASE("card_pile_ops: play_card fail-safe paths emit no effect") {
    Harness h;
    Entity player = AddPlayer(h.store, "p", 0);
    Entity card = MakeCard(h.store, "vanilla:red_5", "red");
    PutInHand(h.store, player, card);

    // INFO: unbound card.
    CHECK(h.Invoke("play_card", json::object()).effects.empty());

    // INFO: dead card.
    Entity dead = h.store.Create();
    h.store.Destroy(dead);
    CHECK(h.Invoke("play_card", json::object(), {{"card", {dead}}})
              .effects.empty());

    // INFO: a card with no in_zone is refused.
    Entity orphan = h.store.Create();
    CardIdentity identity;
    identity.kind_id = "vanilla:red_5";
    h.store.Add(orphan, identity);
    CHECK(h.Invoke("play_card", json::object(), {{"card", {orphan}}})
              .effects.empty());

    // INFO: an explicit non-player actor is refused.
    Entity not_player = h.store.Create();
    CHECK(h.Invoke("play_card", json::object(),
                   {{"card", {card}}, {"player", {not_player}}})
              .effects.empty());

    CHECK(HandOf(h.store, player) == std::vector<Entity>{card});
}

/* INFO: bind a match entity carrying the required active type, so the
 *       `drawn_card_playable` predicate has colour facts to compare. */
void AddActiveType(Harness& h, const std::string& type) {
    Entity match = AddMatch(h.store, 1);
    ActiveTypeReq req;
    req.type = type;
    h.store.Add(match, req);
}

TEST_CASE("card_pile_ops: draw_until_playable stops at the first playable") {
    Harness h;
    Entity player = AddPlayer(h.store, "p", 0);
    Entity draw = AddPile(h.store, PileKind::kDraw);
    AddActiveType(h, "red");

    Entity untouched = MakeCard(h.store, "vanilla:red_4", "red");
    Entity playable = MakeCard(h.store, "vanilla:red_3", "red");
    Entity unplayable = MakeCard(h.store, "vanilla:blue_9", "blue");
    h.store.Get<FaceSpec>(unplayable)->label = "9";
    // INFO: pile order bottom -> top: untouched, playable, unplayable. The op
    //       draws blue (unplayable), then red (playable) and stops; red_4
    //       stays buried.
    PutInPile(h.store, draw, untouched);
    PutInPile(h.store, draw, playable);
    PutInPile(h.store, draw, unplayable);

    const OpResult result =
        h.Invoke("draw_until_playable", json::object(), {{"target", {player}}});
    CHECK(result.status == OpStatus::kResolved);
    CHECK(HandOf(h.store, player) == std::vector<Entity>{unplayable, playable});
    CHECK(PileOf(h.store, draw) == std::vector<Entity>{untouched});
    CHECK(result.value["drawn"] == 2);
    REQUIRE(result.events.size() == 1);
    CHECK(result.events[0]["type"] == "cards_drawn");
    CHECK(result.events[0]["payload"]["count"] == 2);
    CHECK(result.events[0]["payload"]["source"] == "draw");
}

TEST_CASE("card_pile_ops: draw_until_playable exhausts a dry pile") {
    Harness h;
    Entity player = AddPlayer(h.store, "p", 0);
    Entity draw = AddPile(h.store, PileKind::kDraw);
    AddActiveType(h, "red");

    Entity a = MakeCard(h.store, "vanilla:blue_1", "blue");
    Entity b = MakeCard(h.store, "vanilla:green_2", "green");
    PutInPile(h.store, draw, a);
    PutInPile(h.store, draw, b);

    const OpResult result =
        h.Invoke("draw_until_playable", json::object(), {{"target", {player}}});
    CHECK(result.status == OpStatus::kResolved);
    CHECK(HandOf(h.store, player).size() == 2);
    CHECK(PileOf(h.store, draw).empty());
    CHECK(result.value["drawn"] == 2);
    REQUIRE(result.events.size() == 1);
    CHECK(result.events[0]["payload"]["count"] == 2);
}

TEST_CASE("card_pile_ops: draw_until_playable reshuffles to reach one") {
    Harness h;
    Entity player = AddPlayer(h.store, "p", 0);
    Entity draw = AddPile(h.store, PileKind::kDraw);
    Entity discard = AddPile(h.store, PileKind::kDiscard);
    AddActiveType(h, "red");

    Entity unplayable = MakeCard(h.store, "vanilla:blue_1", "blue");
    Entity playable = MakeCard(h.store, "vanilla:red_2", "red");
    PutInPile(h.store, draw, unplayable);
    PutInPile(h.store, discard, playable);

    const OpResult result =
        h.Invoke("draw_until_playable", json::object(), {{"target", {player}}});
    CHECK(result.status == OpStatus::kResolved);
    CHECK(HandOf(h.store, player) == std::vector<Entity>{unplayable, playable});
    CHECK(result.value["drawn"] == 2);
    REQUIRE(result.events.size() == 2);
    CHECK(result.events[0]["type"] == "reshuffle");
    CHECK(result.events[0]["payload"]["draw_size"] == 1);
    CHECK(result.events[1]["type"] == "cards_drawn");
    CHECK(result.events[1]["payload"]["count"] == 2);
}

TEST_CASE("card_pile_ops: draw_until_playable honors an explicit from pile") {
    Harness h;
    Entity player = AddPlayer(h.store, "p", 0);
    Entity draw = AddPile(h.store, PileKind::kDraw);
    Entity discard = AddPile(h.store, PileKind::kDiscard);
    AddActiveType(h, "red");

    Entity untouched = MakeCard(h.store, "vanilla:red_4", "red");
    Entity unplayable = MakeCard(h.store, "vanilla:blue_1", "blue");
    Entity playable = MakeCard(h.store, "vanilla:red_2", "red");
    PutInPile(h.store, draw, untouched);
    // INFO: discard top (back) is the unplayable card, so it is drawn first.
    PutInPile(h.store, discard, playable);
    PutInPile(h.store, discard, unplayable);

    const OpResult result = h.Invoke(
        "draw_until_playable", json{{"from", "discard"}},
        {{"target", {player}}});
    CHECK(result.status == OpStatus::kResolved);
    CHECK(HandOf(h.store, player) == std::vector<Entity>{unplayable, playable});
    CHECK(PileOf(h.store, draw) == std::vector<Entity>{untouched});
    CHECK(result.value["drawn"] == 2);
    REQUIRE(result.events.size() == 1);
    CHECK(result.events[0]["payload"]["source"] == "discard");
}

TEST_CASE("card_pile_ops: draw_until_playable fail-safe targets and from") {
    Harness h;
    Entity draw = AddPile(h.store, PileKind::kDraw);
    Entity card = MakeCard(h.store, "vanilla:red_1", "red");
    PutInPile(h.store, draw, card);
    Entity not_a_player = h.store.Create();

    // INFO: unbound target -> no targets -> no-op, no events.
    const OpResult unbound =
        h.Invoke("draw_until_playable", json::object());
    CHECK(unbound.status == OpStatus::kResolved);
    CHECK(unbound.events.empty());
    CHECK(PileOf(h.store, draw).size() == 1);

    // INFO: a live non-hand carrier is skipped.
    const OpResult non_player = h.Invoke(
        "draw_until_playable", json::object(), {{"target", {not_a_player}}});
    CHECK(non_player.status == OpStatus::kResolved);
    CHECK(non_player.events.empty());
    CHECK(PileOf(h.store, draw).size() == 1);

    // INFO: a dead target is skipped.
    Entity dead = h.store.Create();
    h.store.Destroy(dead);
    const OpResult dead_result = h.Invoke(
        "draw_until_playable", json::object(), {{"target", {dead}}});
    CHECK(dead_result.status == OpStatus::kResolved);
    CHECK(dead_result.events.empty());
    CHECK(PileOf(h.store, draw).size() == 1);

    // INFO: an unknown `from` token is a fail-safe no-op.
    Entity player = AddPlayer(h.store, "p", 0);
    const OpResult bad_from = h.Invoke(
        "draw_until_playable", json{{"from", "limbo"}}, {{"target", {player}}});
    CHECK(bad_from.status == OpStatus::kResolved);
    CHECK(bad_from.events.empty());
    CHECK(HandOf(h.store, player).empty());
    CHECK(PileOf(h.store, draw).size() == 1);
}
