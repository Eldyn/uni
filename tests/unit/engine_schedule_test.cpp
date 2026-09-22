#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
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
 * @file engine_schedule_test.cpp
 * @brief Scheduled-graph driver tests for `match::engine::MatchInstance`
 *
 * Assembles a real match from the shipped `mods/` tree, attaches a synthetic
 * mod system whose behavior graph reaches a `schedule` node, and asserts:
 * an `ms` leg arms on resolve and only runs once `Tick` passes the deadline; a
 * `turns` leg runs on the turn-end driver rather than wall-clock; a scheduled
 * subgraph that pauses on `kNeedsInput` parks instead of being dropped; and a
 * malformed duration is not armed.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
namespace ops = match::ops;
using namespace match::engine;
using namespace match::ecs;
using namespace match::modload;

namespace {

using nlohmann::json;

/** @brief Deterministic wall clock for the scheduler / timers. */
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

std::unique_ptr<MatchInstance> Assemble(Content& content, int players,
                                        int cards, uint64_t seed,
                                        match::WindowConfig config,
                                        match::NowMs clock) {
    DeckDef deck = content.classic;
    deck.mods = {"vanilla"};
    AssemblyResult result = MatchAssembler::Assemble(
        content.mods, deck, Players(players, cards, seed));
    REQUIRE_MESSAGE(result.ok(), AssemblyMessage(result));
    return std::make_unique<MatchInstance>(std::move(result.assembly), config,
                                           std::move(clock));
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

std::size_t CountSignals(const MatchInstance& engine,
                         const std::string& name) {
    std::size_t count = 0;
    for (const json& event : engine.Events()) {
        if (!event.is_object() || event.value("type", "") != "signal") {
            continue;
        }
        const json payload =
            event.contains("payload") ? event["payload"] : json::object();
        if (payload.value("name", "") == name) ++count;
    }
    return count;
}

bool HasSignal(const MatchInstance& engine, const std::string& name) {
    return CountSignals(engine, name) > 0;
}

/** @brief True when `type` carries a payload referencing `card`. */
bool HasCardEvent(const MatchInstance& engine, const std::string& type,
                  ecs::Entity card) {
    for (const json& event : engine.Events()) {
        if (event.value("type", std::string()) != type) continue;
        if (!event.contains("payload") || !event["payload"].is_object()) {
            continue;
        }
        const json& payload = event["payload"];
        if (payload.contains("card") && payload["card"].is_object()
            && payload["card"].value("index", 0u) == card.index) {
            return true;
        }
    }
    return false;
}

ecs::PileContents* Pile(MatchInstance& engine, ecs::PileKind kind) {
    for (ecs::Entity entity :
         engine.Store().EntitiesWith<ecs::PileContents>()) {
        ecs::PileContents* contents =
            engine.Store().Get<ecs::PileContents>(entity);
        if (contents != nullptr && contents->kind == kind) return contents;
    }
    return nullptr;
}

/** @brief Move a numeric, non-wild card to the top of the draw pile. */
std::optional<ecs::Entity> NumericTopCard(MatchInstance& engine) {
    ecs::PileContents* draw = Pile(engine, ecs::PileKind::kDraw);
    if (draw == nullptr) return std::nullopt;
    for (std::size_t i = 0; i < draw->cards.size(); ++i) {
        const ecs::FaceSpec* face =
            engine.Store().Get<ecs::FaceSpec>(draw->cards[i]);
        if (face == nullptr || face->color == "white") continue;
        if (face->label.size() != 1) continue;
        if (!std::isdigit(static_cast<unsigned char>(face->label[0]))) {
            continue;
        }
        std::swap(draw->cards[i], draw->cards.back());
        for (std::size_t j = 0; j < draw->cards.size(); ++j) {
            if (ecs::InZone* in =
                    engine.Store().Get<ecs::InZone>(draw->cards[j])) {
                in->ordinal = static_cast<uint32_t>(j);
            }
        }
        return draw->cards.back();
    }
    return std::nullopt;
}

void SetActiveType(MatchInstance& engine, const std::string& type) {
    ecs::ActiveTypeReq* req = engine.Store().Get<ecs::ActiveTypeReq>(
        engine.Registries().match);
    REQUIRE(req != nullptr);
    req->type = type;
}

/** @brief Force the turn owner (test setup; bypasses `advance_turn`). */
void SetCurrentPlayer(MatchInstance& engine, const std::string& username) {
    for (ecs::Entity player : engine.Registries().players) {
        ecs::TurnState* turn = engine.Store().Get<ecs::TurnState>(player);
        if (turn == nullptr) continue;
        const ecs::PlayerInfo* info =
            engine.Store().Get<ecs::PlayerInfo>(player);
        turn->is_current = info != nullptr && info->username == username;
    }
}

/**
 * @brief Attach a synthetic mod system to the live bus (test setup).
 *
 * Mirrors what the engine does for a mod system so the engine's schedule
 * arm/expiry path runs it exactly as it runs shipped content.
 */
std::size_t AttachSystem(MatchInstance& engine, ecs::HookId hook,
                         const std::string& source_id, BehaviorGraph graph) {
    MatchAssembly& assembly = engine.Assembly();
    ModSystem system;
    system.mod_id = "test";
    system.registration_index = 0;
    system.hook = hook;
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

/** @brief Graph: `schedule` node `n1`, then `emit_signal` at `next`. */
BehaviorGraph ScheduleGraph(const json& duration, const std::string& signal) {
    BehaviorGraph graph;
    graph.nodes = json::array(
        {json{{"id", "n1"},
              {"schedule", true},
              {"next", "n2"},
              {"duration", duration}},
         json{{"id", "n2"},
              {"op", "emit_signal"},
              {"args", json{{"name", signal}}}}});
    graph.raw = json{{"nodes", graph.nodes}};
    return graph;
}

/** @brief Graph: a bare `prompt` op that parks on `kNeedsInput`. */
BehaviorGraph PromptGraph() {
    BehaviorGraph graph;
    graph.nodes = json::array(
        {json{{"id", "n1"},
              {"op", "prompt"},
              {"args", json{{"kind", "test_choice"},
                            {"target", "@current_player"}}}}});
    graph.raw = json{{"nodes", graph.nodes}};
    return graph;
}

/** @brief Graph: `schedule` node `n1`, then `play_card @card @self` at `n2`. */
BehaviorGraph SchedulePlayCardGraph(const json& duration) {
    BehaviorGraph graph;
    graph.nodes = json::array(
        {json{{"id", "n1"},
              {"schedule", true},
              {"next", "n2"},
              {"duration", duration}},
         json{{"id", "n2"},
              {"op", "play_card"},
              {"args", json{{"card", "@card"}, {"player", "@self"}}}}});
    graph.raw = json{{"nodes", graph.nodes}};
    return graph;
}

/** @brief Graph: `schedule` node `n1`, then a `prompt` op at `n2`. */
BehaviorGraph SchedulePromptGraph(const json& duration) {
    BehaviorGraph graph;
    graph.nodes = json::array(
        {json{{"id", "n1"},
              {"schedule", true},
              {"next", "n2"},
              {"duration", duration}},
         json{{"id", "n2"},
              {"op", "prompt"},
              {"args", json{{"kind", "test_choice"},
                            {"target", "@current_player"}}}}});
    graph.raw = json{{"nodes", graph.nodes}};
    return graph;
}

}  // namespace

TEST_CASE("engine schedule: ms leg arms on resolve and runs once after Tick") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine =
        Assemble(content, 3, 7, 42, FixedWindow(1000), clock.Fn());
    AttachSystem(*engine, ecs::HookId{"play", ecs::HookPhase::kAfter},
                 "test:schedule-ms",
                 ScheduleGraph(json{{"unit", "ms"}, {"value", 1000}},
                               "scheduled:done"));

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::vector<ecs::Entity> cards = LegalNumbered(*engine, 2);
    REQUIRE(cards.size() == 2);
    ForceHand(*engine, player0, cards);
    REQUIRE(engine->PlayCard("player0", cards[0]));

    // The graph resolved to the schedule node: armed, not yet run.
    CHECK_FALSE(HasSignal(*engine, "scheduled:done"));

    clock.now = 500;
    engine->Tick();
    CHECK_FALSE(HasSignal(*engine, "scheduled:done"));

    clock.now = 1000;
    engine->Tick();
    CHECK(CountSignals(*engine, "scheduled:done") == 1);

    engine->Tick();
    CHECK(CountSignals(*engine, "scheduled:done") == 1);
}

TEST_CASE("engine schedule: turns leg runs on turn_end, not wall clock") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine =
        Assemble(content, 3, 7, 42, FixedWindow(1000), clock.Fn());
    AttachSystem(*engine, ecs::HookId{"turn_start", ecs::HookPhase::kAfter},
                 "test:schedule-turns",
                 ScheduleGraph(json{{"unit", "turns"}, {"value", 1}},
                               "turns:done"));

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::vector<ecs::Entity> cards = LegalNumbered(*engine, 2);
    REQUIRE(cards.size() == 2);
    ForceHand(*engine, player0, cards);
    REQUIRE(engine->PlayCard("player0", cards[0]));

    // The play ended the turn, firing `turn_start` and arming the schedule.
    CHECK_FALSE(HasSignal(*engine, "turns:done"));

    // A wall-clock tick must not advance a `turns` leg.
    clock.now = 1000000;
    engine->Tick();
    CHECK_FALSE(HasSignal(*engine, "turns:done"));

    // The next ended turn drives it through `OnTurnEnd`.
    REQUIRE(engine->DrawCard(engine->GetCurrentPlayerUsername()));
    CHECK(CountSignals(*engine, "turns:done") == 1);
}

TEST_CASE("engine schedule: a scheduled subgraph that pauses on input parks") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine =
        Assemble(content, 3, 7, 42, FixedWindow(1000), clock.Fn());
    AttachSystem(*engine, ecs::HookId{"play", ecs::HookPhase::kAfter},
                 "test:schedule-prompt",
                 SchedulePromptGraph(json{{"unit", "ms"}, {"value", 1000}}));

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::vector<ecs::Entity> cards = LegalNumbered(*engine, 2);
    REQUIRE(cards.size() == 2);
    ForceHand(*engine, player0, cards);
    REQUIRE(engine->PlayCard("player0", cards[0]));

    CHECK_FALSE(engine->PendingInput().has_value());

    clock.now = 1500;
    engine->Tick();

    const std::optional<json> pending = engine->PendingInput();
    REQUIRE(pending.has_value());
    CHECK((*pending)["kind"] == "test_choice");
}

TEST_CASE("engine schedule: a malformed duration is not armed") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine =
        Assemble(content, 3, 7, 42, FixedWindow(1000), clock.Fn());
    AttachSystem(*engine, ecs::HookId{"play", ecs::HookPhase::kAfter},
                 "test:schedule-bad",
                 ScheduleGraph(json{{"unit", "fortnights"}, {"value", 3}},
                               "bad:done"));

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::vector<ecs::Entity> cards = LegalNumbered(*engine, 2);
    REQUIRE(cards.size() == 2);
    ForceHand(*engine, player0, cards);
    REQUIRE(engine->PlayCard("player0", cards[0]));

    clock.now = 1000000;
    engine->Tick();
    CHECK_FALSE(HasSignal(*engine, "bad:done"));
    CHECK_FALSE(engine->PendingInput().has_value());
}

TEST_CASE("engine schedule: an elapsed schedule is not lost behind a pause") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine =
        Assemble(content, 3, 7, 42, FixedWindow(1000), clock.Fn());
    AttachSystem(*engine, ecs::HookId{"play", ecs::HookPhase::kAfter},
                 "test:schedule-deferred",
                 ScheduleGraph(json{{"unit", "ms"}, {"value", 1000}},
                               "deferred:done"));
    AttachSystem(*engine, ecs::HookId{"play", ecs::HookPhase::kAfter},
                 "test:prompt", PromptGraph());

    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::vector<ecs::Entity> cards = LegalNumbered(*engine, 2);
    REQUIRE(cards.size() == 2);
    ForceHand(*engine, player0, cards);
    REQUIRE(engine->PlayCard("player0", cards[0]));

    // The play parked a prompt and armed the ms schedule.
    REQUIRE(engine->PendingInput().has_value());
    CHECK_FALSE(HasSignal(*engine, "deferred:done"));

    // The schedule elapses, but must wait behind the parked prompt.
    clock.now = 2000;
    engine->Tick();
    CHECK_FALSE(HasSignal(*engine, "deferred:done"));

    // Resolving the prompt runs the deferred schedule, not drops it.
    REQUIRE(engine->SubmitInput("player0", json{{"choice", 1}}));
    CHECK_FALSE(engine->PendingInput().has_value());
    CHECK(CountSignals(*engine, "deferred:done") == 1);
}

TEST_CASE("engine schedule: a scheduled graph force-plays its play_card") {
    Content content;
    REQUIRE(LoadContent(content));
    FakeClock clock;
    std::unique_ptr<MatchInstance> engine =
        Assemble(content, 2, 5, 7, FixedWindow(1000), clock.Fn());
    AttachSystem(*engine, ecs::HookId{"draw", ecs::HookPhase::kAfter},
                 "test:schedule-play",
                 SchedulePlayCardGraph(json{{"unit", "ms"}, {"value", 1000}}));

    const std::optional<ecs::Entity> drawn = NumericTopCard(*engine);
    REQUIRE(drawn.has_value());
    const ecs::FaceSpec* face = engine->Store().Get<ecs::FaceSpec>(*drawn);
    REQUIRE(face != nullptr);
    SetActiveType(*engine, face->color);

    REQUIRE(engine->DrawCard("player0"));
    // The schedule is armed but not yet elapsed: no forced play.
    CHECK_FALSE(HasCardEvent(*engine, "card_played", *drawn));

    // INFO: the draw advanced the turn; the deferred play targets the drawer,
    //       so restore the turn to make the forced play legal (test setup).
    SetCurrentPlayer(*engine, "player0");
    clock.now = 2000;
    engine->Tick();

    // The elapsed subgraph's `play_card` effect ran the normal play pipeline.
    CHECK(HasCardEvent(*engine, "card_played", *drawn));
    const ecs::InZone* zone = engine->Store().Get<ecs::InZone>(*drawn);
    REQUIRE(zone != nullptr);
    CHECK(zone->zone.kind == ecs::ZoneKind::kDiscardPile);
}
