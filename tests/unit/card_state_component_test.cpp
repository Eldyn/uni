#include <doctest/doctest.h>

#include <match/ecs/components.hpp>
#include <match/ecs/entity_store.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/modload/mod_loader.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

using match::ecs::CardState;
using match::ecs::EffectiveMember;
using match::ecs::EntityStore;
using match::engine::MatchAssembler;
using match::engine::MatchAssemblyOptions;
using match::engine::MatchPlayerSpec;
using match::modload::DeckDef;
using match::modload::LoadResult;
using match::modload::LoadedMod;

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
        if (parent == path) {
            break;
        }
        path = parent;
    }
    return {};
}

fs::path FixtureClean() {
    return ProjectRoot() / "tests" / "fixtures" / "card_schema" / "clean";
}

struct Content {
    std::vector<LoadedMod> mods;
    DeckDef deck;
};

bool LoadContent(const fs::path& root, Content& out) {
    const LoadResult load = match::modload::ScanModsDirectory(root.string());
    if (!load.ok()) return false;
    out.mods = load.mods;
    for (const LoadedMod& mod : out.mods) {
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == "uni:mini") out.deck = deck;
        }
    }
    return !out.deck.deck_id.empty();
}

MatchAssemblyOptions Players(int count, int starting_cards, uint64_t seed) {
    MatchAssemblyOptions options;
    options.starting_cards = starting_cards;
    options.seed = seed;
    for (int i = 0; i < count; ++i) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(i);
        options.players.push_back(spec);
    }
    return options;
}

}  // namespace

TEST_SUITE("card state component") {

TEST_CASE("store_add_get_card_state") {
    EntityStore store;
    const match::ecs::Entity card = store.Create();

    CardState state;
    state.member_overrides["color"] = "uni:blue";
    state.properties["uni:revealed"] = true;

    CardState* added = store.Add<CardState>(card, state);
    REQUIRE(added != nullptr);
    CHECK(*added == state);
    CHECK(store.Has<CardState>(card));

    CardState* fetched = store.Get<CardState>(card);
    REQUIRE(fetched != nullptr);
    CHECK(fetched->member_overrides.at("color") == "uni:blue");
    CHECK(fetched->properties.at("uni:revealed") == true);
}

TEST_CASE("effective_member_prefers_override") {
    const std::map<std::string, std::string> dealt = {{"color", "uni:red"}};
    CardState state;
    state.member_overrides["color"] = "uni:blue";

    CHECK(EffectiveMember(&state, dealt, "color") == "uni:blue");
}

TEST_CASE("effective_member_override_for_undealt_slot") {
    const std::map<std::string, std::string> dealt = {{"color", "uni:red"}};
    CardState state;
    state.member_overrides["value"] = "uni:9";

    CHECK(EffectiveMember(&state, dealt, "value") == "uni:9");
}

TEST_CASE("effective_member_falls_back_to_dealt") {
    const std::map<std::string, std::string> dealt = {{"color", "uni:red"}};
    CardState empty;

    CHECK(EffectiveMember(nullptr, dealt, "color") == "uni:red");
    CHECK(EffectiveMember(&empty, dealt, "color") == "uni:red");
}

TEST_CASE("effective_member_missing_slot_is_empty") {
    const std::map<std::string, std::string> dealt = {{"color", "uni:red"}};
    CardState empty;
    CardState other_slot;
    other_slot.member_overrides["value"] = "uni:9";

    CHECK(EffectiveMember(nullptr, dealt, "value").empty());
    CHECK(EffectiveMember(&empty, dealt, "value").empty());
    CHECK(EffectiveMember(&other_slot, dealt, "color") == "uni:red");
}

TEST_CASE("card_state_is_not_in_the_component_catalog") {
    CHECK_FALSE(match::ecs::ComponentCatalog::Instance().Contains("card_state"));
}

TEST_CASE("no_component_by_default") {
    Content content;
    REQUIRE(LoadContent(FixtureClean(), content));

    const auto result =
        MatchAssembler::Assemble(content.mods, content.deck, Players(2, 3, 1));
    REQUIRE(result.ok());
    REQUIRE_FALSE(result.assembly->registries.cards.empty());

    for (const match::ecs::Entity card : result.assembly->registries.cards) {
        CHECK_FALSE(result.assembly->store.Has<CardState>(card));
    }
}

}  // namespace
