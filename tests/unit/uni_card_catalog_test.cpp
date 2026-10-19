#include <doctest/doctest.h>

#include <match/modload/mod_loader.hpp>

#include <common/crypto_hash.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

/**
 * @file uni_card_catalog_test.cpp
 * @brief The migrated `uni` schema v1 data equals the phase-2a oracle.
 *
 * The oracle `tests/fixtures/card_catalog/uni.json` is the snapshot of the 54
 * cards the old `mods/uni/cards/*.json` files shipped; `uni_classic_deck.json`
 * is the snapshot of the 108-card classic deck. The loader must expand the
 * migrated `attributes/`, `kinds/` and `decks/` files into the same cards and
 * the same deck.
 */

namespace fs = std::filesystem;
using nlohmann::json;
using match::modload::CardDef;
using match::modload::DeckDef;
using match::modload::LoadResult;
using match::modload::LoadedMod;

namespace {

/** @brief Walks up from this file to the repository root (`contract/schemas`). */
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

json ReadFixture(const fs::path& path) {
    std::ifstream in(path);
    REQUIRE(in);
    json parsed;
    in >> parsed;
    return parsed;
}

const LoadedMod* FindMod(const LoadResult& load, const std::string& mod_id) {
    for (const LoadedMod& mod : load.mods) {
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

const DeckDef* FindDeck(const LoadedMod& mod, const std::string& deck_id) {
    for (const DeckDef& deck : mod.decks) {
        if (deck.deck_id == deck_id) return &deck;
    }
    return nullptr;
}

}  // namespace

TEST_SUITE("uni card catalog") {

TEST_CASE("generated_cards_equal_oracle") {
    const fs::path root = ProjectRoot();
    REQUIRE_FALSE(root.empty());
    const LoadResult load = match::modload::ScanModsDirectory((root / "mods").string());
    // INFO: the `test` mod is still schema 0 until Task 17, so the whole scan is
    // not `ok` yet; the catalog under test is the migrated `uni` mod alone.
    CHECK_FALSE(load.fatal());
    const LoadedMod* uni = FindMod(load, "uni");
    REQUIRE(uni != nullptr);

    const json oracle = ReadFixture(root / "tests" / "fixtures" / "card_catalog" / "uni.json");
    REQUIRE(oracle.is_array());
    CHECK(uni->cards.size() == oracle.size());

    for (const json& entry : oracle) {
        const std::string kind_id = entry.at("kind_id").get<std::string>();
        INFO("oracle entry " << kind_id);
        const CardDef* card = FindCard(*uni, kind_id);
        REQUIRE(card != nullptr);

        CHECK(card->title == entry.at("title").get<std::string>());

        const json& face = entry.at("face");
        CHECK(match::modload::ToString(card->face.kind) == face.at("kind").get<std::string>());
        CHECK(card->face.color.value_or("") == face.at("color").get<std::string>());
        CHECK(card->face.label.value_or("") == face.at("label").get<std::string>());
        CHECK(card->face.art_version == face.at("art_version").get<int>());

        const std::set<std::string> expected_tags =
            entry.at("tags").get<std::set<std::string>>();
        const std::set<std::string> actual_tags(card->tags.begin(), card->tags.end());
        CHECK(actual_tags == expected_tags);

        // INFO: the oracle hashes the behavior object with sorted keys and no
        // spaces (`sort_keys=True, separators=(",", ":")`); nlohmann `dump()`
        // sorts object keys and is compact, so the digests agree.
        std::string behavior_sha256;
        if (card->raw.contains("behavior")) {
            behavior_sha256 = Sha256Hex(json(card->raw.at("behavior")).dump());
        }
        CHECK(behavior_sha256 == entry.at("behavior_sha256").get<std::string>());
    }
}

TEST_CASE("classic_deck_equals_oracle") {
    const fs::path root = ProjectRoot();
    REQUIRE_FALSE(root.empty());
    const LoadResult load = match::modload::ScanModsDirectory((root / "mods").string());
    CHECK_FALSE(load.fatal());
    const LoadedMod* uni = FindMod(load, "uni");
    REQUIRE(uni != nullptr);
    const DeckDef* classic = FindDeck(*uni, "uni:classic");
    REQUIRE(classic != nullptr);

    const json oracle =
        ReadFixture(root / "tests" / "fixtures" / "card_catalog" / "uni_classic_deck.json");
    REQUIRE(oracle.is_object());
    std::map<std::string, int> expected;
    for (auto it = oracle.begin(); it != oracle.end(); ++it) {
        expected.emplace(it.key(), it.value().get<int>());
    }

    std::map<std::string, int> actual;
    int total = 0;
    for (const std::pair<std::string, int>& dealt : classic->cards) {
        actual[dealt.first] = dealt.second;
        total += dealt.second;
    }
    CHECK(actual == expected);
    CHECK(actual.size() == 54);
    CHECK(total == 108);
}

}  // TEST_SUITE

/**
 * @brief The migrated `test` schema v1 data equals the phase-2a oracle.
 *
 * `test.json` is the snapshot of the two cards the old `mods/test/cards/*.json`
 * files shipped; `test_classic_bombs_deck.json` is the snapshot of the old
 * `cards` object of the `test:classic_bombs` deck. The loader must expand the
 * migrated `kinds/` and `decks/` files into the same cards and the same deck.
 */
TEST_SUITE("test mod catalog") {

TEST_CASE("bomb_and_emoji_wild_equal_oracle") {
    const fs::path root = ProjectRoot();
    REQUIRE_FALSE(root.empty());
    const LoadResult load = match::modload::ScanModsDirectory((root / "mods").string());
    REQUIRE(load.ok());
    const LoadedMod* test_mod = FindMod(load, "test");
    REQUIRE(test_mod != nullptr);

    const json oracle = ReadFixture(root / "tests" / "fixtures" / "card_catalog" / "test.json");
    REQUIRE(oracle.is_array());
    CHECK(test_mod->cards.size() == oracle.size());

    for (const json& entry : oracle) {
        const std::string kind_id = entry.at("kind_id").get<std::string>();
        INFO("oracle entry " << kind_id);
        const CardDef* card = FindCard(*test_mod, kind_id);
        REQUIRE(card != nullptr);

        CHECK(card->title == entry.at("title").get<std::string>());

        const json& face = entry.at("face");
        CHECK(match::modload::ToString(card->face.kind) == face.at("kind").get<std::string>());
        CHECK(card->face.art.value_or("") == face.value("art", ""));
        CHECK(card->face.color.value_or("") == face.at("color").get<std::string>());
        CHECK(card->face.label.value_or("") == face.at("label").get<std::string>());
        CHECK(card->face.art_version == face.at("art_version").get<int>());

        const std::set<std::string> expected_tags =
            entry.at("tags").get<std::set<std::string>>();
        const std::set<std::string> actual_tags(card->tags.begin(), card->tags.end());
        CHECK(actual_tags == expected_tags);

        std::string behavior_sha256;
        if (card->raw.contains("behavior")) {
            behavior_sha256 = Sha256Hex(json(card->raw.at("behavior")).dump());
        }
        CHECK(behavior_sha256 == entry.at("behavior_sha256").get<std::string>());
    }
}

TEST_CASE("classic_bombs_deck_equals_old_cards") {
    const fs::path root = ProjectRoot();
    REQUIRE_FALSE(root.empty());
    const LoadResult load = match::modload::ScanModsDirectory((root / "mods").string());
    REQUIRE(load.ok());
    const LoadedMod* test_mod = FindMod(load, "test");
    REQUIRE(test_mod != nullptr);
    const DeckDef* deck = FindDeck(*test_mod, "test:classic_bombs");
    REQUIRE(deck != nullptr);

    const json oracle = ReadFixture(root / "tests" / "fixtures" / "card_catalog" /
                                    "test_classic_bombs_deck.json");
    REQUIRE(oracle.is_object());
    std::map<std::string, int> expected;
    for (auto it = oracle.begin(); it != oracle.end(); ++it) {
        expected.emplace(it.key(), it.value().get<int>());
    }

    std::map<std::string, int> actual;
    int total = 0;
    for (const std::pair<std::string, int>& dealt : deck->cards) {
        actual[dealt.first] = dealt.second;
        total += dealt.second;
    }
    CHECK(actual == expected);
    CHECK(actual.size() == 56);
    CHECK(total == 120);
}

}  // TEST_SUITE
