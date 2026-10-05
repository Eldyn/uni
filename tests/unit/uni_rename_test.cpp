#include <doctest/doctest.h>

#include <match/engine/match_assembler.hpp>
#include <match/modload/mod_loader.hpp>
#include <match/ops/op_helpers.hpp>

#include <algorithm>
#include <filesystem>
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

}  // namespace

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
    CHECK(result.error->check == "assembly.unknown_kind");
}
