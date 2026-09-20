#include <doctest/doctest.h>

#include <match/ops/op_helpers.hpp>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

using match::ecs::CardIdentity;
using match::ecs::Entity;
using match::ecs::EntityStore;
using match::ecs::FaceSpec;
using match::ecs::Hand;
using match::ecs::InZone;
using match::ecs::MatchMeta;
using match::ecs::PileContents;
using match::ecs::PileKind;
using match::ecs::PlayerInfo;
using match::ecs::TurnState;
using match::ecs::ZoneKind;
using match::ecs::ZoneRef;
using match::ops::BindLastRoll;
using match::ops::BindTurnsElapsed;
using match::ops::CardHasTag;
using match::ops::CardInZone;
using match::ops::CardOrdinal;
using match::ops::CardKindId;
using match::ops::CardsHeldBy;
using match::ops::ClearCardTags;
using match::ops::DrawTop;
using match::ops::FindCardZone;
using match::ops::FindCurrentPlayer;
using match::ops::FindMatch;
using match::ops::FindPile;
using match::ops::HandComponent;
using match::ops::HandOf;
using match::ops::LastRollTotal;
using match::ops::MakeEvent;
using match::ops::MoveCardToZone;
using match::ops::PileComponent;
using match::ops::PileOf;
using match::ops::PlayersBySeat;
using match::ops::RegisterCardTags;
using match::ops::ResolutionFrame;
using match::ops::SetHandOrder;
using match::ops::TurnsElapsed;

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

}  // namespace

TEST_CASE("op_helpers: traversal finds match, seat order and piles") {
    EntityStore store;
    Entity match = store.Create();
    store.Add(match, MatchMeta{});
    Entity b = AddPlayer(store, "b", 2);
    Entity a = AddPlayer(store, "a", 0);
    Entity c = AddPlayer(store, "c", 1);
    TurnState turn;
    turn.is_current = true;
    store.Add(a, turn);
    Entity draw = AddPile(store, PileKind::kDraw);
    Entity discard = AddPile(store, PileKind::kDiscard);

    REQUIRE(FindMatch(store).has_value());
    CHECK(*FindMatch(store) == match);

    const std::vector<Entity> players = PlayersBySeat(store);
    REQUIRE(players.size() == 3);
    CHECK(players[0] == a);
    CHECK(players[1] == c);
    CHECK(players[2] == b);

    REQUIRE(FindCurrentPlayer(store).has_value());
    CHECK(*FindCurrentPlayer(store) == a);

    REQUIRE(FindPile(store, PileKind::kDraw).has_value());
    REQUIRE(FindPile(store, PileKind::kDiscard).has_value());
    CHECK(*FindPile(store, PileKind::kDraw) == draw);
    CHECK(*FindPile(store, PileKind::kDiscard) == discard);
}

TEST_CASE("op_helpers: hand, pile and card queries") {
    EntityStore store;
    Entity player = AddPlayer(store, "p", 0);
    Entity other = AddPlayer(store, "q", 1);
    Entity draw = AddPile(store, PileKind::kDraw);
    Entity card = MakeCard(store, "vanilla:red_5", "red");
    PutInHand(store, player, card);
    Entity pile_card = MakeCard(store, "vanilla:blue_2", "blue");
    PutInPile(store, draw, pile_card);

    CHECK(HandOf(store, player).size() == 1);
    CHECK(HandOf(store, other).empty());
    REQUIRE(HandComponent(store, player) != nullptr);
    Entity no_hand = store.Create();
    CHECK(HandComponent(store, no_hand) == nullptr);

    CHECK(PileOf(store, draw).size() == 1);
    CHECK(PileOf(store, player).empty());
    REQUIRE(PileComponent(store, draw) != nullptr);
    CHECK(PileComponent(store, player) == nullptr);

    REQUIRE(FindCardZone(store, card).has_value());
    CHECK(FindCardZone(store, card)->kind == ZoneKind::kHand);
    CHECK(CardInZone(store, card, ZoneKind::kHand));
    CHECK_FALSE(CardInZone(store, card, ZoneKind::kLimbo));
    CHECK(CardKindId(store, card) == "vanilla:red_5");
    CHECK(CardKindId(store, other).empty());

    CHECK(CardsHeldBy(store, player) == std::vector<Entity>{card});
    CHECK(CardsHeldBy(store, draw) == std::vector<Entity>{pile_card});
    CHECK(CardsHeldBy(store, card) == std::vector<Entity>{card});
    CHECK(CardsHeldBy(store, other).empty());
}

TEST_CASE("op_helpers: MoveCardToZone keeps containers consistent") {
    EntityStore store;
    Entity a = AddPlayer(store, "a", 0);
    Entity b = AddPlayer(store, "b", 1);
    Entity draw = AddPile(store, PileKind::kDraw);
    Entity discard = AddPile(store, PileKind::kDiscard);
    Entity c1 = MakeCard(store, "vanilla:red_1", "red");
    Entity c2 = MakeCard(store, "vanilla:red_2", "red");
    PutInHand(store, a, c1);
    PutInHand(store, a, c2);

    // INFO: hand -> discard removes from the hand and pushes to the top.
    REQUIRE(MoveCardToZone(store, c1,
                           ZoneRef{ZoneKind::kDiscardPile, Entity{}}));
    CHECK(HandOf(store, a) == std::vector<Entity>{c2});
    CHECK(CardInZone(store, c1, ZoneKind::kDiscardPile));
    CHECK(CardOrdinal(store, c1).value() == 0);
    CHECK(PileOf(store, discard) == std::vector<Entity>{c1});

    // INFO: discard -> another hand moves the card and renumbers it.
    REQUIRE(MoveCardToZone(store, c1, ZoneRef{ZoneKind::kHand, b}));
    CHECK(PileOf(store, discard).empty());
    CHECK(HandOf(store, b) == std::vector<Entity>{c1});
    CHECK(CardInZone(store, c1, ZoneKind::kHand));
    CHECK(CardOrdinal(store, c1).value() == 0);

    // INFO: moving within the same hand appends at the end.
    REQUIRE(MoveCardToZone(store, c2, ZoneRef{ZoneKind::kHand, a}));
    CHECK(HandOf(store, a).size() == 1);
    REQUIRE(MoveCardToZone(store, c1, ZoneRef{ZoneKind::kHand, a}));
    CHECK(HandOf(store, a) == std::vector<Entity>{c2, c1});
    CHECK(CardOrdinal(store, c1).value() == 1);

    // INFO: move to limbo detaches from any container.
    REQUIRE(MoveCardToZone(store, c1, ZoneRef{ZoneKind::kLimbo, Entity{}}));
    CHECK(CardInZone(store, c1, ZoneKind::kLimbo));
    CHECK(HandOf(store, a) == std::vector<Entity>{c2});

    // INFO: dead card is a structured false.
    Entity ghost = store.Create();
    store.Destroy(ghost);
    CHECK_FALSE(MoveCardToZone(store, ghost,
                               ZoneRef{ZoneKind::kDiscardPile, Entity{}}));

    // INFO: a hand destination without a hand component fails safe and the
    //       card ends in limbo rather than stranded in its old container.
    Entity no_hand = store.Create();
    Entity c3 = MakeCard(store, "vanilla:green_3", "green");
    PutInHand(store, a, c3);
    CHECK_FALSE(MoveCardToZone(store, c3, ZoneRef{ZoneKind::kHand, no_hand}));
    CHECK(CardInZone(store, c3, ZoneKind::kLimbo));
    CHECK(HandOf(store, a) == std::vector<Entity>{c2});
}

TEST_CASE("op_helpers: DrawTop pops the top and empties to nullopt") {
    EntityStore store;
    Entity draw = AddPile(store, PileKind::kDraw);
    Entity c1 = MakeCard(store, "vanilla:red_1", "red");
    Entity c2 = MakeCard(store, "vanilla:red_2", "red");
    Entity c3 = MakeCard(store, "vanilla:red_3", "red");
    PutInPile(store, draw, c1);
    PutInPile(store, draw, c2);
    PutInPile(store, draw, c3);

    const std::optional<Entity> first = DrawTop(store, draw);
    REQUIRE(first.has_value());
    CHECK(*first == c3);
    CHECK(PileOf(store, draw) == std::vector<Entity>{c1, c2});
    CHECK(CardInZone(store, c3, ZoneKind::kLimbo));

    const std::optional<Entity> second = DrawTop(store, draw);
    REQUIRE(second.has_value());
    CHECK(*second == c2);
    CHECK(PileOf(store, draw) == std::vector<Entity>{c1});
    CHECK(CardInZone(store, c2, ZoneKind::kLimbo));

    REQUIRE(DrawTop(store, draw).has_value());
    CHECK(PileOf(store, draw).empty());
    CHECK_FALSE(DrawTop(store, draw).has_value());
}

TEST_CASE("op_helpers: SetHandOrder renumbers the listed cards") {
    EntityStore store;
    Entity player = AddPlayer(store, "p", 0);
    Entity c1 = MakeCard(store, "vanilla:red_1", "red");
    Entity c2 = MakeCard(store, "vanilla:red_2", "red");
    PutInHand(store, player, c1);
    PutInHand(store, player, c2);

    REQUIRE(SetHandOrder(store, player, {c2, c1}));
    CHECK(HandOf(store, player) == std::vector<Entity>{c2, c1});
    CHECK(CardOrdinal(store, c2).value() == 0);
    CHECK(CardOrdinal(store, c1).value() == 1);
    CHECK(FindCardZone(store, c1)->owner == player);

    Entity no_hand = store.Create();
    CHECK_FALSE(SetHandOrder(store, no_hand, {c1}));
}

TEST_CASE("op_helpers: MakeEvent builds the event descriptor") {
    const json event = MakeEvent("cards_drawn", json{{"n", 2}});
    CHECK(event["type"] == "cards_drawn");
    CHECK(event["payload"]["n"] == 2);
    CHECK_FALSE(event.contains("seq"));

    const json bare = MakeEvent("reshuffle");
    CHECK(bare["type"] == "reshuffle");
    CHECK(bare["payload"].is_object());
    CHECK(bare["payload"].empty());
}

TEST_CASE("op_helpers: last-roll and turns-elapsed frame seams") {
    ResolutionFrame frame;
    CHECK_FALSE(LastRollTotal(frame).has_value());
    CHECK_FALSE(TurnsElapsed(frame).has_value());

    BindLastRoll(frame, 7, json::array({3, 4}));
    REQUIRE(LastRollTotal(frame).has_value());
    CHECK(*LastRollTotal(frame) == 7);
    const json* roll = match::ops::LastRoll(frame);
    REQUIRE(roll != nullptr);
    CHECK((*roll)["outcomes"].size() == 2);

    BindTurnsElapsed(frame, 5);
    REQUIRE(TurnsElapsed(frame).has_value());
    CHECK(*TurnsElapsed(frame) == 5);
}

TEST_CASE("op_helpers: card tag table register and clear") {
    ClearCardTags();
    CHECK_FALSE(CardHasTag("vanilla:red_5", "stackable"));

    RegisterCardTags("vanilla:red_5", {"stackable", "draw_penalty"});
    CHECK(CardHasTag("vanilla:red_5", "stackable"));
    CHECK(CardHasTag("vanilla:red_5", "draw_penalty"));
    CHECK_FALSE(CardHasTag("vanilla:red_5", "ghost"));
    CHECK_FALSE(CardHasTag("vanilla:blue_2", "stackable"));

    match::ops::SetCardTagTable({{"vanilla:blue_2", {"stackable"}}});
    CHECK_FALSE(CardHasTag("vanilla:red_5", "stackable"));
    CHECK(CardHasTag("vanilla:blue_2", "stackable"));
    CHECK(match::ops::CardTags().size() == 1);

    ClearCardTags();
    CHECK(match::ops::CardTags().empty());
    CHECK_FALSE(CardHasTag("vanilla:blue_2", "stackable"));
}
