#pragma once
#include <common/game/effect.hpp>
#include <string>
#include <cstdint>

/**
 * @file standard.hpp
 * @brief Implementation of the standard effects associated with the base cards.
 * * This file defines the set of classic effects (e.g. Skip turn, Reverse,
 * Draw cards, Choose colour) that are enqueued and resolved by the game engine during a match.
 */

namespace game {

    /**
     * @class AdvanceTurnEffect
     * @brief Base effect that advances the turn to the next player.
     * Usually enqueued at the end of a standard play or after the resolution of the other effects.
     * @tag EFFECT-STD-001
     */
    class AdvanceTurnEffect : public Effect {
    public:
        /**
         * @brief Resolves the effect by updating the index of the current player in the game state.
         * @param game_state The current game state.
         * @param match_instance The match instance.
         * @return EffectResult The outcome of the resolution (always kResolved in the absence of errors).
         * @tag EFFECT-STD-MTH-001
         */
        EffectResult Resolve(GameState* game_state, MatchInstance* match_instance) override;

        /**
         * @brief Returns the enumerative type of the effect.
         * @return EffectType Identifier kAdvanceTurn.
         * @tag EFFECT-STD-MTH-002
         */
        EffectType GetType() const override { return EffectType::kAdvanceTurn; }
    };

    /**
     * @class DecideDrawnCardEffect
     * @brief Interactive effect that asks the player whether they want to play the just-drawn card.
     * * Triggered when a player draws a card from the deck (not as a penalty) and
     * it turns out to be playable according to the current rules.
     * @tag EFFECT-STD-002
     */
    class DecideDrawnCardEffect : public Effect {
    public:
        /**
         * @brief Constructor of the effect.
         * @param username Name of the player who has just drawn.
         * @param card_id The 16-bit ID of the just-drawn card.
         */
        explicit DecideDrawnCardEffect(const std::string& username, uint16_t card_id) : username_(username), card_id_(card_id) {}

        /**
         * @brief Suspends the game engine while waiting for the user's input.
         * @param state The game state.
         * @param match The match instance.
         * @return EffectResult Returns a kNeedsInput status to ask the client what to do.
         * @tag EFFECT-STD-MTH-003
         */
        EffectResult Resolve(GameState* state, MatchInstance* match) override;

        /**
         * @brief Returns the enumerative type of the effect.
         * @return EffectType Identifier kDecideDrawnCard.
         * @tag EFFECT-STD-MTH-004
         */
        EffectType GetType() const override { return EffectType::kDecideDrawnCard; }
    private:
        std::string username_; /**< The user who must make the decision. */
        uint16_t card_id_;     /**< The ID of the card to decide on. */
    };

    /**
     * @class DrawEffect
     * @brief Penalty/obligation effect that makes a player draw a number of cards.
     * Used for the start of the turn, the +2, +4 cards, or for penalties from mods.
     * @tag EFFECT-STD-003
     */
    class DrawEffect : public Effect {
    public:
        /**
         * @brief Constructor of the draw effect.
         * @param count The number of cards to draw.
         * @param target_username The user who will undergo the effect.
         */
        explicit DrawEffect(int count, const std::string& target_username) : count_(count), target_username_(target_username) {}

        /**
         * @brief Resolves the effect by taking the cards from the deck and adding them to the target's hand.
         * Automatically handles the reshuffling of the discard pile if necessary.
         * @param state The game state.
         * @param match_instance The match instance.
         * @return EffectResult Outcome of the operation.
         * @tag EFFECT-STD-MTH-005
         */
        EffectResult Resolve(GameState* state, MatchInstance* match_instance) override;

        /**
         * @brief Returns the enumerative type of the effect.
         * @return EffectType Identifier kDraw.
         * @tag EFFECT-STD-MTH-006
         */
        EffectType GetType() const override { return EffectType::kDraw; }
    private:
        int count_;                     /**< Quantity of cards. */
        std::string target_username_;   /**< Target of the effect. */
    };

    /**
     * @class SkipEffect
     * @brief Effect that skips the next player's turn (Skip card).
     * @tag EFFECT-STD-004
     */
    class SkipEffect : public Effect {
    public:
        /**
         * @brief Advances the player index by one additional step, effectively skipping them.
         * @param state The game state.
         * @param match_instance The match instance.
         * @return EffectResult Outcome of the operation.
         * @tag EFFECT-STD-MTH-007
         */
        EffectResult Resolve(GameState* state, MatchInstance* match_instance) override;

        /**
         * @brief Returns the enumerative type of the effect.
         * @return EffectType Identifier kSkip.
         * @tag EFFECT-STD-MTH-008
         */
        EffectType GetType() const override { return EffectType::kSkip; }
    };

    /**
     * @class ReverseEffect
     * @brief Effect that reverses the direction of turn rotation (Reverse card).
     * @tag EFFECT-STD-005
     */
    class ReverseEffect : public Effect {
    public:
        /**
         * @brief Reverses the play direction (e.g. from 1 to -1 or vice versa). With 2 players it acts as a Skip.
         * @param state The game state.
         * @param match_instance The match instance.
         * @return EffectResult Outcome of the operation.
         * @tag EFFECT-STD-MTH-009
         */
        EffectResult Resolve(GameState* state, MatchInstance* match_instance) override;

        /**
         * @brief Returns the enumerative type of the effect.
         * @return EffectType Identifier kReverse.
         * @tag EFFECT-STD-MTH-010
         */
        EffectType GetType() const override { return EffectType::kReverse; }
    };

    /**
     * @class ChooseColorEffect
     * @brief Interactive effect that requires the selection of the new active colour (Wild cards).
     * @tag EFFECT-STD-006
     */
    class ChooseColorEffect : public Effect {
    public:
        /**
         * @brief Constructor of the effect.
         * @param target_username The player who played the Wild and must choose the colour.
         */
        explicit ChooseColorEffect(const std::string& target_username) : target_username_(target_username) {}

        /**
         * @brief Suspends execution while waiting for input from the target. If the input is already present, it updates the global colour.
         * @param state The game state.
         * @param match_instance The match instance.
         * @return EffectResult Outcome of the operation or kNeedsInput.
         * @tag EFFECT-STD-MTH-011
         */
        EffectResult Resolve(GameState* state, MatchInstance* match_instance) override;

        /**
         * @brief Returns the enumerative type of the effect.
         * @return EffectType Identifier kChooseColor.
         * @tag EFFECT-STD-MTH-012
         */
        EffectType GetType() const override { return EffectType::kChooseColor; }
    private:
        std::string target_username_; /**< User who must provide the colour choice. */
    };
}
