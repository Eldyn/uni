#pragma once

#include "common/lobby.hpp"
#include <common/match/matchrule.hpp>
#include <nlohmann/json.hpp>
#include <functional>
#include <random>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <optional>
#include <unordered_set>

class Database;
struct LobbySettings;

namespace match {

    /**
     * @struct BotAdvanceResult
     * @brief Outcome of a synchronous burst of consecutive bot turns.
     */
    struct BotAdvanceResult {
        int steps = 0;      /**< Number of bot moves actually taken. */
        bool stalled = false;    /**< True if aborted due to detected stall. */
        bool match_over = false;  /**< True if the match reached a terminal state. */
    };

    /**
     * @enum TurnTimeoutPolicy
     * @brief Describes what kind of timer (if any) should govern the current turn.
     */
    enum class TurnTimeoutPolicy {
        kBotThinking,        /**< Current player is a bot: arm a "thinking" delay timer. */
        kInstantBotAdvance,    /**< Current player is a disconnected human under kPlayInstantly. */
        kInputWaitTimeout,     /**< The engine is waiting for input from a pending player. */
        kHumanAfkTimeout,      /**< Current player is a human under kWaitUntilTurnEnd. */
        kNone                  /**< No timer should be armed for the current turn. */
    };

    /**
     * @class MatchInstance
     * @brief Represents an active instance of a match.
     * * Maintains the internal match state (`MatchState`), manages the flow of turns,
     * the execution of moves (playing cards, drawing), and processes the effects and
     * active rules applied to this specific match.
     */
    class MatchInstance {
    public:
        /**
         * @brief Constructor for a new match starting from the players' information and settings.
         * @param players_info Vector of {username, is_bot, seat_index} tuples indicating the
         * participants, seat_index carried over from LobbyMember::seat_index for stable
         * per-player color/seat identity.
         * @param settings Current settings of the lobby.
         */
        explicit MatchInstance(
            const std::vector<std::tuple<std::string, bool, int>>& players_info,
            const LobbySettings& settings);

        /**
         * @brief Convenience overload for callers (chiefly tests) without a real
         * per-player seat_index yet; assigns seats by vector position.
         */
        explicit MatchInstance(const std::vector<std::pair<std::string, bool>>& players_info,
                                const LobbySettings& settings);

        /**
         * @brief Constructor for reloading a match starting from a state saved in the database.
         * @param saved_state The JSON containing the serialized state.
         * @param settings Lobby settings associated with the save.
         */
        explicit MatchInstance(const json& saved_state, const LobbySettings& settings);

        /**
         * @brief Initializes the match, creates the initial deck and establishes the turns.
         */
        void Start();

        /**
         * @brief Periodic update of the match (used to process the queued effects or timeouts).
         */
        void Tick();

        /**
         * @brief Request from a user to play a given card.
         * @param username The user playing the card.
         * @param card_id The 16-bit identifier of the compact card.
         * @return true if the play was validated and processed, false otherwise.
         */
        bool PlayCard(const std::string& username, uint16_t card_id);

        /**
         * @brief Request from a user to draw a card from the deck.
         * @param username The user who intends to draw.
         * @return true if the action is allowed and executed, false otherwise.
         */
        bool DrawCard(const std::string& username);

        /**
         * @brief Provides an input to a pending effect (e.g. choice of a colour).
         * @param username The player providing the input.
         * @param input String representing the choice made.
         */
        void ProvideInput(const std::string& username, const std::string& input);

        /**
         * @brief Invokes the artificial intelligence to make the Bot take its turn.
         */
        void TakeBotTurn();

        /**
         * @brief Returns the username of the player whose turn is currently active.
         * @return std::string Name of the player.
         */
        std::string GetCurrentPlayerUsername() const;

        /**
         * @brief Adds or replaces a player mid-match (e.g. bot takeover).
         * @param username The new player.
         * @param is_bot True if it is a bot, False for a human.
         */
        void AddPlayerMidGame(const std::string& username, bool is_bot, int seat_index = -1);

        /**
         * @brief Removes a player mid-match, typically turning them into a Bot.
         * @param username The disconnected/departed player.
         */
        void RemovePlayerMidGame(const std::string& username);

        /**
         * @brief Shared core to remove a player index from rotation and correctly update current_player_index.
         * @param index_to_remove Position in state_.players to remove.
         * @param will_advance_turn True if an AdvanceTurnEffect is queued to run subsequently.
         */
        void RemovePlayerFromRotation(int index_to_remove, bool will_advance_turn = false);

        /**
         * @brief Exports the entire MatchState into a savable JSON format.
         * @return json Serialized state.
         */
        json ExportState() const;

        /**
         * @brief Checks whether the game engine is waiting for explicit input from a player.
         * @return true if the current effect needs input.
         */
        bool IsWaitingForInput() const { return !state_.pending_player.empty(); }

        /**
         * @brief Sets the time limit for the end of the current turn.
         * @param end_time The timestamp at which the turn will expire (triggering the AFK or bot).
         */
        void SetTurnEndTime(std::chrono::steady_clock::time_point end_time) {
            state_.turn_end_time = end_time;
        }

        /**
         * @brief Retrieves the internal data of a specific player by username.
         * @param username Identifier of the user.
         * @return Player* Pointer to the player's data or nullptr if it does not exist.
         */
        Player* GetPlayer(const std::string& username);

        /**
         * @brief Checks whether the given player is bot-controlled.
         * @param username Identifier of the user.
         * @return true if the player exists and is a bot, false otherwise.
         */
        bool IsBot(const std::string& username) const;

        /**
         * @brief Synchronously advances consecutive bot turns until a stop condition is met.
         * A stop condition is reached when the current player is a connected human
         * (per @p is_connected), the match ends, a stall is detected (current player
         * and waiting-state both unchanged after a move), or the safety cap of
         * @c kMaxInstantBotSteps consecutive moves is hit.
         * @param is_connected Predicate telling whether a given username is currently connected.
         * @param on_step Invoked after each individual bot move completes.
         * @param max_steps Safety cap on consecutive bot moves in this burst.
         * @return BotAdvanceResult Summary of the burst (steps taken, stalled, match over).
         */
        BotAdvanceResult AdvanceBotTurns(const std::function<bool(const std::string&)>&
                                              is_connected,
                                          const std::function<void()>& on_step,
                                          int max_steps = kMaxInstantBotSteps);

        /**
         * @brief Determines what kind of timer should govern the current turn.
         * @param is_connected Predicate telling whether a given username is currently connected.
         * @return TurnTimeoutPolicy The policy the controller should act on.
         */
        TurnTimeoutPolicy GetTurnTimeoutPolicy(
            const std::function<bool(const std::string&)>& is_connected) const;

        /**
         * @brief Returns the user from whom a mandatory input is being awaited.
         * @return std::string Username or empty string if nothing is awaited.
         */
        std::string GetPendingPlayer() const { return state_.pending_player; }

        /**
         * @brief Returns the action type the engine is waiting for.
         * @return Action The required action.
         */
        Action GetPendingAction() const { return state_.pending_action; }

        /**
         * @brief Returns contextual JSON data for the input request.
         * @return nlohmann::json The input context.
         */
        const nlohmann::json& GetPendingInputContext() const {
            return state_.pending_input_context;
        }

        /**
         * @brief Determines whether the match has reached a terminal state.
         * @return true if ended.
         */
        bool IsMatchOver() const { return state_.status == MatchStatus::kFinished; }

        /**
         * @brief Retrieves the username of the winner (if the match is concluded).
         * @return std::string Username of the winner.
         */
        std::string GetWinner() const { return state_.winner; }
        const std::vector<std::string>& GetPlacements() const { return state_.placements; }

        /**
         * @brief Creates a JSON that represents the masked match state,
         * specific to the point of view of the provided player (hiding opponents' hands).
         * * @param username The player for whom the view is generated.
         * @return nlohmann::json The censored state, ready to be sent to the frontend.
         */
        nlohmann::json SerializePlayerState(const std::string& username) const;

        /**
         * @brief Creates the JSON portion of the match state shared identically
         * across all viewers (discard pile, turn timer, player roster, etc.),
         * excluding any single player's own hand.
         * @return nlohmann::json The shared state, without any viewer's hand.
         */
        nlohmann::json SerializeBaseState() const;

        /**
         * @brief Creates the JSON array describing a single player's own hand,
         * including per-card playability when it is that player's turn.
         * @param username The player whose hand should be serialized.
         * @return nlohmann::json The array of cards in the player's hand.
         */
        nlohmann::json SerializeHandFor(const std::string& username) const;

        /**
         * @brief Retrieves the unique UUID of the match (for saving to the DB).
         * @return std::string ID of the match.
         */
        std::string GetMatchId() const { return match_id_; }

        /**
         * @brief Assigns a persistent UUID to the match.
         * @param id The identifier to assign.
         */
        void SetMatchId(const std::string& id) { match_id_ = id; }

        /**
         * @brief Exposes the match's shuffle RNG for effects that need it.
         * @return std::mt19937& Reference to the shared RNG.
         */
        std::mt19937& Rng() const { return rng_; }

        /**
         * @brief Checks whether this match is eligible for ranked statistics.
         * Match must have ranked setting true and at least kMinRankedHumans at match start.
         */
        bool IsRankedEligible() const {
            return (settings_.mode != "elimination") && settings_.ranked && (initial_human_count_ >= kMinRankedHumans);
        }

        /**
         * @brief Returns the number of human participants at match start.
         */
        int GetInitialHumanCount() const { return initial_human_count_; }

        /**
         * @brief Returns the initial human participants list.
         */
        const std::vector<std::string>& GetInitialHumans() const { return initial_humans_; }

        /**
         * @brief Records completion of the match (standard win/loss).
         */
        void RecordMatchCompleted(const std::string& winner);

        /**
         * @brief Records a mid-match quit for an individual participant.
         */
        void RecordPlayerQuit(const std::string& username);

        /**
         * @brief Records an aborted match (e.g. quit_deletes_match or unrecoverable drop).
         */
        void RecordMatchAborted();

    private:
        /**< Safety cap on consecutive bot moves in a single AdvanceBotTurns burst. */
        static constexpr int kMaxInstantBotSteps = 20;
        /**< Minimum number of real humans required at start for a match to count for ranked stats. */
        static constexpr int kMinRankedHumans = 3;

        MatchState state_;                        /**< The central match state. */
        LobbySettings settings_;                 /**< The rules and preferences of the match. */
        /**< Unique identifier of the match in the database. */
        std::string match_id_;
        /**< Shared RNG for shuffles. */
        mutable std::mt19937 rng_{std::random_device {}()};

        /**< List of human participants captured at match start. */
        std::vector<std::string> initial_humans_;
        int initial_human_count_ = 0;
        /**< Set of participants whose ledger row has already been written. */
        std::unordered_set<std::string> recorded_humans_;

        /**< Statistics collected during the match. */
        std::unordered_map<std::string, PlayerSessionStats> session_stats_;
        std::vector<std::unique_ptr<MatchRule>> active_rules_;  /**< Set of active rules. */

        /**
         * @brief Writes a single row to the match_history ledger.
         */
        void WriteLedgerRow(Database& db,
                            const std::string& username,
                            const std::string& mode,
                            std::optional<int> placement,
                            const std::string& result,
                            const std::string& ended_reason,
                            int ranked);

        /**
         * @brief Checks whether a given optional rule mod is currently active for this match.
         * @param name The mod identifier (e.g. "seven_zero", "draw_stacking", "no_bluffing").
         * @return true if the mod is enabled in this lobby's settings.
         */
        bool HasMod(const std::string& name) const {
            return std::find(settings_.active_mods.begin(), settings_.active_mods.end(), name)
                   != settings_.active_mods.end();
        }

        /**
         * @brief Initializes the deck of cards based on the provided LobbySettings.
         */
        void GenerateDeck();
    };
}  // namespace match
