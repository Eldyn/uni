#pragma once

#include <common/game/card_types.hpp>
#include <common/game/effect.hpp>
#include <deque>
#include <memory>
#include <string>
#include <vector>
#include <chrono>

/**
 * @file game_state.hpp
 * @brief Foundational data structures for representing the state of the "UNO" game.
 * * Contains the pure data decoupled from the routing logic. The game state is
 * the context on which the Effects and the rule Validators (GameRule) act.
 */

namespace game {

    /**
     * @struct PlayerSessionStats
     * @brief Tracks the statistics of cards played/drawn in a single session.
     * @tag GAME-STRUCT-001
     */
    struct PlayerSessionStats {
        int color_counts[5] = {0};  /**< Frequency count for the various Colours (including Wild). */
        int value_counts[15] = {0}; /**< Frequency count for the Values (0-9, and special cards). */
    };

    /**
     * @struct Player
     * @brief Represents a real player or Bot within the ongoing match.
     * @tag GAME-STRUCT-002
     */
    struct Player {
        std::string username;           /**< Identifying username. */
        std::vector<CompactCard> hand;  /**< The cards currently in the player's hand. */
        bool is_bot;                    /**< Flag indicating whether the player is driven by the CPU. */
        bool has_called_uno = false;    /**< Flag indicating whether they have correctly declared "UNO". */

        /**
         * @brief Full constructor of the Player.
         * @param u Username.
         * @param h Initial hand of cards.
         * @param b True if Bot.
         * @param uno True if they have already called UNO.
         */
        Player(std::string u, std::vector<CompactCard> h, bool b, bool uno) : username(u), hand(h), is_bot(b), has_called_uno(uno) {};

        /**
         * @brief Constructor without cards in hand (setup phase).
         */
        Player(std::string u, bool b, bool uno) : username(u), is_bot(b), has_called_uno(uno) {};
    };

    /**
     * @enum MatchStatus
     * @brief Phases of the global lifecycle of a match.
     * @tag GAME-ENUM-001
     */
    enum class MatchStatus {
        kWaitingToStart, /**< The match is initializing the decks or is in pre-game. */
        kPlaying,        /**< Match actively in progress (turns are flowing). */
        kFinished        /**< Match ended. There is an established winner. */
    };

    /**
     * @struct LastPlay
     * @brief Describes the last card played and its origin, so that the client
     * can animate the card from the specific position (slot) of the hand of whoever played it.
     * @tag GAME-STRUCT-004
     */
    struct LastPlay {
        bool valid = false;       /**< True if it contains a valid play to animate. */
        std::string player;       /**< Username of whoever played the card. */
        int hand_index = -1;      /**< Index in the hand slot from which the card departed. */
        CompactCard card{};       /**< The played card (compact id/colour/value). */
    };

    /**
     * @struct GameState
     * @brief Monolithic data structure that contains the entire "snapshot" of the match at a given instant.
     * It is manipulated by the Effect and MatchInstance classes and serialized to JSON.
     * @tag GAME-STRUCT-003
     */
    struct GameState {
        MatchStatus status = MatchStatus::kWaitingToStart; /**< The progression state. */
        std::string winner;                                /**< Username of the winner (if concluded). */

        std::vector<Player> players;                       /**< Ordered list (by turn) of the players. */
        int current_player_index = 0;                      /**< Index of the player with the active turn. */
        int play_direction = 1;                            /**< Direction of play (1 = clockwise, -1 = counter-clockwise). */
        int pending_draws = 0;                             /**< Accumulated cards the next player will have to draw (e.g. +2/+4 chain). */
        LastPlay last_play;                                /**< Origin of the last played card, used for the client animations. */

        std::vector<CompactCard> draw_pile;                /**< The deck of cards to draw from. */
        std::vector<CompactCard> discard_pile;             /**< The discard pile in the centre of the table. */
        Color active_color = Color::kRed;                  /**< Colour currently required to respond (changes with Wild cards). */

        std::deque<std::unique_ptr<Effect>> effect_queue;  /**< Queue of effects to resolve (asynchronous architecture for chained moves). */

        std::string pending_player;                        /**< The user who MUST provide an input before the game continues. */
        std::string pending_input_type;                    /**< Type of input required (e.g. "choose_color"). */
        std::string pending_input_context;                 /**< Additional JSON payload to help the client render the input box. */
        std::string provided_input;                        /**< Asynchronous response stored as soon as it is sent by the player for the effect. */

        std::chrono::steady_clock::time_point turn_end_time; /**< Timestamp at which the AFK/Timeout of the current turn will fire. */
    };

    /**
     * @brief System utility that moves and reshuffles the cards from the discard pile into the main deck.
     * Invoked automatically by the game engine when `draw_pile` is exhausted.
     * @param game_state Pointer to the game state to manipulate.
     * @tag GAME-UTIL-001
     */
    void ReshuffleDiscardIntoDraw(GameState* game_state);
}
