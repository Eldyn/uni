#include <doctest/doctest.h>

#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/ecs/components.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/timers.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file engine_window_test.cpp
 * @brief Response-window flow tests for `match::engine::MatchInstance`
 * .
 *
 * Assembles real matches from the shipped `mods/` tree (plus synthetic
 * `after:play` window systems for the route/eligibility cases) and asserts:
 * a window opens on a draw-penalty play, all-pass early close, timeout default
 * route, first-response-wins with a loser returned, the turn clock pausing and
 * resuming across the window, and responses passing through the restriction
 * pipeline.
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

std::unique_ptr<MatchInstance> MakeEngine(Content& content, int players,
                                          int cards, uint64_t seed,
                                          match::WindowConfig config,
                                          match::NowMs clock) {
    return Assemble(content, {"vanilla"}, players, cards, seed, config,
                    std::move(clock));
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

bool PileHas(const MatchInstance& engine, ecs::PileKind kind,
             ecs::Entity card) {
    for (ecs::Entity pile : engine.Store().EntitiesWith<ecs::PileContents>()) {
        const ecs::PileContents* contents =
            engine.Store().Get<ecs::PileContents>(pile);
        if (contents == nullptr || contents->kind != kind) continue;
        return std::find(contents->cards.begin(), contents->cards.end(), card)
            != contents->cards.end();
    }
    return false;
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

bool HasSignal(const MatchInstance& engine, const std::string& name) {
    for (const json& event : engine.Events()) {
        if (!event.is_object() || event.value("type", "") != "signal") {
            continue;
        }
        const json payload =
            event.contains("payload") ? event["payload"] : json::object();
        if (payload.value("name", "") == name) return true;
    }
    return false;
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

/** @brief Two cards distinct from `exclude` for the non-current responders. */
std::vector<ecs::Entity> OtherCards(
    MatchInstance& engine, const std::vector<ecs::Entity>& exclude) {
    const std::optional<ecs::Entity> top = TopDiscard(engine);
    std::vector<ecs::Entity> out;
    for (ecs::Entity card : engine.Registries().cards) {
        if (std::find(exclude.begin(), exclude.end(), card) != exclude.end()) {
            continue;
        }
        if (top.has_value() && card == *top) continue;
        out.push_back(card);
        if (out.size() == 2) break;
    }
    return out;
}

}  // namespace

TEST_CASE("engine window: opens on a draw-penalty (+2/+4) play") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine = Assemble(
        content, {"vanilla", "draw_stacking"}, 3, 7, 42, FixedWindow(1000),
        clock.Fn());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::optional<ecs::Entity> draw2 = FindCard(*engine, "red", "+2");
    REQUIRE(draw2.has_value());
    ForceHand(*engine, player0, {*draw2});
    // INFO: make the +2 legal against the active colour, independent of seed.
    engine->Store().Get<ecs::ActiveTypeReq>(engine->Registries().match)->type =
        "red";

    REQUIRE(engine->PlayCard("player0", *draw2));
    CHECK(engine->WindowOpen());
    CHECK(FindEvent(*engine, "window_open") != nullptr);

    const json window = engine->ExportWindow();
    CHECK(window["open"] == true);
    REQUIRE(window["responders"].is_array());
    CHECK(window["responders"].size() == 2);
    CHECK(window["default_route"] == "n2");
    CHECK(engine->PendingWindow().has_value());
}

TEST_CASE("engine window: all-pass closes early and routes the default") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine =
        MakeEngine(content, 3, 7, 42, FixedWindow(1000), clock.Fn());
    AttachWindowSystem(*engine, WindowGraph(json::object(), "any"));

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::vector<ecs::Entity> cards = LegalNumbered(*engine, 2);
    REQUIRE(cards.size() == 2);
    ForceHand(*engine, player0, cards);

    REQUIRE(engine->PlayCard("player0", cards[0]));
    REQUIRE(engine->WindowOpen());

    REQUIRE(engine->PassWindow("player1"));
    CHECK(engine->WindowOpen());
    REQUIRE(engine->PassWindow("player2"));

    CHECK_FALSE(engine->WindowOpen());
    const json* close = FindEvent(*engine, "window_close");
    REQUIRE(close != nullptr);
    CHECK((*close)["payload"]["outcome"] == "all_pass");
    CHECK(HasSignal(*engine, "window:default"));
    CHECK_FALSE(HasSignal(*engine, "window:responded"));
}

TEST_CASE("engine window: timeout routes the default route") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine =
        MakeEngine(content, 3, 7, 42, FixedWindow(1000), clock.Fn());
    AttachWindowSystem(*engine, WindowGraph(json::object(), "any"));

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::vector<ecs::Entity> cards = LegalNumbered(*engine, 2);
    REQUIRE(cards.size() == 2);
    ForceHand(*engine, player0, cards);

    REQUIRE(engine->PlayCard("player0", cards[0]));
    REQUIRE(engine->WindowOpen());

    clock.now = 1000;
    engine->Tick();

    CHECK_FALSE(engine->WindowOpen());
    const json* close = FindEvent(*engine, "window_close");
    REQUIRE(close != nullptr);
    CHECK((*close)["payload"]["outcome"] == "timeout");
    CHECK((*close)["payload"]["route"] == "n2");
    CHECK(HasSignal(*engine, "window:default"));
}

TEST_CASE("engine window: first response wins and the loser is returned") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine =
        MakeEngine(content, 3, 7, 42, FixedWindow(1000), clock.Fn());
    AttachWindowSystem(*engine, WindowGraph(json::object(), "any"));
    AllowAllPlays(*engine);

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Entity player2 = *engine->FindPlayer("player2");
    const std::vector<ecs::Entity> cards = LegalNumbered(*engine, 2);
    REQUIRE(cards.size() == 2);
    ForceHand(*engine, player0, cards);

    const std::vector<ecs::Entity> others = OtherCards(*engine, cards);
    REQUIRE(others.size() == 2);
    ForceHand(*engine, player1, {others[0]});
    ForceHand(*engine, player2, {others[1]});

    REQUIRE(engine->PlayCard("player0", cards[0]));
    REQUIRE(engine->WindowOpen());

    REQUIRE(engine->RespondWindow("player1", others[0]));
    CHECK(engine->WindowOpen());
    REQUIRE(engine->RespondWindow("player2", others[1]));

    CHECK_FALSE(engine->WindowOpen());
    // INFO: the first response is committed; the loser never left its hand.
    CHECK(PileHas(*engine, ecs::PileKind::kDiscard, others[0]));
    CHECK_FALSE(PileHas(*engine, ecs::PileKind::kDiscard, others[1]));
    CHECK(HandSize(*engine, player2) == 1);
    CHECK(CountEvents(*engine, "window_response") == 2);

    const json* close = FindEvent(*engine, "window_close");
    REQUIRE(close != nullptr);
    CHECK((*close)["payload"]["outcome"] == "response");
    CHECK((*close)["payload"]["winner"] == "player1");
    CHECK(HasSignal(*engine, "window:responded"));
}

TEST_CASE("engine window: turn clock pauses while open and resumes on close") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine =
        MakeEngine(content, 3, 7, 42, FixedWindow(1000), clock.Fn());
    AttachWindowSystem(*engine, WindowGraph(json::object(), "any"));

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::vector<ecs::Entity> cards = LegalNumbered(*engine, 2);
    REQUIRE(cards.size() == 2);
    ForceHand(*engine, player0, cards);

    ecs::TurnState* turn = engine->Store().Get<ecs::TurnState>(player0);
    REQUIRE(turn != nullptr);
    REQUIRE(engine->Timers().Turn().Arm(*turn, 5000));
    CHECK(turn->turn_deadline_ms == 5000);

    REQUIRE(engine->PlayCard("player0", cards[0]));
    REQUIRE(engine->WindowOpen());
    CHECK(turn->turn_deadline_ms == 0);  // INFO: suspended.

    clock.now = 1000;
    engine->Tick();
    CHECK_FALSE(engine->WindowOpen());
    // INFO: the remaining 5000ms resume from the close time, not wall time.
    CHECK(turn->turn_deadline_ms == 6000);
}

TEST_CASE("engine window: responses pass through the restriction pipeline") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine =
        MakeEngine(content, 3, 7, 42, FixedWindow(1000), clock.Fn());
    AttachWindowSystem(*engine, WindowGraph(json::object(), "any"));

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const std::vector<ecs::Entity> cards = LegalNumbered(*engine, 2);
    REQUIRE(cards.size() == 2);
    ForceHand(*engine, player0, cards);
    const std::vector<ecs::Entity> others = OtherCards(*engine, cards);
    REQUIRE(!others.empty());
    ForceHand(*engine, player1, {others[0]});

    REQUIRE(engine->PlayCard("player0", cards[0]));
    REQUIRE(engine->WindowOpen());

    // INFO: vanilla:turn_order denies the out-of-turn response.
    CHECK_FALSE(engine->RespondWindow("player1", others[0]));
    const json* rejected = FindEvent(*engine, "play_rejected");
    REQUIRE(rejected != nullptr);
    CHECK((*rejected)["payload"]["reason_id"] == "vanilla:turn_order");
    CHECK(CountEvents(*engine, "window_response") == 0);

    // INFO: an allow entry rescues the otherwise-denied response.
    AllowAllPlays(*engine);
    CHECK(engine->RespondWindow("player1", others[0]));
    CHECK(CountEvents(*engine, "window_response") == 1);
}
