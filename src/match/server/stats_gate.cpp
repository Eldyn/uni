#include <match/server/stats_gate.hpp>

#include <match/modload/base_mod.hpp>
#include <match/modload/mod_loader.hpp>

#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

/**
 * @file stats_gate.cpp
 * @brief Stats gate comparator implementation.
 */

namespace match::server {

std::vector<std::pair<std::string, int>> UniClassicDeckCards(
    const std::vector<match::modload::LoadedMod>& mods) {
    const auto uni = std::ranges::find_if(
        mods, [](const match::modload::LoadedMod& mod) {
            return modload::IsBaseModId(mod.manifest.id);
        });
    if (uni == mods.end()) return {};

    std::string error;
    const match::modload::DeckDef deck = match::modload::ExpandDeckDef(
        mods, "uni:classic", {"uni"}, match::modload::DefaultSettings(*uni),
        error);
    if (!error.empty()) return {};
    return deck.cards;
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
                         deck_cards,
                     const std::vector<match::modload::LoadedMod>&
                         loaded_mods) {
    if (mods.size() != 1 || !modload::IsBaseModId(mods.front())) {
        return false;
    }
    const std::vector<std::pair<std::string, int>> classic =
        UniClassicDeckCards(loaded_mods);
    if (classic.empty()) return false;
    return DeckKindMultisetsEqual(ExpandKindMultiset(deck_cards),
                                  ExpandKindMultiset(classic));
}

}  // namespace match::server
