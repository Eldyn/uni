#pragma once

#include <string>
#include <utility>
#include <vector>

/**
 * @file stats_gate.hpp
 * @brief Stats gate: a pure comparator deciding whether a
 * finished match's `player_stats` may be updated.
 *
 * `player_stats` counts vanilla-only matches. A match
 * qualifies when its mod set is exactly `["vanilla"]` AND its deck's kind
 * multiset equals the vanilla classic deck's kind multiset. Match rows and
 * placements are recorded for every match regardless (match history stays
 * complete); this gate governs ONLY the per-player aggregate update.
 *
 * The comparator is pure: no DB, no file IO, no mutable globals beyond the
 * constant vanilla deck. `ExpandKindMultiset` turns a `DeckDef::cards`
 * kind -> count list into a sorted vector of kind string IDs (one entry per
 * card copy), and `DeckKindMultisetsEqual` is sorted-multiset equality over
 * those IDs.
 *
 * ADDITIVE: new `match::server` files only. The controller wires the gate into
 * the new-engine match-end stats path; the old-engine `MatchInstance` path is
 * deliberately left untouched.
 */

namespace match::server {

/**
 * @brief The vanilla classic deck as kind -> count pairs (`DeckDef::cards`).
 *
 * Mirrors `mods/vanilla/decks/classic.json`: four colours of 0 x1, 1-9 x2,
 * skip x2, reverse x2, draw2 x2, plus wild x4 and wild_draw4 x4 (108 cards).
 *
 * @return The canonical vanilla classic deck cards, in declaration order.
 */
const std::vector<std::pair<std::string, int>>& VanillaClassicDeckCards();

/**
 * @brief Expand a kind -> count list into a sorted multiset of kind IDs.
 *
 * One entry per card copy; counts <= 0 contribute nothing. The result is
 * sorted so two multisets can be compared with a plain equality test.
 *
 * @param cards Kind -> count pairs, e.g. `DeckDef::cards`.
 * @return Sorted kind IDs, one per copy.
 */
std::vector<std::string> ExpandKindMultiset(
    const std::vector<std::pair<std::string, int>>& cards);

/**
 * @brief Order-insensitive multiset equality on kind string IDs.
 *
 * Sorts copies of both inputs, so callers need not pre-sort.
 *
 * @param lhs One kind multiset.
 * @param rhs The other kind multiset.
 * @return true when both hold the same kind IDs with the same multiplicities.
 */
bool DeckKindMultisetsEqual(const std::vector<std::string>& lhs,
                            const std::vector<std::string>& rhs);

/**
 * @brief The gate: may `player_stats` be updated for this match?
 *
 * @param mods       The match's active mod set (must be exactly `["vanilla"]`).
 * @param deck_cards The match deck's kind -> count list.
 * @return true when mods is exactly `{"vanilla"}` and the deck kind multiset
 *         equals the vanilla classic deck multiset.
 */
bool StatsGateAllows(const std::vector<std::string>& mods,
                     const std::vector<std::pair<std::string, int>>&
                         deck_cards);

}  // namespace match::server
