#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>
#include <match/view/event_sink.hpp>
#include <match/view/event_tail.hpp>
#include <match/view/view_builder.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file card_state_event_test.cpp
 * @brief `card_changed` projection and redaction by attribute/property
 *        visibility.
 *
 * The tests drive the Task 7 ops through the engine's default runtime and
 * project the emitted descriptor with the same `ViewBuilder` path the live
 * stream uses. An `owner` value reaches only the holder, a `hidden` value
 * reaches nobody, and a card in the discard pile shows its identity but not a
 * non-public value.
 */

namespace {

namespace fs = std::filesystem;
namespace ecs = match::ecs;
namespace ops = match::ops;
using match::engine::MatchAssembler;
using match::engine::MatchAssemblyOptions;
using match::engine::MatchInstance;
using match::engine::MatchPlayerSpec;
using match::modload::DeckDef;
using match::modload::LoadResult;
using match::modload::LoadedMod;
using match::view::EventSink;
using match::view::EventTail;
using match::view::ViewBuilder;
using match::view::Viewer;
using nlohmann::json;

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

fs::path FixtureClean() {
    return ProjectRoot() / "tests" / "fixtures" / "card_schema" / "clean";
}

std::vector<LoadedMod> LoadMods() {
    const LoadResult load =
        match::modload::ScanModsDirectory(FixtureClean().string());
    REQUIRE(load.ok());
    return load.mods;
}

DeckDef Deck() {
    DeckDef deck;
    deck.id = "card_state_event";
    deck.namespace_id = "test";
    deck.deck_id = "test:card_state_event";
    deck.name = "Card state event";
    deck.mods = {"uni"};
    // INFO: the six trinkets cover both values of every slot. The numbered
    //       cards are the plan-1 generated cards.
    deck.cards = {{"uni:red_spade_alpha", 1},
                  {"uni:red_spade_beta", 1},
                  {"uni:red_heart_alpha", 1},
                  {"uni:red_heart_beta", 1},
                  {"uni:blue_spade_alpha", 1},
                  {"uni:blue_heart_beta", 1},
                  {"uni:red_1", 6}};
    return deck;
}

std::unique_ptr<MatchInstance> MakeMatch(const std::vector<LoadedMod>& mods) {
    MatchAssemblyOptions options;
    options.starting_cards = 0;
    options.seed = 1;
    for (const char* player : {"Alice", "Bob"}) {
        MatchPlayerSpec spec;
        spec.username = player;
        options.players.push_back(spec);
    }
    auto assembly = MatchAssembler::Assemble(mods, Deck(), options);
    REQUIRE(assembly.ok());
    return std::make_unique<MatchInstance>(std::move(assembly.assembly));
}

ecs::Entity FindCard(MatchInstance& engine, const std::string& kind_id) {
    for (const ecs::Entity card : engine.Registries().cards) {
        const ecs::CardIdentity* identity =
            engine.Store().Get<ecs::CardIdentity>(card);
        if (identity != nullptr && identity->kind_id == kind_id) {
            return card;
        }
    }
    FAIL("card not found: " << kind_id);
    return {};
}

void MoveTo(MatchInstance& engine, ecs::Entity card, ecs::ZoneRef zone) {
    REQUIRE(ops::MoveCardToZone(engine.Store(), card, zone));
}

ecs::Entity HandCard(MatchInstance& engine, const std::string& kind_id) {
    const ecs::Entity card = FindCard(engine, kind_id);
    REQUIRE(engine.FindPlayer("Alice").has_value());
    MoveTo(engine, card,
           ecs::ZoneRef{ecs::ZoneKind::kHand, *engine.FindPlayer("Alice")});
    return card;
}

ops::OpResult RunOp(MatchInstance& engine, const std::string& op,
                    const json& raw, ecs::Entity card) {
    ops::OpArgs args(op, raw);
    args.BindSelector("card", {card});
    ops::ResolutionFrame frame;
    ops::OpContext context(engine.Assembly().bus, engine.Assembly().budget,
                           frame);
    context.registries = &engine.Registries();
    return engine.Assembly().runtime.Invoke(op, engine.Store(), args, context);
}

/**
 * @brief The `card_changed` payload this viewer receives, if any.
 *
 * A dropped event never becomes a packet: `ViewBuilder::Wrap` returns
 * `nullopt` when no field survives. A delivered packet must carry a
 * non-empty payload.
 */
std::optional<json> CardChangedFor(ViewBuilder& builder, const json& event,
                                   const Viewer& viewer) {
    EventSink sink;
    const std::optional<json> packet = builder.Wrap(event, viewer, sink);
    if (!packet.has_value()) return std::nullopt;
    if (packet->value("id", std::string()) != "card_changed") {
        return std::nullopt;
    }
    return (*packet)["payload"];
}

const Viewer kAlice = Viewer::Player("Alice");
const Viewer kBob = Viewer::Player("Bob");
const Viewer kSpectator = Viewer::PublicSpectator();

json Emit(MatchInstance& engine, const std::string& op, const json& raw,
          ecs::Entity card) {
    const ops::OpResult result = RunOp(engine, op, raw, card);
    REQUIRE(result.status == ops::OpStatus::kResolved);
    REQUIRE(result.events.size() == 1);
    REQUIRE(result.events.front().value("type", std::string())
            == "card_changed");
    return result.events.front();
}

}  // namespace

TEST_SUITE("card state event") {

TEST_CASE("public_change_reaches_everyone") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    const json event =
        Emit(*engine, "set_card_member",
             json{{"slot", "color"}, {"member", "uni:blue"}}, card);

    for (const Viewer* viewer : {&kAlice, &kBob, &kSpectator}) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        const std::optional<json> payload =
            CardChangedFor(builder, event, *viewer);
        REQUIRE(payload.has_value());
        CHECK((*payload)["slot"] == "color");
        CHECK((*payload)["member"] == "uni:blue");
        CHECK((*payload)["previous"] == "uni:red");
        if (viewer != &kAlice) CHECK_FALSE(payload->contains("card"));
        if (viewer == &kAlice) CHECK(payload->contains("card"));
    }
}

TEST_CASE("owner_change_reaches_the_holder_only") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    const json event =
        Emit(*engine, "set_card_member",
             json{{"slot", "suit"}, {"member", "uni:heart"}}, card);

    const std::optional<json> alice =
        CardChangedFor(builder, event, kAlice);
    REQUIRE(alice.has_value());
    CHECK((*alice)["slot"] == "suit");
    CHECK((*alice)["member"] == "uni:heart");
    CHECK((*alice)["previous"] == "uni:spade");
    CHECK(alice->contains("card"));

    CHECK_FALSE(CardChangedFor(builder, event, kBob).has_value());
    CHECK_FALSE(CardChangedFor(builder, event, kSpectator).has_value());
}

TEST_CASE("hidden_change_reaches_nobody_with_value") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    const json event =
        Emit(*engine, "set_card_member",
             json{{"slot", "secret"}, {"member", "uni:beta"}}, card);

    const std::optional<json> alice =
        CardChangedFor(builder, event, kAlice);
    REQUIRE(alice.has_value());
    CHECK((*alice)["slot"] == "secret");
    CHECK(alice->contains("card"));
    CHECK_FALSE(alice->contains("member"));
    CHECK_FALSE(alice->contains("previous"));

    CHECK_FALSE(CardChangedFor(builder, event, kBob).has_value());
    CHECK_FALSE(CardChangedFor(builder, event, kSpectator).has_value());
}

TEST_CASE("discard_pile_card_is_public_identity") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = FindCard(*engine, "uni:red_spade_alpha");
    MoveTo(*engine, card,
           ecs::ZoneRef{ecs::ZoneKind::kDiscardPile, ecs::Entity{}});

    const json event =
        Emit(*engine, "set_card_member",
             json{{"slot", "suit"}, {"member", "uni:heart"}}, card);

    for (const Viewer* viewer : {&kAlice, &kBob, &kSpectator}) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        const std::optional<json> payload =
            CardChangedFor(builder, event, *viewer);
        REQUIRE(payload.has_value());
        CHECK(payload->contains("card"));
        CHECK((*payload)["slot"] == "suit");
        CHECK_FALSE(payload->contains("member"));
        CHECK_FALSE(payload->contains("previous"));
    }
}

TEST_CASE("property_visibility_owner") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    const json event =
        Emit(*engine, "set_card_property",
             json{{"property", "uni:revealed"}, {"value", true}}, card);

    const std::optional<json> alice =
        CardChangedFor(builder, event, kAlice);
    REQUIRE(alice.has_value());
    CHECK((*alice)["property"] == "uni:revealed");
    CHECK((*alice)["value"] == true);
    CHECK((*alice)["previous"] == false);
    CHECK(alice->contains("card"));

    CHECK_FALSE(CardChangedFor(builder, event, kBob).has_value());
    CHECK_FALSE(CardChangedFor(builder, event, kSpectator).has_value());
}

TEST_CASE("a_dropped_event_does_not_consume_a_sequence") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    const json event =
        Emit(*engine, "set_card_member",
             json{{"slot", "suit"}, {"member", "uni:heart"}}, card);

    EventSink sink;
    CHECK_FALSE(builder.Wrap(event, kBob, sink).has_value());
    CHECK(sink.NextSeq() == 0);
    const std::optional<json> alice = builder.Wrap(event, kAlice, sink);
    REQUIRE(alice.has_value());
    CHECK((*alice)["sequence"] == 0);
    CHECK(sink.NextSeq() == 1);
}

TEST_CASE("late_join_tail_respects_redaction") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    const json event =
        Emit(*engine, "set_card_member",
             json{{"slot", "suit"}, {"member", "uni:heart"}}, card);

    EventTail tail;
    tail.Record(event);
    const json alice_envelopes = builder.BuildTail(kAlice, tail);
    REQUIRE(alice_envelopes.size() == 1);
    CHECK(alice_envelopes.front()["payload"]["member"] == "uni:heart");

    const json bob_envelopes = builder.BuildTail(kBob, tail);
    CHECK(bob_envelopes.empty());
}

}  // namespace
