#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/status.hpp>
#include <match/timers.hpp>
#include <match/view/event_sink.hpp>
#include <match/view/view_builder.hpp>

#include "support/leak_scanner.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file engine_draw_events_test.cpp
 * @brief The `cards_drawn` and `drawn_card_kept` events, from the op to the
 *        seat stream.
 *
 * The `draw_cards` primitive and the engine draw path emit `cards_drawn`, so a
 * card play, a hook, and a mutation that still reaches the primitive all emit
 * it. `KeepDrawn` and the turn clock emit `drawn_card_kept`. A mutation that
 * moves a card from the draw pile with `move_card` belongs to the card
 * movement events, not to this file.
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

constexpr const char* kDrawnType = "cards_drawn";
constexpr const char* kKeptType = "drawn_card_kept";
constexpr int kPlayerCount = 3;
constexpr int64_t kTimeLimitMs = 15'000;

struct FakeClock {
    int64_t now = 0;

    match::NowMs Fn() {
        return [this]() { return now; };
    }
};

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

std::unique_ptr<MatchInstance> MakeEngine(Content& content,
                                          match::NowMs clock = nullptr) {
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
    if (clock) {
        return std::make_unique<MatchInstance>(std::move(result.assembly),
                                               std::move(clock));
    }
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

std::size_t HandSize(MatchInstance& engine, const std::string& username) {
    const ecs::Hand* hand =
        engine.Store().Get<ecs::Hand>(*engine.FindPlayer(username));
    return hand == nullptr ? 0 : hand->cards.size();
}

std::vector<json> EventsOfType(const std::vector<json>& events,
                               const std::string& type) {
    std::vector<json> found;
    for (const json& event : events) {
        if (event.value("type", "") == type) found.push_back(event);
    }
    return found;
}

ecs::PileContents* DrawPile(MatchInstance& engine) {
    return engine.Store().Get<ecs::PileContents>(
        engine.Registries().draw_pile);
}

/** @brief Move a numbered card to the draw pile top; make it playable. */
std::optional<ecs::Entity> ArmPlayableDraw(MatchInstance& engine) {
    ecs::PileContents* draw = DrawPile(engine);
    if (draw == nullptr) return std::nullopt;
    for (std::size_t index = 0; index < draw->cards.size(); ++index) {
        const ecs::FaceSpec* face =
            engine.Store().Get<ecs::FaceSpec>(draw->cards[index]);
        if (face == nullptr || face->color == "white") continue;
        if (face->label.size() != 1) continue;
        if (!std::isdigit(static_cast<unsigned char>(face->label[0]))) {
            continue;
        }
        std::swap(draw->cards[index], draw->cards.back());
        for (std::size_t slot = 0; slot < draw->cards.size(); ++slot) {
            if (ecs::InZone* zone =
                    engine.Store().Get<ecs::InZone>(draw->cards[slot])) {
                zone->ordinal = static_cast<uint32_t>(slot);
            }
        }
        ecs::ActiveTypeReq* active = engine.Store().Get<ecs::ActiveTypeReq>(
            engine.Assembly().registries.match);
        REQUIRE(active != nullptr);
        active->type = face->color;
        return draw->cards.back();
    }
    return std::nullopt;
}

uint32_t BitsOf(MatchInstance& engine, ecs::Entity card) {
    const std::optional<ecs::CompactCardV2> id =
        engine.Registries().CardId(card);
    REQUIRE(id.has_value());
    return id->bits;
}

BehaviorGraph MakeGraph(json nodes) {
    BehaviorGraph graph;
    graph.nodes = std::move(nodes);
    graph.raw = json{{"nodes", graph.nodes}};
    return graph;
}

json DrawNode(const std::string& id, const json& extra_args,
              const std::string& next = "") {
    json args = json{{"target", "@self"}};
    args.update(extra_args);
    json node = json{{"id", id}, {"op", "draw_cards"}, {"args", args}};
    if (!next.empty()) node["next"] = next;
    return node;
}

void AttachAfterPlaySystem(MatchInstance& engine, BehaviorGraph graph) {
    MatchAssembly& assembly = engine.Assembly();
    ModSystem system;
    system.mod_id = "test";
    system.registration_index = 0;
    system.hook = ecs::HookId{"play", ecs::HookPhase::kAfter};
    system.source_id = "test:draw_system";
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

CardDef DrawerCard(const std::string& ns, const std::string& local) {
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
    entry.graph = MakeGraph(json::array({DrawNode("n1", json{{"n", 1}})}));
    card.behaviors.push_back(std::move(entry));
    return card;
}

MutationDef Mutation(const std::string& mode, json nodes) {
    MutationDef mutation;
    mutation.id = "swap";
    mutation.namespace_id = "fix";
    mutation.mutation_id = "fix:swap";
    mutation.target = "base:drawer";
    mutation.mode = mode;
    mutation.replacement = MakeGraph(std::move(nodes));
    return mutation;
}

/** @brief Play the base:drawer card and return the engine events. */
std::vector<json> PlayDrawerWith(const std::vector<MutationDef>& mutations) {
    LoadedMod base;
    base.manifest = Manifest("base");
    base.cards.push_back(DrawerCard("base", "drawer"));
    LoadedMod fix;
    fix.manifest = Manifest("fix");
    fix.mutations = mutations;

    DeckDef deck;
    deck.id = "fixture";
    deck.namespace_id = "fixture";
    deck.deck_id = "fixture:deck";
    deck.name = "Fixture";
    deck.mods = {"base", "fix"};
    deck.cards = {{"base:drawer", 16}};

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
    const ecs::Hand* hand = engine.Store().Get<ecs::Hand>(player);
    REQUIRE(hand != nullptr);
    REQUIRE_FALSE(hand->cards.empty());
    const ecs::Entity card = hand->cards.front();
    ForceHand(engine, player, {card});
    engine.TakeEvents();
    REQUIRE(engine.PlayCard("player0", card));
    return engine.Events();
}

}  // namespace

TEST_CASE("cards_drawn: a voluntary draw carries target data, hand size "
          "and actor") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    engine->TakeEvents();

    REQUIRE(engine->DrawCard("player0"));

    const std::vector<json> drawn =
        EventsOfType(engine->Events(), kDrawnType);
    REQUIRE(drawn.size() == 1);
    CHECK(drawn[0]["payload"]["count"] == 1);
    CHECK(drawn[0]["payload"]["hand_size_after"]
          == HandSize(*engine, "player0"));
    CHECK(drawn[0]["payload"]["from_debt"] == false);
    CHECK(drawn[0]["causal_frame"]["actor"] == "player0");
    CHECK(drawn[0]["causal_frame"]["cause"] == "draw");
}

TEST_CASE("cards_drawn: a card play that draws emits it with the actor") {
    const std::vector<json> events = PlayDrawerWith({});

    const std::vector<json> drawn = EventsOfType(events, kDrawnType);
    REQUIRE(drawn.size() == 1);
    CHECK(drawn[0]["payload"]["count"] == 1);
    CHECK(drawn[0]["payload"]["from_debt"] == false);
    CHECK(drawn[0]["causal_frame"]["actor"] == "player0");
    CHECK(drawn[0]["causal_frame"]["cause"] == "play");
}

TEST_CASE("cards_drawn: a hook that pays a debt marks from_debt") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    AttachAfterPlaySystem(
        *engine,
        MakeGraph(json::array({DrawNode("n1", json{{"n_from_debt", true}})})));
    const std::optional<ecs::Entity> card = FindPlainNumberedCard(*engine);
    REQUIRE(card.has_value());
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    ForceHand(*engine, player0, {*card});
    match::status::ApplyRequest debt;
    debt.status_id = std::string(ops::kDrawDebtStatusId);
    debt.magnitude = 3;
    REQUIRE(match::status::Apply(engine->Store(), player0, debt).applied);
    engine->TakeEvents();

    REQUIRE(engine->PlayCard("player0", *card));

    const std::vector<json> drawn =
        EventsOfType(engine->Events(), kDrawnType);
    REQUIRE(drawn.size() == 1);
    CHECK(drawn[0]["payload"]["count"] == 3);
    CHECK(drawn[0]["payload"]["from_debt"] == true);
    CHECK(drawn[0]["payload"]["hand_size_after"]
          == HandSize(*engine, "player0"));
    CHECK(drawn[0]["causal_frame"]["actor"] == "player0");
}

TEST_CASE("cards_drawn: a hook with a fixed count is not from_debt") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    AttachAfterPlaySystem(
        *engine, MakeGraph(json::array({DrawNode("n1", json{{"n", 2}})})));
    const std::optional<ecs::Entity> card = FindPlainNumberedCard(*engine);
    REQUIRE(card.has_value());
    ForceHand(*engine, *engine->FindPlayer("player0"), {*card});
    engine->TakeEvents();

    REQUIRE(engine->PlayCard("player0", *card));

    const std::vector<json> drawn =
        EventsOfType(engine->Events(), kDrawnType);
    REQUIRE(drawn.size() == 1);
    CHECK(drawn[0]["payload"]["count"] == 2);
    CHECK(drawn[0]["payload"]["from_debt"] == false);
}

TEST_CASE("cards_drawn: a replacing mutation still emits it") {
    const std::vector<json> events = PlayDrawerWith({Mutation(
        "replace",
        json::array({DrawNode("r1", json{{"n", 2}}, "r2"),
                     DrawNode("r2", json{{"n", 3}})}))});

    const std::vector<json> drawn = EventsOfType(events, kDrawnType);
    REQUIRE(drawn.size() == 2);
    CHECK(drawn[0]["payload"]["count"] == 2);
    CHECK(drawn[1]["payload"]["count"] == 3);
    CHECK(drawn[0]["causal_frame"]["actor"] == "player0");
}

TEST_CASE("cards_drawn: a wrapping mutation emits it once per op") {
    const std::vector<json> events = PlayDrawerWith({Mutation(
        "wrap", json::array({DrawNode("w1", json{{"n", 2}}, "w2"),
                             json{{"id", "w2"}, {"op", "call_original"}}}))});

    const std::vector<json> drawn = EventsOfType(events, kDrawnType);
    REQUIRE(drawn.size() == 2);
    CHECK(drawn[0]["payload"]["count"] == 2);
    CHECK(drawn[1]["payload"]["count"] == 1);
}

TEST_CASE("cards_drawn: identities go to the drawer only, the rest is "
          "public") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    engine->TakeEvents();
    REQUIRE(engine->DrawCard("player0"));
    const std::vector<json> drawn =
        EventsOfType(engine->Events(), kDrawnType);
    REQUIRE(drawn.size() == 1);
    const std::size_t hand_size = HandSize(*engine, "player0");

    ViewBuilder builder(*engine, content.mods);
    std::vector<Viewer> viewers = {Viewer::PublicSpectator()};
    for (int index = 0; index < kPlayerCount; ++index) {
        viewers.push_back(Viewer::Player("player" + std::to_string(index)));
    }
    for (const Viewer& viewer : viewers) {
        EventSink sink;
        const std::optional<json> wrapped =
            builder.Wrap(drawn[0], viewer, sink);
        REQUIRE(wrapped.has_value());
        const json& payload = (*wrapped)["payload"];
        CHECK((*wrapped)["id"] == kDrawnType);
        CHECK((*wrapped)["actor"] == "player0");
        CHECK(payload["target"] == "player0");
        CHECK(payload["count"] == 1);
        CHECK(payload["hand_size_after"] == hand_size);
        CHECK(payload["from_debt"] == false);
        CHECK(payload.contains("cards") == (viewer.username == "player0"));
    }
}

TEST_CASE("drawn_card_kept: a keep emits it with the owner-only card") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    const std::optional<ecs::Entity> drawn_card = ArmPlayableDraw(*engine);
    REQUIRE(drawn_card.has_value());
    REQUIRE(engine->DrawCard("player0"));
    REQUIRE(engine->PendingPlayDrawnState().has_value());
    engine->TakeEvents();

    REQUIRE(engine->KeepDrawn("player0"));

    const std::vector<json> kept = EventsOfType(engine->Events(), kKeptType);
    REQUIRE(kept.size() == 1);
    CHECK(kept[0]["causal_frame"]["actor"] == "player0");
    CHECK(kept[0]["causal_frame"]["cause"] == "keep_drawn");

    ViewBuilder builder(*engine, content.mods);
    std::vector<Viewer> viewers = {Viewer::PublicSpectator()};
    for (int index = 0; index < kPlayerCount; ++index) {
        viewers.push_back(Viewer::Player("player" + std::to_string(index)));
    }
    for (const Viewer& viewer : viewers) {
        EventSink sink;
        const std::optional<json> wrapped =
            builder.Wrap(kept[0], viewer, sink);
        REQUIRE(wrapped.has_value());
        const json& payload = (*wrapped)["payload"];
        CHECK((*wrapped)["id"] == kKeptType);
        CHECK((*wrapped)["actor"] == "player0");
        CHECK(payload["target"] == "player0");
        if (viewer.username == "player0") {
            CHECK(payload["card"] == BitsOf(*engine, *drawn_card));
        } else {
            CHECK_FALSE(payload.contains("card"));
        }
    }
}

TEST_CASE("drawn_card_kept: a lapsed turn clock keeps the card and emits it") {
    FakeClock clock;
    Content content = LoadContent();
    auto engine = MakeEngine(content, clock.Fn());
    REQUIRE(ArmPlayableDraw(*engine).has_value());
    REQUIRE(engine->DrawCard("player0"));
    REQUIRE(engine->PendingPlayDrawnState().has_value());
    engine->TakeEvents();

    engine->SyncClocks(kTimeLimitMs);
    clock.now = kTimeLimitMs + 1;
    engine->Tick();

    const std::vector<json> kept = EventsOfType(engine->Events(), kKeptType);
    REQUIRE(kept.size() == 1);
    CHECK(kept[0]["causal_frame"]["cause"] == "turn_timer_expired");
}

TEST_CASE("drawn_card_kept: a refused keep and a play emit nothing") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    const std::optional<ecs::Entity> drawn_card = ArmPlayableDraw(*engine);
    REQUIRE(drawn_card.has_value());
    REQUIRE(engine->DrawCard("player0"));
    engine->TakeEvents();

    CHECK_FALSE(engine->KeepDrawn("player1"));
    CHECK(EventsOfType(engine->Events(), kKeptType).empty());

    REQUIRE(engine->PlayCard("player0", *drawn_card));
    CHECK(EventsOfType(engine->Events(), kKeptType).empty());
}

TEST_CASE("cards_drawn: a voluntary draw with a debt open never pays the debt") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    const ecs::Entity player0 = *engine->FindPlayer("player0");
    match::status::ApplyRequest debt;
    debt.status_id = std::string(ops::kDrawDebtStatusId);
    debt.magnitude = 2;
    REQUIRE(match::status::Apply(engine->Store(), player0, debt).applied);
    engine->TakeEvents();

    const bool accepted = engine->DrawCard("player0");

    const std::vector<json> drawn =
        EventsOfType(engine->Events(), kDrawnType);
    if (!accepted) CHECK(drawn.empty());
    for (const json& event : drawn) {
        CHECK(event["payload"]["from_debt"] == false);
    }
}

TEST_CASE("drawn_card_kept: other seats and a private spectator never see "
          "the card") {
    Content content = LoadContent();
    auto engine = MakeEngine(content);
    const std::optional<ecs::Entity> drawn_card = ArmPlayableDraw(*engine);
    REQUIRE(drawn_card.has_value());
    REQUIRE(engine->DrawCard("player0"));
    engine->TakeEvents();
    REQUIRE(engine->KeepDrawn("player0"));
    const std::vector<json> kept = EventsOfType(engine->Events(), kKeptType);
    REQUIRE(kept.size() == 1);

    const leak_scanner::HiddenCard hidden{*drawn_card,
                                          BitsOf(*engine, *drawn_card)};
    ViewBuilder builder(*engine, content.mods);
    const std::vector<Viewer> viewers = {
        Viewer::Player("player1"), Viewer::Player("player2"),
        Viewer::PublicSpectator(), Viewer::Spectator({"player0"})};
    for (const Viewer& viewer : viewers) {
        EventSink sink;
        const std::optional<json> wrapped =
            builder.Wrap(kept[0], viewer, sink);
        REQUIRE(wrapped.has_value());
        CHECK(leak_scanner::FindLeaks(*wrapped, {hidden}).empty());
        CHECK_FALSE((*wrapped)["payload"].contains("card"));
    }
}
