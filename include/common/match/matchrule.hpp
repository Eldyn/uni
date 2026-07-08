#pragma once

#include <common/match/card_types.hpp>
#include <match/match_state.hpp>

/**
 * @file include/common/match/matchrule.hpp
 * @brief Defines the base engine for applying the game rules.
 *
 * @tag FEAT-CG-003 Flexible Rule Validation (Rule Engine): Interception of plays through dedicated events (CardPlayedEvent) to authorize, reject or extend the standard game mechanics.
 */

namespace match {

    /**
     * @struct CardPlayedEvent
     * @brief Structure that encapsulates the data concerning the event of a card being put into play.
     */
    struct CardPlayedEvent {
        std::string player_username;  /**< The username of the player attempting the play. */
        CompactCard played_card;      /**< The card being played. */

        /**< Flag indicating whether the play is valid or must be rejected
         * (settable by the validators). */
        bool is_valid_play = true;
        /**< Flag indicating whether the event has already been fully processed by a rule. */
        bool is_handled = false;
        /**< Flag indicating whether the play occurred out of turn
         * (useful for special rules or penalties). */
        bool is_out_of_turn = false;
    };

    struct CardDrawnEvent {
        std::string player_username;
        CompactCard drawn_card;
        bool is_handled = false;

        bool keep_drawing = false;  // Hook for "Progressive" mod
        bool force_play = false;   // Hook for "Force Play" mod
    };


    /**
     * @class MatchRule
     * @brief Base interface that defines the lifecycle for interpreting a play.
     */
    class MatchRule {
    public:
        virtual ~MatchRule() = default;

        /**
         * @brief Evaluates whether the played card complies with the imposed rules.
         * Called before the card is actually accepted by the engine.
         * @param state The current match state.
         * @param event The event carrying the play data. The method should set event.is_valid_play to false if the move is illegal.
         */
        virtual void ValidatePlay(const MatchState* state, CardPlayedEvent& event) {}

        /**
         * @brief Triggers the consequences arising from a validated play.
         * Called only if ValidatePlay allowed the play. Typically used to instantiate and enqueue
         * the child instances of `Effect` into the effect queue of `MatchState`.
         * @param state The current match state.
         * @param event The event containing the resolved play.
         */
        virtual void OnCardPlayed(MatchState* state, CardPlayedEvent& event) {}

        virtual void OnCardDrawn(MatchState* state, CardDrawnEvent& event) {}
    };
}  // namespace match
