#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/artifacts.hpp>
#include <match/ops/op_helpers.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file engine_choose_player_test.cpp
 * @brief `@choose_player` sugar: a selector opens the prompt.
 *
 * A graph names `@choose_player` as an op selector with no prior `prompt`
 * node. The Resolver must open a `choose_player` prompt implicitly for the
 * acting seat, offer the other seats as `payload.options`, and on a valid
 * answer bind the chosen seat so the op runs against it. This is the same
 * mechanism `seven_zero`'s 7-swap relies on (see engine_golden_replay_test).
 */

namespace ecs = match::ecs;
namespace ops = match::ops;
using namespace match::engine;
using namespace match::ecs;
using namespace match::modload;

namespace {

using nlohmann::json;

std::string AssemblyMessage(const AssemblyResult& result) {
    return result.error.has_value() ? result.error->message
                                    : std::string("assembly failed");
}

ModManifest Manifest(const std::string& id) {
    ModManifest manifest;
    manifest.id = id;
    manifest.name = id;
    manifest.version = "1.0.0";
    manifest.api = "1";
    return manifest;
}

/** @brief A blank numbered text card (draw-pile filler). */
CardDef PlainCard(const std::string& ns, const std::string& local,
                  const std::string& label) {
    CardDef card;
    card.id = local;
    card.namespace_id = ns;
    card.kind_id = ns + ":" + local;
    card.title = local;
    card.face.kind = match::modload::FaceKind::kText;
    card.face.color = std::string("red");
    card.face.label = label;
    card.tags = {"numbered"};
    return card;
}

DeckDef FixtureDeck(const std::vector<std::string>& mods,
                    const std::vector<std::pair<std::string, int>>& cards) {
    DeckDef deck;
    deck.id = "fixture";
    deck.namespace_id = "fixture";
    deck.deck_id = "fixture:deck";
    deck.name = "Fixture";
    deck.mods = mods;
    deck.cards = cards;
    return deck;
}

struct Fixture {
    std::vector<LoadedMod> mods;
    DeckDef deck;
};

/** @brief `base` holds the draw pile; `modx` proves a non-zero mod index. */
Fixture MakeFixture() {
    LoadedMod base;
    base.manifest = Manifest("base");
    base.cards.push_back(PlainCard("base", "pad", "9"));
    LoadedMod modx;
    modx.manifest = Manifest("modx");
    modx.cards.push_back(PlainCard("modx", "probe", "1"));
    Fixture fixture;
    fixture.mods = {std::move(base), std::move(modx)};
    fixture.deck = FixtureDeck(
        {"base", "modx"}, {{"base:pad", 20}, {"modx:probe", 20}});
    return fixture;
}

std::unique_ptr<MatchInstance> Assemble(const Fixture& fixture, int players) {
    MatchAssemblyOptions options;
    options.starting_cards = 3;
    options.seed = 20260922;
    for (int i = 0; i < players; ++i) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(i);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(fixture.mods, fixture.deck, options);
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

/** @brief Set `player`'s hand to exactly `cards` (test setup). */
void ForceHand(MatchInstance& engine, ecs::Entity player,
               const std::vector<ecs::Entity>& cards) {
    ecs::Hand* hand = engine.Store().Get<ecs::Hand>(player);
    REQUIRE(hand != nullptr);
    const std::vector<ecs::Entity> existing = hand->cards;
    for (ecs::Entity card : existing) {
        ops::MoveCardToZone(engine.Store(), card,
                            ecs::ZoneRef{ecs::ZoneKind::kDrawPile,
                                         ecs::Entity{}});
    }
    for (ecs::Entity card : cards) {
        ops::MoveCardToZone(engine.Store(), card,
                            ecs::ZoneRef{ecs::ZoneKind::kHand, player});
    }
}

/** @brief First assembled card of `kind_id`, or nullopt. */
std::optional<ecs::Entity> FindKindCard(MatchInstance& engine,
                                        const std::string& kind_id) {
    for (ecs::Entity card : engine.Registries().cards) {
        const ecs::CardIdentity* identity =
            engine.Store().Get<ecs::CardIdentity>(card);
        if (identity != nullptr && identity->kind_id == kind_id) return card;
    }
    return std::nullopt;
}

/** @brief Attach a `draw_cards(target=@choose_player)` graph on after:play. */
void AttachChoosePlayerSystem(MatchInstance& engine) {
    MatchAssembly& assembly = engine.Assembly();
    ModSystem system;
    system.mod_id = "test";
    system.registration_index = 0;
    system.hook = ecs::HookId{"play", ecs::HookPhase::kAfter};
    system.source_id = "test:choose_player_probe";
    system.graph.nodes = json::array({json{
        {"id", "n1"},
        {"op", "draw_cards"},
        {"args", json{{"target", "@choose_player"}, {"n", 2}}}}});
    system.graph.raw = json{{"nodes", system.graph.nodes}};
    const std::size_t index = assembly.systems.size();
    assembly.systems.push_back(std::move(system));
    MatchAssembly* self = &engine.Assembly();
    assembly.bus.Subscribe("test", 0, assembly.systems[index].hook,
                           [self, index](ecs::HookPayload& payload) {
                               self->RunSystem(index, payload);
                           });
}

int CardCount(const MatchInstance& engine, std::size_t player) {
    return engine.ExportState()["players"][player]["card_count"]
        .get<int>();
}

}  // namespace

TEST_CASE("choose_player sugar: selector opens a prompt for the acting seat") {
    Fixture fixture = MakeFixture();
    std::unique_ptr<MatchInstance> engine = Assemble(fixture, 3);
    AttachChoosePlayerSystem(*engine);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> probe =
        FindKindCard(*engine, "modx:probe");
    const std::optional<ecs::Entity> pad = FindKindCard(*engine, "base:pad");
    REQUIRE(probe.has_value());
    REQUIRE(pad.has_value());
    ForceHand(*engine, player0, {*probe, *pad});

    REQUIRE(engine->PlayCard("player0", *probe));

    const std::optional<json> pending = engine->PendingInput();
    REQUIRE(pending.has_value());
    CHECK((*pending)["kind"] == "choose_player");

    // INFO: the prompt asks the acting seat and offers the other seats only.
    const json& payload = (*pending)["payload"];
    REQUIRE(payload["options"].is_array());
    CHECK(payload["options"] == json::array({"player1", "player2"}));

    // INFO: only the targeted seat may answer.
    CHECK_FALSE(engine->SubmitInput("player1", json("player2")));
    REQUIRE(engine->PendingInput().has_value());

    // INFO: a non-string, or a username that names no live seat, is refused
    //       and leaves the parked prompt answerable.
    CHECK_FALSE(engine->SubmitInput("player0", 2));
    CHECK_FALSE(engine->SubmitInput("player0", json("ghost")));
    REQUIRE(engine->PendingInput().has_value());

    const int before = CardCount(*engine, 2);
    REQUIRE(engine->SubmitInput("player0", json("player2")));
    CHECK_FALSE(engine->PendingInput().has_value());
    CHECK(CardCount(*engine, 2) == before + 2);
}
