#include <doctest/doctest.h>

#include <common/crypto_hash.hpp>
#include <match/ecs/components.hpp>
#include <match/engine/match_assembler.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/view/defs_builder.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "cardschema_test_support.hpp"

/**
 * @file fifth_color_test.cpp
 * @brief Plan 2a-2 Task 11: a fifth color joins the deck with one member file.
 *
 * The exit test of the card-schema work. A `magenta` mod contributes one
 * `uni:color` member and nothing else; the shipped `mods/uni` folder is copied
 * unchanged next to it. Every deck-sized result must grow by the 25 cards of
 * that color, and the engine must deal them.
 */

namespace {

namespace fs = std::filesystem;

using match::ecs::CardIdentity;
using match::ecs::Entity;
using match::ecs::Hand;
using match::ecs::PileContents;
using match::ecs::PileKind;
using match::engine::AssemblyResult;
using match::engine::MatchAssembly;
using match::engine::MatchAssemblyOptions;
using match::engine::MatchPlayerSpec;
using match::modload::DeckDef;
using match::modload::DefaultSettings;
using match::modload::ExpandDeckDef;
using match::modload::LoadResult;
using match::modload::LoadedMod;
using match::modload::ScanModsDirectory;

// INFO: walk up from this test file to the repository root, so the shipped
// mods and the fixture resolve no matter where the test binary is started.
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

fs::path ShippedUni() { return ProjectRoot() / "mods" / "uni"; }

fs::path FixtureMagenta() {
    return ProjectRoot() / "tests" / "fixtures" / "card_schema" / "fifth_color"
           / "magenta";
}

fs::path UniManifest() {
    return ProjectRoot() / "tests" / "fixtures" / "card_schema" / "fifth_color"
           / "uni_manifest.sha256";
}

// INFO: a scratch `mods/` root holding an untouched copy of the shipped uni
// mod and the magenta fixture next to it.
struct MagentaMods {
    CardSchemaTempDir directory;

    MagentaMods() {
        fs::copy(ShippedUni(), directory.root / "uni",
                 fs::copy_options::recursive);
        fs::copy(FixtureMagenta(), directory.root / "magenta",
                 fs::copy_options::recursive);
    }
};

const LoadedMod* FindMod(const LoadResult& result, const std::string& mod_id) {
    for (const LoadedMod& mod : result.mods) {
        if (mod.manifest.id == mod_id) return &mod;
    }
    return nullptr;
}

int CountCards(const DeckDef& deck) {
    int total = 0;
    for (const auto& [kind_id, count] : deck.cards) {
        (void)kind_id;
        total += count;
    }
    return total;
}

int CountOf(const DeckDef& deck, const std::string& kind_id) {
    for (const auto& [dealt_kind, count] : deck.cards) {
        if (dealt_kind == kind_id) return count;
    }
    return 0;
}

std::size_t CardsInZones(const MatchAssembly& assembly) {
    std::size_t total = 0;
    for (Entity player : assembly.registries.players) {
        const Hand* hand = assembly.store.Get<Hand>(player);
        if (hand != nullptr) total += hand->cards.size();
    }
    for (Entity pile : assembly.store.EntitiesWith<PileContents>()) {
        const PileContents* contents = assembly.store.Get<PileContents>(pile);
        if (contents != nullptr) total += contents->cards.size();
    }
    return total;
}

bool HasMagentaFive(const MatchAssembly& assembly) {
    const auto is_magenta_five = [&](Entity card) {
        const CardIdentity* identity = assembly.store.Get<CardIdentity>(card);
        return identity != nullptr && identity->kind_id == "uni:magenta_5";
    };
    for (Entity player : assembly.registries.players) {
        const Hand* hand = assembly.store.Get<Hand>(player);
        if (hand == nullptr) continue;
        if (std::any_of(hand->cards.begin(), hand->cards.end(),
                        is_magenta_five)) {
            return true;
        }
    }
    for (Entity pile : assembly.store.EntitiesWith<PileContents>()) {
        const PileContents* contents = assembly.store.Get<PileContents>(pile);
        if (contents == nullptr || contents->kind != PileKind::kDraw) continue;
        if (std::any_of(contents->cards.begin(), contents->cards.end(),
                        is_magenta_five)) {
            return true;
        }
    }
    return false;
}

MatchAssemblyOptions TwoPlayers() {
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 11;
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

std::vector<std::string> RelativeFiles(const fs::path& root) {
    std::vector<std::string> files;
    for (const fs::directory_entry& entry :
         fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        files.push_back(fs::relative(entry.path(), root).generic_string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

const nlohmann::json* FindKindDef(const nlohmann::json& defs,
                                  const std::string& string_id) {
    for (const nlohmann::json& kind : defs["kinds"]) {
        if (kind["string_id"] == string_id) return &kind;
    }
    return nullptr;
}

}  // namespace

TEST_SUITE("fifth color") {

TEST_CASE("deck_grows_without_edits") {
    MagentaMods mods;
    const LoadResult result =
        ScanModsDirectory(mods.directory.root.string());
    REQUIRE(result.ok());
    const LoadedMod* uni = FindMod(result, "uni");
    REQUIRE(uni != nullptr);

    std::string error;
    const DeckDef deck = ExpandDeckDef(result.mods, "uni:classic",
                                       {"uni", "magenta"},
                                       DefaultSettings(*uni), error);

    CHECK(error.empty());
    CHECK(CountCards(deck) == 133);
    CHECK(CountOf(deck, "uni:magenta_0") == 1);
    CHECK(CountOf(deck, "uni:magenta_5") == 2);
    CHECK(CountOf(deck, "uni:magenta_skip") == 2);
    CHECK(CountOf(deck, "uni:magenta_draw2") == 2);
    CHECK(CountOf(deck, "uni:wild") == 4);
    CHECK(CountOf(deck, "uni:wild_draw4") == 4);
}

TEST_CASE("deck_unchanged_when_mod_inactive") {
    MagentaMods mods;
    const LoadResult result =
        ScanModsDirectory(mods.directory.root.string());
    REQUIRE(result.ok());
    const LoadedMod* uni = FindMod(result, "uni");
    REQUIRE(uni != nullptr);

    std::string error;
    const DeckDef deck = ExpandDeckDef(result.mods, "uni:classic", {"uni"},
                                       DefaultSettings(*uni), error);

    CHECK(error.empty());
    CHECK(CountCards(deck) == 108);
    CHECK(CountOf(deck, "uni:magenta_0") == 0);
}

TEST_CASE("uni_files_untouched") {
    const fs::path manifest_path = UniManifest();
    std::ifstream manifest(manifest_path);
    REQUIRE_MESSAGE(manifest.good(),
                    "cannot read manifest " << manifest_path.string());

    std::map<std::string, std::string> expected;
    std::string line;
    while (std::getline(manifest, line)) {
        if (line.empty()) continue;
        const std::size_t separator = line.find("  ");
        REQUIRE_MESSAGE(separator != std::string::npos,
                        "malformed manifest line: " << line);
        expected.emplace(line.substr(separator + 2),
                         line.substr(0, separator));
    }
    REQUIRE_FALSE(expected.empty());

    const fs::path shipped = ShippedUni();
    std::map<std::string, std::string> actual;
    for (const std::string& relative : RelativeFiles(shipped)) {
        actual.emplace("mods/uni/" + relative,
                       Sha256Hex(ReadBytes(shipped / relative)));
    }

    std::vector<std::string> expected_paths;
    for (const auto& [path, hash] : expected) {
        (void)hash;
        expected_paths.push_back(path);
    }
    std::vector<std::string> actual_paths;
    for (const auto& [path, hash] : actual) {
        (void)hash;
        actual_paths.push_back(path);
    }
    CHECK(actual_paths == expected_paths);

    for (const auto& [path, hash] : expected) {
        CAPTURE(path);
        const auto found = actual.find(path);
        if (found == actual.end()) continue;
        CHECK(found->second == hash);
    }
}

TEST_CASE("match_with_magenta_deals_magenta_cards") {
    MagentaMods mods;
    const LoadResult result =
        ScanModsDirectory(mods.directory.root.string());
    REQUIRE(result.ok());
    const LoadedMod* uni = FindMod(result, "uni");
    REQUIRE(uni != nullptr);

    std::string error;
    const DeckDef deck = ExpandDeckDef(result.mods, "uni:classic",
                                       {"uni", "magenta"},
                                       DefaultSettings(*uni), error);
    REQUIRE(error.empty());
    REQUIRE(CountCards(deck) == 133);

    const AssemblyResult assembly =
        match::engine::MatchAssembler::Assemble(result.mods, deck,
                                                TwoPlayers());
    REQUIRE_MESSAGE(assembly.ok(), AssemblyMessage(assembly));
    REQUIRE(assembly.assembly != nullptr);

    CHECK(CardsInZones(*assembly.assembly) == 133);
    CHECK(assembly.assembly->registries.kind_index.count("uni:magenta_5") == 1);
    CHECK(HasMagentaFive(*assembly.assembly));
}

TEST_CASE("defs_list_the_new_kinds") {
    MagentaMods mods;
    const LoadResult result =
        ScanModsDirectory(mods.directory.root.string());
    REQUIRE(result.ok());
    const LoadedMod* uni = FindMod(result, "uni");
    REQUIRE(uni != nullptr);

    std::string error;
    const DeckDef deck = ExpandDeckDef(result.mods, "uni:classic",
                                       {"uni", "magenta"},
                                       DefaultSettings(*uni), error);
    REQUIRE(error.empty());

    const AssemblyResult assembly =
        match::engine::MatchAssembler::Assemble(result.mods, deck,
                                                TwoPlayers());
    REQUIRE_MESSAGE(assembly.ok(), AssemblyMessage(assembly));
    REQUIRE(assembly.assembly != nullptr);

    const nlohmann::json defs = match::view::DefsBuilder::Build(
        assembly.assembly->registries, result.mods);
    const nlohmann::json* kind = FindKindDef(defs, "uni:magenta_5");
    REQUIRE(kind != nullptr);
    CHECK((*kind)["face"]["color"] == "magenta");
    CHECK((*kind)["face"]["label"] == "5");
}

}  // TEST_SUITE("fifth color")
