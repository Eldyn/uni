#pragma once

#include <string>
#include <utility>
#include <vector>

namespace match::modload {
struct LoadedMod;
}

/**
 * @file stats_gate.hpp
 * @brief Stats gate: a pure comparator deciding whether a
 * finished match's `player_stats` may be updated.
 *
 * `player_stats` counts uni-only matches. A match
 * qualifies when its mod set is exactly `["uni"]` AND its deck's kind
 * multiset equals the uni classic deck's kind multiset. Match rows and
 * placements are recorded for every match regardless (match history stays
 * complete); this gate governs ONLY the per-player aggregate update.
 *
 * The comparator is pure: no DB, no file IO, no mutable globals. The classic
 * deck is expanded from the already-loaded `uni:classic` data deck.
 * `ExpandKindMultiset` turns a `DeckDef::cards` kind -> count list into a
 * sorted vector of kind string IDs (one entry per card copy), and
 * `DeckKindMultisetsEqual` is sorted-multiset equality over those IDs.
 *
 * ADDITIVE: new `match::server` files only. The controller wires the gate into
 * the new-engine match-end stats path; the old-engine `MatchInstance` path is
 * deliberately left untouched.
 */

namespace match::server {

/**
 * @brief The uni classic deck as kind -> count pairs (`DeckDef::cards`).
 *
 * Expands the `uni:classic` data deck from the loaded mods with the `uni`
 * mod's manifest default settings. The shipped data file is
 * `mods/uni/decks/classic.json`: four colours of 0 x1, 1-9 x2, skip x2,
 * reverse x2, draw2 x2, plus wild x4 and wild_draw4 x4 (108 cards, 54 kinds).
 *
 * @param mods The loaded mods contributing schemas.
 * @return The expanded uni classic deck cards, or an empty list when the
 *         `uni` mod is absent or the deck does not expand.
 */
std::vector<std::pair<std::string, int>> UniClassicDeckCards(
    const std::vector<match::modload::LoadedMod>& mods);

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
 * @param mods        The match's active mod set (must be exactly `["uni"]`).
 * @param deck_cards  The match deck's kind -> count list.
 * @param loaded_mods The loaded mods used to expand the classic deck.
 * @return true when mods is exactly `{"uni"}`, the expanded classic deck is
 *         non-empty, and the deck kind multiset equals it.
 */
bool StatsGateAllows(const std::vector<std::string>& mods,
                     const std::vector<std::pair<std::string, int>>&
                         deck_cards,
                     const std::vector<match::modload::LoadedMod>&
                         loaded_mods);

}  // namespace match::server
