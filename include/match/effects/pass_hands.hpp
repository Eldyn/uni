#pragma once
#include <common/match/effect.hpp>

/**
 * @file pass_hands.hpp
 * @brief Implementation of the effect for rotating or swapping hands of cards.
 * * This effect handles the physical movement of cards between players.
 * It is typically used in the "7-0" mods (where the 0 rotates everyone's hands
 * in the direction of play) or after a player has chosen a target to swap with.
 */

namespace match {

    /**
     * @class PassHandsEffect
     * @brief Effect that performs the swapping of the cards in the players' hands.
     */
    class PassHandsEffect : public Effect {
    public:
        /**
         * @brief Resolves the effect by swapping the `hand` vectors within `MatchState::players`.
         * If triggered by a global rotation (e.g. the "0" rule), it moves the hands
         * by one position following the current direction of play.
         * @param state The current match state.
         * @param match The match instance.
         * @return EffectResult The outcome of the operation.
         */
        EffectResult Resolve(MatchState* state, MatchInstance* match) override;
    };
}  // namespace match
