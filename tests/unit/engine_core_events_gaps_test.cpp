#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/view/event_sink.hpp>
#include <match/view/view_builder.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "support/leak_scanner.hpp"

/**
 * @file engine_core_events_gaps_test.cpp
 * @brief Real-engine checks for the new names of the remaining core events.
 *
 * Each test runs a real op or a real engine path. No payload is made by hand
 * when the engine can make it.
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
    return content;
}

std::unique_ptr<MatchInstance> MakeEngine(Content& content,
                                          int player_count) {
    DeckDef deck = content.classic;
    deck.mods = {"uni"};
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 42;
    for (int index = 0; index < player_count; ++index) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble(content.mods, deck, options);
    REQUIRE(result.ok());
    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

std::vector<json> EventsOfType(const MatchInstance& engine,
                               const std::string& type) {
    std::vector<json> found;
    for (const json& event : engine.Events()) {
        if (event.value("type", std::string()) == type) {
            found.push_back(event);
        }
    }
    return found;
}

void ForceHand(MatchInstance& engine, ecs::Entity player,
               const std::vector<ecs::Entity>& cards) {
    ecs::Hand* hand = engine.Store().Get<ecs::Hand>(player);
    REQUIRE(hand != nullptr);
    const std::vector<ecs::Entity> existing = hand->cards;
    for (ecs::Entity card : existing) {
        ops::MoveCardToZone(
            engine.Store(), card,
            ecs::ZoneRef{ecs::ZoneKind::kDrawPile, ecs::Entity{}});
    }
    for (ecs::Entity card : cards) {
        ops::MoveCardToZone(engine.Store(), card,
                            ecs::ZoneRef{ecs::ZoneKind::kHand, player});
    }
}

// --- a fixture whose card runs real roll ops --------------------------------

json Node(const std::string& id, const std::string& op, json args,
          const std::string& next = "") {
    json node = json{{"id", id}, {"op", op}, {"args", std::move(args)}};
    if (!next.empty()) node["next"] = next;
    return node;
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

CardDef RollerCard() {
    CardDef card;
    card.id = "roller";
    card.namespace_id = "base";
    card.kind_id = "base:roller";
    card.title = "roller";
    card.face.kind = FaceKind::kText;
    card.face.color = std::string("red");
    card.face.label = std::string("1");
    card.tags = {"numbered"};
    BehaviorEntry entry;
    entry.hook = "on_play";
    entry.graph = MakeGraph(json::array(
        {Node("n1", "roll", json{{"spec", json{{"sides", 6}, {"count", 1}}}},
              "n2"),
         Node("n2", "roll_table", json{{"table", "base:loot"}})}));
    card.behaviors.push_back(std::move(entry));
    return card;
}

TableDef LootTable() {
    TableDef loot;
    loot.id = "loot";
    loot.namespace_id = "base";
    loot.table_id = "base:loot";
    loot.entries = {TableEntry{"six", 1, 6, std::nullopt, std::nullopt,
                               json::object()}};
    return loot;
}

constexpr uint64_t kEngineCounterStart = 4000;

std::unique_ptr<MatchInstance> MakeRollerEngine(
    std::vector<LoadedMod>& mods) {
    LoadedMod base;
    base.manifest = Manifest("base");
    base.cards.push_back(RollerCard());
    base.tables.push_back(LootTable());
    mods = {base};

    DeckDef deck;
    deck.id = "fixture";
    deck.namespace_id = "fixture";
    deck.deck_id = "fixture:deck";
    deck.name = "Fixture";
    deck.mods = {"base"};
    deck.cards = {{"base:roller", 12}};

    MatchAssemblyOptions options;
    options.starting_cards = 2;
    options.seed = 7;
    for (int index = 0; index < 2; ++index) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyResult result = MatchAssembler::Assemble(mods, deck, options);
    REQUIRE(result.ok());
    return std::make_unique<MatchInstance>(std::move(result.assembly));
}

}  // namespace

TEST_CASE("outcome_rolled: only a dice roll fires the roll hook") {
    std::vector<LoadedMod> mods;
    std::unique_ptr<MatchInstance> engine = MakeRollerEngine(mods);
    // INFO: a big engine counter shows that the wire never carries it.
    engine->Store()
        .Get<ecs::RngState>(engine->Registries().match)
        ->op_counter = kEngineCounterStart;
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    ecs::Entity roller{};
    for (ecs::Entity card : engine->Registries().cards) {
        const ecs::CardIdentity* identity =
            engine->Store().Get<ecs::CardIdentity>(card);
        if (identity != nullptr && identity->kind_id == "base:roller") {
            roller = card;
            break;
        }
    }
    ForceHand(*engine, player0, {roller});
    ForceHand(*engine, *engine->FindPlayer("player1"), {});
    int roll_hooks = 0;
    engine->Assembly().bus.Subscribe(
        "base", 0, ecs::HookId{"roll", ecs::HookPhase::kAfter},
        [&roll_hooks](ecs::HookPayload&) { ++roll_hooks; });
    REQUIRE(engine->PlayCard("player0", roller));
    CHECK(roll_hooks == 1);
}

TEST_CASE("outcome_rolled: real roll and roll_table ops, order and ordinal") {
    std::vector<LoadedMod> mods;
    std::unique_ptr<MatchInstance> engine = MakeRollerEngine(mods);
    // INFO: a big engine counter shows that the wire never carries it.
    engine->Store()
        .Get<ecs::RngState>(engine->Registries().match)
        ->op_counter = kEngineCounterStart;
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    ecs::Entity roller{};
    for (ecs::Entity card : engine->Registries().cards) {
        const ecs::CardIdentity* identity =
            engine->Store().Get<ecs::CardIdentity>(card);
        if (identity != nullptr && identity->kind_id == "base:roller") {
            roller = card;
            break;
        }
    }
    ForceHand(*engine, player0, {roller});
    ForceHand(*engine, *engine->FindPlayer("player1"), {});
    REQUIRE(engine->PlayCard("player0", roller));

    std::vector<std::string> order;
    for (const json& event : engine->Events()) {
        const std::string type = event.value("type", std::string());
        if (type == "outcome_rolled") order.push_back(type);
    }
    CHECK(order == std::vector<std::string>{"outcome_rolled",
                                            "outcome_rolled"});

    ViewBuilder builder(*engine, mods);
    for (const Viewer& viewer :
         {Viewer::Player("player1"), Viewer::PublicSpectator()}) {
        EventSink sink;
        std::vector<json> packets;
        for (const json& event : engine->Events()) {
            const std::optional<json> packet =
                builder.Wrap(event, viewer, sink);
            if (!packet.has_value()) continue;
            const std::string id = (*packet)["id"].get<std::string>();
            if (id == "outcome_rolled") packets.push_back(*packet);
        }
        REQUIRE(packets.size() == 2);
        CHECK(packets[0]["payload"]["roll_ordinal"] == 1);
        CHECK(packets[0]["payload"].contains("outcomes"));
        CHECK(packets[1]["payload"]["roll_ordinal"] == 2);
        CHECK(packets[1]["payload"]["table"] == "base:loot");
        for (const json& new_packet : {packets[0], packets[1]}) {
            const std::string text = new_packet.dump();
            CHECK_FALSE(new_packet["payload"].contains("counter"));
            CHECK_FALSE(new_packet["payload"].contains("roll_counter"));
            CHECK(text.find(std::to_string(kEngineCounterStart))
                  == std::string::npos);
        }
    }
}

// --- window_responded with the real engine event ---------------------------

namespace {

BehaviorGraph WindowGraph() {
    BehaviorGraph graph;
    json window = json{{"responders", "@others"},
                       {"respond_with", json::object()},
                       {"duration", json("env")},
                       {"filter_digest", "any"}};
    graph.nodes = json::array(
        {json{{"id", "n1"},
              {"window", window},
              {"default", "n2"},
              {"on_response", json{{"any", "n3"}}}},
         json{{"id", "n2"},
              {"op", "emit_signal"},
              {"args", json{{"name", "window:default"}}}},
         json{{"id", "n3"},
              {"op", "emit_signal"},
              {"args", json{{"name", "window:responded"}}}}});
    graph.raw = json{{"nodes", graph.nodes}};
    return graph;
}

void AttachWindowSystem(MatchInstance& engine) {
    MatchAssembly& assembly = engine.Assembly();
    ModSystem system;
    system.mod_id = "test";
    system.registration_index = 0;
    system.hook = ecs::HookId{"play", ecs::HookPhase::kAfter};
    system.source_id = "test:window";
    system.graph = WindowGraph();
    const std::size_t index = assembly.systems.size();
    assembly.systems.push_back(std::move(system));
    assembly.bus.Subscribe(
        "test", 0, assembly.systems[index].hook,
        [&assembly, index](ecs::HookPayload& payload) {
            assembly.RunSystem(index, payload);
        });
}

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

std::vector<ecs::Entity> PlainNumbered(MatchInstance& engine,
                                       std::size_t count) {
    const json active = engine.ExportState()["active_type"];
    REQUIRE(active.is_string());
    const ecs::PileContents* discard = engine.Store().Get<ecs::PileContents>(
        engine.Registries().discard_pile);
    std::vector<ecs::Entity> found;
    for (ecs::Entity card : engine.Registries().cards) {
        if (found.size() == count) break;
        if (discard != nullptr && !discard->cards.empty()
            && card == discard->cards.back()) {
            continue;
        }
        const ecs::FaceSpec* face = engine.Store().Get<ecs::FaceSpec>(card);
        if (face == nullptr || face->color != active.get<std::string>()
            || face->label.size() != 1
            || std::isdigit(static_cast<unsigned char>(face->label[0])) == 0) {
            continue;
        }
        const ecs::CardBehavior* behavior =
            engine.Store().Get<ecs::CardBehavior>(card);
        if (behavior != nullptr && !behavior->triggers.empty()) continue;
        found.push_back(card);
    }
    return found;
}

}  // namespace

TEST_CASE("window_responded: a real losing response hides the card") {
    Content content = LoadContent();
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 3);
    AttachWindowSystem(*engine);
    AllowAllPlays(*engine);
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Entity player2 = *engine->FindPlayer("player2");
    const std::vector<ecs::Entity> mine = PlainNumbered(*engine, 3);
    REQUIRE(mine.size() == 3);
    ForceHand(*engine, player0, {mine[0]});
    ForceHand(*engine, player1, {mine[1]});
    ForceHand(*engine, player2, {mine[2]});
    ForceHand(*engine, player0, {mine[0], mine[0]});
    REQUIRE(engine->PlayCard("player0", mine[0]));
    REQUIRE(engine->WindowOpen());
    REQUIRE(engine->RespondWindow("player1", mine[1]));
    REQUIRE(engine->RespondWindow("player2", mine[2]));

    const std::optional<ecs::CompactCardV2> lost_identity =
        engine->Registries().CardId(mine[2]);
    REQUIRE(lost_identity.has_value());
    const leak_scanner::HiddenCard hidden{mine[2], lost_identity->bits};

    ViewBuilder builder(*engine, content.mods);
    for (const std::string type : {"window_responded", "window_responded"}) {
        CAPTURE(type);
        json lost;
        for (const json& event : EventsOfType(*engine, type)) {
            if (event["payload"].value("outcome", "") == "lost") lost = event;
        }
        REQUIRE_FALSE(lost.is_null());
        for (const Viewer& other :
             {Viewer::Player("player0"), Viewer::Player("player1"),
              Viewer::PublicSpectator(), Viewer::Spectator()}) {
            EventSink sink;
            const std::optional<json> packet =
                builder.Wrap(lost, other, sink);
            REQUIRE(packet.has_value());
            CHECK_FALSE((*packet)["payload"].contains("card"));
            CHECK(leak_scanner::FindLeaks(*packet, {hidden}).empty());
        }
        EventSink owner_sink;
        const std::optional<json> own =
            builder.Wrap(lost, Viewer::Player("player2"), owner_sink);
        REQUIRE(own.has_value());
        CHECK((*own)["payload"].contains("card"));
    }
}

// --- the cause memory after many doubled events -----------------------------

TEST_CASE("cause: the memory still maps a cause after many doubled events") {
    Content content = LoadContent();
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 2);
    EventSink sink;
    constexpr int kPairs = 400;
    int64_t event_id = 0;
    const auto wrap = [&](const std::string& type, int64_t cause_event) {
        json event = json{{"type", type},
                          {"event_id", ++event_id},
                          {"payload", json{{"round", 1}}}};
        if (cause_event > 0) {
            event["causal_frame"] = json{{"cause_event", cause_event}};
        }
        return sink.WrapPublic(event, *engine);
    };
    const std::optional<json> first = wrap("round_advance", 0);
    REQUIRE(first.has_value());
    int64_t recent_id = 0;
    uint32_t recent_sequence = 0;
    int64_t middle_id = 0;
    uint32_t middle_sequence = 0;
    for (int pair = 0; pair < kPairs; ++pair) {
        const std::optional<json> old_name = wrap("window_opened", 0);
        recent_id = event_id;
        recent_sequence = (*old_name)["sequence"].get<uint32_t>();
        if (pair == kPairs / 2) {
            middle_id = recent_id;
            middle_sequence = recent_sequence;
        }
        wrap("window_opened", 0);
    }
    const std::optional<json> later = wrap("round_advance", recent_id);
    REQUIRE(later.has_value());
    REQUIRE(later->contains("cause"));
    CHECK((*later)["cause"].get<uint32_t>() == recent_sequence);

    // INFO: an event about 400 ids back is still inside the memory.
    const std::optional<json> middle = wrap("round_advance", middle_id);
    REQUIRE(middle.has_value());
    REQUIRE(middle->contains("cause"));
    CHECK((*middle)["cause"].get<uint32_t>() == middle_sequence);

    // INFO: the first event is older than the memory; it maps to no cause.
    const std::optional<json> stale = wrap("round_advance", 1);
    REQUIRE(stale.has_value());
    CHECK_FALSE(stale->contains("cause"));
}

// --- public status and the match end digest ---------------------------------

TEST_CASE("status_modified: a public status reaches every seat") {
    Content content = LoadContent();
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 3);
    LoadedMod stub;
    stub.manifest.id = "testmod";
    stub.manifest.version = "1.0.0";
    StatusDef open;
    open.id = "open";
    open.namespace_id = "testmod";
    open.status_id = "testmod:open";
    open.hidden = false;
    stub.statuses.push_back(open);
    std::vector<LoadedMod> mods = content.mods;
    mods.push_back(stub);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const json modified = json{
        {"type", "status_modified"},
        {"payload", json{{"target", json{{"index", player0.index},
                                         {"generation", player0.generation}}},
                         {"status_kind", "testmod:open"},
                         {"magnitude", 5},
                         {"delta", 3},
                         {"instance", 2}}}};
    for (const Viewer& viewer :
         {Viewer::Player("player0"), Viewer::Player("player1"),
          Viewer::Player("player2"), Viewer::PublicSpectator()}) {
        EventSink sink;
        const std::optional<json> packet = builder.Wrap(modified, viewer, sink);
        REQUIRE(packet.has_value());
        CHECK((*packet)["payload"]["target"] == "player0");
        CHECK((*packet)["payload"]["magnitude"] == 5);
        CHECK((*packet)["payload"]["delta"] == 3);
    }
}

TEST_CASE("match_ended: the digest equals the digest of match_end") {
    Content content = LoadContent();
    std::unique_ptr<MatchInstance> engine = MakeEngine(content, 3);
    std::vector<ecs::Entity> wilds;
    for (ecs::Entity card : engine->Registries().cards) {
        const ecs::CardIdentity* identity =
            engine->Store().Get<ecs::CardIdentity>(card);
        if (identity != nullptr && identity->kind_id == "uni:wild") {
            wilds.push_back(card);
        }
    }
    REQUIRE_FALSE(wilds.empty());
    ForceHand(*engine, *engine->FindPlayer("player0"), {wilds[0]});
    REQUIRE(engine->PlayCard("player0", wilds[0]));
    REQUIRE(engine->SubmitInput("player0", "red"));
    REQUIRE(engine->IsMatchOver());

    ViewBuilder builder(*engine, content.mods);
    for (const Viewer& viewer :
         {Viewer::Player("player1"), Viewer::PublicSpectator()}) {
        EventSink sink;
        const std::optional<json> old_packet = builder.Wrap(
            EventsOfType(*engine, "match_ended")[0], viewer, sink);
        const std::optional<json> new_packet = builder.Wrap(
            EventsOfType(*engine, "match_ended")[0], viewer, sink);
        REQUIRE(old_packet.has_value());
        REQUIRE(new_packet.has_value());
        const std::string digest =
            (*old_packet)["payload"]["final_digest"].get<std::string>();
        CHECK_FALSE(digest.empty());
        CHECK((*new_packet)["payload"]["final_digest"] == digest);
    }
}
