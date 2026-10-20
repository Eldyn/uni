#include <doctest/doctest.h>

#include <match/cardschema/catalog.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/modload/mod_loader.hpp>

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "cardschema_test_support.hpp"

/**
 * @file card_state_active_mods_test.cpp
 * @brief Plan 2a-2 Task 1: generated cards belong to the active mod list.
 *
 * The clean fixture hosts every member of every loaded mod, so adding the
 * `magenta` member generates `uni:magenta_0` with `requires_mods == {"magenta"}`.
 * A match that does not enable `magenta` must not freeze or deal that kind.
 */

namespace {

namespace fs = std::filesystem;

using match::modload::CardDef;
using match::modload::DeckDef;
using match::modload::LoadResult;
using match::modload::LoadedMod;
using namespace match::engine;

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

fs::path FixtureUni() {
    return ProjectRoot() / "tests" / "fixtures" / "card_schema" / "clean" / "uni";
}

// INFO: a scratch `mods/` root with the clean mini mod and the magenta member
// that makes the host generate a foreign card.
struct MagentaMods {
    CardSchemaTempDir directory;

    MagentaMods() {
        fs::copy(FixtureUni(), directory.root / "uni",
                 fs::copy_options::recursive);
        directory.Write(
            "magenta/mod.json",
            R"({"id":"magenta","name":"Magenta","version":"1.0.0","api":"1"})");
        directory.Write(
            "magenta/attributes/color/magenta.json",
            R"({"format_version":"1.0.0","attribute_member":)"
            R"({"attribute":"uni:color","identifier":"magenta:magenta",)"
            R"("tags":["colored","magenta"],"order":4}})");
    }
};

const LoadedMod* FindMod(const LoadResult& result, const std::string& mod_id) {
    for (const LoadedMod& mod : result.mods) {
        if (mod.manifest.id == mod_id) return &mod;
    }
    return nullptr;
}

const CardDef* FindCard(const LoadedMod& mod, const std::string& kind_id) {
    for (const CardDef& card : mod.cards) {
        if (card.kind_id == kind_id) return &card;
    }
    return nullptr;
}

std::vector<match::cardschema::ModSchema> Schemas(const LoadResult& result) {
    std::vector<match::cardschema::ModSchema> schemas;
    schemas.reserve(result.mods.size());
    for (const LoadedMod& mod : result.mods) schemas.push_back(mod.card_schema);
    return schemas;
}

match::cardschema::ExpandedDeck ExpandMini(
    const LoadResult& result, const std::set<std::string>& active_mods) {
    return match::cardschema::ExpandDeck(
        Schemas(result), "uni:mini", active_mods, {{"count_wild", 4}});
}

DeckDef MiniDeck(const match::cardschema::ExpandedDeck& expanded,
                 std::vector<std::string> mods) {
    DeckDef deck;
    deck.id = "mini";
    deck.namespace_id = "uni";
    deck.deck_id = expanded.deck_id;
    deck.name = expanded.name;
    deck.mods = std::move(mods);
    deck.cards = expanded.cards;
    return deck;
}

MatchAssemblyOptions TwoPlayers() {
    MatchAssemblyOptions options;
    options.starting_cards = 1;
    options.seed = 7;
    MatchPlayerSpec first;
    first.username = "alice";
    options.players.push_back(first);
    MatchPlayerSpec second;
    second.username = "bob";
    options.players.push_back(second);
    return options;
}

std::string AssemblyMessage(const AssemblyResult& result) {
    return result.error.has_value() ? result.error->message
                                    : std::string("assembly failed");
}

}  // namespace

TEST_SUITE("card active mods") {

TEST_CASE("card_active_true_when_mod_listed") {
    MagentaMods mods;
    const LoadResult result =
        match::modload::ScanModsDirectory(mods.directory.root.string());
    REQUIRE(result.ok());
    const LoadedMod* uni = FindMod(result, "uni");
    REQUIRE(uni != nullptr);

    const CardDef* magenta = FindCard(*uni, "uni:magenta_0");
    REQUIRE(magenta != nullptr);
    CHECK(magenta->requires_mods == std::vector<std::string>{"magenta"});
    CHECK(match::modload::CardActive(*magenta, {"uni", "magenta"}));
    CHECK_FALSE(match::modload::CardActive(*magenta, {"uni"}));

    const CardDef* own = FindCard(*uni, "uni:red_0");
    REQUIRE(own != nullptr);
    CHECK(own->requires_mods.empty());
    CHECK(match::modload::CardActive(*own, {"uni"}));
    CHECK(match::modload::CardActive(*own, {}));
}

TEST_CASE("assembly_without_magenta_has_no_magenta_kind") {
    MagentaMods mods;
    const LoadResult result =
        match::modload::ScanModsDirectory(mods.directory.root.string());
    REQUIRE(result.ok());
    const DeckDef deck = MiniDeck(ExpandMini(result, {"uni"}), {"uni"});

    const AssemblyResult assembly =
        MatchAssembler::Assemble(result.mods, deck, TwoPlayers());

    REQUIRE_MESSAGE(assembly.ok(), AssemblyMessage(assembly));
    CHECK(assembly.assembly->registries.kind_index.count("uni:magenta_0")
          == 0);
}

TEST_CASE("assembly_with_magenta_has_it") {
    MagentaMods mods;
    const LoadResult result =
        match::modload::ScanModsDirectory(mods.directory.root.string());
    REQUIRE(result.ok());
    const DeckDef deck =
        MiniDeck(ExpandMini(result, {"uni", "magenta"}), {"uni", "magenta"});

    const AssemblyResult assembly =
        MatchAssembler::Assemble(result.mods, deck, TwoPlayers());

    REQUIRE_MESSAGE(assembly.ok(), AssemblyMessage(assembly));
    CHECK(assembly.assembly->registries.kind_index.count("uni:magenta_0")
          == 1);
}

TEST_CASE("deck_with_inactive_kind_is_rejected") {
    MagentaMods mods;
    const LoadResult result =
        match::modload::ScanModsDirectory(mods.directory.root.string());
    REQUIRE(result.ok());
    DeckDef deck;
    deck.id = "mini";
    deck.namespace_id = "uni";
    deck.deck_id = "uni:mini";
    deck.name = "Mini";
    deck.mods = {"uni"};
    deck.cards = {{"uni:magenta_0", 1}};

    const AssemblyResult assembly =
        MatchAssembler::Assemble(result.mods, deck, TwoPlayers());

    REQUIRE_FALSE(assembly.ok());
    REQUIRE(assembly.error.has_value());
    CHECK(assembly.error->check == "assembly.unknown_kind");
}

}  // TEST_SUITE("card active mods")
