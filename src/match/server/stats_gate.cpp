#include <match/server/stats_gate.hpp>

#include <algorithm>

/**
 * @file stats_gate.cpp
 * @brief Stats gate comparator implementation.
 */

namespace match::server {
namespace {

/** @brief Build the vanilla classic deck kind -> count list. */
std::vector<std::pair<std::string, int>> BuildVanillaClassicCards() {
    std::vector<std::pair<std::string, int>> cards;
    for (const char* color : {"red", "blue", "green", "yellow"}) {
        const std::string prefix = std::string("vanilla:") + color + "_";
        cards.emplace_back(prefix + "0", 1);
        for (int number = 1; number <= 9; ++number) {
            cards.emplace_back(prefix + std::to_string(number), 2);
        }
        cards.emplace_back(prefix + "skip", 2);
        cards.emplace_back(prefix + "reverse", 2);
        cards.emplace_back(prefix + "draw2", 2);
    }
    cards.emplace_back("vanilla:wild", 4);
    cards.emplace_back("vanilla:wild_draw4", 4);
    return cards;
}

}  // namespace

const std::vector<std::pair<std::string, int>>& VanillaClassicDeckCards() {
    static const std::vector<std::pair<std::string, int>> kCards =
        BuildVanillaClassicCards();
    return kCards;
}

std::vector<std::string> ExpandKindMultiset(
    const std::vector<std::pair<std::string, int>>& cards) {
    std::vector<std::string> kinds;
    for (const auto& [kind, count] : cards) {
        for (int i = 0; i < count; ++i) {
            kinds.push_back(kind);
        }
    }
    std::sort(kinds.begin(), kinds.end());
    return kinds;
}

bool DeckKindMultisetsEqual(const std::vector<std::string>& lhs,
                            const std::vector<std::string>& rhs) {
    if (lhs.size() != rhs.size()) return false;
    std::vector<std::string> left = lhs;
    std::vector<std::string> right = rhs;
    std::sort(left.begin(), left.end());
    std::sort(right.begin(), right.end());
    return left == right;
}

bool StatsGateAllows(const std::vector<std::string>& mods,
                     const std::vector<std::pair<std::string, int>>&
                         deck_cards) {
    if (mods.size() != 1 || mods.front() != "vanilla") {
        return false;
    }
    return DeckKindMultisetsEqual(
        ExpandKindMultiset(deck_cards),
        ExpandKindMultiset(VanillaClassicDeckCards()));
}

}  // namespace match::server
