#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/engine/play_evaluator.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/timers.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file play_evaluator_test.cpp
 * @brief Parity tests for `PlayEvaluator`, the single play-legality authority.
 *
 * Assembles real `mods/` content and checks that the evaluator's verdicts
 * mirror the engine's existing decision points: `CanPlayInTurn` agrees with an
 * accepted `PlayCard`, and `CanRespond` agrees with `CanRespondWindow` while a
 * window is open. A stability case exercises the per-evaluator verdict memo.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
namespace ops = match::ops;
using namespace match::engine;
using namespace match::ecs;
using namespace match::modload;

namespace {

using nlohmann::json;

/** @brief Deterministic wall clock for the window/turn timers. */
struct FakeClock {
    int64_t now = 0;

    match::NowMs Fn() {
        return [this]() { return now; };
    }
};

match::WindowConfig FixedWindow(int64_t ms) {
    match::WindowConfig config;
    config.window_ms = ms;
    config.mode = match::WindowMode::kFixed;
    return config;
}

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

std::unique_ptr<MatchInstance> Assemble(
    Content& content, const std::vector<std::string>& mods, int players,
    int cards, uint64_t seed, match::WindowConfig config,
    match::NowMs clock) {
    DeckDef deck = content.classic;
    if (!mods.empty()) deck.mods = mods;
    AssemblyResult result = MatchAssembler::Assemble(
        content.mods, deck, Players(players, cards, seed));
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    return std::make_unique<MatchInstance>(std::move(result.assembly), config,
                                           std::move(clock));
}

std::unique_ptr<MatchInstance> MakeVanillaEngine(
    Content& content, int players, int cards, uint64_t seed,
    match::WindowConfig config, match::NowMs clock) {
    return Assemble(content, {"vanilla"}, players, cards, seed, config,
                    std::move(clock));
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

/** @brief First card whose face matches `color` + `label`, or nullopt. */
std::optional<ecs::Entity> FindCard(MatchInstance& engine,
                                    const std::string& color,
                                    const std::string& label) {
    for (ecs::Entity card : engine.Registries().cards) {
        const ecs::FaceSpec* face = engine.Store().Get<ecs::FaceSpec>(card);
        if (face != nullptr && face->color == color && face->label == label) {
            return card;
        }
    }
    return std::nullopt;
}

std::optional<ecs::Entity> TopDiscard(MatchInstance& engine) {
    const ecs::PileContents* discard = engine.Store().Get<ecs::PileContents>(
        engine.Registries().discard_pile);
    if (discard == nullptr || discard->cards.empty()) return std::nullopt;
    return discard->cards.back();
}

/** @brief `n` numbered cards matching the match's active colour. */
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
        const ecs::CardBehavior* behavior =
            engine.Store().Get<ecs::CardBehavior>(card);
        if (behavior != nullptr && !behavior->triggers.empty()) continue;
        out.push_back(card);
    }
    return out;
}

/** @brief `n` cards neither in `exclude` nor the discard top. */
std::vector<ecs::Entity> OtherCards(MatchInstance& engine,
                                    const std::vector<ecs::Entity>& exclude,
                                    std::size_t n) {
    const std::optional<ecs::Entity> top = TopDiscard(engine);
    std::vector<ecs::Entity> out;
    for (ecs::Entity card : engine.Registries().cards) {
        if (out.size() == n) break;
        if (std::find(exclude.begin(), exclude.end(), card) != exclude.end()) {
            continue;
        }
        if (top.has_value() && card == *top) continue;
        out.push_back(card);
    }
    return out;
}

/** @brief Behaviour graph: window node `n1` with `emit_signal` routes. */
BehaviorGraph WindowGraph(const json& respond_with,
                          const std::string& filter_digest) {
    BehaviorGraph graph;
    json window = json{{"responders", "@others"},
                       {"respond_with", respond_with},
                       {"duration", "env"}};
    if (!filter_digest.empty()) window["filter_digest"] = filter_digest;
    const json node =
        json{{"id", "n1"},
             {"window", window},
             {"default", "n2"},
             {"on_response", json{{filter_digest, "n3"}}}};
    graph.nodes = json::array(
        {node,
         json{{"id", "n2"},
              {"op", "emit_signal"},
              {"args", json{{"name", "window:default"}}}},
         json{{"id", "n3"},
              {"op", "emit_signal"},
              {"args", json{{"name", "window:responded"}}}}});
    graph.raw = json{{"nodes", graph.nodes}};
    return graph;
}

/**
 * @brief Attach a synthetic `after:play` window system to the live bus.
 *
 * Mirrors what the engine does for a mod system so the engine's open/collect
 * path runs it exactly as it runs shipped content.
 */
std::size_t AttachWindowSystem(MatchInstance& engine, BehaviorGraph graph) {
    MatchAssembly& assembly = engine.Assembly();
    ModSystem system;
    system.mod_id = "test";
    system.registration_index = 0;
    system.hook = ecs::HookId{"play", ecs::HookPhase::kAfter};
    system.source_id = "test:window";
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

/** @brief Append an always-matching `allow` restriction (test setup). */
void AllowAllPlays(MatchInstance& engine) {
    ecs::PlayRestriction* pipeline = engine.Store().Get<ecs::PlayRestriction>(
        engine.Registries().match);
    REQUIRE(pipeline != nullptr);
    ecs::RestrictionEntry entry;
    entry.id = "test:allow_all";
    entry.phase = ecs::RestrictionPhase::kAllow;
    entry.condition = json();
    pipeline->entries.push_back(std::move(entry));
}

}  // namespace

TEST_CASE("play_evaluator: CanPlayInTurn mirrors PlayCard for in-hand cards") {
    Content content;
    REQUIRE(LoadContent(content));

    struct Probe {
        int player;
        std::string color;
        std::string label;
    };
    // INFO: three cards per seat, mixing a matching colour with mismatches,
    //       probed in turn (player0) and out of turn (player1/player2). Only
    //       cards actually held are probed: the pipeline is kind-based while
    //       `PlayCard` additionally requires the exact entity in hand.
    const std::vector<Probe> probes = {
        {0, "red", "5"},   {0, "blue", "6"},   {0, "yellow", "7"},
        {1, "red", "6"},   {1, "yellow", "7"}, {1, "blue", "8"},
        {2, "red", "9"},   {2, "green", "5"},  {2, "yellow", "6"},
    };

    for (const Probe& probe : probes) {
        FakeClock clock;
        std::unique_ptr<MatchInstance> engine = MakeVanillaEngine(
            content, 3, 7, 42, FixedWindow(1000), clock.Fn());

        const std::string username = "player" + std::to_string(probe.player);
        const ecs::Entity player = *engine->FindPlayer(username);
        const std::optional<ecs::Entity> card =
            FindCard(*engine, probe.color, probe.label);
        REQUIRE(card.has_value());
        ForceHand(*engine, player, {*card});
        engine->Store()
            .Get<ecs::ActiveTypeReq>(engine->Registries().match)
            ->type = "red";

        const PlayEvaluator evaluator = engine->MakePlayEvaluator();
        const bool can_play = evaluator.CanPlayInTurn(player, *card);
        const bool accepted = engine->PlayCard(username, *card);
        CHECK(can_play == accepted);
    }
}

TEST_CASE("play_evaluator: CanRespond matches the expected responder oracle") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeVanillaEngine(
        content, 3, 7, 42, FixedWindow(1000), clock.Fn());
    AttachWindowSystem(*engine, WindowGraph(json::object(), "any"));
    AllowAllPlays(*engine);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Entity player2 = *engine->FindPlayer("player2");

    const std::vector<ecs::Entity> opener = LegalNumbered(*engine, 2);
    REQUIRE(opener.size() == 2);
    ForceHand(*engine, player0, opener);
    const std::vector<ecs::Entity> others = OtherCards(*engine, opener, 4);
    REQUIRE(others.size() == 4);
    ForceHand(*engine, player1, {others[0], others[1]});
    ForceHand(*engine, player2, {others[2], others[3]});

    REQUIRE(engine->PlayCard("player0", opener[0]));
    REQUIRE(engine->WindowOpen());

    const ecs::WindowState* window = engine->Store().Get<ecs::WindowState>(
        engine->Registries().match);
    REQUIRE(window != nullptr);
    REQUIRE(window->open);
    const auto filters = engine->WindowFilters();
    REQUIRE_FALSE(filters.empty());

    const PlayEvaluator evaluator = engine->MakePlayEvaluator();
    const PlayEvaluator::WindowView view{
        window->responders, window->responses, filters};

    // INFO: independent oracle: the synthetic window admits any card from a
    //       responder (empty respond_with) and AllowAllPlays rescues the
    //       out-of-turn deny. Hard-coded expectations, not CanRespondWindow,
    //       so this test can fail if the evaluator regresses.
    struct Probe {
        ecs::Entity player;
        ecs::Entity card;
        bool expected;
    };
    const std::vector<Probe> probes = {
        {player1, others[0], true},   // responder, card in hand
        {player1, others[1], true},   // responder, card in hand
        {player2, others[2], true},   // responder, card in hand
        {player1, opener[0], false},  // responder, card not held
        {player2, others[0], false},  // responder, card held by another seat
        {player0, opener[1], false},  // not a responder
    };

    for (const Probe& probe : probes) {
        const bool got = evaluator.CanRespond(view, probe.player, probe.card);
        CHECK(got == probe.expected);
    }
}

TEST_CASE("play_evaluator: repeated verdicts are stable (memoized)") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = MakeVanillaEngine(
        content, 3, 7, 42, FixedWindow(1000), clock.Fn());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> red5 = FindCard(*engine, "red", "5");
    const std::optional<ecs::Entity> blue6 = FindCard(*engine, "blue", "6");
    REQUIRE(red5.has_value());
    REQUIRE(blue6.has_value());
    ForceHand(*engine, player0, {*red5, *blue6});
    engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)->type =
        "red";

    const PlayEvaluator evaluator = engine->MakePlayEvaluator();
    for (ecs::Entity card : {*red5, *blue6}) {
        const bool first = evaluator.CanPlayInTurn(player0, card);
        const bool second = evaluator.CanPlayInTurn(player0, card);
        CHECK(first == second);
    }
    // INFO: a second evaluator over the same unchanged state agrees.
    const PlayEvaluator rebuilt = engine->MakePlayEvaluator();
    CHECK(rebuilt.CanPlayInTurn(player0, *red5)
          == evaluator.CanPlayInTurn(player0, *red5));
}
