#pragma once

#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

/**
 * @file include/common/game/effect.hpp
 * @brief Base system for defining and resolving game effects.
 *
 * @tag FEAT-CG-002 Asynchronous Effect Handling (Effect Engine): Polymorphic, event- and state-based architecture to process and suspend the resolution of effects while waiting for user input.
 */

namespace match {

    struct MatchState;
    class MatchInstance;

    /**
     * @enum EffectStatus
     * @brief Enumerates the possible outcomes of resolving an effect.
     */
    enum class EffectStatus {
        kResolved,   /**< The effect was applied successfully and completed. */
        kNeedsInput, /**< The effect is suspended waiting for input from a player. */
        kError       /**< An error occurred during resolution. */
    };

    /**
     * @enum Action
     * @brief Identifies the type of player input an effect is waiting for.
     * Transmitted as an integer on the wire (action_required field).
     */
    enum class Action : uint8_t {
        kChooseType   = 0, /**< Player must choose a card type (formerly "choose_color") */
        /**< Player must decide to play or keep a drawn card (formerly "play_drawn_card") */
        kPlayDrawn    = 1,
        kChooseTarget = 2  /**< Player must choose a target player (formerly "choose_target") */
    };

    /**
     * @struct EffectResult
     * @brief Structure returned after attempting to resolve an effect.
     */
    struct EffectResult {
        EffectStatus status;                        /**< The resolution status of the effect. */
        /**< The action required (meaningful only when status == kNeedsInput). */
        Action action = Action::kChooseType;
        /**< The player from whom the input is awaited. */
        std::string target_player = "";
        /**< A generic JSON payload with contextual information for the frontend. */
        std::string input_context = "";
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
        kChooseColor,       /**< Choice of a new active type (e.g. after a Wild) */
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
        virtual EffectResult Resolve(MatchState* state, MatchInstance* match) = 0;

        virtual nlohmann::json ToJson() const { return {{"type", static_cast<int>(GetType())}}; }
    };

}  // namespace match
