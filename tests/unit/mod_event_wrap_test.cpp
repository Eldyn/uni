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
#include <vector>

/**
 * @file mod_event_wrap_test.cpp
 * @brief Which mod id a mutation graph runs under, for `emit_event`.
 *
 * RULE: a mutation replaces or wraps the graph of a system of the TARGET mod.
 * The system keeps the mod id of the target mod, so `OpContext::mod_id` is
 * the id of mod A while the graph of mod B runs. Result: the graph of mod B
 * emits an event of mod A, and the op drops an event of mod B. A mutation
 * cannot make mod A emit a forged event of another namespace.
 */

namespace ecs = match::ecs;
using namespace match::engine;
using namespace match::modload;
using nlohmann::json;

namespace {

json EmitNodeFor(const std::string& id, const std::string& event_id,
                 const std::string& next = std::string()) {
    json node = {{"id", id},
                 {"op", "emit_event"},
                 {"args", {{"id", event_id}}}};
    if (!next.empty()) node["next"] = next;
    return node;
}

BehaviorGraph GraphOf(json nodes) {
    BehaviorGraph graph;
    graph.nodes = std::move(nodes);
    graph.raw = json{{"nodes", graph.nodes}};
    return graph;
}

ModManifest ManifestOf(const std::string& id) {
    ModManifest manifest;
    manifest.id = id;
    manifest.name = id;
    manifest.version = "1.0.0";
    manifest.api = "1";
    return manifest;
}

EventDef EventOf(const std::string& ns, const std::string& local) {
    EventDef event;
    event.id = local;
    event.namespace_id = ns;
    event.event_id = ns + ":" + local;
    event.retention = "log";
    return event;
}

LoadedMod ModA() {
    LoadedMod mod;
    mod.manifest = ManifestOf("mod_a");
    mod.events.push_back(EventOf("mod_a", "ping"));
    CardDef card;
    card.id = "wild";
    card.namespace_id = "mod_a";
    card.kind_id = "mod_a:wild";
    card.title = "wild";
    card.face.kind = FaceKind::kText;
    card.face.color = std::string("red");
    card.face.label = std::string("1");
    card.tags = {"numbered"};
    BehaviorEntry entry;
    entry.hook = "on_play";
    entry.graph = GraphOf(json::array({EmitNodeFor("n1", "mod_a:ping")}));
    card.behaviors.push_back(std::move(entry));
    mod.cards.push_back(std::move(card));
    return mod;
}

LoadedMod ModBWithMutation(const std::string& mode, json nodes) {
    LoadedMod mod;
    mod.manifest = ManifestOf("mod_b");
    mod.events.push_back(EventOf("mod_b", "pong"));
    MutationDef mutation;
    mutation.id = "tweak";
    mutation.namespace_id = "mod_b";
    mutation.mutation_id = "mod_b:tweak";
    mutation.target = "mod_a:wild";
    mutation.mode = mode;
    mutation.replacement = GraphOf(std::move(nodes));
    mod.mutations.push_back(std::move(mutation));
    return mod;
}

std::vector<std::string> PlayAndListModEvents(const LoadedMod& mod_a,
                                              const LoadedMod& mod_b) {
    DeckDef deck;
    deck.id = "fixture";
    deck.namespace_id = "fixture";
    deck.deck_id = "fixture:deck";
    deck.name = "Fixture";
    deck.mods = {"mod_a", "mod_b"};
    deck.cards = {{"mod_a:wild", 8}};
    MatchAssemblyOptions options;
    options.starting_cards = 2;
    options.seed = 7;
    for (int index = 0; index < 2; ++index) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble({mod_a, mod_b}, deck, options);
    REQUIRE(result.ok());
    MatchInstance engine(std::move(result.assembly));

    std::optional<ecs::Entity> card;
    for (ecs::Entity candidate : engine.Registries().cards) {
        const ecs::CardIdentity* identity =
            engine.Store().Get<ecs::CardIdentity>(candidate);
        if (identity != nullptr && identity->kind_id == "mod_a:wild") {
            card = candidate;
            break;
        }
    }
    REQUIRE(card.has_value());
    const ecs::Entity player = *engine.FindPlayer("player0");
    const std::vector<ecs::Entity> existing =
        engine.Store().Get<ecs::Hand>(player)->cards;
    for (ecs::Entity held : existing) {
        match::ops::MoveCardToZone(
            engine.Store(), held,
            ecs::ZoneRef{ecs::ZoneKind::kDrawPile, ecs::Entity{}});
    }
    match::ops::MoveCardToZone(engine.Store(), *card,
                               ecs::ZoneRef{ecs::ZoneKind::kHand, player});
    engine.TakeEvents();
    REQUIRE(engine.PlayCard("player0", *card));

    std::vector<std::string> ids;
    for (const json& event : engine.Events()) {
        if (event.value("type", "") != "mod_event") continue;
        ids.push_back(event["payload"].value("id", ""));
    }
    return ids;
}

}  // namespace

TEST_CASE("mod event wrap: the wrapped graph runs under the target mod id") {
    const LoadedMod mod_a = ModA();

    SUBCASE("the wrap of mod B emits an event of mod B: dropped") {
        const LoadedMod mod_b = ModBWithMutation(
            "wrap", json::array({EmitNodeFor("w1", "mod_b:pong", "w2"),
                                 json{{"id", "w2"}, {"op", "call_original"}}}));
        const std::vector<std::string> ids =
            PlayAndListModEvents(mod_a, mod_b);
        REQUIRE(ids.size() == 1);
        CHECK(ids[0] == "mod_a:ping");
    }

    SUBCASE("the replace of mod B emits an event of mod A: emitted") {
        const LoadedMod mod_b = ModBWithMutation(
            "replace", json::array({EmitNodeFor("r1", "mod_a:ping")}));
        const std::vector<std::string> ids =
            PlayAndListModEvents(mod_a, mod_b);
        REQUIRE(ids.size() == 1);
        CHECK(ids[0] == "mod_a:ping");
    }
}
