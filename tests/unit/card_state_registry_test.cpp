#include <doctest/doctest.h>

#include <match/engine/match_assembler.hpp>
#include <match/modload/mod_loader.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "cardschema_test_support.hpp"

namespace {

namespace fs = std::filesystem;

using match::engine::MatchAssembler;
using match::engine::MatchAssemblyOptions;
using match::engine::MatchPlayerSpec;
using match::modload::CardActive;
using match::modload::CardDef;
using match::modload::DeckDef;
using match::modload::LoadError;
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

fs::path FixtureCleanUni() {
    return FixtureClean() / "uni";
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

std::vector<std::string> ActiveKinds(
    const std::vector<LoadedMod>& mods,
    const std::vector<std::string>& active_mods) {
    std::vector<std::string> kinds;
    for (const LoadedMod& mod : mods) {
        for (const CardDef& card : mod.cards) {
            if (CardActive(card, active_mods)) kinds.push_back(card.kind_id);
        }
    }
    return kinds;
}

}  // namespace

TEST_SUITE("card state registry") {

TEST_CASE("slot_members_dealt") {
    Content content;
    REQUIRE(LoadContent(FixtureClean(), content));

    const auto result =
        MatchAssembler::Assemble(content.mods, content.deck, Players(2, 0, 1));
    REQUIRE(result.ok());

    const auto found =
        result.assembly->registries.kind_slot_members.find("uni:red_1");
    REQUIRE(found != result.assembly->registries.kind_slot_members.end());
    const std::map<std::string, std::string> expected = {
        {"color", "uni:red"}, {"value", "uni:1"}};
    CHECK(found->second == expected);
}

TEST_CASE("attribute_visibility_loaded") {
    Content content;
    REQUIRE(LoadContent(FixtureClean(), content));

    const auto result =
        MatchAssembler::Assemble(content.mods, content.deck, Players(2, 0, 1));
    REQUIRE(result.ok());

    const auto found =
        result.assembly->registries.attribute_visibility.find("uni:color");
    REQUIRE(found != result.assembly->registries.attribute_visibility.end());
    CHECK(found->second == "public");
}

TEST_CASE("member_attribute_loaded") {
    Content content;
    REQUIRE(LoadContent(FixtureClean(), content));

    const auto result =
        MatchAssembler::Assemble(content.mods, content.deck, Players(2, 0, 1));
    REQUIRE(result.ok());

    const auto found =
        result.assembly->registries.member_attribute.find("uni:red");
    REQUIRE(found != result.assembly->registries.member_attribute.end());
    CHECK(found->second == "uni:color");
}

TEST_CASE("card_property_loaded") {
    Content content;
    REQUIRE(LoadContent(FixtureClean(), content));

    const auto result =
        MatchAssembler::Assemble(content.mods, content.deck, Players(2, 0, 1));
    REQUIRE(result.ok());

    const auto found =
        result.assembly->registries.card_properties.find("uni:revealed");
    REQUIRE(found != result.assembly->registries.card_properties.end());
    CHECK(found->second.type == "boolean");
    CHECK(found->second.visibility == "owner");
}

TEST_CASE("inactive_mod_members_absent") {
    CardSchemaTempDir temp;
    fs::copy(FixtureCleanUni(), temp.root / "uni",
             fs::copy_options::recursive);
    temp.Write("magenta/mod.json",
               R"({"id":"magenta","name":"M","version":"1.0.0","api":"1"})");
    temp.Write(
        "magenta/attributes/color/magenta.json",
        R"({"format_version":"1.0.0","attribute_member":{"attribute":"uni:color",)"
        R"("identifier":"magenta:magenta","tags":["colored","magenta"],)"
        R"("order":4}})");
    temp.Write("magenta/attributes/suit.json",
               R"({"format_version":"1.0.0","attribute":{"identifier":)"
               R"("magenta:suit","visibility":"hidden"}})");
    temp.Write(
        "magenta/attributes/suit/suit_a.json",
        R"({"format_version":"1.0.0","attribute_member":{"attribute":)"
        R"("magenta:suit","identifier":"magenta:suit_a","order":0}})");
    temp.Write(
        "magenta/properties/secret.json",
        R"({"format_version":"1.0.0","property":{"identifier":)"
        R"("magenta:secret","scope":"card","type":"boolean",)"
        R"("default":false,"visibility":"hidden"}})");

    Content content;
    const LoadResult load = match::modload::ScanModsDirectory(temp.root.string());
    for (const LoadError& error : load.errors) {
        INFO(error.path << ": " << error.message);
    }
    REQUIRE(load.ok());
    content.mods = load.mods;
    for (const LoadedMod& mod : content.mods) {
        for (const DeckDef& deck : mod.decks) {
            if (deck.deck_id == "uni:mini") content.deck = deck;
        }
    }
    REQUIRE_FALSE(content.deck.deck_id.empty());
    REQUIRE(content.mods.size() == 2);
    bool magenta_card = false;
    for (const LoadedMod& mod : content.mods) {
        for (const CardDef& card : mod.cards) {
            for (const std::string& required : card.requires_mods) {
                if (required == "magenta") magenta_card = true;
            }
        }
    }
    CHECK(magenta_card);

    const std::vector<std::string> active_mods = {"uni"};
    const std::vector<std::string> active_kinds =
        ActiveKinds(content.mods, active_mods);

    DeckDef deck = content.deck;
    deck.mods = active_mods;
    std::vector<std::pair<std::string, int>> kept;
    for (const auto& [kind, count] : deck.cards) {
        for (const std::string& active : active_kinds) {
            if (kind == active) {
                kept.emplace_back(kind, count);
                break;
            }
        }
    }
    deck.cards = std::move(kept);

    const auto result =
        MatchAssembler::Assemble(content.mods, deck, Players(2, 0, 1));
    REQUIRE(result.ok());

    const auto& registries = result.assembly->registries;
    CHECK(registries.member_attribute.count("uni:red") == 1);
    CHECK(registries.member_attribute.count("magenta:magenta") == 0);
    CHECK(registries.attribute_visibility.count("magenta:suit") == 0);
    CHECK(registries.card_properties.count("magenta:secret") == 0);
    for (const auto& [member, attribute] : registries.member_attribute) {
        (void)attribute;
        CHECK_FALSE(member.starts_with("magenta:"));
    }
    for (const auto& [kind, slots] : registries.kind_slot_members) {
        (void)kind;
        for (const auto& [slot, member] : slots) {
            (void)slot;
            CHECK_FALSE(member.starts_with("magenta:"));
        }
    }
}

}  // namespace
