#include <doctest/doctest.h>

#include <match/modload/mod_loader.hpp>
#include <match/server/stats_gate.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

/**
 * @file stats_gate_test.cpp
 * @brief Stats gate tests: positive + negatives.
 *
 * Covers the comparator only: `StatsGateAllows` (mod set exactness and deck
 * kind multiset equality against the expanded data deck),
 * `DeckKindMultisetsEqual`, and `ExpandKindMultiset`. No DB, no match
 * assembly.
 */

namespace {

namespace fs = std::filesystem;

using match::server::DeckKindMultisetsEqual;
using match::server::ExpandKindMultiset;
using match::server::StatsGateAllows;
using match::server::UniClassicDeckCards;

using DeckCards = std::vector<std::pair<std::string, int>>;

/** @brief Locate the project root from this file (mirrors engine tests). */
fs::path ProjectRoot() {
    fs::path p(__FILE__);
    while (!p.empty()) {
        std::error_code ec;
        if (fs::is_directory(p / "contract" / "schemas", ec)) return p;
        const fs::path parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return {};
}

/** @brief The shipped mods, scanned once; empty when the tree is unavailable. */
const std::vector<match::modload::LoadedMod>& ShippedMods() {
    static const std::vector<match::modload::LoadedMod> kMods = [] {
        const match::modload::LoadResult load =
            match::modload::ScanModsDirectory((ProjectRoot() / "mods").string());
        return load.mods;
    }();
    return kMods;
}

/** @brief The expanded uni classic deck. */
DeckCards ClassicDeck() { return UniClassicDeckCards(ShippedMods()); }

}  // namespace

TEST_SUITE("StatsGate") {

TEST_CASE("allows: uni mod id with the classic deck") {
    CHECK(StatsGateAllows({"uni"}, ClassicDeck(), ShippedMods()));
}

TEST_CASE("allows: uni mod id with a uni-prefixed classic deck") {
    DeckCards deck = ClassicDeck();
    for (auto& [kind, count] : deck) {
        kind = "uni:" + kind.substr(kind.find(':') + 1);
    }
    CHECK(StatsGateAllows({"uni"}, deck, ShippedMods()));
}

TEST_CASE("rejects: uni with another mod id") {
    CHECK_FALSE(StatsGateAllows({"uni", "seven_zero"}, ClassicDeck(), ShippedMods()));
}

TEST_CASE("rejects: changed uni-prefixed deck kind multiset") {
    DeckCards deck = ClassicDeck();
    deck.emplace_back("uni:custom_card", 1);
    CHECK_FALSE(StatsGateAllows({"uni"}, deck, ShippedMods()));
}

TEST_CASE("rejects: extra mod in the set") {
    CHECK_FALSE(StatsGateAllows({"uni", "extra"}, ClassicDeck(), ShippedMods()));
}

TEST_CASE("rejects: non-base mod set") {
    CHECK_FALSE(StatsGateAllows({"other"}, ClassicDeck(), ShippedMods()));
    CHECK_FALSE(StatsGateAllows({}, ClassicDeck(), ShippedMods()));
}

TEST_CASE("rejects: changed deck kind multiset (count)") {
    DeckCards deck = ClassicDeck();
    // One extra copy of a card the classic deck holds exactly once.
    for (auto& [kind, count] : deck) {
        if (kind == "uni:red_0") {
            ++count;
            break;
        }
    }
    CHECK_FALSE(StatsGateAllows({"uni"}, deck, ShippedMods()));
}

TEST_CASE("rejects: changed deck kind multiset (extra kind)") {
    DeckCards deck = ClassicDeck();
    deck.emplace_back("uni:custom_card", 1);
    CHECK_FALSE(StatsGateAllows({"uni"}, deck, ShippedMods()));
}

TEST_CASE("rejects: changed deck kind multiset (missing kind)") {
    DeckCards deck = ClassicDeck();
    deck.erase(deck.begin());
    CHECK_FALSE(StatsGateAllows({"uni"}, deck, ShippedMods()));
}

TEST_CASE("allows: deck pair order does not matter") {
    DeckCards deck = ClassicDeck();
    std::reverse(deck.begin(), deck.end());
    CHECK(StatsGateAllows({"uni"}, deck, ShippedMods()));
}

TEST_CASE("ExpandKindMultiset: sorted, one entry per copy") {
    const std::vector<std::string> kinds =
        ExpandKindMultiset({{"b", 2}, {"a", 1}, {"c", 0}, {"d", -1}});
    REQUIRE(kinds.size() == 3);
    CHECK(kinds[0] == "a");
    CHECK(kinds[1] == "b");
    CHECK(kinds[2] == "b");
}

TEST_CASE("ExpandKindMultiset: classic deck is the 108-card multiset") {
    CHECK(ExpandKindMultiset(ClassicDeck()).size() == 108);
}

TEST_CASE("classic_has_108_cards_and_54_kinds") {
    const DeckCards deck = ClassicDeck();
    int total = 0;
    std::set<std::string> kinds;
    std::map<std::string, int> count_by_kind;
    for (const auto& [kind, count] : deck) {
        total += count;
        kinds.insert(kind);
        count_by_kind[kind] = count;
    }
    CHECK(total == 108);
    CHECK(kinds.size() == 54);
    CHECK(count_by_kind["uni:red_0"] == 1);
    CHECK(count_by_kind["uni:red_5"] == 2);
    CHECK(count_by_kind["uni:red_skip"] == 2);
    CHECK(count_by_kind["uni:wild"] == 4);
    CHECK(count_by_kind["uni:wild_draw4"] == 4);
}

TEST_CASE("rejects: no uni mod means no classic deck") {
    CHECK_FALSE(StatsGateAllows({"uni"}, ClassicDeck(), {}));
}

TEST_CASE("DeckKindMultisetsEqual: order-insensitive, multiplicity-aware") {
    CHECK(DeckKindMultisetsEqual({"a", "b"}, {"b", "a"}));
    CHECK_FALSE(DeckKindMultisetsEqual({"a", "b"}, {"a", "a"}));
    CHECK_FALSE(DeckKindMultisetsEqual({"a"}, {"a", "b"}));
    CHECK(DeckKindMultisetsEqual({}, {}));
}

}  // TEST_SUITE
