#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/view/event_sink.hpp>
#include <match/view/view_builder.hpp>

#include "support/leak_scanner.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file engine_hand_card_events_test.cpp
 * @brief The hand and card events, from the op to the seat stream.
 *
 * `hands_swapped`, `hands_passed`, `hands_redistributed`, `card_moved`,
 * `card_created`, `card_removed` and `card_replaced` come from the primitive.
 * A card play, a hook, a replacing mutation and a wrapping mutation that
 * calls the original all emit them. The hook path is `after:draw`, where the
 * engine binds `@drawn_card` to a card in the hand of the drawer.
 */

namespace fs = std::filesystem;
namespace ecs = match::ecs;
using namespace match::engine;
using namespace match::modload;
using match::view::EventSink;
using match::view::ViewBuilder;
using match::view::Viewer;
using nlohmann::json;

namespace {

constexpr int kPlayerCount = 3;
constexpr const char* kNewKind = "base:third";

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

BehaviorGraph MakeGraph(json nodes) {
    BehaviorGraph graph;
    graph.nodes = std::move(nodes);
    graph.raw = json{{"nodes", graph.nodes}};
    return graph;
}

void AttachAfterDrawSystem(MatchInstance& engine, BehaviorGraph graph) {
    MatchAssembly& assembly = engine.Assembly();
    ModSystem system;
    system.mod_id = "test";
    system.registration_index = 0;
    system.hook = ecs::HookId{"draw", ecs::HookPhase::kAfter};
    system.source_id = "test:hand_card_system";
    system.graph = std::move(graph);
    const std::size_t index = assembly.systems.size();
    assembly.systems.push_back(std::move(system));
    assembly.bus.Subscribe(
        "test", 0, assembly.systems[index].hook,
        [&assembly, index,
         fired = std::make_shared<bool>(false)](ecs::HookPayload& payload) {
            if (*fired) return;
            *fired = true;
            assembly.RunSystem(index, payload);
        });
}

std::vector<json> EventsOfType(const std::vector<json>& events,
                               const std::string& type) {
    std::vector<json> found;
    for (const json& event : events) {
        if (event.value("type", "") == type) found.push_back(event);
    }
    return found;
}

uint32_t BitsOf(MatchInstance& engine, ecs::Entity card) {
    const std::optional<ecs::CompactCardV2> id =
        engine.Registries().CardId(card);
    REQUIRE(id.has_value());
    return id->bits;
}

/** @brief One op under test and the event that it must emit. */
struct OpCase {
    std::string event_type;
    /** @brief The op node; `card_selector` names the card to act on. */
    std::function<json(const std::string& card_selector)> node;
    /** @brief Events of a wrapping mutation: the wrap and the original. */
    std::size_t wrapped_events;
};

json OpNode(const std::string& op, json args) {
    return json{{"id", "n1"}, {"op", op}, {"args", std::move(args)}};
}

std::vector<OpCase> OpCases() {
    return {
        {"hands_swapped",
         [](const std::string&) {
             return OpNode("swap_hands",
                           {{"a", "@self"}, {"b", "@next_player"}});
         },
         2},
        {"hands_passed",
         [](const std::string&) {
             return OpNode("pass_hands", {{"direction", "forward"}});
         },
         2},
        {"hands_redistributed",
         [](const std::string&) {
             return OpNode("redistribute_hands", {{"mode", "even"}});
         },
         2},
        {"card_moved",
         [](const std::string& card) {
             return OpNode("move_card",
                           {{"card", card}, {"to_zone", "limbo"}});
         },
         1},
        {"card_created",
         [](const std::string&) {
             return OpNode("materialize_card", {{"target_player", "@self"},
                                                {"kind", kNewKind}});
         },
         2},
        {"card_removed",
         [](const std::string& card) {
             return OpNode("remove_card", {{"card", card}});
         },
         1},
        {"card_replaced",
         [](const std::string& card) {
             return OpNode("replace_card",
                           {{"card", card}, {"kind", kNewKind}});
         },
         1},
    };
}

ModManifest Manifest(const std::string& id) {
    ModManifest manifest;
    manifest.id = id;
    manifest.name = id;
    manifest.version = "1.0.0";
    manifest.api = "1";
    return manifest;
}

CardDef ProbeCard(const json& node) {
    CardDef card;
    card.id = "probe";
    card.namespace_id = "base";
    card.kind_id = "base:probe";
    card.title = "probe";
    card.face.kind = FaceKind::kText;
    card.face.color = std::string("red");
    card.face.label = std::string("1");
    card.tags = {"numbered"};
    BehaviorEntry entry;
    entry.hook = "on_play";
    entry.graph = MakeGraph(json::array({node}));
    card.behaviors.push_back(std::move(entry));
    return card;
}

MutationDef Mutation(const std::string& mode, json nodes) {
    MutationDef mutation;
    mutation.id = "swap";
    mutation.namespace_id = "fix";
    mutation.mutation_id = "fix:swap";
    mutation.target = "base:probe";
    mutation.mode = mode;
    mutation.replacement = MakeGraph(std::move(nodes));
    return mutation;
}

/** @brief Play the base:probe card of player0; return the engine events. */
std::vector<json> PlayProbeWith(const json& node,
                                const std::vector<MutationDef>& mutations) {
    LoadedMod base;
    base.manifest = Manifest("base");
    base.cards.push_back(ProbeCard(node));
    LoadedMod fix;
    fix.manifest = Manifest("fix");
    fix.mutations = mutations;

    DeckDef deck;
    deck.id = "fixture";
    deck.namespace_id = "fixture";
    deck.deck_id = "fixture:deck";
    deck.name = "Fixture";
    deck.mods = {"base", "fix"};
    deck.cards = {{"base:probe", 24}};

    MatchAssemblyOptions options;
    options.starting_cards = 3;
    options.seed = 7;
    for (int index = 0; index < kPlayerCount; ++index) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(index);
        options.players.push_back(spec);
    }
    AssemblyResult result =
        MatchAssembler::Assemble({base, fix}, deck, options);
    REQUIRE(result.ok());
    MatchInstance engine(std::move(result.assembly));

    const ecs::Entity player = *engine.FindPlayer("player0");
    const ecs::Hand* hand = engine.Store().Get<ecs::Hand>(player);
    REQUIRE(hand != nullptr);
    REQUIRE_FALSE(hand->cards.empty());
    const ecs::Entity card = hand->cards.front();
    engine.TakeEvents();
    REQUIRE(engine.PlayCard("player0", card));
    return engine.Events();
}

/** @brief Draw for player0 with `node` attached to `after:draw`. */
std::vector<json> DrawWithHook(MatchInstance& engine, const json& node) {
    AttachAfterDrawSystem(engine, MakeGraph(json::array({node})));
    engine.TakeEvents();
    REQUIRE(engine.DrawCard("player0"));
    return engine.Events();
}

std::vector<Viewer> AllViewers() {
    std::vector<Viewer> viewers = {Viewer::PublicSpectator(),
                                   Viewer::Spectator({}),
                                   Viewer::Spectator({"player0"}),
                                   Viewer::Spectator({"player1"})};
    for (int index = 0; index < kPlayerCount; ++index) {
        viewers.push_back(Viewer::Player("player" + std::to_string(index)));
    }
    return viewers;
}

json WrapFor(ViewBuilder& builder, const json& event, const Viewer& viewer) {
    EventSink sink;
    const std::optional<json> wrapped = builder.Wrap(event, viewer, sink);
    REQUIRE(wrapped.has_value());
    return *wrapped;
}

std::vector<leak_scanner::HiddenCard> EveryCard(MatchInstance& engine) {
    std::vector<leak_scanner::HiddenCard> hidden;
    for (ecs::Entity card : engine.Registries().cards) {
        hidden.push_back({card, BitsOf(engine, card)});
    }
    return hidden;
}

}  // namespace

TEST_CASE("hand and card events: a card play emits one event per op") {
    for (const OpCase& op_case : OpCases()) {
        CAPTURE(op_case.event_type);
        const std::vector<json> events =
            PlayProbeWith(op_case.node("@card"), {});
        const std::vector<json> found =
            EventsOfType(events, op_case.event_type);
        REQUIRE(found.size() == 1);
        CHECK(found[0]["causal_frame"]["actor"] == "player0");
        CHECK(found[0]["causal_frame"]["cause"] == "play");
    }
}

TEST_CASE("hand and card events: a hook emits one event per op") {
    for (const OpCase& op_case : OpCases()) {
        CAPTURE(op_case.event_type);
        Content content = LoadContent();
        auto engine = MakeEngine(content);
        const std::vector<json> events =
            DrawWithHook(*engine, op_case.node("@drawn_card"));
        const std::vector<json> found =
            EventsOfType(events, op_case.event_type);
        REQUIRE(found.size() == 1);
        CHECK(found[0]["causal_frame"]["actor"] == "player0");
    }
}

TEST_CASE("hand and card events: a replacing mutation still emits them") {
    for (const OpCase& op_case : OpCases()) {
        CAPTURE(op_case.event_type);
        const std::vector<json> events = PlayProbeWith(
            OpNode("draw_cards", {{"target", "@self"}, {"n", 1}}),
            {Mutation("replace", json::array({op_case.node("@card")}))});
        const std::vector<json> found =
            EventsOfType(events, op_case.event_type);
        REQUIRE(found.size() == 1);
        CHECK(found[0]["causal_frame"]["actor"] == "player0");
    }
}

TEST_CASE("hand and card events: a wrapping mutation emits once per op") {
    for (const OpCase& op_case : OpCases()) {
        CAPTURE(op_case.event_type);
        json wrap_nodes = json::array(
            {op_case.node("@card"),
             json{{"id", "n2"}, {"op", "call_original"}}});
        wrap_nodes[0]["next"] = "n2";
        const std::vector<json> events = PlayProbeWith(
            op_case.node("@card"), {Mutation("wrap", wrap_nodes)});
        const std::vector<json> found =
            EventsOfType(events, op_case.event_type);
        CHECK(found.size() == op_case.wrapped_events);
    }
}

TEST_CASE("hand events: counts and seats are public, no card identity") {
    const std::vector<std::pair<std::string, json>> hand_nodes = {
        {"hands_swapped",
         OpNode("swap_hands", {{"a", "@self"}, {"b", "@next_player"}})},
        {"hands_passed", OpNode("pass_hands", {{"direction", "forward"}})},
        {"hands_redistributed",
         OpNode("redistribute_hands", {{"mode", "even"}})},
    };
    for (const auto& [event_type, node] : hand_nodes) {
        CAPTURE(event_type);
        Content content = LoadContent();
        auto engine = MakeEngine(content);
        const std::vector<json> found =
            EventsOfType(DrawWithHook(*engine, node), event_type);
        REQUIRE(found.size() == 1);

        const std::vector<leak_scanner::HiddenCard> hidden =
            EveryCard(*engine);
        ViewBuilder builder(*engine, content.mods);
        json first_payload;
        for (const Viewer& viewer : AllViewers()) {
            const json wrapped = WrapFor(builder, found[0], viewer);
            CHECK(wrapped["id"] == event_type);
            CHECK(wrapped["actor"] == "player0");
            CHECK(leak_scanner::FindLeaks(wrapped, hidden).empty());
            CHECK_FALSE(wrapped["payload"].contains("card"));
            CHECK_FALSE(wrapped["payload"].contains("cards"));
            if (first_payload.is_null()) first_payload = wrapped["payload"];
            CHECK(wrapped["payload"] == first_payload);
        }
    }
}

TEST_CASE("hands_swapped: the payload names both seats and the card counts") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    const std::vector<json> found = EventsOfType(
        DrawWithHook(*engine,
                     OpNode("swap_hands",
                            {{"a", "@self"}, {"b", "@next_player"}})),
        "hands_swapped");
    REQUIRE(found.size() == 1);
    ViewBuilder builder(*engine, content.mods);
    const json payload =
        WrapFor(builder, found[0], Viewer::Player("player2"))["payload"];
    CHECK(payload["source"] == "player0");
    CHECK(payload["target"] == "player1");
    CHECK(payload["count"].is_number_integer());
    CHECK(payload["target_count"].is_number_integer());
    CHECK_FALSE(payload.contains("a"));
    CHECK_FALSE(payload.contains("a_size"));
}

TEST_CASE("hands_passed and hands_redistributed: seats and counts only") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    const std::vector<json> passed = EventsOfType(
        DrawWithHook(*engine,
                     OpNode("pass_hands", {{"direction", "backward"}})),
        "hands_passed");
    REQUIRE(passed.size() == 1);
    ViewBuilder builder(*engine, content.mods);
    const json payload =
        WrapFor(builder, passed[0], Viewer::PublicSpectator())["payload"];
    CHECK(payload["direction"] == "backward");
    CHECK(payload["targets"]
          == json::array({"player0", "player1", "player2"}));
    CHECK(payload["counts"].size() == kPlayerCount);
    CHECK_FALSE(payload.contains("players"));
    CHECK_FALSE(payload.contains("hand_sizes"));

    Content other_content = LoadContent();
    auto other = MakeEngine(other_content);
    const std::vector<json> redistributed = EventsOfType(
        DrawWithHook(*other, OpNode("redistribute_hands", {{"mode", "even"}})),
        "hands_redistributed");
    REQUIRE(redistributed.size() == 1);
    ViewBuilder other_builder(*other, other_content.mods);
    const json redistributed_payload =
        WrapFor(other_builder, redistributed[0],
                Viewer::Player("player1"))["payload"];
    CHECK(redistributed_payload.size() == 2);
    CHECK(redistributed_payload["targets"].size() == kPlayerCount);
    std::size_t total = 0;
    for (const json& count : redistributed_payload["counts"]) {
        total += count.get<std::size_t>();
    }
    std::size_t hand_total = 0;
    for (int index = 0; index < kPlayerCount; ++index) {
        hand_total += other->Store()
                          .Get<ecs::Hand>(*other->FindPlayer(
                              "player" + std::to_string(index)))
                          ->cards.size();
    }
    CHECK(total == hand_total);
}

TEST_CASE("card_moved: a hand to hand move shows the card to the new owner "
          "only") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const std::vector<ecs::Entity> before =
        engine->Store().Get<ecs::Hand>(player1)->cards;

    const std::vector<json> found = EventsOfType(
        DrawWithHook(*engine,
                     OpNode("transfer_card", {{"from_player", "@self"},
                                              {"to_player", "@next_player"},
                                              {"selector", "random"}})),
        "card_moved");
    REQUIRE(found.size() == 1);

    const std::vector<ecs::Entity>& after =
        engine->Store().Get<ecs::Hand>(player1)->cards;
    REQUIRE(after.size() == before.size() + 1);
    ecs::Entity moved{};
    for (ecs::Entity card : after) {
        if (std::find(before.begin(), before.end(), card) == before.end()) {
            moved = card;
        }
    }
    const leak_scanner::HiddenCard hidden{moved, BitsOf(*engine, moved)};

    ViewBuilder builder(*engine, content.mods);
    for (const Viewer& viewer : AllViewers()) {
        const json wrapped = WrapFor(builder, found[0], viewer);
        const json& payload = wrapped["payload"];
        CAPTURE(viewer.username);
        CHECK(payload["from_zone"] == "hand");
        CHECK(payload["to_zone"] == "hand");
        CHECK(payload["source"] == "player0");
        CHECK(payload["target"] == "player1");
        const bool sees_card =
            (!viewer.is_spectator && viewer.username == "player1")
            || (viewer.is_spectator && !viewer.SeesCardBacksOf("player1"));
        if (sees_card) {
            CHECK(payload["card"] == hidden.bits);
        } else {
            CHECK_FALSE(payload.contains("card"));
            CHECK(leak_scanner::FindLeaks(wrapped, {hidden}).empty());
        }
    }
}

TEST_CASE("card_moved: a card that leaves for limbo carries no identity") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    const std::vector<json> found = EventsOfType(
        DrawWithHook(*engine,
                     OpNode("move_card", {{"card", "@drawn_card"},
                                          {"to_zone", "limbo"}})),
        "card_moved");
    REQUIRE(found.size() == 1);
    ViewBuilder builder(*engine, content.mods);
    for (const Viewer& viewer : AllViewers()) {
        const json payload = WrapFor(builder, found[0], viewer)["payload"];
        CHECK(payload["from_zone"] == "hand");
        CHECK(payload["to_zone"] == "limbo");
        CHECK(payload["source"] == "player0");
        CHECK_FALSE(payload.contains("card"));
    }
}

TEST_CASE("card_removed: the identity goes to the former owner only") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    const std::vector<ecs::Entity> before =
        engine->Store().Get<ecs::Hand>(player0)->cards;
    const std::vector<json> found = EventsOfType(
        DrawWithHook(*engine, OpNode("remove_card", {{"card", "@drawn_card"}})),
        "card_removed");
    REQUIRE(found.size() == 1);

    const ecs::PileContents* draw =
        engine->Store().Get<ecs::PileContents>(engine->Registries().draw_pile);
    REQUIRE(draw != nullptr);
    ecs::Entity removed{};
    for (ecs::Entity card : engine->Registries().cards) {
        const ecs::InZone* zone = engine->Store().Get<ecs::InZone>(card);
        if (zone != nullptr && zone->zone.kind == ecs::ZoneKind::kLimbo) {
            removed = card;
        }
    }
    REQUIRE(engine->Store().IsAlive(removed));
    const leak_scanner::HiddenCard hidden{removed, BitsOf(*engine, removed)};

    ViewBuilder builder(*engine, content.mods);
    for (const Viewer& viewer : AllViewers()) {
        const json wrapped = WrapFor(builder, found[0], viewer);
        const json& payload = wrapped["payload"];
        CAPTURE(viewer.username);
        CHECK(payload["target"] == "player0");
        CHECK(payload["from_zone"] == "hand");
        const bool sees_card =
            (!viewer.is_spectator && viewer.username == "player0")
            || (viewer.is_spectator && !viewer.SeesCardBacksOf("player0"));
        if (sees_card) {
            CHECK(payload["card"] == hidden.bits);
        } else {
            CHECK_FALSE(payload.contains("card"));
            CHECK(leak_scanner::FindLeaks(wrapped, {hidden}).empty());
        }
    }
}

TEST_CASE("card_created and card_replaced: seat and zone, never identity") {
    const std::vector<std::pair<std::string, json>> nodes = {
        {"card_created",
         OpNode("materialize_card",
                {{"target_player", "@self"}, {"kind", kNewKind}})},
        {"card_replaced",
         OpNode("replace_card",
                {{"card", "@drawn_card"}, {"kind", kNewKind}})},
    };
    for (const auto& [event_type, node] : nodes) {
        CAPTURE(event_type);
        Content content = LoadContent();
        auto engine = MakeEngine(content);
        const std::vector<json> found =
            EventsOfType(DrawWithHook(*engine, node), event_type);
        REQUIRE(found.size() == 1);
        const std::vector<leak_scanner::HiddenCard> hidden =
            EveryCard(*engine);
        ViewBuilder builder(*engine, content.mods);
        for (const Viewer& viewer : AllViewers()) {
            const json wrapped = WrapFor(builder, found[0], viewer);
            const json& payload = wrapped["payload"];
            CHECK(payload["target"] == "player0");
            CHECK(payload["zone"] == "hand");
            CHECK(payload.size() == 2);
            CHECK(leak_scanner::FindLeaks(wrapped, hidden).empty());
        }
    }
}

TEST_CASE("hand and card events: an op that changes nothing emits nothing") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    const std::vector<json> events = DrawWithHook(
        *engine,
        OpNode("move_card", {{"card", "@drawn_card"}, {"to_zone", "hand"}}));
    CHECK(EventsOfType(events, "card_moved").empty());

    Content other_content = LoadContent();
    auto other = MakeEngine(other_content);
    const std::vector<json> same_kind = DrawWithHook(
        *other, json{{"id", "n1"},
                     {"op", "swap_hands"},
                     {"args", {{"a", "@self"}, {"b", "@self"}}}});
    CHECK(EventsOfType(same_kind, "hands_swapped").empty());
}

TEST_CASE("card_moved: a card that moves twice in one batch is not shown by "
          "the first event") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    json to_pile = OpNode("move_card", {{"card", "@drawn_card"},
                                        {"to_zone", "draw_pile"}});
    to_pile["next"] = "n2";
    const json draw_again = json{
        {"id", "n2"},
        {"op", "draw_cards"},
        {"args", {{"target", "@next_player"}, {"n", 1}}}};
    AttachAfterDrawSystem(*engine, MakeGraph(json::array({to_pile,
                                                          draw_again})));
    engine->TakeEvents();
    REQUIRE(engine->DrawCard("player0"));
    const std::vector<json> events = engine->Events();

    const std::vector<json> moved = EventsOfType(events, "card_moved");
    const std::vector<json> drawn = EventsOfType(events, "cards_drawn");
    REQUIRE(moved.size() == 1);
    REQUIRE(drawn.size() == 2);
    const ecs::Entity player1 = *engine->FindPlayer("player1");
    const ecs::Hand* hand = engine->Store().Get<ecs::Hand>(player1);
    const ecs::Entity card = hand->cards.back();
    const leak_scanner::HiddenCard hidden{card, BitsOf(*engine, card)};
    REQUIRE(moved[0]["payload"]["card"]["index"] == card.index);

    ViewBuilder builder(*engine, content.mods);
    for (const Viewer& viewer : AllViewers()) {
        const json wrapped = WrapFor(builder, moved[0], viewer);
        CAPTURE(viewer.username);
        CHECK_FALSE(wrapped["payload"].contains("card"));
        CHECK(leak_scanner::FindLeaks(wrapped, {hidden}).empty());
    }
    bool own_draw_found = false;
    for (const json& event : drawn) {
        const json own = WrapFor(builder, event, Viewer::Player("player1"));
        if (own["payload"]["target"] != "player1") continue;
        own_draw_found = true;
        CHECK(own["payload"]["cards"] == json::array({hidden.bits}));
    }
    CHECK(own_draw_found);
}

TEST_CASE("card_removed: a removal from a pile reaches nobody with the "
          "card") {
    for (const std::string zone : {"draw_pile", "discard_pile"}) {
        CAPTURE(zone);
        Content content = LoadContent();
        auto engine = MakeEngine(content);
        json to_pile = OpNode("move_card", {{"card", "@drawn_card"},
                                            {"to_zone", zone}});
        to_pile["next"] = "n2";
        json removal =
            OpNode("remove_card", {{"card", "@drawn_card"}});
        removal["id"] = "n2";
        AttachAfterDrawSystem(*engine,
                              MakeGraph(json::array({to_pile, removal})));
        engine->TakeEvents();
        REQUIRE(engine->DrawCard("player0"));
        const std::vector<json> found =
            EventsOfType(engine->Events(), "card_removed");
        REQUIRE(found.size() == 1);
        const std::vector<leak_scanner::HiddenCard> hidden =
            EveryCard(*engine);
        ViewBuilder builder(*engine, content.mods);
        for (const Viewer& viewer : AllViewers()) {
            const json wrapped = WrapFor(builder, found[0], viewer);
            CHECK(wrapped["payload"]["from_zone"] == zone);
            CHECK_FALSE(wrapped["payload"].contains("target"));
            CHECK_FALSE(wrapped["payload"].contains("card"));
            CHECK(leak_scanner::FindLeaks(wrapped, hidden).empty());
        }
    }
}

TEST_CASE("hands_swapped: the actor can differ from the source seat") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    const std::vector<json> found = EventsOfType(
        DrawWithHook(*engine,
                     OpNode("swap_hands",
                            {{"a", "@next_player"}, {"b", "@prev_player"}})),
        "hands_swapped");
    REQUIRE(found.size() == 1);
    ViewBuilder builder(*engine, content.mods);
    for (const Viewer& viewer : AllViewers()) {
        const json wrapped = WrapFor(builder, found[0], viewer);
        CHECK(wrapped["actor"] == "player0");
        CHECK(wrapped["payload"]["source"] == "player1");
        CHECK(wrapped["payload"]["target"] == "player2");
    }
}
