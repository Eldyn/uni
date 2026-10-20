#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/engine/match_instance.hpp>
#include <match/modload/artifacts.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>
#include <match/ops/ops.hpp>
#include <match/view/event_sink.hpp>
#include <match/view/view_builder.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/**
 * @file card_state_snapshot_test.cpp
 * @brief The snapshot `card_state` array: non-default state for the cards a
 *        viewer may see, redacted by attribute/property visibility.
 *
 * A member override is public, owner-only or strict-owner-only; a property
 * follows its own visibility and never reaches the wire when it is not
 * synced to clients. A card with no surviving value is dropped, and the key
 * is absent when no card survives, so a default snapshot stays unchanged.
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
    deck.id = "card_state_snapshot";
    deck.namespace_id = "test";
    deck.deck_id = "test:card_state_snapshot";
    deck.name = "Card state snapshot";
    deck.mods = {"uni"};
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

void DiscardCard(MatchInstance& engine, ecs::Entity card) {
    MoveTo(engine, card,
           ecs::ZoneRef{ecs::ZoneKind::kDiscardPile, ecs::Entity{}});
}

void LimboCard(MatchInstance& engine, ecs::Entity card) {
    MoveTo(engine, card, ecs::ZoneRef{ecs::ZoneKind::kLimbo, ecs::Entity{}});
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

void SetMember(MatchInstance& engine, ecs::Entity card, const std::string& slot,
               const std::string& member) {
    const ops::OpResult result =
        RunOp(engine, "set_card_member",
              json{{"slot", slot}, {"member", member}}, card);
    REQUIRE(result.status == ops::OpStatus::kResolved);
    REQUIRE(result.events.size() == 1);
}

void SetProperty(MatchInstance& engine, ecs::Entity card,
                 const std::string& property, const json& value) {
    const ops::OpResult result =
        RunOp(engine, "set_card_property",
              json{{"property", property}, {"value", value}}, card);
    REQUIRE(result.status == ops::OpStatus::kResolved);
    REQUIRE(result.events.size() == 1);
}

json SnapshotFor(ViewBuilder& builder, const Viewer& viewer) {
    EventSink sink;
    const json packet = builder.BuildSnapshot(viewer, sink);
    return packet.at("match_state");
}

const json* EntryFor(const json& state, uint32_t bits) {
    const auto array = state.find("card_state");
    if (array == state.end()) return nullptr;
    for (const json& entry : *array) {
        if (entry.value("card", uint32_t{0}) == bits) return &entry;
    }
    return nullptr;
}

const Viewer kAlice = Viewer::Player("Alice");
const Viewer kBob = Viewer::Player("Bob");
const Viewer kOwnerSpectator = Viewer::Spectator();
const Viewer kPrivateSpectator = Viewer::Spectator({"Alice"});
const Viewer kSpectator = Viewer::PublicSpectator();
const Viewer kDirector = Viewer::PlayerDirector("Carol");

const Viewer* const kAllViewers[] = {&kAlice,        &kBob,       &kOwnerSpectator,
                                     &kPrivateSpectator, &kSpectator, &kDirector};

/** @brief Set one loaded attribute's visibility before assembly. */
void SetAttributeVisibility(std::vector<LoadedMod>& mods,
                            const std::string& identifier,
                            const std::string& visibility) {
    for (LoadedMod& mod : mods) {
        for (match::cardschema::AttributeDef& attribute :
             mod.card_schema.attributes) {
            if (attribute.identifier != identifier) continue;
            attribute.visibility = visibility;
            return;
        }
    }
    FAIL("attribute not found: " << identifier);
}

/** @brief Set one loaded property's visibility before assembly. */
void SetPropertyVisibility(std::vector<LoadedMod>& mods,
                           const std::string& identifier,
                           const std::string& visibility) {
    for (LoadedMod& mod : mods) {
        for (match::cardschema::PropertyDef& property :
             mod.card_schema.properties) {
            if (property.identifier != identifier) continue;
            property.visibility = visibility;
            return;
        }
    }
    FAIL("property not found: " << identifier);
}

}  // namespace

TEST_SUITE("card state snapshot") {

TEST_CASE("snapshot_without_state_has_no_key") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);

    for (const Viewer* viewer : kAllViewers) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        CHECK_FALSE(SnapshotFor(builder, *viewer).contains("card_state"));
    }
}

TEST_CASE("snapshot_lists_public_override_for_all") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");
    const uint32_t bits = engine->Registries().CardId(card)->bits;

    SetMember(*engine, card, "color", "uni:blue");

    const json alice = SnapshotFor(builder, kAlice);
    const json* alice_entry = EntryFor(alice, bits);
    REQUIRE(alice_entry != nullptr);
    CHECK((*alice_entry)["members"]["color"] == "uni:blue");

    CHECK_FALSE(SnapshotFor(builder, kBob).contains("card_state"));

    DiscardCard(*engine, card);
    for (const Viewer* viewer : {&kAlice, &kBob, &kSpectator}) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        const json state = SnapshotFor(builder, *viewer);
        const json* entry = EntryFor(state, bits);
        REQUIRE(entry != nullptr);
        CHECK((*entry)["members"]["color"] == "uni:blue");
    }
}

TEST_CASE("snapshot_redacts_owner_property") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = FindCard(*engine, "uni:red_spade_alpha");
    DiscardCard(*engine, card);
    SetProperty(*engine, card, "uni:revealed", true);

    for (const Viewer* viewer : kAllViewers) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        CHECK_FALSE(SnapshotFor(builder, *viewer).contains("card_state"));
    }
}

TEST_CASE("snapshot_owner_property_reaches_holder_and_privacy_off_spectator") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");
    const uint32_t bits = engine->Registries().CardId(card)->bits;
    SetProperty(*engine, card, "uni:revealed", true);

    for (const Viewer* viewer : {&kAlice, &kOwnerSpectator}) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        const json state = SnapshotFor(builder, *viewer);
        const json* entry = EntryFor(state, bits);
        REQUIRE(entry != nullptr);
        REQUIRE((*entry).contains("properties"));
        CHECK((*entry)["properties"]["uni:revealed"] == true);
    }

    for (const Viewer* viewer :
         {&kPrivateSpectator, &kBob, &kSpectator, &kDirector}) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        CHECK_FALSE(SnapshotFor(builder, *viewer).contains("card_state"));
    }
}

TEST_CASE("snapshot_owner_member_reaches_holder_and_privacy_off_spectator") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");
    const uint32_t bits = engine->Registries().CardId(card)->bits;
    SetMember(*engine, card, "suit", "uni:heart");

    for (const Viewer* viewer : {&kAlice, &kOwnerSpectator}) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        const json state = SnapshotFor(builder, *viewer);
        const json* entry = EntryFor(state, bits);
        REQUIRE(entry != nullptr);
        CHECK((*entry)["members"]["suit"] == "uni:heart");
    }

    for (const Viewer* viewer :
         {&kPrivateSpectator, &kBob, &kSpectator, &kDirector}) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        CHECK_FALSE(SnapshotFor(builder, *viewer).contains("card_state"));
    }
}

TEST_CASE("snapshot_strict_owner_property_reaches_holder_only") {
    std::vector<LoadedMod> mods = LoadMods();
    SetPropertyVisibility(mods, "uni:revealed", "strict_owner");
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");
    const uint32_t bits = engine->Registries().CardId(card)->bits;
    SetProperty(*engine, card, "uni:revealed", true);

    const json alice_state = SnapshotFor(builder, kAlice);
    const json* alice_entry = EntryFor(alice_state, bits);
    REQUIRE(alice_entry != nullptr);
    REQUIRE(alice_entry->contains("properties"));
    CHECK((*alice_entry)["properties"]["uni:revealed"] == true);

    for (const Viewer* viewer : {&kOwnerSpectator, &kPrivateSpectator, &kBob,
                                 &kSpectator, &kDirector}) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        CHECK_FALSE(SnapshotFor(builder, *viewer).contains("card_state"));
    }
}

TEST_CASE("snapshot_strict_owner_member_reaches_holder_only") {
    std::vector<LoadedMod> mods = LoadMods();
    SetAttributeVisibility(mods, "uni:suit", "strict_owner");
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");
    const uint32_t bits = engine->Registries().CardId(card)->bits;
    SetMember(*engine, card, "suit", "uni:heart");

    const json alice_state = SnapshotFor(builder, kAlice);
    const json* alice_entry = EntryFor(alice_state, bits);
    REQUIRE(alice_entry != nullptr);
    REQUIRE(alice_entry->contains("members"));
    CHECK((*alice_entry)["members"]["suit"] == "uni:heart");

    for (const Viewer* viewer : {&kOwnerSpectator, &kPrivateSpectator, &kBob,
                                 &kSpectator, &kDirector}) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        CHECK_FALSE(SnapshotFor(builder, *viewer).contains("card_state"));
    }
}

TEST_CASE("snapshot_omits_limbo_card_state") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");
    SetMember(*engine, card, "color", "uni:blue");
    LimboCard(*engine, card);

    for (const Viewer* viewer : kAllViewers) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        CHECK_FALSE(SnapshotFor(builder, *viewer).contains("card_state"));
    }
}

TEST_CASE("snapshot_hides_a_hidden_member") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");
    SetMember(*engine, card, "secret", "uni:beta");

    for (const Viewer* viewer : kAllViewers) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        CHECK_FALSE(SnapshotFor(builder, *viewer).contains("card_state"));
    }
}

TEST_CASE("snapshot_hides_a_hidden_property") {
    std::vector<LoadedMod> mods = LoadMods();
    SetPropertyVisibility(mods, "uni:revealed", "hidden");
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");
    SetProperty(*engine, card, "uni:revealed", true);

    for (const Viewer* viewer : kAllViewers) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        CHECK_FALSE(SnapshotFor(builder, *viewer).contains("card_state"));
    }
}

TEST_CASE("snapshot_director_sees_public_state_of_another_hand") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");
    const uint32_t bits = engine->Registries().CardId(card)->bits;
    SetMember(*engine, card, "color", "uni:blue");

    const Viewer director = Viewer::PlayerDirector("Bob");
    const json state = SnapshotFor(builder, director);
    const json* entry = EntryFor(state, bits);
    REQUIRE(entry != nullptr);
    CHECK((*entry)["members"]["color"] == "uni:blue");
}

TEST_CASE("snapshot_filters_a_reverted_property") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    SetProperty(*engine, card, "uni:power", 5);
    SetProperty(*engine, card, "uni:power", 0);

    const ecs::CardState* state = engine->Store().Get<ecs::CardState>(card);
    REQUIRE(state != nullptr);
    const auto property = state->properties.find("uni:power");
    REQUIRE(property != state->properties.end());
    CHECK(property->second == 0);
    CHECK_FALSE(SnapshotFor(builder, kAlice).contains("card_state"));
}

TEST_CASE("snapshot_filters_a_reverted_override") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = HandCard(*engine, "uni:red_spade_alpha");

    SetMember(*engine, card, "color", "uni:blue");
    SetMember(*engine, card, "color", "uni:red");

    const ecs::CardState* state = engine->Store().Get<ecs::CardState>(card);
    REQUIRE(state != nullptr);
    const auto override = state->member_overrides.find("color");
    REQUIRE(override != state->member_overrides.end());
    CHECK(override->second == "uni:red");
    CHECK_FALSE(SnapshotFor(builder, kAlice).contains("card_state"));
}

TEST_CASE("snapshot_skips_a_non_synced_property") {
    const std::vector<LoadedMod> mods = LoadMods();
    const std::unique_ptr<MatchInstance> engine = MakeMatch(mods);
    ViewBuilder builder(*engine, mods);
    const ecs::Entity card = FindCard(*engine, "uni:red_spade_alpha");
    DiscardCard(*engine, card);
    SetProperty(*engine, card, "uni:locked", true);

    for (const Viewer* viewer : kAllViewers) {
        CAPTURE(viewer->username);
        CAPTURE(viewer->is_spectator);
        CHECK_FALSE(SnapshotFor(builder, *viewer).contains("card_state"));
    }
}

}  // namespace
