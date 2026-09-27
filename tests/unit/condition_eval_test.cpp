#include <doctest/doctest.h>

#include <match/engine/match_assembler.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/resolver.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <vector>

using match::ecs::ActiveTypeReq;
using match::ecs::BudgetLedger;
using match::ecs::CardIdentity;
using match::ecs::Direction;
using match::ecs::Entity;
using match::ecs::EntityStore;
using match::ecs::EventBus;
using match::ecs::FaceSpec;
using match::ecs::Hand;
using match::ecs::InZone;
using match::ecs::MatchMeta;
using match::ecs::PileContents;
using match::ecs::PileKind;
using match::ecs::PlayerInfo;
using match::ecs::Status;
using match::ecs::StatusList;
using match::ecs::ZoneKind;
using match::ecs::ZoneRef;
using match::engine::MatchRegistries;
using match::modload::ConditionCatalog;
using match::ops::BindLastRoll;
using match::ops::BindTurnsElapsed;
using match::ops::OpContext;
using match::ops::RegisterDefaultConditions;
using match::ops::ResolutionFrame;
using match::resolver::ConditionRegistry;

using nlohmann::json;

namespace {

json Cond(const std::string& keyword, json args) {
    return json{{keyword, std::move(args)}};
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

void AddToHand(EntityStore& store, Entity player, Entity card) {
    Hand* hand = store.Get<Hand>(player);
    REQUIRE(hand != nullptr);
    hand->cards.push_back(card);
    InZone* in = store.Get<InZone>(card);
    REQUIRE(in != nullptr);
    in->zone = ZoneRef{ZoneKind::kHand, player};
    in->ordinal = static_cast<uint32_t>(hand->cards.size() - 1);
}

void AddToPile(EntityStore& store, Entity pile, Entity card) {
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

/** Minimal store + registry with no match/players (fail-safe probes). */
struct Harness {
    EntityStore store;
    BudgetLedger ledger;
    EventBus bus;
    ResolutionFrame frame;
    ConditionRegistry conditions;
    MatchRegistries registries;
    OpContext ctx;

    Harness() : ctx(bus, ledger, frame) {
        ctx.registries = &registries;
        RegisterDefaultConditions(conditions);
    }

    bool Eval(const json& condition) {
        return conditions.Evaluate(store, condition, ctx);
    }
};

/** Fully populated match: 2 players, piles, statuses, debt, active type. */
struct Fixture : Harness {
    Entity match;
    Entity self;
    Entity target;
    Entity draw;
    Entity discard;
    Entity self_red;
    Entity self_blue;
    Entity target_green;
    Entity discard_top;

    Fixture() {
        match = store.Create();
        MatchMeta meta;
        meta.round = 3;
        meta.direction = Direction::kForward;
        store.Add(match, meta);
        ActiveTypeReq req;
        req.type = "red";
        store.Add(match, req);

        self = AddPlayer(store, "self", 0);
        target = AddPlayer(store, "target", 1);
        draw = store.Create();
        PileContents draw_pile;
        draw_pile.kind = PileKind::kDraw;
        store.Add(draw, draw_pile);
        discard = store.Create();
        PileContents discard_pile;
        discard_pile.kind = PileKind::kDiscard;
        store.Add(discard, discard_pile);

        self_red = MakeCard(store, "vanilla:red_5", "red");
        self_blue = MakeCard(store, "vanilla:blue_2", "blue");
        target_green = MakeCard(store, "vanilla:green_1", "green");
        discard_top = MakeCard(store, "vanilla:red_7", "red");
        AddToHand(store, self, self_red);
        AddToHand(store, self, self_blue);
        AddToHand(store, target, target_green);
        AddToPile(store, draw, MakeCard(store, "vanilla:yellow_0", "yellow"));
        AddToPile(store, discard, discard_top);

        Status status;
        status.status_id = "vanilla:draw_debt";
        status.magnitude = 2;
        StatusList statuses;
        statuses.instances.push_back(status);
        store.Add(self, statuses);

        frame.BindSelector("@self", {self});
        frame.BindSelector("@target", {target});
        frame.BindSelector("@all_players", {self, target});

        registries.card_tags = {{"vanilla:red_5", {"stackable"}},
                                {"vanilla:red_7", {"stackable"}}};
    }
};

}  // namespace

TEST_CASE("condition_eval: every catalog keyword is registered") {
    Fixture fixture;
    std::size_t store_conditions = 0;
    std::size_t play_conditions = 0;
    for (const auto& signature : ConditionCatalog()) {
        CHECK(fixture.conditions.Has(signature.keyword));
        if (signature.play_context) {
            ++play_conditions;
        } else {
            ++store_conditions;
        }
    }
    CHECK(store_conditions == 16);
    CHECK(play_conditions == 13);
    CHECK(ConditionCatalog().size() == 29);
    CHECK_FALSE(fixture.conditions.Has("no_such_condition"));
}

TEST_CASE("condition_eval: play-context keywords are fail-safe false") {
    // INFO: the store-domain resolver never carries a PlayAttempt, so every
    //       play-context predicate is bound to false there.
    Harness bare;
    for (const auto& signature : ConditionCatalog()) {
        if (!signature.play_context) continue;
        CHECK_FALSE(bare.Eval(json{{signature.keyword, json::object()}}));
    }
}

TEST_CASE("condition_eval: has_card_kind") {
    Fixture f;
    CHECK(f.Eval(Cond("has_card_kind",
                      {{"target", "@self"}, {"kind", "vanilla:red_5"}})));
    CHECK(f.Eval(Cond("has_card_kind",
                      {{"target", "@self"}, {"kind", "red_5"}})));
    CHECK(f.Eval(Cond("has_card_kind",
                      {{"target", "@target"}, {"kind", "green_1"}})));
    CHECK_FALSE(f.Eval(Cond(
        "has_card_kind", {{"target", "@self"}, {"kind", "vanilla:red_9"}})));
    CHECK_FALSE(f.Eval(Cond(
        "has_card_kind", {{"target", "@nowhere"}, {"kind", "vanilla:red_5"}})));
    CHECK_FALSE(f.Eval(Cond("has_card_kind", {{"target", "@self"}})));
    CHECK_FALSE(f.Eval(Cond("has_card_kind", json::array())));
}

TEST_CASE("condition_eval: has_card_tag") {
    Fixture f;
    CHECK(f.Eval(
        Cond("has_card_tag", {{"target", "@self"}, {"tag", "stackable"}})));
    CHECK_FALSE(
        f.Eval(Cond("has_card_tag", {{"target", "@self"}, {"tag", "ghost"}})));
    CHECK_FALSE(f.Eval(
        Cond("has_card_tag", {{"target", "@nowhere"}, {"tag", "stackable"}})));
    CHECK_FALSE(f.Eval(Cond("has_card_tag", {{"target", "@self"}})));

    // INFO: Another match's registries must not change this
    //       match's tag semantics (the old global table was clobbered by the
    //       most recent assembly).
    MatchRegistries other;
    other.card_tags["vanilla:red_5"] = {"ghost"};
    f.ctx.registries = &other;
    CHECK_FALSE(f.Eval(
        Cond("has_card_tag", {{"target", "@self"}, {"tag", "stackable"}})));
    CHECK(f.Eval(
        Cond("has_card_tag", {{"target", "@self"}, {"tag", "ghost"}})));
    f.ctx.registries = &f.registries;
    CHECK(f.Eval(
        Cond("has_card_tag", {{"target", "@self"}, {"tag", "stackable"}})));
}

TEST_CASE("condition_eval: hand_size comparisons and fail-safe paths") {
    Fixture f;
    CHECK(f.Eval(Cond("hand_size",
                      {{"target", "@self"}, {"cmp", "eq"}, {"n", 2}})));
    CHECK(f.Eval(Cond("hand_size",
                      {{"target", "@self"}, {"cmp", "gte"}, {"n", 2}})));
    CHECK_FALSE(f.Eval(Cond("hand_size",
                            {{"target", "@self"}, {"cmp", "gt"}, {"n", 2}})));
    CHECK_FALSE(f.Eval(Cond("hand_size",
                            {{"target", "@self"}, {"cmp", "lt"}, {"n", 2}})));
    CHECK(f.Eval(Cond("hand_size",
                      {{"target", "@target"}, {"cmp", "lt"}, {"n", 2}})));
    // INFO: symbol spellings from ComparisonTokens() resolve too.
    CHECK(f.Eval(Cond("hand_size",
                      {{"target", "@self"}, {"cmp", "=="}, {"n", 2}})));
    CHECK_FALSE(f.Eval(Cond("hand_size",
                            {{"target", "@self"}, {"cmp", "bogus"},
                             {"n", 2}})));
    CHECK_FALSE(f.Eval(Cond("hand_size",
                            {{"target", "@nowhere"}, {"cmp", "eq"},
                             {"n", 2}})));
    CHECK_FALSE(f.Eval(Cond("hand_size",
                            {{"target", "@self"}, {"cmp", "eq"}})));
}

TEST_CASE("condition_eval: player_count comparisons and fail-safe paths") {
    Fixture f;
    CHECK(f.Eval(Cond("player_count", {{"cmp", "eq"}, {"n", 2}})));
    CHECK(f.Eval(Cond("player_count", {{"cmp", "gte"}, {"n", 2}})));
    CHECK(f.Eval(Cond("player_count", {{"cmp", "lt"}, {"n", 3}})));
    CHECK_FALSE(f.Eval(Cond("player_count", {{"cmp", "gt"}, {"n", 2}})));
    CHECK_FALSE(f.Eval(Cond("player_count", {{"cmp", "ne"}, {"n", 2}})));
    // INFO: missing / malformed args are a fail-safe false.
    CHECK_FALSE(f.Eval(Cond("player_count", {{"cmp", "eq"}})));
    CHECK_FALSE(f.Eval(Cond("player_count", json::object())));

    Harness bare;
    CHECK_FALSE(bare.Eval(Cond("player_count", {{"cmp", "eq"}, {"n", 2}})));
    CHECK(bare.Eval(Cond("player_count", {{"cmp", "eq"}, {"n", 0}})));
}

TEST_CASE("condition_eval: drawn_card_playable") {
    Fixture f;
    // INFO: the Fixture's active type is red and its discard top is red_7.
    f.store.Get<FaceSpec>(f.discard_top)->label = "7";

    // No `@drawn_card` bound -> fail-safe false.
    CHECK_FALSE(f.Eval(Cond("drawn_card_playable", json::object())));

    // Colour matches the active type.
    Entity red = MakeCard(f.store, "vanilla:red_3", "red");
    f.frame.BindSelector("@drawn_card", {red});
    CHECK(f.Eval(Cond("drawn_card_playable", json::object())));

    // Value matches the discard top (blue_7 vs red_7), colour does not.
    Entity blue_seven = MakeCard(f.store, "vanilla:blue_7", "blue");
    f.store.Get<FaceSpec>(blue_seven)->label = "7";
    f.frame.BindSelector("@drawn_card", {blue_seven});
    CHECK(f.Eval(Cond("drawn_card_playable", json::object())));

    // Neither colour nor value matches -> not playable.
    Entity blue_two = MakeCard(f.store, "vanilla:blue_2", "blue");
    f.store.Get<FaceSpec>(blue_two)->label = "2";
    f.frame.BindSelector("@drawn_card", {blue_two});
    CHECK_FALSE(f.Eval(Cond("drawn_card_playable", json::object())));

    // Wild (white face) is always playable.
    Entity wild = MakeCard(f.store, "vanilla:wild", "white");
    f.frame.BindSelector("@drawn_card", {wild});
    CHECK(f.Eval(Cond("drawn_card_playable", json::object())));

    // A dead drawn card is false.
    Entity dead = f.store.Create();
    f.frame.BindSelector("@drawn_card", {dead});
    CHECK_FALSE(f.Eval(Cond("drawn_card_playable", json::object())));
}

TEST_CASE("condition_eval: active_type_is") {
    Fixture f;
    CHECK(f.Eval(Cond("active_type_is", {{"type", "red"}})));
    CHECK_FALSE(f.Eval(Cond("active_type_is", {{"type", "blue"}})));
    CHECK_FALSE(f.Eval(Cond("active_type_is", json::object())));

    Harness bare;
    CHECK_FALSE(bare.Eval(Cond("active_type_is", {{"type", "red"}})));

    Fixture cleared;
    cleared.store.Get<ActiveTypeReq>(cleared.match)->type.reset();
    CHECK_FALSE(cleared.Eval(Cond("active_type_is", {{"type", "red"}})));
}

TEST_CASE("condition_eval: top_of_discard kind, tag and type") {
    Fixture f;
    CHECK(f.Eval(Cond("top_of_discard", {{"kind", "vanilla:red_7"}})));
    CHECK(f.Eval(Cond("top_of_discard", {{"kind", "red_7"}})));
    CHECK(f.Eval(Cond("top_of_discard", {{"tag", "stackable"}})));
    CHECK(f.Eval(Cond("top_of_discard", {{"type", "red"}})));
    CHECK_FALSE(f.Eval(Cond("top_of_discard", {{"type", "blue"}})));
    CHECK_FALSE(f.Eval(Cond("top_of_discard", {{"kind", "vanilla:blue_2"}})));
    CHECK_FALSE(f.Eval(Cond("top_of_discard", {{"tag", "ghost"}})));
    CHECK_FALSE(f.Eval(Cond("top_of_discard", json::object())));

    Harness bare;
    CHECK_FALSE(bare.Eval(Cond("top_of_discard", {{"kind", "red_7"}})));
}

TEST_CASE("condition_eval: status_active") {
    Fixture f;
    CHECK(f.Eval(Cond(
        "status_active",
        {{"target", "@self"}, {"status_kind", "vanilla:draw_debt"}})));
    CHECK_FALSE(f.Eval(Cond(
        "status_active",
        {{"target", "@self"}, {"status_kind", "vanilla:shielded"}})));
    CHECK_FALSE(f.Eval(Cond(
        "status_active",
        {{"target", "@nowhere"}, {"status_kind", "vanilla:draw_debt"}})));
    CHECK_FALSE(f.Eval(Cond("status_active", {{"target", "@self"}})));

    // INFO: a second, independent instance of another kind also satisfies the
    //       condition (the multi-instance status_list container).
    StatusList* statuses = f.store.Get<StatusList>(f.self);
    REQUIRE(statuses != nullptr);
    Status shielded;
    shielded.status_id = "vanilla:shielded";
    statuses->instances.push_back(shielded);
    CHECK(f.Eval(Cond(
        "status_active",
        {{"target", "@self"}, {"status_kind", "vanilla:shielded"}})));
    CHECK(f.Eval(Cond(
        "status_active",
        {{"target", "@self"}, {"status_kind", "vanilla:draw_debt"}})));
}

TEST_CASE("condition_eval: draw_debt comparisons") {
    Fixture f;
    CHECK(f.Eval(Cond("draw_debt",
                      {{"target", "@self"}, {"cmp", "eq"}, {"n", 2}})));
    CHECK(f.Eval(Cond("draw_debt",
                      {{"target", "@self"}, {"cmp", "gt"}, {"n", 1}})));
    CHECK_FALSE(f.Eval(Cond("draw_debt",
                            {{"target", "@self"}, {"cmp", "lt"}, {"n", 2}})));
    CHECK_FALSE(f.Eval(Cond("draw_debt",
                            {{"target", "@target"}, {"cmp", "eq"},
                             {"n", 2}})));
    CHECK_FALSE(f.Eval(Cond("draw_debt", {{"target", "@self"}, {"n", 2}})));
}

TEST_CASE("condition_eval: rolled reads the frame's last roll") {
    Fixture f;
    CHECK_FALSE(f.Eval(Cond("rolled", {{"cmp", "eq"}, {"n", 7}})));
    BindLastRoll(f.frame, 7, json::array({7}));
    CHECK(f.Eval(Cond("rolled", {{"cmp", "eq"}, {"n", 7}})));
    CHECK(f.Eval(Cond("rolled", {{"cmp", "gte"}, {"n", 7}})));
    CHECK_FALSE(f.Eval(Cond("rolled", {{"cmp", "gt"}, {"n", 7}})));
    CHECK_FALSE(f.Eval(Cond("rolled", {{"cmp", "eq"}})));
}

TEST_CASE("condition_eval: is_direction") {
    Fixture f;
    CHECK(f.Eval(Cond("is_direction", {{"direction", "fwd"}})));
    CHECK_FALSE(f.Eval(Cond("is_direction", {{"direction", "rev"}})));
    CHECK_FALSE(f.Eval(Cond("is_direction", {{"direction", "sideways"}})));
    CHECK_FALSE(f.Eval(Cond("is_direction", json::object())));

    f.store.Get<MatchMeta>(f.match)->direction = Direction::kReverse;
    CHECK(f.Eval(Cond("is_direction", {{"direction", "rev"}})));

    Harness bare;
    CHECK_FALSE(bare.Eval(Cond("is_direction", {{"direction", "fwd"}})));
}

TEST_CASE("condition_eval: round and turns_elapsed counters") {
    Fixture f;
    CHECK(f.Eval(Cond("round", {{"cmp", "eq"}, {"n", 3}})));
    CHECK(f.Eval(Cond("round", {{"cmp", "gte"}, {"n", 3}})));
    CHECK_FALSE(f.Eval(Cond("round", {{"cmp", "ne"}, {"n", 3}})));
    CHECK_FALSE(f.Eval(Cond("round", {{"n", 3}})));

    CHECK_FALSE(f.Eval(Cond("turns_elapsed", {{"cmp", "eq"}, {"n", 4}})));
    BindTurnsElapsed(f.frame, 4);
    CHECK(f.Eval(Cond("turns_elapsed", {{"cmp", "eq"}, {"n", 4}})));
    CHECK(f.Eval(Cond("turns_elapsed", {{"cmp", "lte"}, {"n", 4}})));
    CHECK_FALSE(f.Eval(Cond("turns_elapsed", {{"cmp", "lt"}, {"n", 4}})));

    Harness bare;
    CHECK_FALSE(bare.Eval(Cond("round", {{"cmp", "eq"}, {"n", 0}})));
}

TEST_CASE("condition_eval: always, never and literal booleans") {
    Fixture f;
    CHECK(f.Eval(Cond("always", json::object())));
    CHECK_FALSE(f.Eval(Cond("never", json::object())));
    CHECK(f.Eval(json(true)));
    CHECK_FALSE(f.Eval(json(false)));
}

TEST_CASE("condition_eval: malformed input is a fail-safe false") {
    Fixture f;
    CHECK_FALSE(f.Eval(Cond("has_card_kind", json::array())));
    CHECK_FALSE(f.Eval(Cond("hand_size", json::array())));
    CHECK_FALSE(f.Eval(Cond("has_card_tag", json::array())));
    CHECK_FALSE(f.Eval(Cond("status_active", json::array())));
    CHECK_FALSE(f.Eval(Cond("unknown_keyword", json::object())));
    CHECK_FALSE(f.Eval(json(42)));
}
