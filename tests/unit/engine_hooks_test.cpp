#include <doctest/doctest.h>

#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/ecs/components.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file engine_hooks_test.cpp
 * @brief `match::engine::MatchInstance` hook + resolver tests.
 *
 * Assembles real matches from the shipped `mods/` tree and scripts plays /
 * draws through the public engine surface, subscribing recorder / veto
 * systems on the `EventBus` to assert dispatch order, phases and veto
 * semantics, resolver op-event collection must-apply auto-play and
 * a `kNeedsInput` pause resumed through `SubmitInput`.
 */

namespace fs = std::filesystem;
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

fs::path ProjectRoot() {
    fs::path p(__FILE__);
    while (!p.empty()) {
        std::error_code ec;
        if (fs::is_directory(p / "contract" / "schemas", ec)) return p;
        fs::path parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return {};
}

struct Content {
    std::vector<LoadedMod> mods;
    DeckDef classic;
};

bool LoadContent(Content& out) {
    const fs::path root = ProjectRoot();
    if (root.empty()) return false;
    LoadResult load = ScanModsDirectory((root / "mods").string());
    if (!load.ok()) return false;
    out.mods = std::move(load.mods);
    for (const LoadedMod& mod : out.mods) {
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == "vanilla:classic") out.classic = deck;
        }
    }
    return !out.classic.deck_id.empty();
}

MatchAssemblyOptions Players(int count, int starting_cards, uint64_t seed) {
    MatchAssemblyOptions options;
    options.starting_cards = starting_cards;
    options.seed = seed;
    for (int i = 0; i < count; ++i) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(i);
        options.players.push_back(spec);
    }
    return options;
}

std::unique_ptr<MatchInstance> MakeEngine(Content& content, int players,
                                          int cards, uint64_t seed) {
    AssemblyResult result = MatchAssembler::Assemble(
        content.mods, content.classic, Players(players, cards, seed));
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

std::unique_ptr<MatchInstance> MakeEngineWithMods(
    Content& content, const std::vector<std::string>& mods, int players,
    int cards, uint64_t seed) {
    DeckDef deck = content.classic;
    deck.mods = mods;
    AssemblyResult result = MatchAssembler::Assemble(
        content.mods, deck, Players(players, cards, seed));
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

std::size_t HandSize(const MatchInstance& engine, ecs::Entity player) {
    const ecs::Hand* hand = engine.Store().Get<ecs::Hand>(player);
    return hand == nullptr ? 0 : hand->cards.size();
}

std::size_t PileSize(const MatchInstance& engine, ecs::PileKind kind) {
    for (ecs::Entity pile : engine.Store().EntitiesWith<ecs::PileContents>()) {
        const ecs::PileContents* contents =
            engine.Store().Get<ecs::PileContents>(pile);
        if (contents != nullptr && contents->kind == kind) {
            return contents->cards.size();
        }
    }
    return 0;
}

std::optional<ecs::Entity> TopDiscard(MatchInstance& engine) {
    const ecs::PileContents* discard = engine.Store().Get<ecs::PileContents>(
        engine.Registries().discard_pile);
    if (discard == nullptr || discard->cards.empty()) return std::nullopt;
    return discard->cards.back();
}

/** @brief Replace a player's hand with exactly `cards` (test setup). */
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

/**
 * @brief `n` numbered cards matching the match's active colour.
 *
 * A numbered card has no `card_behavior`, so playing it never opens a prompt;
 * it is also a legal colour match for the restriction pipeline.
 */
std::vector<ecs::Entity> LegalNumbered(MatchInstance& engine, std::size_t n) {
    std::vector<ecs::Entity> out;
    const json active = engine.ExportState()["active_type"];
    if (!active.is_string()) return out;
    const std::string color = active.get<std::string>();

    const std::optional<ecs::Entity> top = TopDiscard(engine);
    for (ecs::Entity card : engine.Registries().cards) {
        if (out.size() == n) break;
        if (top.has_value() && card == *top) continue;
        const ecs::FaceSpec* face = engine.Store().Get<ecs::FaceSpec>(card);
        if (face == nullptr || face->color != color) continue;
        if (face->label.size() != 1) continue;
        if (std::isdigit(static_cast<unsigned char>(face->label[0])) == 0) {
            continue;
        }
        // INFO: assembly installs an (empty) CardBehavior on every card; a
        //       numbered card's trigger map is empty, so it opens no prompt.
        const ecs::CardBehavior* behavior =
            engine.Store().Get<ecs::CardBehavior>(card);
        if (behavior != nullptr && !behavior->triggers.empty()) continue;
        out.push_back(card);
    }
    return out;
}

const json* FindEvent(const MatchInstance& engine, const std::string& type) {
    for (const json& event : engine.Events()) {
        if (event.is_object() && event.value("type", "") == type) {
            return &event;
        }
    }
    return nullptr;
}

std::size_t CountEvents(const MatchInstance& engine,
                        const std::string& type) {
    std::size_t count = 0;
    for (const json& event : engine.Events()) {
        if (event.is_object() && event.value("type", "") == type) ++count;
    }
    return count;
}

/** @brief Behaviour graph with a single `emit_signal` node. */
BehaviorGraph SignalGraph(const std::string& name) {
    BehaviorGraph graph;
    graph.nodes = json::array({json{{"id", "n1"},
                                    {"op", "emit_signal"},
                                    {"args", json{{"name", name}}}}});
    graph.raw = json{{"nodes", graph.nodes}};
    return graph;
}

/** @brief Behaviour graph: `prompt(choose_color)` -> `emit_signal`. */
BehaviorGraph PromptGraph(const std::string& signal) {
    BehaviorGraph graph;
    graph.nodes = json::array(
        {json{{"id", "n1"},
              {"op", "prompt"},
              {"args", json{{"kind", "choose_color"},
                            {"target", "@self"},
                            {"payload", json{{"options",
                                              json::array({"red", "blue"})}}}}},
              {"next", "n2"}},
         json{{"id", "n2"},
              {"op", "emit_signal"},
              {"args", json{{"name", signal}}}}});
    graph.raw = json{{"nodes", graph.nodes}};
    return graph;
}

/**
 * @brief Attach a synthetic rule system to `after:play` on the live bus.
 *
 * Mirrors what the engine does for a mod system so the engine's `collect`/pause
 * path runs it exactly as it runs shipped content.
 *
 * @return The system index (for context).
 */
std::size_t AttachAfterPlaySystem(MatchInstance& engine, BehaviorGraph graph,
                                  const std::string& source_id) {
    MatchAssembly& assembly = engine.Assembly();
    ModSystem system;
    system.mod_id = "test";
    system.registration_index = 0;
    system.hook = ecs::HookId{"play", ecs::HookPhase::kAfter};
    system.source_id = source_id;
    system.graph = std::move(graph);
    const std::size_t index = assembly.systems.size();
    assembly.systems.push_back(std::move(system));
    assembly.bus.Subscribe(
        "test", 0, assembly.systems[index].hook,
        [&assembly, index](ecs::HookPayload& payload) {
            assembly.RunSystem(index, payload);
        });
    return index;
}

}  // namespace

TEST_CASE("engine hooks: play/turn hooks fire in order and phase") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 7, 42);

    std::vector<std::string> order;
    auto record = [&order](const char* tag) {
        return [&order, tag](ecs::HookPayload&) { order.push_back(tag); };
    };
    EventBus& bus = engine->Assembly().bus;
    bus.Subscribe("rec", 0, {"play", HookPhase::kBefore},
                  record("before:play"));
    bus.Subscribe("rec", 1, {"play", HookPhase::kAfter},
                  record("after:play"));
    bus.Subscribe("rec", 2, {"turn_end", HookPhase::kBefore},
                  record("before:turn_end"));
    bus.Subscribe("rec", 3, {"turn_end", HookPhase::kAfter},
                  record("after:turn_end"));
    bus.Subscribe("rec", 4, {"turn_start", HookPhase::kBefore},
                  record("before:turn_start"));
    bus.Subscribe("rec", 5, {"turn_start", HookPhase::kAfter},
                  record("after:turn_start"));

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::vector<ecs::Entity> cards = LegalNumbered(*engine, 2);
    REQUIRE(cards.size() == 2);
    ForceHand(*engine, player0, cards);

    REQUIRE(engine->PlayCard("player0", cards[0]));

    const std::vector<std::string> expected = {
        "before:play",      "after:play",
        "before:turn_end",  "after:turn_end",
        "before:turn_start", "after:turn_start"};
    CHECK(order == expected);
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
}

TEST_CASE("engine hooks: before:play veto cancels the engine default") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 7, 42);

    engine->Assembly().bus.Subscribe(
        "veto", 0, {"play", HookPhase::kBefore},
        [](ecs::HookPayload& payload) { payload.veto = true; });

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::vector<ecs::Entity> cards = LegalNumbered(*engine, 2);
    REQUIRE(cards.size() == 2);
    ForceHand(*engine, player0, cards);

    const std::size_t hand_before = HandSize(*engine, player0);
    const std::size_t discard_before =
        PileSize(*engine, ecs::PileKind::kDiscard);

    CHECK(engine->PlayCard("player0", cards[0]));
    CHECK(HandSize(*engine, player0) == hand_before);
    CHECK(PileSize(*engine, ecs::PileKind::kDiscard) == discard_before);
    CHECK(engine->GetCurrentPlayerUsername() == "player0");
    CHECK(FindEvent(*engine, "card_played") == nullptr);
}

TEST_CASE("engine hooks: before:draw_attempt veto skips the draw") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 7, 42);

    engine->Assembly().bus.Subscribe(
        "veto", 0, {"draw_attempt", HookPhase::kBefore},
        [](ecs::HookPayload& payload) { payload.veto = true; });

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::size_t hand_before = HandSize(*engine, player0);
    const std::size_t draw_before = PileSize(*engine, ecs::PileKind::kDraw);

    REQUIRE(engine->DrawCard("player0"));
    CHECK(HandSize(*engine, player0) == hand_before);
    CHECK(PileSize(*engine, ecs::PileKind::kDraw) == draw_before);
    CHECK(FindEvent(*engine, "cards_drawn") == nullptr);
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
}

TEST_CASE("engine hooks: before:turn_end veto grants an extra turn") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 7, 42);

    engine->Assembly().bus.Subscribe(
        "veto", 0, {"turn_end", HookPhase::kBefore},
        [](ecs::HookPayload& payload) { payload.veto = true; });

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::vector<ecs::Entity> cards = LegalNumbered(*engine, 2);
    REQUIRE(cards.size() == 2);
    ForceHand(*engine, player0, cards);

    REQUIRE(engine->PlayCard("player0", cards[0]));
    CHECK(engine->GetCurrentPlayerUsername() == "player0");
    CHECK(FindEvent(*engine, "turn_advance") == nullptr);
}

TEST_CASE("engine hooks: before:hand_empty veto blocks the win") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 7, 42);

    engine->Assembly().bus.Subscribe(
        "veto", 0, {"hand_empty", HookPhase::kBefore},
        [](ecs::HookPayload& payload) { payload.veto = true; });

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::vector<ecs::Entity> cards = LegalNumbered(*engine, 1);
    REQUIRE(cards.size() == 1);
    ForceHand(*engine, player0, cards);

    REQUIRE(engine->PlayCard("player0", cards[0]));
    CHECK_FALSE(engine->IsMatchOver());
    CHECK(engine->GetWinner().empty());
    CHECK(engine->GetPlacements().empty());
    CHECK(FindEvent(*engine, "placement") == nullptr);
    // INFO: the blocked win still ends the turn.
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
}

TEST_CASE("engine hooks: resolver op events drain and effect_applied fires") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 7, 42);

    AttachAfterPlaySystem(*engine, SignalGraph("op_event"), "test:signal");

    std::size_t effect_applied = 0;
    engine->Assembly().bus.Subscribe(
        "rec", 0, {"effect_applied", HookPhase::kAfter},
        [&effect_applied](ecs::HookPayload&) { ++effect_applied; });

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::vector<ecs::Entity> cards = LegalNumbered(*engine, 2);
    REQUIRE(cards.size() == 2);
    ForceHand(*engine, player0, cards);

    REQUIRE(engine->PlayCard("player0", cards[0]));

    const json* signal = FindEvent(*engine, "signal");
    REQUIRE(signal != nullptr);
    CHECK((*signal)["payload"]["name"] == "op_event");
    CHECK(effect_applied >= 1);
}

TEST_CASE(
    "engine hooks: must-apply auto card fires once and emits auto_played") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 7, 42);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::vector<ecs::Entity> cards = LegalNumbered(*engine, 3);
    REQUIRE(cards.size() == 3);
    const ecs::Entity auto_card = cards[2];
    ForceHand(*engine, player0, cards);

    ecs::AutoTrigger trigger;
    trigger.condition = json(true);
    trigger.graph = json{{"nodes", json::array()}};
    trigger.must_apply = true;
    REQUIRE(engine->Store().Add(auto_card, std::move(trigger)) != nullptr);

    REQUIRE(engine->PlayCard("player0", cards[0]));

    CHECK(CountEvents(*engine, "auto_played") == 1);
    // INFO: the auto card left the hand (it auto-played itself).
    const ecs::Hand* hand = engine->Store().Get<ecs::Hand>(player0);
    REQUIRE(hand != nullptr);
    CHECK(std::find(hand->cards.begin(), hand->cards.end(), auto_card)
          == hand->cards.end());
}

TEST_CASE("engine hooks: kNeedsInput pauses and SubmitInput resumes") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2, 7, 42);

    AttachAfterPlaySystem(*engine, PromptGraph("resumed"), "test:prompt");

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::vector<ecs::Entity> cards = LegalNumbered(*engine, 2);
    REQUIRE(cards.size() == 2);
    ForceHand(*engine, player0, cards);

    REQUIRE(engine->PlayCard("player0", cards[0]));

    const std::optional<json> pending = engine->PendingInput();
    REQUIRE(pending.has_value());
    CHECK((*pending)["kind"] == "choose_color");
    CHECK(engine->GetCurrentPlayerUsername() == "player0");

    // INFO: input is locked out until the pause resolves.
    CHECK_FALSE(engine->PlayCard("player0", cards[1]));

    CHECK_FALSE(engine->SubmitInput("player1", "red"));
    REQUIRE(engine->SubmitInput("player0", "red"));
    CHECK_FALSE(engine->PendingInput().has_value());

    const json* signal = FindEvent(*engine, "signal");
    REQUIRE(signal != nullptr);
    CHECK((*signal)["payload"]["name"] == "resumed");
    CHECK(engine->GetCurrentPlayerUsername() == "player1");
}

TEST_CASE("engine hooks: a window pause is parked") {
    Content content;
    REQUIRE(LoadContent(content));
    std::unique_ptr<MatchInstance> engine =
        MakeEngineWithMods(content, {"vanilla", "jump_in"}, 3, 7, 42);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::vector<ecs::Entity> cards = LegalNumbered(*engine, 2);
    REQUIRE(cards.size() == 2);
    ForceHand(*engine, player0, cards);

    REQUIRE(engine->PlayCard("player0", cards[0]));
    CHECK(engine->PendingWindow().has_value());
    CHECK_FALSE(engine->PendingInput().has_value());
    // INFO: The window is not run, so the turn has not advanced.
    CHECK(engine->GetCurrentPlayerUsername() == "player0");
}
