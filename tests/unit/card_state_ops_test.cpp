#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file card_state_ops_test.cpp
 * @brief `set_card_member` and `set_card_property` through the engine op path.
 *
 * Each test assembles a three-card match from the `clean` fixture mod, moves
 * the card under test into a hand and invokes the op on the engine's default
 * runtime with its frozen registries. Safe fails leave both the state and the
 * event stream untouched.
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
using nlohmann::json;

// INFO: walk up from this test file to the repository root, so the fixture
// mods resolve no matter where the test binary is started.
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

MatchAssemblyOptions Players(int count) {
    MatchAssemblyOptions options;
    options.starting_cards = 0;
    options.seed = 1;
    for (int index = 0; index < count; ++index) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(index);
        options.players.push_back(spec);
    }
    return options;
}

DeckDef SmallDeck() {
    DeckDef deck;
    deck.id = "card_state";
    deck.namespace_id = "test";
    deck.deck_id = "test:card_state";
    deck.name = "Card state";
    deck.mods = {"uni"};
    deck.cards = {{"uni:red_1", 2}, {"uni:blue_1", 1}};
    return deck;
}

std::unique_ptr<MatchInstance> MakeMatch() {
    const std::vector<LoadedMod> mods = LoadMods();
    auto assembly = MatchAssembler::Assemble(mods, SmallDeck(), Players(2));
    REQUIRE(assembly.ok());
    return std::make_unique<MatchInstance>(std::move(assembly.assembly));
}

std::optional<ecs::Entity> FindCard(MatchInstance& engine,
                                    const std::string& kind_id) {
    for (const ecs::Entity card : engine.Registries().cards) {
        const ecs::CardIdentity* identity =
            engine.Store().Get<ecs::CardIdentity>(card);
        if (identity != nullptr && identity->kind_id == kind_id) {
            return card;
        }
    }
    return std::nullopt;
}

ecs::Entity HandCard(MatchInstance& engine, const std::string& kind_id) {
    const std::optional<ecs::Entity> card = FindCard(engine, kind_id);
    REQUIRE(card.has_value());
    const std::optional<ecs::Entity> player =
        engine.FindPlayer("player0");
    REQUIRE(player.has_value());
    REQUIRE(ops::MoveCardToZone(
        engine.Store(), *card,
        ecs::ZoneRef{ecs::ZoneKind::kHand, *player}));
    return *card;
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

}  // namespace

TEST_SUITE("card state ops") {

TEST_CASE("set_member_changes_effective_member_and_emits") {
    const std::unique_ptr<MatchInstance> engine = MakeMatch();
    const ecs::Entity card = HandCard(*engine, "uni:red_1");
    const std::optional<ecs::Entity> player = engine->FindPlayer("player0");
    REQUIRE(player.has_value());

    const ops::OpResult result = RunOp(
        *engine, "set_card_member",
        json{{"slot", "color"}, {"member", "uni:blue"}}, card);

    CHECK(result.status == ops::OpStatus::kResolved);
    const ecs::CardState* state = engine->Store().Get<ecs::CardState>(card);
    REQUIRE(state != nullptr);
    CHECK(state->member_overrides
          == std::map<std::string, std::string>{{"color", "uni:blue"}});
    REQUIRE(result.events.size() == 1);
    const json& event = result.events.front();
    CHECK(event["type"] == "card_changed");
    CHECK(event["payload"]["slot"] == "color");
    CHECK(event["payload"]["member"] == "uni:blue");
    CHECK(event["payload"]["previous"] == "uni:red");
    CHECK(event["payload"]["visibility"] == "public");
    CHECK(event["payload"]["card"]
          == json{{"index", card.index}, {"generation", card.generation}});
    CHECK(event["payload"]["target"]
          == json{{"index", player->index},
                  {"generation", player->generation}});
}

TEST_CASE("set_member_revert_emits_and_keeps_the_override") {
    const std::unique_ptr<MatchInstance> engine = MakeMatch();
    const ecs::Entity card = HandCard(*engine, "uni:red_1");

    const ops::OpResult first = RunOp(
        *engine, "set_card_member",
        json{{"slot", "color"}, {"member", "uni:blue"}}, card);
    REQUIRE(first.events.size() == 1);

    const ops::OpResult revert = RunOp(
        *engine, "set_card_member",
        json{{"slot", "color"}, {"member", "uni:red"}}, card);

    CHECK(revert.status == ops::OpStatus::kResolved);
    REQUIRE(revert.events.size() == 1);
    const json& event = revert.events.front();
    CHECK(event["type"] == "card_changed");
    CHECK(event["payload"]["member"] == "uni:red");
    CHECK(event["payload"]["previous"] == "uni:blue");
    // INFO: the ops never erase an override; the snapshot filters values
    //       that equal the dealt member (Task 10).
    const ecs::CardState* state = engine->Store().Get<ecs::CardState>(card);
    REQUIRE(state != nullptr);
    CHECK(state->member_overrides
          == std::map<std::string, std::string>{{"color", "uni:red"}});
}

TEST_CASE("set_member_same_value_is_noop") {
    const std::unique_ptr<MatchInstance> engine = MakeMatch();
    const ecs::Entity card = HandCard(*engine, "uni:red_1");

    const ops::OpResult result = RunOp(
        *engine, "set_card_member",
        json{{"slot", "color"}, {"member", "uni:red"}}, card);

    CHECK(result.status == ops::OpStatus::kResolved);
    CHECK(result.events.empty());
    CHECK(engine->Store().Get<ecs::CardState>(card) == nullptr);
}

TEST_CASE("set_member_foreign_attribute_rejected") {
    const std::unique_ptr<MatchInstance> engine = MakeMatch();
    const ecs::Entity card = HandCard(*engine, "uni:red_1");

    const ops::OpResult result = RunOp(
        *engine, "set_card_member",
        json{{"slot", "color"}, {"member", "uni:1"}}, card);

    CHECK(result.status == ops::OpStatus::kResolved);
    CHECK(result.events.empty());
    CHECK(engine->Store().Get<ecs::CardState>(card) == nullptr);
}

TEST_CASE("set_member_unknown_slot_rejected") {
    const std::unique_ptr<MatchInstance> engine = MakeMatch();
    const ecs::Entity card = HandCard(*engine, "uni:red_1");

    const ops::OpResult result = RunOp(
        *engine, "set_card_member",
        json{{"slot", "suit"}, {"member", "uni:blue"}}, card);

    CHECK(result.status == ops::OpStatus::kResolved);
    CHECK(result.events.empty());
    CHECK(engine->Store().Get<ecs::CardState>(card) == nullptr);
}

TEST_CASE("set_property_boolean_emits") {
    const std::unique_ptr<MatchInstance> engine = MakeMatch();
    const ecs::Entity card = HandCard(*engine, "uni:red_1");

    const ops::OpResult result =
        RunOp(*engine, "set_card_property",
              json{{"property", "uni:revealed"}, {"value", true}}, card);

    const ecs::CardState* state = engine->Store().Get<ecs::CardState>(card);
    REQUIRE(state != nullptr);
    CHECK(state->properties.at("uni:revealed") == true);
    CHECK(result.status == ops::OpStatus::kResolved);
    REQUIRE(result.events.size() == 1);
    const json& event = result.events.front();
    CHECK(event["type"] == "card_changed");
    CHECK(event["payload"]["property"] == "uni:revealed");
    CHECK(event["payload"]["value"] == true);
    CHECK(event["payload"]["previous"] == false);
    CHECK(event["payload"]["visibility"] == "owner");
    CHECK(event["payload"]["card"]
          == json{{"index", card.index}, {"generation", card.generation}});
}

TEST_CASE("set_property_type_mismatch_rejected") {
    const std::unique_ptr<MatchInstance> engine = MakeMatch();
    const ecs::Entity card = HandCard(*engine, "uni:red_1");

    const ops::OpResult result =
        RunOp(*engine, "set_card_property",
              json{{"property", "uni:revealed"}, {"value", "yes"}}, card);

    CHECK(result.status == ops::OpStatus::kResolved);
    CHECK(result.events.empty());
    CHECK(engine->Store().Get<ecs::CardState>(card) == nullptr);
}

TEST_CASE("set_property_range_clamps") {
    const std::unique_ptr<MatchInstance> engine = MakeMatch();
    const ecs::Entity card = HandCard(*engine, "uni:red_1");

    const ops::OpResult result =
        RunOp(*engine, "set_card_property",
              json{{"property", "uni:power"}, {"value", 9}}, card);

    const ecs::CardState* state = engine->Store().Get<ecs::CardState>(card);
    REQUIRE(state != nullptr);
    CHECK(state->properties.at("uni:power") == 5);
    CHECK(result.status == ops::OpStatus::kResolved);
    REQUIRE(result.events.size() == 1);
    const json& event = result.events.front();
    CHECK(event["type"] == "card_changed");
    CHECK(event["payload"]["property"] == "uni:power");
    CHECK(event["payload"]["value"] == 5);
    CHECK(event["payload"]["previous"] == 0);
}

TEST_CASE("set_property_same_value_is_noop") {
    const std::unique_ptr<MatchInstance> engine = MakeMatch();
    const ecs::Entity card = HandCard(*engine, "uni:red_1");

    const ops::OpResult result =
        RunOp(*engine, "set_card_property",
              json{{"property", "uni:power"}, {"value", 0}}, card);

    CHECK(result.status == ops::OpStatus::kResolved);
    CHECK(result.events.empty());
    CHECK(engine->Store().Get<ecs::CardState>(card) == nullptr);
}

}  // namespace
