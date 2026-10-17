/**
 * @file engine_last_card_effects_test.cpp
 * @brief Effects resolve before the win check: a hand-moving card played as
 *        the last card hands the empty hand on, and its receiver wins.
 */

#include <doctest/doctest.h>
#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/artifacts.hpp>
#include <match/ops/op_helpers.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ecs = match::ecs;
namespace ops = match::ops;
using namespace match::engine;
using namespace match::ecs;
using namespace match::modload;

namespace {

using nlohmann::json;

ModManifest Manifest(const std::string& id) {
    ModManifest manifest;
    manifest.id = id;
    manifest.name = id;
    manifest.version = "1.0.0";
    manifest.api = "1";
    return manifest;
}

CardDef RedCard(const std::string& local, const std::string& label) {
    CardDef card;
    card.id = local;
    card.namespace_id = "base";
    card.kind_id = "base:" + local;
    card.title = local;
    card.face.kind = match::modload::FaceKind::kText;
    card.face.color = std::string("red");
    card.face.label = label;
    card.tags = {"numbered"};
    return card;
}

std::unique_ptr<MatchInstance> AssembleThreeSeats() {
    LoadedMod base;
    base.manifest = Manifest("base");
    base.cards.push_back(RedCard("pad", "9"));
    base.cards.push_back(RedCard("last", "0"));
    DeckDef deck;
    deck.id = "fixture";
    deck.namespace_id = "fixture";
    deck.deck_id = "fixture:deck";
    deck.name = "Fixture";
    deck.mods = {"base"};
    deck.cards = {{"base:pad", 30}, {"base:last", 4}};

    MatchAssemblyOptions options;
    options.starting_cards = 3;
    options.seed = 20261016;
    for (int seat = 0; seat < 3; ++seat) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(seat);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble({std::move(base)}, deck, options);
    REQUIRE_MESSAGE(result.ok(), "assembly failed");
    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

void ForceHand(MatchInstance& engine, ecs::Entity player,
               const std::vector<ecs::Entity>& cards) {
    ecs::Hand* hand = engine.Store().Get<ecs::Hand>(player);
    REQUIRE(hand != nullptr);
    const std::vector<ecs::Entity> existing = hand->cards;
    for (ecs::Entity card : existing) {
        ops::MoveCardToZone(
            engine.Store(), card,
            ecs::ZoneRef{ecs::ZoneKind::kDrawPile, ecs::Entity{}});
    }
    for (ecs::Entity card : cards) {
        ops::MoveCardToZone(engine.Store(), card,
                            ecs::ZoneRef{ecs::ZoneKind::kHand, player});
    }
}

std::optional<ecs::Entity> FindKindCard(MatchInstance& engine,
                                        const std::string& kind_id) {
    for (ecs::Entity card : engine.Registries().cards) {
        const ecs::CardIdentity* identity =
            engine.Store().Get<ecs::CardIdentity>(card);
        if (identity != nullptr && identity->kind_id == kind_id) return card;
    }
    return std::nullopt;
}

/** @brief `pass_hands` forward on every `after:play`, as the 0 card does. */
void AttachPassHandsSystem(MatchInstance& engine) {
    MatchAssembly& assembly = engine.Assembly();
    ModSystem system;
    system.mod_id = "test";
    system.registration_index = 0;
    system.hook = ecs::HookId{"play", ecs::HookPhase::kAfter};
    system.source_id = "test:pass_hands_probe";
    system.graph.nodes = json::array(
        {json{{"id", "n1"},
              {"op", "pass_hands"},
              {"args", json{{"direction", "forward"}}}}});
    system.graph.raw = json{{"nodes", system.graph.nodes}};
    const std::size_t index = assembly.systems.size();
    assembly.systems.push_back(std::move(system));
    MatchAssembly* self = &engine.Assembly();
    assembly.bus.Subscribe("test", 0, assembly.systems[index].hook,
                           [self, index](ecs::HookPayload& payload) {
                               self->RunSystem(index, payload);
                           });
}

/** @brief `swap_hands` with a chosen seat on every `after:play`, as the 7. */
void AttachSwapHandsSystem(MatchInstance& engine) {
    MatchAssembly& assembly = engine.Assembly();
    ModSystem system;
    system.mod_id = "test";
    system.registration_index = 0;
    system.hook = ecs::HookId{"play", ecs::HookPhase::kAfter};
    system.source_id = "test:swap_hands_probe";
    system.graph.nodes = json::array(
        {json{{"id", "n1"},
              {"op", "swap_hands"},
              {"args", json{{"a", "@self"}, {"b", "@choose_player"}}}}});
    system.graph.raw = json{{"nodes", system.graph.nodes}};
    const std::size_t index = assembly.systems.size();
    assembly.systems.push_back(std::move(system));
    MatchAssembly* self = &engine.Assembly();
    assembly.bus.Subscribe("test", 0, assembly.systems[index].hook,
                           [self, index](ecs::HookPayload& payload) {
                               self->RunSystem(index, payload);
                           });
}

}  // namespace

TEST_CASE("last card hand pass: the seat that receives the empty hand wins") {
    std::unique_ptr<MatchInstance> engine = AssembleThreeSeats();
    AttachPassHandsSystem(*engine);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> last = FindKindCard(*engine, "base:last");
    REQUIRE(last.has_value());
    ForceHand(*engine, player0, {*last});

    REQUIRE(engine->PlayCard("player0", *last));

    CHECK(engine->GetWinner() == "player1");
}

TEST_CASE("last card without a hand effect: the player of the card wins") {
    std::unique_ptr<MatchInstance> engine = AssembleThreeSeats();

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> last = FindKindCard(*engine, "base:last");
    REQUIRE(last.has_value());
    ForceHand(*engine, player0, {*last});

    REQUIRE(engine->PlayCard("player0", *last));

    CHECK(engine->GetWinner() == "player0");
}

TEST_CASE("hand pass with cards left: the match goes on") {
    std::unique_ptr<MatchInstance> engine = AssembleThreeSeats();
    AttachPassHandsSystem(*engine);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> last = FindKindCard(*engine, "base:last");
    const std::optional<ecs::Entity> pad = FindKindCard(*engine, "base:pad");
    REQUIRE(last.has_value());
    REQUIRE(pad.has_value());
    ForceHand(*engine, player0, {*last, *pad});

    REQUIRE(engine->PlayCard("player0", *last));

    CHECK(engine->GetWinner().empty());
}

TEST_CASE("last card hand swap: the chosen seat receives the empty hand and wins") {
    std::unique_ptr<MatchInstance> engine = AssembleThreeSeats();
    AttachSwapHandsSystem(*engine);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> last = FindKindCard(*engine, "base:last");
    REQUIRE(last.has_value());
    ForceHand(*engine, player0, {*last});

    REQUIRE(engine->PlayCard("player0", *last));
    REQUIRE(engine->PendingInput().has_value());
    REQUIRE(engine->SubmitInput("player0", json("player2")));

    CHECK(engine->GetWinner() == "player2");
}
