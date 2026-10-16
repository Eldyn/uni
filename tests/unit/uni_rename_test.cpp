#include <doctest/doctest.h>

#include <match/engine/match_assembler.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

/**
 * @file uni_rename_test.cpp
 * @brief The base mod ships as `uni`; the old `vanilla` name is a hard break.
 */

namespace fs = std::filesystem;
using namespace match::modload;
using match::engine::MatchAssembler;
using match::engine::MatchAssemblyOptions;
using match::engine::MatchPlayerSpec;

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

LoadResult LoadShippedMods() {
    return ScanModsDirectory((ProjectRoot() / "mods").string());
}

const LoadedMod* FindMod(const LoadResult& load, const std::string& id) {
    for (const LoadedMod& mod : load.mods) {
        if (mod.manifest.id == id) return &mod;
    }
    return nullptr;
}

MatchAssemblyOptions TwoPlayers() {
    MatchAssemblyOptions options;
    options.starting_cards = 7;
    options.seed = 1;
    for (int index = 0; index < 2; ++index) {
        MatchPlayerSpec spec;
        spec.username = "player" + std::to_string(index);
        options.players.push_back(spec);
    }
    return options;
}

bool IsScannedFile(const fs::path& path) {
    static const std::vector<std::string> kExtensions = {
        ".cpp", ".hpp", ".ts", ".js", ".mjs", ".svelte", ".json",
        ".md",  ".txt", ".py", ".sh", ".yml",  ".css",    ".glsl"};
    return std::ranges::find(kExtensions, path.extension().string()) !=
           kExtensions.end();
}

bool IsExcludedFromScan(const fs::path& relative) {
    static const std::vector<std::string> kExcludedNames = {
        "CHANGELOG.md",       "uni_rename_test.cpp", "uniNamespace.test.ts",
        "node_modules",       "build",               ".svelte-kit",
        "docs",               "assets",              "package-lock.json"};
    for (const fs::path& part : relative) {
        if (std::ranges::find(kExcludedNames, part.string()) !=
            kExcludedNames.end()) {
            return true;
        }
    }
    return false;
}

std::string Lowercase(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char letter) {
        return static_cast<char>(std::tolower(letter));
    });
    return text;
}

std::vector<std::string> FilesNamingOldMod() {
    const fs::path root = ProjectRoot();
    const std::vector<std::string> kScannedEntries = {
        "src",      "include",       "mods",    "tests",
        "contract", "scripts",       "frontend/src",
        "frontend/messages",         "frontend/scripts",
        "README.md", "BUILD.md",     "RELEASING.md", "ATTRIBUTION.md"};
    const std::string old_name = "vanilla";
    std::vector<std::string> offenders;
    for (const std::string& entry : kScannedEntries) {
        const fs::path start = root / entry;
        std::error_code error;
        if (!fs::exists(start, error)) continue;
        std::vector<fs::path> files;
        if (fs::is_directory(start, error)) {
            for (const auto& item : fs::recursive_directory_iterator(start)) {
                files.push_back(item.path());
            }
        } else {
            files.push_back(start);
        }
        for (const fs::path& file : files) {
            const fs::path relative = fs::relative(file, root);
            if (!fs::is_regular_file(file) || !IsScannedFile(file)) continue;
            if (IsExcludedFromScan(relative)) continue;
            std::ifstream stream(file);
            std::stringstream content;
            content << stream.rdbuf();
            if (Lowercase(content.str()).find(old_name) != std::string::npos) {
                offenders.push_back(relative.string());
            }
        }
    }
    return offenders;
}

}  // namespace

TEST_CASE("uni rename: no tracked source, mod or test names the old mod") {
    REQUIRE_FALSE(ProjectRoot().empty());
    CHECK(FilesNamingOldMod() == std::vector<std::string>{});
}

TEST_CASE("uni rename: the shipped base mod has the id uni") {
    LoadResult load = LoadShippedMods();
    REQUIRE(load.ok());
    CHECK(FindMod(load, "uni") != nullptr);
    CHECK(FindMod(load, "vanilla") == nullptr);
    CHECK(FindMod(load, "seven_zero") != nullptr);
    CHECK(FindMod(load, "test") != nullptr);
}

TEST_CASE("uni rename: card kinds, statuses and decks read uni:") {
    LoadResult load = LoadShippedMods();
    REQUIRE(load.ok());
    const LoadedMod* base = FindMod(load, "uni");
    REQUIRE(base != nullptr);
    REQUIRE_FALSE(base->cards.empty());
    for (const CardDef& card : base->cards) {
        CHECK(card.kind_id.rfind("uni:", 0) == 0);
    }
    for (const StatusDef& status : base->statuses) {
        CHECK(status.status_id.rfind("uni:", 0) == 0);
    }
    for (const DeckDef& deck : base->decks) {
        CHECK(deck.deck_id.rfind("uni:", 0) == 0);
    }
    const bool has_zero = std::ranges::any_of(
        base->cards, [](const CardDef& card) {
            return card.kind_id == "uni:red_0";
        });
    CHECK(has_zero);
}

TEST_CASE("uni rename: no shipped mod keeps a vanilla kind id") {
    LoadResult load = LoadShippedMods();
    REQUIRE(load.ok());
    for (const LoadedMod& mod : load.mods) {
        for (const CardDef& card : mod.cards) {
            CHECK(card.kind_id.rfind("vanilla:", 0) != 0);
        }
        for (const DeckDef& deck : mod.decks) {
            for (const auto& [kind, count] : deck.cards) {
                CHECK(kind.rfind("vanilla:", 0) != 0);
            }
        }
    }
}

TEST_CASE("uni rename: the draw debt status id reads uni:draw_debt") {
    CHECK(match::ops::kDrawDebtStatusId == "uni:draw_debt");
}

TEST_CASE("uni rename: a saved deck with vanilla kinds fails safely") {
    LoadResult load = LoadShippedMods();
    REQUIRE(load.ok());
    DeckDef saved_deck;
    saved_deck.deck_id = "vanilla:classic";
    saved_deck.namespace_id = "vanilla";
    saved_deck.mods = {"vanilla"};
    saved_deck.cards = {{"vanilla:red_5", 10}};

    auto result =
        MatchAssembler::Assemble(load.mods, saved_deck, TwoPlayers());

    CHECK_FALSE(result.ok());
    REQUIRE(result.error.has_value());
    CHECK(result.error->check == "assembly.unknown_mod");
}
