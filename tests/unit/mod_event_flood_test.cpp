#include <doctest/doctest.h>

#include <common/env.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/view/view_builder.hpp>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <optional>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/**
 * @file mod_event_flood_test.cpp
 * @brief A mod that floods mod events (ticket 13), through the whole engine.
 *
 * A flood system runs on the `draw_attempt` hook, so that one draw by the
 * current seat is one turn. The golden file
 * `tests/fixtures/golden_mod_event_budget/flood_stream.json` holds the
 * streams of one flood scenario. Set `UNI_UPDATE_GOLDEN=1` to write it
 * again; a default run never writes it.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
using namespace match::engine;
using namespace match::modload;
using match::view::ViewBuilder;
using match::view::Viewer;
using nlohmann::json;

namespace {

constexpr const char* kEventId = "test:apple_eaten";
constexpr int kPlayerCount = 3;

struct ConsoleCapture {
    std::ostringstream text;
    std::streambuf* saved;
    ConsoleCapture() : saved(std::cout.rdbuf(text.rdbuf())) {}
    ~ConsoleCapture() { std::cout.rdbuf(saved); }
    std::size_t Count(const std::string& needle) const {
        std::size_t count = 0;
        const std::string all = text.str();
        for (std::size_t at = all.find(needle); at != std::string::npos;
             at = all.find(needle, at + 1)) {
            ++count;
        }
        return count;
    }
};

struct EnvOverride {
    std::string key;
    EnvOverride(std::string name, const std::string& value)
        : key(std::move(name)) {
        Env::SetEnv(key, value);
    }
    ~EnvOverride() { Env::SetEnv(key, std::string()); }
};

fs::path ProjectRoot() {
    fs::path path(__FILE__);
    while (!path.empty()) {
        std::error_code error;
        if (fs::is_directory(path / "contract" / "schemas", error)) {
            return path;
        }
        const fs::path parent = path.parent_path();
        if (parent == path) break;
        path = parent;
    }
    return {};
}

struct Content {
    std::vector<LoadedMod> mods;
    DeckDef deck;
};

Content LoadContent() {
    Content content;
    LoadResult load = ScanModsDirectory((ProjectRoot() / "mods").string());
    REQUIRE(load.ok());
    content.mods = std::move(load.mods);
    for (const LoadedMod& mod : content.mods) {
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == "test:classic_bombs") content.deck = deck;
        }
    }
    REQUIRE_FALSE(content.deck.deck_id.empty());
    return content;
}

std::unique_ptr<MatchInstance> MakeEngine(
    Content& content, std::optional<EventDef> extra_event = std::nullopt) {
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (int index = 0; index < kPlayerCount; ++index) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyContentResult prepared =
        MatchAssembler::PrepareContent(content.mods, content.deck);
    REQUIRE(prepared.ok());
    if (extra_event.has_value()) {
        prepared.content->events[extra_event->event_id] = *extra_event;
    }
    AssemblyResult result =
        MatchAssembler::Assemble(prepared.content, content.deck, options);
    REQUIRE(result.ok());
    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

json EmitNode(int index, int count, const std::string& event_id,
              const json& payload) {
    json node = {{"id", "n" + std::to_string(index)},
                 {"op", "emit_event"},
                 {"args", {{"id", event_id}, {"payload", payload}}}};
    if (index < count) node["next"] = "n" + std::to_string(index + 1);
    return node;
}

json FloodNodes(int count, const std::string& event_id = kEventId) {
    const json payload = {{"color", "red"}, {"count", 1}};
    json nodes = json::array();
    for (int index = 1; index <= count; ++index) {
        nodes.push_back(EmitNode(index, count, event_id, payload));
    }
    return nodes;
}

void AttachDrawAttemptSystem(MatchInstance& engine, const std::string& mod_id,
                             json nodes,
                             const std::string& hook_name = "draw_attempt") {
    BehaviorGraph graph;
    graph.nodes = std::move(nodes);
    graph.raw = json{{"nodes", graph.nodes}};
    MatchAssembly& assembly = engine.Assembly();
    ModSystem system;
    system.mod_id = mod_id;
    system.registration_index = 0;
    system.hook = ecs::HookId{hook_name, ecs::HookPhase::kAfter};
    system.source_id = mod_id + ":flood_system";
    system.graph = std::move(graph);
    const std::size_t index = assembly.systems.size();
    assembly.systems.push_back(std::move(system));
    assembly.bus.Subscribe(
        mod_id, 0, assembly.systems[index].hook,
        [&assembly, index](ecs::HookPayload& payload) {
            assembly.RunSystem(index, payload);
        });
}

uint32_t CurrentTurn(MatchInstance& engine) {
    ecs::EntityStore& store = engine.Store();
    for (ecs::Entity match : store.EntitiesWith<ecs::MatchMeta>()) {
        return store.Get<ecs::MatchMeta>(match)->turn;
    }
    return 0;
}

/** @brief The current seat draws; the turn then passes to the next seat. */
void EndTurnByDrawing(MatchInstance& engine) {
    const std::string name = engine.GetCurrentPlayerUsername();
    REQUIRE(engine.DrawCard(name));
    if (engine.PendingPlayDrawnState().has_value()) {
        REQUIRE(engine.KeepDrawn(name));
    }
}

std::vector<json> EventsOfType(MatchInstance& engine,
                               const std::string& type) {
    std::vector<json> found;
    for (const json& event : engine.Events()) {
        if (event.value("type", "") == type) found.push_back(event);
    }
    return found;
}

std::vector<std::string> CoreTypes(const std::vector<json>& packets) {
    std::vector<std::string> types;
    for (const json& packet : packets) {
        const std::string id = packet.value("id", "");
        if (id.find(':') == std::string::npos) types.push_back(id);
    }
    return types;
}

std::vector<json> PacketsFor(MatchInstance& engine, Content& content,
                             const Viewer& viewer) {
    const ViewBuilder builder(engine, content.mods);
    return builder.BuildPackets(viewer);
}

void CheckGapFree(const std::vector<json>& packets) {
    for (std::size_t index = 0; index < packets.size(); ++index) {
        CHECK(packets[index]["sequence"] == index);
    }
}

struct FloodRun {
    Content content;
    std::unique_ptr<MatchInstance> engine;
};

FloodRun StartFlood(int node_count, int turns) {
    FloodRun run;
    run.content = LoadContent();
    run.engine = MakeEngine(run.content);
    if (node_count > 0) {
        AttachDrawAttemptSystem(*run.engine, "test", FloodNodes(node_count));
    }
    for (int turn = 0; turn < turns; ++turn) EndTurnByDrawing(*run.engine);
    return run;
}

const std::vector<Viewer>& AllViewers() {
    static const std::vector<Viewer> viewers = {
        Viewer::Player("player0"), Viewer::Player("player1"),
        Viewer::Player("player2"), Viewer::PublicSpectator()};
    return viewers;
}

}  // namespace

TEST_CASE("mod event flood: the turn counter moves on each draw") {
    FloodRun run = StartFlood(0, 0);
    const uint32_t first = CurrentTurn(*run.engine);
    EndTurnByDrawing(*run.engine);
    CHECK(CurrentTurn(*run.engine) == first + 1);
}

TEST_CASE("mod event flood: the extra events of one turn are lost and the "
          "core events still arrive in the same stream") {
    EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "5");
    EnvOverride chain("UNI_MOD_CAUSE_CHAIN_MAX", "1000");
    FloodRun flood = StartFlood(12, 1);
    FloodRun baseline = StartFlood(0, 1);

    CHECK(EventsOfType(*flood.engine, "mod_event").size() == 5);
    const auto& counts =
        flood.engine->Assembly().budget.mod_events.DropCounts();
    REQUIRE(counts.size() == 1);
    CHECK(counts.at("test|test:apple_eaten|per_turn") == 7);

    for (const Viewer& viewer : AllViewers()) {
        const std::vector<json> packets =
            PacketsFor(*flood.engine, flood.content, viewer);
        const std::vector<json> expected =
            PacketsFor(*baseline.engine, baseline.content, viewer);
        CheckGapFree(packets);
        CHECK(packets.size() == expected.size() + 5);
        CHECK(CoreTypes(packets) == CoreTypes(expected));
    }
}

TEST_CASE("mod event flood: a limit of zero drops every mod event") {
    EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "0");
    FloodRun flood = StartFlood(3, 1);
    CHECK(EventsOfType(*flood.engine, "mod_event").empty());
    CHECK_FALSE(flood.engine->Assembly().bus.IsDisarmed("test"));
}

TEST_CASE("mod event flood: the count restarts on each turn") {
    EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "3");
    EnvOverride chain("UNI_MOD_CAUSE_CHAIN_MAX", "1000");
    EnvOverride violation("UNI_MOD_VIOLATION_TURNS", "1000");
    FloodRun flood = StartFlood(5, 3);

    const std::vector<json> events = EventsOfType(*flood.engine, "mod_event");
    CHECK(events.size() == 9);
    std::vector<uint32_t> turns;
    for (const json& event : events) {
        const uint32_t turn = event["causal_frame"]["turn"];
        if (turns.empty() || turns.back() != turn) turns.push_back(turn);
    }
    CHECK(turns.size() == 3);
    CHECK(flood.engine->Assembly().budget.mod_events.DropCounts().at(
              "test|test:apple_eaten|per_turn")
          == 6);
}

TEST_CASE("mod event flood: the cause chain stops at the maximum length") {
    EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "1000");
    EnvOverride chain("UNI_MOD_CAUSE_CHAIN_MAX", "3");
    FloodRun flood = StartFlood(6, 1);

    const std::vector<json> events = EventsOfType(*flood.engine, "mod_event");
    REQUIRE(events.size() == 3);
    CHECK(events[1]["causal_frame"]["cause_event"] == events[0]["event_id"]);
    CHECK(events[2]["causal_frame"]["cause_event"] == events[1]["event_id"]);
    CHECK(events[0]["causal_frame"]["cause_event"]
          != events[0]["event_id"]);
    CHECK(flood.engine->Assembly().budget.mod_events.DropCounts().at(
              "test|test:apple_eaten|cause_chain")
          == 3);
}

TEST_CASE("mod event flood: a new entry point starts a new cause chain") {
    EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "1000");
    EnvOverride chain("UNI_MOD_CAUSE_CHAIN_MAX", "3");
    EnvOverride violation("UNI_MOD_VIOLATION_TURNS", "1000");
    FloodRun flood = StartFlood(6, 2);
    CHECK(EventsOfType(*flood.engine, "mod_event").size() == 6);
}

TEST_CASE("mod event flood: a chain of length one never reaches the "
          "chain limit") {
    EnvOverride chain("UNI_MOD_CAUSE_CHAIN_MAX", "1");
    FloodRun flood = StartFlood(1, 1);
    CHECK(EventsOfType(*flood.engine, "mod_event").size() == 1);
    CHECK(flood.engine->Assembly().budget.mod_events.DropCounts().empty());
}

TEST_CASE("mod event flood: a repeat offender is disarmed after three "
          "turns") {
    EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "2");
    EnvOverride chain("UNI_MOD_CAUSE_CHAIN_MAX", "1000");
    FloodRun flood = StartFlood(4, 2);
    MatchInstance& engine = *flood.engine;

    CHECK_FALSE(engine.Assembly().bus.IsDisarmed("test"));
    CHECK(EventsOfType(engine, "mod_disarmed").empty());
    EndTurnByDrawing(engine);
    CHECK(engine.Assembly().bus.IsDisarmed("test"));
    const std::vector<json> disarmed = EventsOfType(engine, "mod_disarmed");
    REQUIRE(disarmed.size() == 1);
    CHECK(disarmed[0]["payload"]["mod"] == "test");

    const std::size_t before = EventsOfType(engine, "mod_event").size();
    EndTurnByDrawing(engine);
    CHECK(EventsOfType(engine, "mod_event").size() == before);
    CHECK(EventsOfType(engine, "mod_disarmed").size() == 1);
}

TEST_CASE("mod event flood: the disarm event reaches no viewer and the "
          "streams stay gap free") {
    EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "2");
    EnvOverride chain("UNI_MOD_CAUSE_CHAIN_MAX", "1000");
    FloodRun flood = StartFlood(4, 4);
    REQUIRE(EventsOfType(*flood.engine, "mod_disarmed").size() == 1);
    for (const Viewer& viewer : AllViewers()) {
        const std::vector<json> packets =
            PacketsFor(*flood.engine, flood.content, viewer);
        CheckGapFree(packets);
        for (const json& packet : packets) {
            CHECK(packet.value("id", "") != "mod_disarmed");
        }
    }
}

TEST_CASE("mod event flood: a flood in one turn does not disarm") {
    EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "2");
    EnvOverride chain("UNI_MOD_CAUSE_CHAIN_MAX", "1000");
    FloodRun flood = StartFlood(50, 1);
    CHECK_FALSE(flood.engine->Assembly().bus.IsDisarmed("test"));
    CHECK(EventsOfType(*flood.engine, "mod_disarmed").empty());
    CHECK(flood.engine->Assembly().budget.mod_events.ViolationTurnCount(
              "test")
          == 1);
}

TEST_CASE("mod event flood: a flood in two turns does not disarm") {
    EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "2");
    EnvOverride chain("UNI_MOD_CAUSE_CHAIN_MAX", "1000");
    FloodRun flood = StartFlood(50, 2);
    CHECK_FALSE(flood.engine->Assembly().bus.IsDisarmed("test"));
}

TEST_CASE("mod event flood: UNI_MOD_VIOLATION_TURNS sets the number of "
          "turns") {
    EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "2");
    EnvOverride chain("UNI_MOD_CAUSE_CHAIN_MAX", "1000");
    EnvOverride violation("UNI_MOD_VIOLATION_TURNS", "1");
    FloodRun flood = StartFlood(4, 1);
    CHECK(flood.engine->Assembly().bus.IsDisarmed("test"));
}

TEST_CASE("mod event flood: a quiet turn between floods does not hide the "
          "count of distinct turns") {
    EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "2");
    EnvOverride chain("UNI_MOD_CAUSE_CHAIN_MAX", "1000");
    FloodRun flood = StartFlood(4, 3);
    CHECK(flood.engine->Assembly().bus.IsDisarmed("test"));
    CHECK(flood.engine->Assembly().budget.mod_events.ViolationTurnCount(
              "test")
          == 3);
}

TEST_CASE("mod event flood: an offender does not disarm or limit another "
          "mod") {
    EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "2");
    EnvOverride chain("UNI_MOD_CAUSE_CHAIN_MAX", "1000");
    Content content = LoadContent();
    EventDef seven_zero_event;
    seven_zero_event.id = "ping";
    seven_zero_event.namespace_id = "seven_zero";
    seven_zero_event.event_id = "seven_zero:ping";
    seven_zero_event.retention = "log";
    seven_zero_event.fields.push_back(
        EventFieldDef{"count", "integer", "all"});
    seven_zero_event.fields.push_back(EventFieldDef{"color", "color", "all"});
    auto engine = MakeEngine(content, seven_zero_event);
    AttachDrawAttemptSystem(*engine, "test", FloodNodes(4));
    AttachDrawAttemptSystem(*engine, "seven_zero",
                            FloodNodes(2, "seven_zero:ping"));
    for (int turn = 0; turn < 4; ++turn) EndTurnByDrawing(*engine);

    CHECK(engine->Assembly().bus.IsDisarmed("test"));
    CHECK_FALSE(engine->Assembly().bus.IsDisarmed("seven_zero"));
    std::size_t quiet_events = 0;
    for (const json& event : EventsOfType(*engine, "mod_event")) {
        if (event["payload"]["id"] == "seven_zero:ping") ++quiet_events;
    }
    CHECK(quiet_events == 8);
    CHECK(engine->Assembly().budget.mod_events.ViolationTurnCount(
              "seven_zero")
          == 0);
}

TEST_CASE("mod event flood: the event budget still drops a mod event and "
          "keeps a core event") {
    EnvOverride event_budget("UNI_EVENT_BUDGET", "1");
    EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "1000");
    EnvOverride chain("UNI_MOD_CAUSE_CHAIN_MAX", "1000");
    FloodRun flood = StartFlood(5, 1);
    FloodRun baseline = StartFlood(0, 1);
    CHECK(EventsOfType(*flood.engine, "mod_event").size() < 5);
    CHECK(flood.engine->Assembly().budget.mod_events.DropCounts().empty());
    for (const Viewer& viewer : AllViewers()) {
        CHECK(CoreTypes(PacketsFor(*flood.engine, flood.content, viewer))
              == CoreTypes(
                  PacketsFor(*baseline.engine, baseline.content, viewer)));
    }
}

TEST_CASE("mod event flood: both budgets together count each event once") {
    EnvOverride event_budget("UNI_EVENT_BUDGET", "100000");
    EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "4");
    EnvOverride chain("UNI_MOD_CAUSE_CHAIN_MAX", "1000");
    EnvOverride violation("UNI_MOD_VIOLATION_TURNS", "1000");
    FloodRun flood = StartFlood(6, 2);
    CHECK(EventsOfType(*flood.engine, "mod_event").size() == 8);
    CHECK(flood.engine->Assembly().budget.mod_events.DropCounts().at(
              "test|test:apple_eaten|per_turn")
          == 4);
}

TEST_CASE("mod event flood: a drop never removes a core event") {
    EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "0");
    Content content = LoadContent();
    DeckDef deck;
    for (const LoadedMod& mod : content.mods) {
        for (const DeckDef& candidate : mod.decks) {
            if (candidate.deck_id == "uni:classic") deck = candidate;
        }
    }
    REQUIRE_FALSE(deck.deck_id.empty());
    deck.mods = {"uni", "draw_stacking", "test"};
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (int index = 0; index < kPlayerCount; ++index) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, deck, options);
    REQUIRE(result.ok());
    MatchInstance engine(std::move(result.assembly));
    AttachDrawAttemptSystem(engine, "test", FloodNodes(5), "play");

    std::optional<ecs::Entity> draw_two;
    for (ecs::Entity card : engine.Registries().cards) {
        const ecs::FaceSpec* face = engine.Store().Get<ecs::FaceSpec>(card);
        if (face != nullptr && face->color == "red" && face->label == "+2") {
            draw_two = card;
        }
    }
    REQUIRE(draw_two.has_value());
    const ecs::Entity player0 = *engine.FindPlayer("player0");
    ecs::Hand* hand = engine.Store().Get<ecs::Hand>(player0);
    const std::vector<ecs::Entity> existing = hand->cards;
    for (ecs::Entity card : existing) {
        match::ops::MoveCardToZone(
            engine.Store(), card,
            ecs::ZoneRef{ecs::ZoneKind::kDrawPile, ecs::Entity{}});
    }
    match::ops::MoveCardToZone(
        engine.Store(), *draw_two,
        ecs::ZoneRef{ecs::ZoneKind::kHand, player0});
    engine.Store().Get<ecs::ActiveTypeReq>(engine.Registries().match)->type =
        "red";
    REQUIRE(engine.PlayCard("player0", *draw_two));

    CHECK(EventsOfType(engine, "mod_event").empty());
    CHECK(engine.Assembly().budget.mod_events.DropCounts().at(
              "test|test:apple_eaten|per_turn")
          == 5);
    CHECK(EventsOfType(engine, "window_opened").size() == 1);
    CHECK(EventsOfType(engine, "card_played").size() == 1);
}

TEST_CASE("mod event flood: an oversize payload in three turns disarms "
          "and drops the events that the mod emitted before it") {
    EnvOverride payload_max("UNI_MOD_EVENT_PAYLOAD_MAX", "200");
    EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "1000");
    EnvOverride cause_chain("UNI_MOD_CAUSE_CHAIN_MAX", "1000");
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    const json small = {{"color", "red"}, {"count", 1}};
    const json large = {{"color", "red"},
                        {"count", 1},
                        {"secret", std::string(250, 'x')}};
    json nodes = json::array();
    nodes.push_back(EmitNode(1, 2, kEventId, small));
    nodes.push_back(EmitNode(2, 2, kEventId, large));
    AttachDrawAttemptSystem(*engine, "test", nodes);

    EndTurnByDrawing(*engine);
    EndTurnByDrawing(*engine);
    CHECK_FALSE(engine->Assembly().bus.IsDisarmed("test"));
    CHECK(EventsOfType(*engine, "mod_event").size() == 2);
    ConsoleCapture console;
    EndTurnByDrawing(*engine);
    CHECK(engine->Assembly().bus.IsDisarmed("test"));
    CHECK(EventsOfType(*engine, "mod_event").size() == 2);
    CHECK(EventsOfType(*engine, "mod_disarmed").size() == 1);
    CHECK(console.Count("disarmed") >= 1);
    CHECK(engine->Assembly().budget.mod_events.DropCounts().at(
              "test|test:apple_eaten|payload_bytes")
          == 3);
}

TEST_CASE("mod event flood: the chain budget still aborts and the new "
          "budget counts each event once") {
    EnvOverride chain_budget("UNI_CHAIN_BUDGET", "6");
    EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "3");
    EnvOverride cause_chain("UNI_MOD_CAUSE_CHAIN_MAX", "1000");
    FloodRun flood = StartFlood(12, 1);
    MatchInstance& engine = *flood.engine;

    CHECK(EventsOfType(engine, "chain_aborted").size() == 1);
    CHECK(EventsOfType(engine, "mod_event").size() == 3);
    CHECK(engine.Assembly().budget.mod_events.DropCounts().at(
              "test|test:apple_eaten|per_turn")
          == 3);
}

TEST_CASE("mod event flood: the chain budget resets on each entry point "
          "next to the per turn reset") {
    EnvOverride chain_budget("UNI_CHAIN_BUDGET", "100");
    EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "1000");
    EnvOverride cause_chain("UNI_MOD_CAUSE_CHAIN_MAX", "1000");
    FloodRun flood = StartFlood(20, 3);
    CHECK(EventsOfType(*flood.engine, "chain_aborted").empty());
    CHECK(EventsOfType(*flood.engine, "mod_event").size() == 60);
}

TEST_CASE("mod event flood: the old abort path and the new path disarm a "
          "mod once") {
    EnvOverride chain_budget("UNI_CHAIN_BUDGET", "6");
    EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "3");
    EnvOverride cause_chain("UNI_MOD_CAUSE_CHAIN_MAX", "1000");
    FloodRun flood = StartFlood(12, 4);
    CHECK(flood.engine->Assembly().bus.IsDisarmed("test"));
    CHECK(EventsOfType(*flood.engine, "mod_disarmed").size() == 1);
}

TEST_CASE("mod event flood: the old abort path alone still disarms") {
    EnvOverride chain_budget("UNI_CHAIN_BUDGET", "4");
    EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "1000");
    EnvOverride cause_chain("UNI_MOD_CAUSE_CHAIN_MAX", "1000");
    FloodRun flood = StartFlood(12, 3);
    CHECK(flood.engine->Assembly().bus.IsDisarmed("test"));
    CHECK(EventsOfType(*flood.engine, "mod_disarmed").size() == 1);
    CHECK(flood.engine->Assembly().budget.mod_events.DropCounts().empty());
}

namespace {

/** @brief The streams, the drop counts and the disarm of one flood. */
json DescribeFlood(FloodRun& flood) {
    json streams = json::object();
    for (const Viewer& viewer : AllViewers()) {
        json lines = json::array();
        for (const json& packet :
             PacketsFor(*flood.engine, flood.content, viewer)) {
            lines.push_back(json{{"sequence", packet["sequence"]},
                                 {"id", packet["id"]}});
        }
        streams[viewer.is_spectator ? "public_spectator"
                                    : viewer.username] = std::move(lines);
    }
    json drops = json::object();
    for (const auto& [key, count] :
         flood.engine->Assembly().budget.mod_events.DropCounts()) {
        drops[key] = count;
    }
    return json{{"streams", std::move(streams)},
                {"drops", std::move(drops)},
                {"disarmed", flood.engine->Assembly().bus.IsDisarmed("test")}};
}

}  // namespace

TEST_CASE("mod event flood: the flood streams match the golden file") {
    json actual = json::object();
    {
        EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "4");
        EnvOverride chain("UNI_MOD_CAUSE_CHAIN_MAX", "1000");
        EnvOverride violation("UNI_MOD_VIOLATION_TURNS", "1000");
        FloodRun flood = StartFlood(8, 2);
        actual["per_turn_budget"] = DescribeFlood(flood);
    }
    {
        EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "1000");
        EnvOverride chain("UNI_MOD_CAUSE_CHAIN_MAX", "3");
        EnvOverride violation("UNI_MOD_VIOLATION_TURNS", "1000");
        FloodRun flood = StartFlood(8, 2);
        actual["cause_chain_budget"] = DescribeFlood(flood);
    }
    {
        EnvOverride per_turn("UNI_MOD_EVENTS_PER_TURN", "2");
        EnvOverride chain("UNI_MOD_CAUSE_CHAIN_MAX", "1000");
        FloodRun flood = StartFlood(5, 4);
        actual["repeat_offender_disarmed"] = DescribeFlood(flood);
    }

    const fs::path golden = ProjectRoot() / "tests" / "fixtures"
                            / "golden_mod_event_budget" / "flood_stream.json";
    if (std::getenv("UNI_UPDATE_GOLDEN") != nullptr) {
        fs::create_directories(golden.parent_path());
        std::ofstream(golden) << actual.dump(2) << "\n";
    }
    std::ifstream input(golden);
    REQUIRE_MESSAGE(input.good(), "missing golden file " << golden);
    CHECK(json::parse(input) == actual);
}
