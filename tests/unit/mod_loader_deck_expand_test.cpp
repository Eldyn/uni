#include <doctest/doctest.h>

#include <match/modload/mod_loader.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

/**
 * @file mod_loader_deck_expand_test.cpp
 * @brief Plan 2a-2 Task 2: one loader entry point expands a data deck.
 */

namespace {

namespace fs = std::filesystem;

using match::modload::DeckDef;
using match::modload::DefaultSettings;
using match::modload::ExpandDeckDef;
using match::modload::LoadResult;
using match::modload::LoadedMod;
using match::modload::ScanModsDirectory;

// INFO: walk up from this test file to the repository root, so the shipped
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

LoadResult LoadShippedMods() {
    return ScanModsDirectory((ProjectRoot() / "mods").string());
}

const LoadedMod* FindMod(const LoadResult& load, const std::string& mod_id) {
    for (const LoadedMod& mod : load.mods) {
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

bool HasKind(const DeckDef& deck, const std::string& kind_id) {
    for (const auto& [dealt_kind, count] : deck.cards) {
        (void)count;
        if (dealt_kind == kind_id) return true;
    }
    return false;
}

}  // namespace

TEST_SUITE("mod loader deck expand") {

TEST_CASE("classic_default_is_108") {
    const LoadResult load = LoadShippedMods();
    const LoadedMod* uni = FindMod(load, "uni");
    REQUIRE(uni != nullptr);

    std::string error;
    const DeckDef deck = ExpandDeckDef(load.mods, "uni:classic", {"uni"},
                                       DefaultSettings(*uni), error);

    CHECK(error.empty());
    CHECK(CountCards(deck) == 108);
    CHECK(deck.deck_id == "uni:classic");
    CHECK(deck.mods == std::vector<std::string>{"uni"});
    CHECK(deck.namespace_id == "uni");
}

TEST_CASE("settings_change_counts") {
    const LoadResult load = LoadShippedMods();
    const LoadedMod* uni = FindMod(load, "uni");
    REQUIRE(uni != nullptr);

    std::map<std::string, int64_t> settings = DefaultSettings(*uni);
    settings["count_wild"] = 0;
    std::string error;
    const DeckDef deck =
        ExpandDeckDef(load.mods, "uni:classic", {"uni"}, settings, error);

    CHECK(error.empty());
    CHECK(CountCards(deck) == 104);
    CHECK_FALSE(HasKind(deck, "uni:wild"));
}

TEST_CASE("unknown_deck") {
    const LoadResult load = LoadShippedMods();
    const LoadedMod* uni = FindMod(load, "uni");
    REQUIRE(uni != nullptr);

    std::string error;
    const DeckDef deck = ExpandDeckDef(load.mods, "uni:nope", {"uni"},
                                       DefaultSettings(*uni), error);

    CHECK(deck.cards.empty());
    CHECK(error.find("unknown deck") != std::string::npos);
}

TEST_CASE("too_big") {
    const LoadResult load = LoadShippedMods();
    const LoadedMod* uni = FindMod(load, "uni");
    REQUIRE(uni != nullptr);

    std::map<std::string, int64_t> settings = DefaultSettings(*uni);
    settings["count_numbered"] = 20;
    settings["count_zeros"] = 20;
    settings["count_skips"] = 20;
    settings["count_reverses"] = 20;
    settings["count_draw_two"] = 20;
    std::string error;
    const DeckDef deck =
        ExpandDeckDef(load.mods, "uni:classic", {"uni"}, settings, error);

    CHECK(deck.cards.empty());
    CHECK(error.find("1024") != std::string::npos);
    CHECK(error.find("mods/uni/decks/classic.json#/deck/cards/")
          != std::string::npos);
}

}  // TEST_SUITE("mod loader deck expand")
