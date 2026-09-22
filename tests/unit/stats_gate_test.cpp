#include <doctest/doctest.h>

#include <match/server/stats_gate.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

/**
 * @file stats_gate_test.cpp
 * @brief Stats gate tests: positive + negatives.
 *
 * Covers the pure comparator only: `StatsGateAllows` (mod set exactness and
 * deck kind multiset equality), `DeckKindMultisetsEqual`, and
 * `ExpandKindMultiset`. No DB, no match assembly.
 */

namespace {

using match::server::DeckKindMultisetsEqual;
using match::server::ExpandKindMultiset;
using match::server::StatsGateAllows;
using match::server::VanillaClassicDeckCards;

using DeckCards = std::vector<std::pair<std::string, int>>;

/** @brief The canonical vanilla classic deck, copied so tests may mutate. */
DeckCards ClassicDeck() { return VanillaClassicDeckCards(); }

}  // namespace

TEST_SUITE("StatsGate") {

TEST_CASE("allows: vanilla mods with the classic deck") {
    CHECK(StatsGateAllows({"vanilla"}, ClassicDeck()));
}

TEST_CASE("rejects: extra mod in the set") {
    CHECK_FALSE(StatsGateAllows({"vanilla", "extra"}, ClassicDeck()));
}

TEST_CASE("rejects: non-vanilla mod set") {
    CHECK_FALSE(StatsGateAllows({"other"}, ClassicDeck()));
    CHECK_FALSE(StatsGateAllows({}, ClassicDeck()));
}

TEST_CASE("rejects: changed deck kind multiset (count)") {
    DeckCards deck = ClassicDeck();
    // One extra copy of a card the classic deck holds exactly once.
    for (auto& [kind, count] : deck) {
        if (kind == "vanilla:red_0") {
            ++count;
            break;
        }
    }
    CHECK_FALSE(StatsGateAllows({"vanilla"}, deck));
}

TEST_CASE("rejects: changed deck kind multiset (extra kind)") {
    DeckCards deck = ClassicDeck();
    deck.emplace_back("vanilla:custom_card", 1);
    CHECK_FALSE(StatsGateAllows({"vanilla"}, deck));
}

TEST_CASE("rejects: changed deck kind multiset (missing kind)") {
    DeckCards deck = ClassicDeck();
    deck.erase(deck.begin());
    CHECK_FALSE(StatsGateAllows({"vanilla"}, deck));
}

TEST_CASE("allows: deck pair order does not matter") {
    DeckCards deck = ClassicDeck();
    std::reverse(deck.begin(), deck.end());
    CHECK(StatsGateAllows({"vanilla"}, deck));
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

TEST_CASE("DeckKindMultisetsEqual: order-insensitive, multiplicity-aware") {
    CHECK(DeckKindMultisetsEqual({"a", "b"}, {"b", "a"}));
    CHECK_FALSE(DeckKindMultisetsEqual({"a", "b"}, {"a", "a"}));
    CHECK_FALSE(DeckKindMultisetsEqual({"a"}, {"a", "b"}));
    CHECK(DeckKindMultisetsEqual({}, {}));
}

}  // TEST_SUITE
