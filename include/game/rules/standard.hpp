#pragma once
#include <common/game/gamerule.hpp>

/**
 * @file standard.hpp
 * @brief Implementation of the classic base rule set of the UNO game.
 * * This rule is responsible for the primary validation (colour on colour,
 * value on value) and for inserting into the effect resolution queue the effects
 * associated with each type of special card played.
 */

namespace game {

   /**
    * @class StandardRule
    * @brief Main validator and processor of the game's standard mechanics.
    * * Ensures the move is legal with respect to the current state of the table and,
    * if validated, decodes the card's value by instantiating the corresponding Effects
    * (e.g. if the card is a "Draw Two", it enqueues a `DrawEffect` and
    * an `AdvanceTurnEffect`).
    * @tag RULE-STD-001
    */
   class StandardRule : public GameRule {
    public:
        ~StandardRule() override = default;

        /**
         * @brief Intercepts the play attempt, evaluating its legality.
         * Checks whether the played card matches in colour or in value/symbol the card
         * on top of the discard pile (or the active colour for Wilds). Sets `event.is_valid_play` to false otherwise.
         * @param state Pointer to the current game state.
         * @param event Event struct containing the details of the play.
         * @tag RULE-STD-MTH-001
         */
        void ValidatePlay(GameState* state, CardPlayedEvent& event) override;

        /**
         * @brief Executes the consequences of a play confirmed as valid.
         * Enqueues into `GameState::effect_queue` the relevant `Effect` classes based
         * on the `Value` enumerator extracted from the compact card played.
         * @param state Pointer to the game state.
         * @param event The event of the played card.
         * @tag RULE-STD-MTH-002
         */
        void OnCardPlayed(GameState* state, CardPlayedEvent& event) override;

    private:
        /**
         * @brief Computes who will be the player to suffer the penalties or the next to play.
         * Support method used internally to determine the target (e.g. for `SkipEffect` or `DrawEffect`).
         * * @param state Pointer to the game state.
         * @return std::string Username of the target player computed following the direction of play.
         * @tag RULE-STD-PRIV-001
         */
        std::string GetNextPlayer(GameState* state);
   };
}
