#pragma once

#include <common/match/card_types.hpp>
#include <common/match/effect.hpp>
#include <deque>
#include <memory>
#include <nlohmann/json.hpp>
#include <random>
#include <string>
#include <vector>
#include <chrono>

/**
 * @file match_state.hpp
 * @brief Foundational data structures for representing the state of the "UNO" match.
 * * Contains the pure data decoupled from the routing logic. The match state is
 * the context on which the Effects and the rule Validators (MatchRule) act.
 */

namespace match {

    /**
     * @struct PlayerSessionStats
     * @brief Tracks the statistics of cards played/drawn in a single session.
     * @tag GAME-STRUCT-001
     */
    struct PlayerSessionStats {
        /**< Frequency count for the various Colours (including Wild). */
        int color_counts[5] = {0};
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
        /**< Flag indicating whether the player is driven by the CPU. */
        bool is_bot;

        /**
         * @brief Full constructor of the Player.
         * @param u Username.
         * @param h Initial hand of cards.
         * @param b True if Bot.
         */
        Player(std::string u, std::vector<CompactCard> h, bool b)
            : username(u), hand(h), is_bot(b) {}

        /**
         * @brief Constructor without cards in hand (setup phase).
         */
        Player(std::string u, bool b) : username(u), is_bot(b) {}
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
     * @struct MatchState
     * @brief Monolithic data structure that contains the entire "snapshot" of the match at a given instant.
     * It is manipulated by the Effect and MatchInstance classes and serialized to JSON.
     * @tag GAME-STRUCT-003
     */
    struct MatchState {
        /**< The progression state. */
        MatchStatus status = MatchStatus::kWaitingToStart;
        std::string winner;  /**< Username of the winner (if concluded). */

        /**< Ordered list (by turn) of the players. */
        std::vector<Player> players;
        /**< Index of the player with the active turn. */
        int current_player_index = 0;
        /**< Direction of play (1 = clockwise, -1 = counter-clockwise). */
        int play_direction = 1;
        /**< Accumulated cards the next player will have to draw (e.g. +2/+4 chain). */
        int pending_draws = 0;
        /**< Origin of the last played card, used for the client animations. */
        LastPlay last_play;

        std::vector<CompactCard> draw_pile;                /**< The deck of cards to draw from. */
        /**< The discard pile in the centre of the table. */
        std::vector<CompactCard> discard_pile;
        /**< Type currently required to respond (changes with Wild cards). */
        Type active_type = Type::kRed;

        /**< Queue of effects to resolve (asynchronous architecture for chained moves). */
        std::deque<std::unique_ptr<Effect>> effect_queue;

        /**< The user who MUST provide an input before the match continues. */
        std::string pending_player;
        /**< Action required (meaningful only when pending_player is set). */
        Action pending_action = Action::kChooseType;
        /**< Additional JSON payload to help the client render the input box. */
        nlohmann::json pending_input_context;
        /**< Asynchronous response stored as soon as it is sent by the player for the effect. */
        std::string provided_input;

        /**< Timestamp at which the AFK/Timeout of the current turn will fire. */
        std::chrono::steady_clock::time_point turn_end_time;
    };

    /**
     * @brief System utility that moves and reshuffles the cards from the discard pile into the main deck.
     * Invoked automatically by the game engine when `draw_pile` is exhausted.
     * @param match_state Pointer to the match state to manipulate.
     * @param rng Shuffle source, owned by the calling MatchInstance.
     * @tag GAME-UTIL-001
     */
    void ReshuffleDiscardIntoDraw(MatchState* match_state, std::mt19937& rng);
}  // namespace match
