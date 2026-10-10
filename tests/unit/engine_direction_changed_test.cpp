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

/**
 * @file engine_direction_changed_test.cpp
 * @brief The `direction_changed` event, from the op to the seat stream.
 *
 * The `reverse_direction` primitive emits the event, so a reverse card, a
 * hook, and a mutation that still reaches the primitive all emit it. This is
 * the tracer for the pattern in `event_audience.hpp`.
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

constexpr const char* kEventType = "direction_changed";
constexpr int kPlayerCount = 3;

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

struct Content {
    std::vector<LoadedMod> mods;
    DeckDef classic;
};

Content LoadContent() {
    Content content;
    LoadResult load = ScanModsDirectory((ProjectRoot() / "mods").string());
    REQUIRE(load.ok());
    content.mods = std::move(load.mods);
    for (const LoadedMod& mod : content.mods) {
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == "uni:classic") content.classic = deck;
        }
    }
    REQUIRE_FALSE(content.classic.deck_id.empty());
    content.classic.mods = {"uni"};
    return content;
}

std::unique_ptr<MatchInstance> MakeEngine(Content& content) {
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (int index = 0; index < kPlayerCount; ++index) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, content.classic, options);
    REQUIRE(result.ok());
    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

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

std::optional<ecs::Entity> FindReverseInActiveColor(MatchInstance& engine) {
    const json active = engine.ExportState()["active_type"];
    if (!active.is_string()) return std::nullopt;
    for (ecs::Entity card : engine.Registries().cards) {
        const ecs::FaceSpec* face = engine.Store().Get<ecs::FaceSpec>(card);
        const ecs::CardIdentity* identity =
            engine.Store().Get<ecs::CardIdentity>(card);
        if (face == nullptr || identity == nullptr) continue;
        if (face->color != active.get<std::string>()) continue;
        if (identity->kind_id.find("_reverse") == std::string::npos) continue;
        return card;
    }
    return std::nullopt;
}

std::vector<json> EventsOfType(const std::vector<json>& events,
                               const std::string& type) {
    std::vector<json> found;
    for (const json& event : events) {
        if (event.value("type", "") == type) found.push_back(event);
    }
    return found;
}

int CurrentDirection(MatchInstance& engine) {
    const ecs::MatchMeta* meta =
        engine.Store().Get<ecs::MatchMeta>(engine.Registries().match);
    REQUIRE(meta != nullptr);
    return static_cast<int>(meta->direction);
}

BehaviorGraph MakeGraph(json nodes) {
    BehaviorGraph graph;
    graph.nodes = std::move(nodes);
    graph.raw = json{{"nodes", graph.nodes}};
    return graph;
}

json ReverseNode(const std::string& id, const std::string& next = "") {
    json node = json{{"id", id}, {"op", "reverse_direction"},
                     {"args", json::object()}};
    if (!next.empty()) node["next"] = next;
    return node;
}

void AttachAfterPlaySystem(MatchInstance& engine, BehaviorGraph graph) {
    MatchAssembly& assembly = engine.Assembly();
    ModSystem system;
    system.mod_id = "test";
    system.registration_index = 0;
    system.hook = ecs::HookId{"play", ecs::HookPhase::kAfter};
    system.source_id = "test:reverse_system";
    system.graph = std::move(graph);
    const std::size_t index = assembly.systems.size();
    assembly.systems.push_back(std::move(system));
    assembly.bus.Subscribe(
        "test", 0, assembly.systems[index].hook,
        [&assembly, index](ecs::HookPayload& payload) {
            assembly.RunSystem(index, payload);
        });
}

std::optional<ecs::Entity> FindPlainNumberedCard(MatchInstance& engine) {
    const json active = engine.ExportState()["active_type"];
    if (!active.is_string()) return std::nullopt;
    for (ecs::Entity card : engine.Registries().cards) {
        const ecs::FaceSpec* face = engine.Store().Get<ecs::FaceSpec>(card);
        const ecs::CardBehavior* behavior =
            engine.Store().Get<ecs::CardBehavior>(card);
        if (face == nullptr || face->label != "5") continue;
        if (face->color != active.get<std::string>()) continue;
        if (behavior != nullptr && !behavior->triggers.empty()) continue;
        return card;
    }
    return std::nullopt;
}

ModManifest Manifest(const std::string& id) {
    ModManifest manifest;
    manifest.id = id;
    manifest.name = id;
    manifest.version = "1.0.0";
    manifest.api = "1";
    return manifest;
}

CardDef ReverseCard(const std::string& ns, const std::string& local) {
    CardDef card;
    card.id = local;
    card.namespace_id = ns;
    card.kind_id = ns + ":" + local;
    card.title = local;
    card.face.kind = FaceKind::kText;
    card.face.color = std::string("red");
    card.face.label = std::string("1");
    card.tags = {"numbered"};
    BehaviorEntry entry;
    entry.hook = "on_play";
    entry.graph = MakeGraph(json::array({ReverseNode("n1")}));
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

/** @brief Play the base:flipper card and return the engine events. */
std::vector<json> PlayFlipperWith(const std::vector<MutationDef>& mutations) {
    LoadedMod base;
    base.manifest = Manifest("base");
    base.cards.push_back(ReverseCard("base", "flipper"));
    LoadedMod fix;
    fix.manifest = Manifest("fix");
    fix.mutations = mutations;

    DeckDef deck;
    deck.id = "fixture";
    deck.namespace_id = "fixture";
    deck.deck_id = "fixture:deck";
    deck.name = "Fixture";
    deck.mods = {"base", "fix"};
    deck.cards = {{"base:flipper", 8}};

    MatchAssemblyOptions options;
    options.starting_cards = 2;
    options.seed = 7;
    for (int index = 0; index < 2; ++index) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble({base, fix}, deck, options);
    REQUIRE(result.ok());
    MatchInstance engine(std::move(result.assembly));

    const ecs::Entity player = *engine.FindPlayer("player0");
    std::optional<ecs::Entity> card;
    for (ecs::Entity candidate : engine.Registries().cards) {
        const ecs::CardIdentity* identity =
            engine.Store().Get<ecs::CardIdentity>(candidate);
        if (identity != nullptr && identity->kind_id == "base:flipper") {
            card = candidate;
            break;
        }
    }
    REQUIRE(card.has_value());
    ForceHand(engine, player, {*card});
    engine.TakeEvents();
    REQUIRE(engine.PlayCard("player0", *card));
    return engine.Events();
}

}  // namespace

TEST_CASE("direction_changed: a reverse card emits it with actor and "
          "direction") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    const std::optional<ecs::Entity> card = FindReverseInActiveColor(*engine);
    REQUIRE(card.has_value());
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    ForceHand(*engine, player0, {*card});
    engine->TakeEvents();
    const int directionBefore = CurrentDirection(*engine);

    REQUIRE(engine->PlayCard("player0", *card));

    const std::vector<json> changed =
        EventsOfType(engine->Events(), kEventType);
    REQUIRE(changed.size() == 1);
    CHECK(changed[0]["payload"]["direction"] == -directionBefore);
    CHECK(changed[0]["payload"]["direction"] == CurrentDirection(*engine));
    CHECK(changed[0]["causal_frame"]["actor"] == "player0");
    CHECK(changed[0]["causal_frame"]["cause"] == "play");
}

TEST_CASE("direction_changed: a hook that calls the op emits it") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    AttachAfterPlaySystem(*engine, MakeGraph(json::array({ReverseNode("n1")})));
    const std::optional<ecs::Entity> card = FindPlainNumberedCard(*engine);
    REQUIRE(card.has_value());
    ForceHand(*engine, *engine->FindPlayer("player0"), {*card});
    engine->TakeEvents();

    REQUIRE(engine->PlayCard("player0", *card));

    const std::vector<json> changed =
        EventsOfType(engine->Events(), kEventType);
    REQUIRE(changed.size() == 1);
    CHECK(changed[0]["payload"]["direction"] == CurrentDirection(*engine));
    CHECK(changed[0]["causal_frame"]["actor"] == "player0");
}

TEST_CASE("direction_changed: a replacing mutation still emits it") {
    const std::vector<json> events = PlayFlipperWith({Mutation(
        "replace", json::array({ReverseNode("r1", "r2"), ReverseNode("r2")}))});

    const std::vector<json> changed = EventsOfType(events, kEventType);
    REQUIRE(changed.size() == 2);
    CHECK(changed[0]["payload"]["direction"] == -1);
    CHECK(changed[1]["payload"]["direction"] == 1);
    CHECK(changed[0]["causal_frame"]["actor"] == "player0");
}

TEST_CASE("direction_changed: a wrapping mutation emits it once per op") {
    const std::vector<json> events = PlayFlipperWith({Mutation(
        "wrap", json::array({ReverseNode("w1", "w2"),
                             json{{"id", "w2"}, {"op", "call_original"}}}))});

    const std::vector<json> changed = EventsOfType(events, kEventType);
    REQUIRE(changed.size() == 2);
    CHECK(changed[0]["payload"]["direction"] == -1);
    CHECK(changed[1]["payload"]["direction"] == 1);
}

TEST_CASE("direction_changed: every seat and a spectator gets the client "
          "event") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    const std::optional<ecs::Entity> card = FindReverseInActiveColor(*engine);
    REQUIRE(card.has_value());
    ForceHand(*engine, *engine->FindPlayer("player0"), {*card});
    engine->TakeEvents();
    REQUIRE(engine->PlayCard("player0", *card));
    const std::vector<json> changed =
        EventsOfType(engine->Events(), kEventType);
    REQUIRE(changed.size() == 1);

    ViewBuilder builder(*engine, content.mods);
    std::vector<Viewer> viewers = {Viewer::PublicSpectator()};
    for (int index = 0; index < kPlayerCount; ++index) {
        viewers.push_back(Viewer::Player("player" + std::to_string(index)));
    }
    for (const Viewer& viewer : viewers) {
        EventSink sink;
        const std::optional<json> wrapped =
            builder.Wrap(changed[0], viewer, sink);
        REQUIRE(wrapped.has_value());
        CHECK((*wrapped)["id"] == kEventType);
        CHECK((*wrapped)["id"] == kEventType);
        CHECK((*wrapped)["actor"] == "player0");
        CHECK((*wrapped)["payload"]["direction"] == CurrentDirection(*engine));
        CHECK((*wrapped).contains("sequence"));
        CHECK((*wrapped).contains("turn"));
        CHECK((*wrapped).contains("round"));
    }
}
