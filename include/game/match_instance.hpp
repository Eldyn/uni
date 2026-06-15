#pragma once

#include "common/lobby.hpp"
#include <common/game/gamerule.hpp>
#include <controllers/lobby_controller.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>

/**
 * @file match_instance.hpp
 * @brief Definition of the main class that manages the entire lifecycle of a game match:
 * a state machine that applies the effects of each played card based on rules that define
 * its various properties.
 */

struct LobbySettings;

namespace game {

    /**
     * @class MatchInstance
     * @brief Represents an active instance of a match.
     * * Maintains the internal game state (`GameState`), manages the flow of turns,
     * the execution of moves (playing cards, drawing), and processes the effects and
     * active rules applied to this specific match.
     * * @tag MATCH-INST-000
     */
    class MatchInstance {
    public:
        /**
         * @brief Constructor for a new match starting from the players' information and settings.
         * @param players_info Vector of {username, is_bot} pairs indicating the participants.
         * @param settings Current settings of the lobby.
         * @tag MATCH-INST-001
         */
        explicit MatchInstance(const std::vector<std::pair<std::string, bool>>& players_info, const LobbySettings& settings);

        /**
         * @brief Constructor for reloading a match starting from a state saved in the database.
         * @param saved_state The JSON containing the serialized state.
         * @param settings Lobby settings associated with the save.
         * @tag MATCH-INST-002
         */
        explicit MatchInstance(const json& saved_state, const LobbySettings& settings);

        /**
         * @brief Initializes the match, creates the initial deck and establishes the turns.
         * @tag MATCH-INST-003
         */
        void Start();

        /**
         * @brief Periodic update of the match (used to process the queued effects or timeouts).
         * @tag MATCH-INST-004
         */
        void Tick();

        /**
         * @brief Request from a user to play a given card.
         * @param username The user playing the card.
         * @param card_id The 16-bit identifier of the compact card.
         * @return true if the play was validated and processed, false otherwise.
         * @tag MATCH-INST-005
         */
        bool PlayCard(const std::string& username, uint16_t card_id);

        /**
         * @brief Request from a user to draw a card from the deck.
         * @param username The user who intends to draw.
         * @return true if the action is allowed and executed, false otherwise.
         * @tag MATCH-INST-006
         */
        bool DrawCard(const std::string& username);

        /**
         * @brief Provides an input to a pending effect (e.g. choice of a colour).
         * @param username The player providing the input.
         * @param input String representing the choice made.
         * @tag MATCH-INST-007
         */
        void ProvideInput(const std::string& username, const std::string& input);

        /**
         * @brief Declares "UNO" for the specific player.
         * @param username The player calling UNO.
         * @tag MATCH-INST-008
         */
        void CallUno(const std::string& username);

        /**
         * @brief Invokes the artificial intelligence to make the Bot take its turn.
         * @tag MATCH-INST-009
         */
        void TakeBotTurn();

        /**
         * @brief Returns the username of the player whose turn is currently active.
         * @return std::string Name of the player.
         * @tag MATCH-INST-010
         */
        std::string GetCurrentPlayerUsername() const;

        /**
         * @brief Adds or replaces a player mid-match (e.g. bot takeover).
         * @param username The new player.
         * @param is_bot True if it is a bot, False for a human.
         * @tag MATCH-INST-011
         */
        void AddPlayerMidGame(const std::string& username, bool is_bot);

        /**
         * @brief Removes a player mid-match, typically turning them into a Bot.
         * @param username The disconnected/departed player.
         * @tag MATCH-INST-012
         */
        void RemovePlayerMidGame(const std::string& username);

        /**
         * @brief Exports the entire GameState into a savable JSON format.
         * @return json Serialized state.
         * @tag MATCH-INST-013
         */
        json ExportState() const;

        /**
         * @brief Checks whether the game engine is waiting for explicit input from a player.
         * @return true if the current effect needs input.
         * @tag MATCH-INST-014
         */
        bool IsWaitingForInput() const { return !state_.pending_player.empty(); }

        /**
         * @brief Sets the time limit for the end of the current turn.
         * @param end_time The timestamp at which the turn will expire (triggering the AFK or bot).
         * @tag MATCH-INST-015
         */
        void SetTurnEndTime(std::chrono::steady_clock::time_point end_time) { state_.turn_end_time = end_time; }

        /**
         * @brief Retrieves the internal data of a specific player by username.
         * @param username Identifier of the user.
         * @return Player* Pointer to the player's data or nullptr if it does not exist.
         * @tag MATCH-INST-016
         */
        Player* GetPlayer(const std::string& username);

        /**
         * @brief Returns the user from whom a mandatory input is being awaited.
         * @return std::string Username or empty string if nothing is awaited.
         * @tag MATCH-INST-017
         */
        std::string GetPendingPlayer() const { return state_.pending_player; }

        /**
         * @brief Returns the type of the pending input (e.g. "choose_color").
         * @return std::string Input code.
         * @tag MATCH-INST-018
         */
        std::string GetPendingInputType() const { return state_.pending_input_type; }

        /**
         * @brief Returns contextual JSON text data for the input request.
         * @return std::string The input context.
         * @tag MATCH-INST-019
         */
        std::string GetPendingInputContext() const { return state_.pending_input_context; }

        /**
         * @brief Determines whether the match has reached a terminal state.
         * @return true if ended.
         * @tag MATCH-INST-020
         */
        bool IsGameOver() const { return state_.status == MatchStatus::kFinished; }

        /**
         * @brief Retrieves the username of the winner (if the match is concluded).
         * @return std::string Username of the winner.
         * @tag MATCH-INST-021
         */
        std::string GetWinner() const { return state_.winner; }

        /**
         * @brief Creates a JSON that represents the masked game state,
         * specific to the point of view of the provided player (hiding opponents' hands).
         * * @param username The player for whom the view is generated.
         * @return nlohmann::json The censored state, ready to be sent to the frontend.
         * @tag MATCH-INST-022
         */
        nlohmann::json SerializePlayerState(const std::string& username) const;

        /**
         * @brief Retrieves the unique UUID of the match (for saving to the DB).
         * @return std::string ID of the match.
         * @tag MATCH-INST-023
         */
        std::string GetMatchId() const { return match_id_; }

        /**
         * @brief Assigns a persistent UUID to the match.
         * @param id The identifier to assign.
         * @tag MATCH-INST-024
         */
        void SetMatchId(const std::string& id) { match_id_ = id; }

    private:
        GameState state_;                        /**< The central game state. */
        LobbySettings settings_;                 /**< The rules and preferences of the match. */
        std::string match_id_;                   /**< Unique identifier of the match in the database. */

        std::unordered_map<std::string, PlayerSessionStats> session_stats_; /**< Statistics collected during the match. */
        std::vector<std::unique_ptr<GameRule>> active_rules_;              /**< Set of active rules. */

        /**
         * @brief Checks whether a given optional rule mod is currently active for this match.
         * @param name The mod identifier (e.g. "seven_zero", "draw_stacking", "no_bluffing").
         * @return true if the mod is enabled in this lobby's settings.
         * @tag MATCH-INST-PRIV-001
         */
        bool HasMod(const std::string& name) const {
            return std::find(settings_.active_mods.begin(), settings_.active_mods.end(), name)
                   != settings_.active_mods.end();
        }

        /**
         * @brief Initializes the deck of cards based on the provided LobbySettings.
         * @tag MATCH-PRIV-002
         */
        void GenerateDeck();
    };
}

