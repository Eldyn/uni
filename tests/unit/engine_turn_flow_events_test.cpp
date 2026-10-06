#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/view/event_sink.hpp>
#include <match/view/view_builder.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <functional>
/**
 * @file engine_turn_flow_events_test.cpp
 * @brief The turn-flow client events, from the op to the seat stream.
 *
 * `extra_turn_granted`, `active_type_changed`, `restriction_added`,
 * `restriction_removed` and `turn_timer_changed` come from their primitive.
 * `turn_skipped` comes from the turn advance that consumes the skip flag; the
 * flag remembers the seat that set it.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
namespace ops = match::ops;
using namespace match::engine;
using namespace match::modload;
using match::view::EventSink;
using match::view::ViewBuilder;
using match::view::Viewer;
using nlohmann::json;

namespace {

fs::path ProjectRoot() {
    fs::path path(__FILE__);
    while (!path.empty()) {
        std::error_code error;
        if (fs::is_directory(path / "contract" / "schemas", error)) {
            return path;
        }
        fs::path parent = path.parent_path();
        if (parent == path) break;
        path = parent;
    }
    return {};
}

/** @brief One core op and the client events that it must emit. */
struct Scenario {
    std::string event_type;
    /** Op nodes; `tag` keeps ids unique when two copies run in one match. */
    std::function<json(const std::string& tag)> nodes;
    /** Events that one run of `nodes` emits (of `event_type`). */
    std::size_t events_per_run = 1;
};

json Node(const std::string& id, const std::string& op, json args,
          const std::string& next = "") {
    json node = json{{"id", id}, {"op", op}, {"args", std::move(args)}};
    if (!next.empty()) node["next"] = next;
    return node;
}

json RestrictionDefinition(const std::string& id) {
    return json{{"id", id}, {"phase", "deny"},
                {"condition", json{{"always", true}}}};
}

std::vector<Scenario> Scenarios() {
    return {
        {"extra_turn_granted",
         [](const std::string& tag) {
             return json::array(
                 {Node(tag + "n1", "extra_turn", json{{"target", "@self"}})});
         }},
        {"active_type_changed",
         [](const std::string& tag) {
             return json::array(
                 {Node(tag + "n1", "set_active_type",
                       json{{"type", "blue"}})});
         }},
        {"restriction_added",
         [](const std::string& tag) {
             return json::array(
                 {Node(tag + "n1", "add_restriction",
                       json{{"entry_def",
                             RestrictionDefinition("fix:" + tag)}})});
         }},
        {"restriction_removed",
         [](const std::string& tag) {
             return json::array(
                 {Node(tag + "n1", "add_restriction",
                       json{{"entry_def",
                             RestrictionDefinition("fix:" + tag)}},
                       tag + "n2"),
                  Node(tag + "n2", "remove_restriction",
                       json{{"entry_id", "fix:" + tag}})});
         }},
        {"turn_timer_changed",
         [](const std::string& tag) {
             return json::array(
                 {Node(tag + "n1", "set_turn_timer",
                       json{{"target", "@self"},
                            {"duration",
                             json{{"unit", "ms"}, {"value", 5000}}}})});
         }},
        {"turn_skipped",
         [](const std::string& tag) {
             return json::array(
                 {Node(tag + "n1", "skip_turn",
                       json{{"target", "@next_player"}})});
         }},
    };
}

BehaviorGraph MakeGraph(json nodes) {
    BehaviorGraph graph;
    graph.nodes = std::move(nodes);
    graph.raw = json{{"nodes", graph.nodes}};
    return graph;
}

ModManifest Manifest(const std::string& id) {
    ModManifest manifest;
    manifest.id = id;
    manifest.name = id;
    manifest.version = "1.0.0";
    manifest.api = "1";
    return manifest;
}

CardDef FlipperCard(json nodes) {
    CardDef card;
    card.id = "flipper";
    card.namespace_id = "base";
    card.kind_id = "base:flipper";
    card.title = "flipper";
    card.face.kind = FaceKind::kText;
    card.face.color = std::string("red");
    card.face.label = std::string("1");
    card.tags = {"numbered"};
    BehaviorEntry entry;
    entry.hook = "on_play";
    entry.graph = MakeGraph(std::move(nodes));
    card.behaviors.push_back(std::move(entry));
    return card;
}

MutationDef Mutation(const std::string& mode, json nodes) {
    MutationDef mutation;
    mutation.id = "swap";
    mutation.namespace_id = "fix";
    mutation.mutation_id = "fix:swap";
    mutation.target = "base:flipper";
    mutation.mode = mode;
    mutation.replacement = MakeGraph(std::move(nodes));
    return mutation;
}

/** @brief A running match with the flipper card in the hand of player0. */
struct Fixture {
    std::unique_ptr<MatchInstance> engine;
    std::vector<LoadedMod> mods;
    ecs::Entity card;
    int player_count = 2;

    ecs::Entity Seat(int index) const {
        return *engine->FindPlayer("player" + std::to_string(index));
    }
};

Fixture MakeFixture(json card_nodes, const std::vector<MutationDef>& mutations,
                    int player_count = 2) {
    Fixture fixture;
    fixture.player_count = player_count;
    LoadedMod base;
    base.manifest = Manifest("base");
    base.cards.push_back(FlipperCard(std::move(card_nodes)));
    LoadedMod fix;
    fix.manifest = Manifest("fix");
    fix.mutations = mutations;
    fixture.mods = {base, fix};

    DeckDef deck;
    deck.id = "fixture";
    deck.namespace_id = "fixture";
    deck.deck_id = "fixture:deck";
    deck.name = "Fixture";
    deck.mods = {"base", "fix"};
    deck.cards = {{"base:flipper", 12}};

    MatchAssemblyOptions options;
    options.starting_cards = 2;
    options.seed = 7;
    for (int index = 0; index < player_count; ++index) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(fixture.mods, deck, options);
    REQUIRE(result.ok());
    fixture.engine =
        std::make_unique<MatchInstance>(std::move(result.assembly));

    std::vector<ecs::Entity> flippers;
    for (ecs::Entity candidate : fixture.engine->Registries().cards) {
        const ecs::CardIdentity* identity =
            fixture.engine->Store().Get<ecs::CardIdentity>(candidate);
        if (identity != nullptr && identity->kind_id == "base:flipper") {
            flippers.push_back(candidate);
        }
    }
    REQUIRE(flippers.size() >= 2);
    fixture.card = flippers[0];
    ecs::Hand* hand =
        fixture.engine->Store().Get<ecs::Hand>(fixture.Seat(0));
    REQUIRE(hand != nullptr);
    const std::vector<ecs::Entity> existing = hand->cards;
    for (ecs::Entity held : existing) {
        ops::MoveCardToZone(
            fixture.engine->Store(), held,
            ecs::ZoneRef{ecs::ZoneKind::kDrawPile, ecs::Entity{}});
    }
    ops::MoveCardToZone(
        fixture.engine->Store(), fixture.card,
        ecs::ZoneRef{ecs::ZoneKind::kHand, fixture.Seat(0)});
    // INFO: a second card keeps the hand non-empty, so the match goes on.
    ops::MoveCardToZone(
        fixture.engine->Store(), flippers[1],
        ecs::ZoneRef{ecs::ZoneKind::kHand, fixture.Seat(0)});
    fixture.engine->TakeEvents();
    return fixture;
}

std::vector<json> EventsOfType(const std::vector<json>& events,
                               const std::string& type) {
    std::vector<json> found;
    for (const json& event : events) {
        if (event.value("type", "") == type) found.push_back(event);
    }
    return found;
}

int PlayerCountFor(const Scenario& scenario) {
    return scenario.event_type == "turn_skipped" ? 3 : 2;
}

std::vector<json> PlayCardScenario(const Scenario& scenario) {
    Fixture fixture =
        MakeFixture(scenario.nodes("o"), {}, PlayerCountFor(scenario));
    REQUIRE(fixture.engine->PlayCard("player0", fixture.card));
    return fixture.engine->Events();
}

std::vector<json> PlayMutatedScenario(const Scenario& scenario,
                                      const std::string& mode) {
    json nodes = scenario.nodes("m");
    if (mode == "wrap") {
        const std::string last_id = nodes[nodes.size() - 1]["id"];
        nodes[nodes.size() - 1]["next"] = "call";
        nodes.push_back(json{{"id", "call"}, {"op", "call_original"}});
    }
    Fixture fixture = MakeFixture(scenario.nodes("o"),
                                  {Mutation(mode, nodes)},
                                  PlayerCountFor(scenario));
    REQUIRE(fixture.engine->PlayCard("player0", fixture.card));
    return fixture.engine->Events();
}

void AttachAfterPlaySystem(MatchInstance& engine, BehaviorGraph graph) {
    MatchAssembly& assembly = engine.Assembly();
    ModSystem system;
    system.mod_id = "test";
    system.registration_index = 0;
    system.hook = ecs::HookId{"play", ecs::HookPhase::kAfter};
    system.source_id = "test:turn_flow_system";
    system.graph = std::move(graph);
    const std::size_t index = assembly.systems.size();
    assembly.systems.push_back(std::move(system));
    assembly.bus.Subscribe(
        "test", 0, assembly.systems[index].hook,
        [&assembly, index](ecs::HookPayload& payload) {
            assembly.RunSystem(index, payload);
        });
}


/** @brief Run one op through the runtime, as the engine would. */
std::vector<json> InvokeOp(Fixture& fixture, const std::string& op,
                           json raw, std::optional<ecs::Entity> actor,
                           const std::vector<ecs::Entity>& targets = {}) {
    MatchAssembly& assembly = fixture.engine->Assembly();
    ops::ResolutionFrame frame;
    frame.causal.actor = actor;
    ops::OpContext context(assembly.bus, assembly.budget, frame);
    ops::OpArgs args(op, std::move(raw));
    if (!targets.empty()) args.BindSelector("target", targets);
    ops::OpResult result =
        assembly.runtime.Invoke(op, assembly.store, args, context);
    return result.events;
}

}  // namespace

TEST_CASE("turn flow events: a card play emits each event once") {
    for (const Scenario& scenario : Scenarios()) {
        CAPTURE(scenario.event_type);
        const std::vector<json> found = EventsOfType(
            PlayCardScenario(scenario), scenario.event_type);
        REQUIRE(found.size() == scenario.events_per_run);
        CHECK(found[0]["causal_frame"]["actor"] == "player0");
    }
}

TEST_CASE("turn flow events: a hook that calls the op emits each event") {
    for (const Scenario& scenario : Scenarios()) {
        CAPTURE(scenario.event_type);
        Fixture fixture = MakeFixture(
            json::array({Node("n1", "reverse_direction", json::object())}),
            {}, PlayerCountFor(scenario));
        // INFO: the card has no turn-flow op; the hook is the only source.
        AttachAfterPlaySystem(*fixture.engine,
                              MakeGraph(scenario.nodes("h")));
        REQUIRE(fixture.engine->PlayCard("player0", fixture.card));
        const std::vector<json> found =
            EventsOfType(fixture.engine->Events(), scenario.event_type);
        REQUIRE(found.size() == scenario.events_per_run);
        CHECK(found[0]["causal_frame"]["actor"] == "player0");
    }
}

TEST_CASE("turn flow events: a replacing mutation still emits each event") {
    for (const Scenario& scenario : Scenarios()) {
        CAPTURE(scenario.event_type);
        const std::vector<json> found = EventsOfType(
            PlayMutatedScenario(scenario, "replace"), scenario.event_type);
        REQUIRE(found.size() == scenario.events_per_run);
    }
}

TEST_CASE("turn flow events: a wrapping mutation emits once per op") {
    for (const Scenario& scenario : Scenarios()) {
        CAPTURE(scenario.event_type);
        const std::vector<json> found = EventsOfType(
            PlayMutatedScenario(scenario, "wrap"), scenario.event_type);
        // INFO: the wrapper and the original each run the op once. A skip
        //       that stacks on one seat is consumed once.
        // INFO: the original sets the same color again, which is no change.
        const bool runs_once = scenario.event_type == "turn_skipped"
                               || scenario.event_type == "active_type_changed";
        const std::size_t expected =
            runs_once ? 1 : 2 * scenario.events_per_run;
        CHECK(found.size() == expected);
    }
}

TEST_CASE("turn_skipped: the skip names the skipped seat and the causer") {
    Fixture fixture = MakeFixture(Scenarios().back().nodes("o"), {}, 3);
    REQUIRE(fixture.engine->PlayCard("player0", fixture.card));
    const std::vector<json> skipped =
        EventsOfType(fixture.engine->Events(), "turn_skipped");
    REQUIRE(skipped.size() == 1);
    CHECK(skipped[0]["payload"]["target"]["index"] ==
          fixture.Seat(1).index);
    CHECK(skipped[0]["causal_frame"]["actor"] == "player0");
    // INFO: the event marks the advance, so it comes before the new turn.
    const std::vector<json>& events = fixture.engine->Events();
    std::size_t skipped_at = 0;
    std::size_t advance_at = 0;
    for (std::size_t index = 0; index < events.size(); ++index) {
        const std::string type = events[index].value("type", "");
        if (type == "turn_skipped") skipped_at = index;
        if (type == "turn_advance") advance_at = index;
    }
    CHECK(skipped_at < advance_at);
}

TEST_CASE("turn_skipped: two skips on one seat give one event") {
    json nodes = json::array(
        {Node("a", "skip_turn", json{{"target", "@next_player"}}, "b"),
         Node("b", "skip_turn", json{{"target", "@next_player"}})});
    Fixture fixture = MakeFixture(nodes, {}, 3);
    REQUIRE(fixture.engine->PlayCard("player0", fixture.card));
    CHECK(EventsOfType(fixture.engine->Events(), "turn_skipped").size() == 1);
}

TEST_CASE("turn_skipped: the flag clears, so a later advance skips nobody") {
    Fixture fixture = MakeFixture(Scenarios().back().nodes("o"), {}, 3);
    REQUIRE(fixture.engine->PlayCard("player0", fixture.card));
    const std::vector<json> first =
        InvokeOp(fixture, "advance_turn", json::object(), std::nullopt);
    CHECK(EventsOfType(first, "turn_skipped").empty());
}

TEST_CASE("turn_skipped: a skip remembers the last actor that set it") {
    Fixture fixture = MakeFixture(json::array(), {}, 3);
    const ecs::Entity victim = fixture.Seat(1);
    InvokeOp(fixture, "skip_turn", json::object(), fixture.Seat(0), {victim});
    InvokeOp(fixture, "skip_turn", json::object(), fixture.Seat(2), {victim});
    const std::vector<json> events =
        InvokeOp(fixture, "advance_turn", json::object(), std::nullopt);
    const std::vector<json> skipped = EventsOfType(events, "turn_skipped");
    REQUIRE(skipped.size() == 1);
    CHECK(skipped[0]["causal_frame"]["actor"] == "player2");
}

TEST_CASE("turn_skipped: a skip with no actor names no actor") {
    Fixture fixture = MakeFixture(json::array(), {}, 3);
    InvokeOp(fixture, "skip_turn", json::object(), std::nullopt,
             {fixture.Seat(1)});
    const std::vector<json> events =
        InvokeOp(fixture, "advance_turn", json::object(), std::nullopt);
    const std::vector<json> skipped = EventsOfType(events, "turn_skipped");
    REQUIRE(skipped.size() == 1);
    CHECK(skipped[0]["causal_frame"]["actor"].is_null());
}

TEST_CASE("turn flow events: payloads carry the changed values") {
    SUBCASE("active_type_changed") {
        Fixture fixture = MakeFixture(
            Scenarios()[1].nodes("o"), {}, 2);
        REQUIRE(fixture.engine->PlayCard("player0", fixture.card));
        const std::vector<json> found =
            EventsOfType(fixture.engine->Events(), "active_type_changed");
        REQUIRE(found.size() == 1);
        CHECK(found[0]["payload"]["color"] == "blue");
        CHECK(found[0]["payload"]["previous_color"] == "red");
    }
    SUBCASE("restriction_removed") {
        const std::vector<json> found = EventsOfType(
            PlayCardScenario(Scenarios()[3]), "restriction_removed");
        REQUIRE(found.size() == 1);
        CHECK(found[0]["payload"]["restriction_id"] == "fix:o");
    }
    SUBCASE("turn_timer_changed") {
        const std::vector<json> found = EventsOfType(
            PlayCardScenario(Scenarios()[4]), "turn_timer_changed");
        REQUIRE(found.size() == 1);
        CHECK(found[0]["payload"]["duration_ms"] == 5000);
        CHECK_FALSE(found[0]["payload"].contains("deadline_ms"));
    }
}

TEST_CASE("turn flow events: every seat and a spectator get the event") {
    for (const Scenario& scenario : Scenarios()) {
        CAPTURE(scenario.event_type);
        Fixture fixture = MakeFixture(scenario.nodes("o"), {},
                                      PlayerCountFor(scenario));
        REQUIRE(fixture.engine->PlayCard("player0", fixture.card));
        const std::vector<json> found =
            EventsOfType(fixture.engine->Events(), scenario.event_type);
        REQUIRE(found.size() == 1);

        ViewBuilder builder(*fixture.engine, fixture.mods);
        std::vector<Viewer> viewers = {Viewer::PublicSpectator()};
        for (int index = 0; index < fixture.player_count; ++index) {
            viewers.push_back(Viewer::Player("player" + std::to_string(index)));
        }
        for (const Viewer& viewer : viewers) {
            EventSink sink;
            const std::optional<json> wrapped =
                builder.Wrap(found[0], viewer, sink);
            REQUIRE(wrapped.has_value());
            CHECK((*wrapped)["id"] == scenario.event_type);
            CHECK((*wrapped)["type"] == scenario.event_type);
            CHECK((*wrapped)["actor"] == "player0");
            CHECK((*wrapped).contains("sequence"));
            CHECK((*wrapped).contains("turn"));
            CHECK((*wrapped).contains("round"));
            const json& payload = (*wrapped)["payload"];
            if (scenario.event_type == "turn_skipped"
                || scenario.event_type == "extra_turn_granted"
                || scenario.event_type == "turn_timer_changed") {
                CHECK(payload["target"].is_string());
            }
            if (scenario.event_type == "turn_skipped") {
                CHECK(payload["target"] == "player1");
            }
            if (scenario.event_type == "extra_turn_granted") {
                CHECK(payload["target"] == "player0");
            }
        }
    }
}

TEST_CASE("turn flow events: a restriction event sends no condition") {
    Fixture fixture = MakeFixture(Scenarios()[2].nodes("o"), {}, 2);
    REQUIRE(fixture.engine->PlayCard("player0", fixture.card));
    const std::vector<json> found =
        EventsOfType(fixture.engine->Events(), "restriction_added");
    REQUIRE(found.size() == 1);
    ViewBuilder builder(*fixture.engine, fixture.mods);
    EventSink sink;
    const std::optional<json> wrapped =
        builder.Wrap(found[0], Viewer::Player("player1"), sink);
    REQUIRE(wrapped.has_value());
    const json& payload = (*wrapped)["payload"];
    CHECK(payload["restriction_id"] == "fix:o");
    CHECK(payload["phase"] == "deny");
    CHECK_FALSE(payload.contains("condition"));
    CHECK(payload.size() == 2);
}
