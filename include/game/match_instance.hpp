#pragma once

#include "common/lobby.hpp"
#include <common/game/gamerule.hpp>
#include <controllers/lobby_controller.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <memory>

struct LobbySettings;

namespace game {
    class MatchInstance {
    public:
        explicit MatchInstance(const std::vector<std::pair<std::string, bool>>& players_info, const LobbySettings& settings);
        explicit MatchInstance(const json& saved_state, const LobbySettings& settings);
        
        void Start();
        void Tick(); 
        
        bool PlayCard(const std::string& username, uint16_t card_id);
        bool DrawCard(const std::string& username);

        void ProvideInput(const std::string& username, const std::string& input);

        void CallUno(const std::string& username);
    
        void TakeBotTurn();

        std::string GetCurrentPlayerUsername() const;

        void AddPlayerMidGame   (const std::string& username, bool is_bot);
        void RemovePlayerMidGame(const std::string& username);
    
        json ExportState() const;

        bool IsWaitingForInput() const { return !state_.pending_player.empty(); }
        void SetTurnEndTime(std::chrono::steady_clock::time_point end_time) { state_.turn_end_time = end_time; }

        Player* GetPlayer(const std::string& username);
        std::string GetPendingPlayer() const { return state_.pending_player; }
        std::string GetPendingInputType() const { return state_.pending_input_type; }
        std::string GetPendingInputContext() const { return state_.pending_input_context; }
        
        bool IsGameOver() const { return state_.status == MatchStatus::kFinished; }
        std::string GetWinner() const { return state_.winner; }
    
        nlohmann::json SerializePlayerState(const std::string& username) const;

        std::string GetMatchId() const { return match_id_; }
        void SetMatchId(const std::string& id) { match_id_ = id; }
    private:
        GameState state_;
        LobbySettings settings_;
        std::string match_id_;

        std::unordered_map<std::string, PlayerSessionStats> session_stats_;
        std::vector<std::unique_ptr<GameRule>> active_rules_;
    
        void GenerateDeck();
    };
}
