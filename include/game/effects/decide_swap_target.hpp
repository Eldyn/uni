#pragma once

#include <common/game/effect.hpp>
#include <string>

/**
 * @file decide_swap_target.hpp
 * @brief Interactive effect for choosing the target for swapping cards.
 * * Requires an explicit action from a player to decide which
 * opponent to swap their entire hand of cards with (typical of the "7" card).
 */

namespace game {

    /**
     * @class DecideSwapTargetEffect
     * @brief Suspends the game to ask a user who they wish to swap cards with.
     * * This effect does not physically swap the cards, but collects the user's input.
     * Once the input (the target's name) is received, it typically enqueues a `PassHandsEffect`
     * or a similar effect targeted at the two players involved.
     * @tag EFFECT-SWAP-001
     */
    class DecideSwapTargetEffect : public Effect {
    public:
        /**
         * @brief Constructor of the interactive effect.
         * @param username Name of the player who must make the decision.
         */
        explicit DecideSwapTargetEffect(std::string username) : username_(std::move(username)) {}

        /**
         * @brief Suspends execution while waiting for input from the player.
         * If the input (opponent's username) has already been provided and stored in the state,
         * it validates the move and proceeds.
         * @param state The game state.
         * @param match The match instance.
         * @return EffectResult Returns kNeedsInput to query the client.
         * @tag EFFECT-SWAP-MTH-001
         */
        EffectResult Resolve(GameState* state, MatchInstance* match) override;

        /**
         * @brief Returns the enumerative type of the effect.
         * @return EffectType Identifier kDecideSwapTarget.
         * @tag EFFECT-SWAP-MTH-002
         */
        EffectType GetType() const override { return EffectType::kDecideSwapTarget; }

    private:
        std::string username_; /**< The user who must select the target. */
    };
}
