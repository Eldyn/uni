#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/event_definition_keys.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>
#include <match/view/event_audience.hpp>
#include <match/view/event_sink.hpp>
#include <match/view/event_tail.hpp>
#include <match/view/view_builder.hpp>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file mod_event_test.cpp
 * @brief A mod event, from the `emit_event` op to the seat streams.
 *
 * The event definition lives in `mods/test/events/apple_eaten.json`. Its
 * fields: `target` (seat, all), `color` (color, actor), `count` (integer,
 * all) and `secret` (string, no audience declared).
 *
 * The golden file `tests/fixtures/golden_mod_event/wire_packets.json` holds the
 * packet of one viewer of each kind. Set `UNI_UPDATE_GOLDEN=1` to write it
 * again; a default run never writes it.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
namespace ops = match::ops;
using namespace match::engine;
using namespace match::modload;
using match::view::EventSink;
using match::view::EventTail;
using match::view::Retention;
using match::view::RetentionOf;
using match::view::ViewBuilder;
using match::view::Viewer;
using nlohmann::json;

namespace {

constexpr const char* kEventId = "test:apple_eaten";
constexpr int kPlayerCount = 3;

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
        MatchAssembler::Assemble(content.mods, content.deck, options);
    REQUIRE(result.ok());
    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

struct OpHarness {
    ecs::EntityStore store;
    ecs::BudgetLedger ledger;
    ecs::EventBus bus = ecs::EventBus(std::vector<std::string>{"test"});
    ops::ResolutionFrame frame;
    ops::OpRuntime runtime = ops::MakeDefaultRuntime();
    const MatchRegistries* registries = nullptr;
    std::string mod_id = "test";
    ecs::Entity self;

    OpHarness() {
        self = store.Create();
        ecs::PlayerInfo info;
        info.username = "player0";
        store.Add(self, info);
        frame.selectors["@self"] = {self};
    }

    ops::OpResult Emit(const json& args) {
        ops::OpArgs op_args("emit_event", args);
        ops::OpContext context(bus, ledger, frame);
        context.registries = registries;
        context.mod_id = &mod_id;
        return runtime.Invoke("emit_event", store, op_args, context);
    }
};

const json* FindModEvent(const ops::OpResult& result) {
    for (const json& event : result.events) {
        if (event.value("type", "") == "mod_event") return &event;
    }
    return nullptr;
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

json EmitNode(const json& args) {
    return json{{"id", "n1"}, {"op", "emit_event"}, {"args", args}};
}

void AttachAfterPlaySystem(MatchInstance& engine, const std::string& mod_id,
                           json nodes) {
    BehaviorGraph graph;
    graph.nodes = std::move(nodes);
    graph.raw = json{{"nodes", graph.nodes}};
    MatchAssembly& assembly = engine.Assembly();
    ModSystem system;
    system.mod_id = mod_id;
    system.registration_index = 0;
    system.hook = ecs::HookId{"play", ecs::HookPhase::kAfter};
    system.source_id = mod_id + ":emit_system";
    system.graph = std::move(graph);
    const std::size_t index = assembly.systems.size();
    assembly.systems.push_back(std::move(system));
    assembly.bus.Subscribe(
        mod_id, 0, assembly.systems[index].hook,
        [&assembly, index](ecs::HookPayload& payload) {
            assembly.RunSystem(index, payload);
        });
}

json AppleArgs() {
    return json{{"id", kEventId},
                {"payload",
                 {{"target", "@self"},
                  {"color", "red"},
                  {"count", 3},
                  {"secret", "hidden"},
                  {"unknown_field", 1}}}};
}

/** @brief Play one card by player0 so the attached system emits the event. */
std::vector<json> PlayAndCollect(MatchInstance& engine) {
    const std::optional<ecs::Entity> card = FindPlainNumberedCard(engine);
    REQUIRE(card.has_value());
    ForceHand(engine, *engine.FindPlayer("player0"), {*card});
    engine.TakeEvents();
    REQUIRE(engine.PlayCard("player0", *card));
    std::vector<json> found;
    for (const json& event : engine.Events()) {
        if (event.value("type", "") == "mod_event") found.push_back(event);
    }
    return found;
}

std::optional<json> WrapFor(const ViewBuilder& builder, const json& event,
                            const Viewer& viewer) {
    EventSink sink;
    return builder.Wrap(event, viewer, sink);
}

}  // namespace

TEST_CASE("mod event: the match registries hold the event definition") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    const auto found = engine->Registries().events.find(kEventId);
    REQUIRE(found != engine->Registries().events.end());
    CHECK(found->second.retention == "log");
}

TEST_CASE("mod event: the op validates the payload against the definition") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    OpHarness harness;
    harness.registries = &engine->Registries();

    const ops::OpResult result = harness.Emit(AppleArgs());
    const json* event = FindModEvent(result);
    REQUIRE(event != nullptr);
    const json& body = (*event)["payload"];
    CHECK(body["id"] == kEventId);
    CHECK(body["retention"] == "log");
    CHECK(body["payload"]["target"] == "player0");
    CHECK(body["payload"]["color"] == "red");
    CHECK(body["payload"]["count"] == 3);
    CHECK(body["payload"]["secret"] == "hidden");
    CHECK_FALSE(body["payload"].contains("unknown_field"));
}

TEST_CASE("mod event: a wrong type drops the whole event") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    OpHarness harness;
    harness.registries = &engine->Registries();

    json args = AppleArgs();
    args["payload"]["count"] = "three";
    CHECK(FindModEvent(harness.Emit(args)) == nullptr);

    args = AppleArgs();
    args["payload"]["target"] = "not_a_selector";
    CHECK(FindModEvent(harness.Emit(args)) == nullptr);
}

TEST_CASE("mod event: an undefined id emits nothing") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    OpHarness harness;
    harness.registries = &engine->Registries();
    CHECK(FindModEvent(harness.Emit(json{{"id", "test:never_defined"}}))
          == nullptr);
}

TEST_CASE("mod event: a mod cannot emit an event of another namespace") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    OpHarness harness;
    harness.registries = &engine->Registries();
    harness.mod_id = "uni";
    CHECK(FindModEvent(harness.Emit(AppleArgs())) == nullptr);
}

TEST_CASE("mod event: a mod rule emits it and every viewer gets the "
          "envelope with its own fields") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    AttachAfterPlaySystem(*engine, "test", json::array({EmitNode(AppleArgs())}));
    const std::vector<json> events = PlayAndCollect(*engine);
    REQUIRE(events.size() == 1);
    const ViewBuilder builder(*engine, content.mods);

    const auto actor = WrapFor(builder, events[0], Viewer::Player("player0"));
    REQUIRE(actor.has_value());
    CHECK((*actor)["id"] == kEventId);
    CHECK((*actor)["actor"] == "player0");
    CHECK((*actor)["payload"]["target"] == "player0");
    CHECK((*actor)["payload"]["color"] == "red");
    CHECK((*actor)["payload"]["count"] == 3);
    CHECK_FALSE((*actor)["payload"].contains("secret"));
    CHECK_FALSE((*actor)["payload"].contains("unknown_field"));

    for (const Viewer& other :
         {Viewer::Player("player1"), Viewer::PublicSpectator()}) {
        const auto wrapped = WrapFor(builder, events[0], other);
        REQUIRE(wrapped.has_value());
        CHECK((*wrapped)["id"] == kEventId);
        CHECK((*wrapped)["payload"]["target"] == "player0");
        CHECK((*wrapped)["payload"]["count"] == 3);
        CHECK_FALSE((*wrapped)["payload"].contains("color"));
        CHECK_FALSE((*wrapped)["payload"].contains("secret"));
    }
}

TEST_CASE("mod event: every seat stream keeps its own gap-free sequence") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    AttachAfterPlaySystem(*engine, "test", json::array({EmitNode(AppleArgs())}));
    PlayAndCollect(*engine);
    const ViewBuilder builder(*engine, content.mods);
    for (const Viewer& viewer :
         {Viewer::Player("player0"), Viewer::Player("player1"),
          Viewer::PublicSpectator()}) {
        const std::vector<json> packets = builder.BuildPackets(viewer);
        for (std::size_t index = 0; index < packets.size(); ++index) {
            CHECK(packets[index]["sequence"] == index);
        }
    }
}

TEST_CASE("mod event: a core event is never affected by a mod event") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    AttachAfterPlaySystem(*engine, "test", json::array({EmitNode(AppleArgs())}));
    const std::vector<json> mod_events = PlayAndCollect(*engine);
    REQUIRE(mod_events.size() == 1);
    int card_played_count = 0;
    for (const json& event : engine->Events()) {
        if (event.value("type", "") == "card_played") ++card_played_count;
    }
    CHECK(card_played_count == 1);
}

TEST_CASE("mod event: retention defaults to log and the definition sets it") {
    const json log_body = {{"id", kEventId}, {"payload", json::object()}};
    CHECK(RetentionOf("mod_event", log_body) == Retention::kLog);
    CHECK(RetentionOf("mod_event", json::object()) == Retention::kLog);
    for (const auto& [word, expected] :
         {std::pair{"transient", Retention::kTransient},
          std::pair{"until_turn_end", Retention::kUntilTurnEnd},
          std::pair{"until_round_end", Retention::kUntilRoundEnd},
          std::pair{"log", Retention::kLog}}) {
        const json body = {{"id", kEventId}, {"retention", word}};
        CHECK(RetentionOf("mod_event", body) == expected);
    }
}

TEST_CASE("mod event: the late join tail replays a log event and skips a "
          "transient one") {
    EventTail tail;
    const auto descriptor = [](const std::string& retention) {
        return json{{"type", "mod_event"},
                    {"payload", {{"id", kEventId}, {"retention", retention}}},
                    {"causal_frame", {{"turn", 1}, {"round", 1}}}};
    };
    tail.Record(descriptor("log"));
    tail.Record(descriptor("transient"));
    CHECK(tail.Entries().size() == 1);
}

TEST_CASE("mod event: the wire packet for each viewer matches the golden "
          "file") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    AttachAfterPlaySystem(*engine, "test", json::array({EmitNode(AppleArgs())}));
    const std::vector<json> events = PlayAndCollect(*engine);
    REQUIRE(events.size() == 1);
    const ViewBuilder builder(*engine, content.mods);

    json actual = json::object();
    const std::pair<const char*, Viewer> viewers[] = {
        {"actor", Viewer::Player("player0")},
        {"other_seat", Viewer::Player("player1")},
        {"public_spectator", Viewer::PublicSpectator()}};
    for (const auto& [name, viewer] : viewers) {
        const auto wrapped = WrapFor(builder, events[0], viewer);
        REQUIRE(wrapped.has_value());
        actual[name] = *wrapped;
    }

    const fs::path golden = ProjectRoot() / "tests" / "fixtures"
                            / "golden_mod_event" / "wire_packets.json";
    if (std::getenv("UNI_UPDATE_GOLDEN") != nullptr) {
        fs::create_directories(golden.parent_path());
        std::ofstream(golden) << actual.dump(2) << "\n";
    }
    std::ifstream input(golden);
    REQUIRE_MESSAGE(input.good(), "missing golden file " << golden);
    CHECK(json::parse(input) == actual);
}

namespace {

/** @brief A registry with one event of the given string fields. */
MatchRegistries RegistriesWithStringEvent(int field_count) {
    MatchRegistries registries;
    EventDef definition;
    definition.id = "wide";
    definition.namespace_id = "test";
    definition.event_id = "test:wide";
    definition.retention = "log";
    for (int index = 0; index < field_count; ++index) {
        definition.fields.push_back(
            EventFieldDef{"f" + std::to_string(index), "string", "all"});
    }
    registries.events["test:wide"] = definition;
    return registries;
}

json WideArgs(const std::vector<std::size_t>& lengths) {
    json payload = json::object();
    for (std::size_t index = 0; index < lengths.size(); ++index) {
        payload["f" + std::to_string(index)] =
            std::string(lengths[index], 'x');
    }
    return json{{"id", "test:wide"}, {"payload", payload}};
}

}  // namespace

TEST_CASE("mod event limits: a string at the limit passes, one over drops") {
    MatchRegistries registries = RegistriesWithStringEvent(1);
    OpHarness harness;
    harness.registries = &registries;
    CHECK(FindModEvent(harness.Emit(
              WideArgs({match::modload::event_keys::kMaxEventStringBytes})))
          != nullptr);
    CHECK(FindModEvent(harness.Emit(WideArgs(
              {match::modload::event_keys::kMaxEventStringBytes + 1})))
          == nullptr);
}

TEST_CASE("mod event limits: a payload at the byte limit passes, one over "
          "drops") {
    namespace keys = match::modload::event_keys;
    MatchRegistries registries = RegistriesWithStringEvent(4);
    OpHarness harness;
    harness.registries = &registries;
    std::vector<std::size_t> lengths = {keys::kMaxEventStringBytes,
                                        keys::kMaxEventStringBytes,
                                        keys::kMaxEventStringBytes, 0};
    const std::size_t fixed_bytes =
        WideArgs(lengths)["payload"].dump().size();
    REQUIRE(fixed_bytes < keys::kMaxEventPayloadBytes);
    lengths[3] = keys::kMaxEventPayloadBytes - fixed_bytes;
    REQUIRE(lengths[3] <= keys::kMaxEventStringBytes);
    REQUIRE(WideArgs(lengths)["payload"].dump().size()
            == keys::kMaxEventPayloadBytes);
    CHECK(FindModEvent(harness.Emit(WideArgs(lengths))) != nullptr);
    lengths[3] += 1;
    CHECK(FindModEvent(harness.Emit(WideArgs(lengths))) == nullptr);
}

TEST_CASE("mod event roles: only a seat typed field fills the owner role") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    EventDef definition;
    definition.id = "probe";
    definition.namespace_id = "test";
    definition.event_id = "test:probe";
    definition.retention = "log";
    definition.fields.push_back(EventFieldDef{"owner", "string", "owner"});
    definition.fields.push_back(EventFieldDef{"target", "string", "target"});
    engine->Assembly().registries.events["test:probe"] = definition;
    const json event = {
        {"type", "mod_event"},
        {"payload",
         {{"id", "test:probe"},
          {"retention", "log"},
          {"payload", {{"owner", "player1"}, {"target", "player1"}}}}},
        {"causal_frame", {{"turn", 1}, {"round", 1}}}};
    const ViewBuilder builder(*engine, content.mods);
    const auto wrapped = WrapFor(builder, event, Viewer::Player("player1"));
    REQUIRE(wrapped.has_value());
    CHECK((*wrapped)["payload"].empty());
}
