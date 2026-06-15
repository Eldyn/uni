#pragma once

#include <string>

/**
 * @file include/common/game/effect.hpp
 * @brief Base system for defining and resolving game effects.
 *
 * @tag FEAT-CG-002 Asynchronous Effect Handling (Effect Engine): Polymorphic, event- and state-based architecture to process and suspend the resolution of effects while waiting for user input.
 */

namespace game {

    struct GameState;
    class MatchInstance;

    /**
     * @enum EffectStatus
     * @brief Enumerates the possible outcomes of resolving an effect.
     */
    enum class EffectStatus {
        kResolved,   /**< The effect was applied successfully and completed. */
        kNeedsInput, /**< The effect is suspended waiting for input from a player (e.g. colour choice). */
        kError       /**< An error occurred during resolution. */
    };

    /**
     * @struct EffectResult
     * @brief Structure returned after attempting to resolve an effect.
     */
    struct EffectResult {
        EffectStatus status;             /**< The resolution status of the effect. */
        std::string input_type;          /**< The type of input required (if status == kNeedsInput). */
        std::string target_player;       /**< The player from whom the input is awaited. */
        std::string input_context = "";  /**< A generic JSON payload with contextual information for the frontend. */
    };

    /**
     * @enum EffectType
     * @brief Codes for the runtime identification of the type of effect in progress.
     */
    enum class EffectType {
        kAdvanceTurn,       /**< Passing the turn to the next player */
        kDraw,              /**< Obligation to draw cards */
        kSkip,              /**< Skipping the turn */
        kPlayCard,          /**< Playing a card */
        kDecideDrawnCard,   /**< Decision about a just-drawn card (play or keep) */
        kChooseColor,       /**< Choice of a new active colour (e.g. after a Wild) */
        kReverse,           /**< Reversal of the play order */
        kDecideSwapTarget,  /**< Choice of the target to swap hands with */
        kPassHands,         /**< Effect for passing or swapping hands between players */
        kCustom             /**< Custom effect (for mods or extensions) */
    };

    /**
     * @class Effect
     * @brief Abstract base class for all the effects applicable in the game.
     */
    class Effect {
    public:
        virtual ~Effect() = default;

        /**
         * @brief Retrieves the type of the implemented effect.
         * @return EffectType Identifier of the effect (defaults to kCustom).
         */
        virtual EffectType GetType() const { return EffectType::kCustom; }

        /**
         * @brief Executes and applies the effect to the current game state.
         * @param state Pointer to the global match state (GameState).
         * @param match Pointer to the match instance (MatchInstance) to coordinate input/bots.
         * @return EffectResult The result of the application (resolved, error or needs input).
         */
        virtual EffectResult Resolve(GameState* state, MatchInstance* match) = 0;
    };

}
